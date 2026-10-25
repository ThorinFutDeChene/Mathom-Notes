/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "backup.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#endif

namespace
{

#ifdef Q_OS_UNIX
// Deterministically fail while writing the staged tar, AFTER the snapshot and
// manifest have succeeded. On the old code this erased the previous backup.
// Restore the process limits and signal handler on every exit path.
class ScopedFileSizeLimit
{
public:
    explicit ScopedFileSizeLimit(rlim_t maximum)
    {
        if (getrlimit(RLIMIT_FSIZE, &m_previousLimit) != 0)
            return;
        struct sigaction ignore = {};
        ignore.sa_handler = SIG_IGN;
        sigemptyset(&ignore.sa_mask);
        if (sigaction(SIGXFSZ, &ignore, &m_previousSignal) != 0)
            return;
        struct rlimit limit = m_previousLimit;
        limit.rlim_cur = qMin(maximum, limit.rlim_cur);
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0) {
            sigaction(SIGXFSZ, &m_previousSignal, nullptr);
            return;
        }
        m_active = true;
    }

    ~ScopedFileSizeLimit()
    {
        if (m_active) {
            setrlimit(RLIMIT_FSIZE, &m_previousLimit);
            sigaction(SIGXFSZ, &m_previousSignal, nullptr);
        }
    }

    bool isActive() const { return m_active; }

private:
    struct rlimit m_previousLimit = {};
    struct sigaction m_previousSignal = {};
    bool m_active = false;
};
#endif

class BackupWorker : public BackupThread
{
public:
    using BackupThread::BackupThread;
    using BackupThread::run;
};

class RestoreWorker : public RestoreThread
{
public:
    using RestoreThread::RestoreThread;
    using RestoreThread::run;
};

bool writeFile(const QString &path, const QByteArray &data)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;

    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class BackupTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void preservesExistingBackupWhenSnapshotFails();
    void preservesExistingBackupWhenArchiveWriteFails();
    void preservesExistingBackupWhenPromotionFails();
    void reportsDestinationErrorWithoutPartialArchive();
    void replacesExistingBackupWithRestorableContent();
};

void BackupTest::preservesExistingBackupWhenSnapshotFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source"));
    const QByteArray note("Original backup must remain restorable");
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("note.txt")), note));

    const QString destination = directory.filePath(QStringLiteral("previous.mathom-backup"));
    BackupWorker original(destination, source);
    original.run();
    QVERIFY2(original.success(), qPrintable(original.errorString()));
    const QByteArray previousContent = readFile(destination);
    QVERIFY(!previousContent.isEmpty());

    BackupWorker failing(destination, directory.filePath(QStringLiteral("missing-source")));
    failing.run();

    QVERIFY(!failing.success());
    QVERIFY(!failing.errorString().isEmpty());
    QCOMPARE(readFile(destination), previousContent);
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden),
             QStringList{QStringLiteral("previous.mathom-backup")});

    const QString restored = directory.filePath(QStringLiteral("restored"));
    RestoreWorker restore(destination, restored);
    restore.run();
    QVERIFY2(restore.success(), qPrintable(restore.errorString()));
    QCOMPARE(readFile(QDir(restored).filePath(QStringLiteral("data/note.txt"))), note);
}

void BackupTest::preservesExistingBackupWhenArchiveWriteFails()
{
#ifndef Q_OS_UNIX
    QSKIP("File-size limit failure test requires Unix.");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source"));

    // Files and manifest remain below the limit, but the compressed tar is
    // larger. This triggers an error after the old implementation had already
    // deleted the existing backup.
    QRandomGenerator generator(0x51EED);
    for (int i = 0; i < 8; ++i) {
        QByteArray contents(3072, '\0');
        for (qsizetype byte = 0; byte < contents.size(); ++byte)
            contents[byte] = static_cast<char>(generator.generate() & 0xFF);
        QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("item_%1.bin").arg(i)), contents));
    }

    const QString destination = directory.filePath(QStringLiteral("existing.mathom-backup"));
    const QByteArray previousContent("An older valid backup's bytes must not be deleted.");
    QVERIFY(writeFile(destination, previousContent));

    {
        ScopedFileSizeLimit limit(8192);
        QVERIFY(limit.isActive());
        BackupWorker backup(destination, source);
        backup.run();
        QVERIFY(!backup.success());
        QVERIFY2(backup.errorString().contains(QStringLiteral("archive"), Qt::CaseInsensitive),
                 qPrintable(backup.errorString()));
    }

    QCOMPARE(readFile(destination), previousContent);
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden),
             QStringList{QStringLiteral("existing.mathom-backup")});
#endif
}

void BackupTest::preservesExistingBackupWhenPromotionFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source"));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("note.txt")), QByteArray("new note")));
    const QString destinationFolder = directory.filePath(QStringLiteral("backups"));
    const QString destination = QDir(destinationFolder).filePath(QStringLiteral("previous.mathom-backup"));
    const QByteArray previousContent("previous backup must survive");
    QVERIFY(writeFile(destination, previousContent));

    const QFileDevice::Permissions permissions = QFileInfo(destinationFolder).permissions();
    QVERIFY(QFile::setPermissions(destinationFolder, QFileDevice::ReadOwner | QFileDevice::ExeOwner));

    // A privileged process can still create temporary files here. Run this case
    // as an ordinary user to exercise the final atomic promotion failure.
    QFile permissionProbe(QDir(destinationFolder).filePath(QStringLiteral("permission-probe")));
    if (permissionProbe.open(QIODevice::WriteOnly)) {
        permissionProbe.close();
        permissionProbe.remove();
        const bool restored = QFile::setPermissions(destinationFolder, permissions);
        QVERIFY(restored);
        QSKIP("The current user bypasses directory write permissions; run without root privileges.");
    }

    BackupWorker backup(destination, source);
    backup.run();
    const bool restored = QFile::setPermissions(destinationFolder, permissions);

    QVERIFY(restored);
    QVERIFY(!backup.success());
    QCOMPARE(backup.errorString(), QStringLiteral("Cannot create backup destination"));
    QCOMPARE(readFile(destination), previousContent);
    QCOMPARE(QDir(destinationFolder).entryList(QDir::Files | QDir::Hidden),
             QStringList{QStringLiteral("previous.mathom-backup")});
}

void BackupTest::reportsDestinationErrorWithoutPartialArchive()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source"));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("note.txt")), QByteArray("new note")));
    const QString blockedParent = directory.filePath(QStringLiteral("blocked"));
    const QByteArray blockerContent("a file cannot be an archive directory");
    QVERIFY(writeFile(blockedParent, blockerContent));
    const QString destination = QDir(blockedParent).filePath(QStringLiteral("new.mathom-backup"));

    BackupWorker backup(destination, source);
    backup.run();

    QVERIFY(!backup.success());
    QCOMPARE(backup.errorString(), QStringLiteral("Cannot create backup destination"));
    QCOMPARE(readFile(blockedParent), blockerContent);
    QVERIFY(!QFileInfo::exists(destination));
}

void BackupTest::replacesExistingBackupWithRestorableContent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source"));
    const QByteArray noteContent("A note with accents: \xc3\xa9\xc3\xa0\n");
    const QByteArray attachmentContent = QByteArray::fromHex("00010203007f80feff");
    const QByteArray hiddenContent("hidden attachment");
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("baskets/page/note.html")), noteContent));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("baskets/page/attachment.bin")), attachmentContent));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("baskets/page/.hidden")), hiddenContent));

    const QString destination = directory.filePath(QStringLiteral("previous.mathom-backup"));
    const QByteArray previousContent("obsolete backup");
    QVERIFY(writeFile(destination, previousContent));

    BackupWorker backup(destination, source);
    backup.run();

    QVERIFY2(backup.success(), qPrintable(backup.errorString()));
    QVERIFY(readFile(destination) != previousContent);

    const QString restored = directory.filePath(QStringLiteral("restored"));
    RestoreWorker restore(destination, restored);
    restore.run();

    QVERIFY2(restore.success(), qPrintable(restore.errorString()));
    QVERIFY(!restore.isLegacyBackup());
    QCOMPARE(restore.fileCount(), 3);
    QCOMPARE(restore.totalBytes(), noteContent.size() + attachmentContent.size() + hiddenContent.size());
    QCOMPARE(readFile(QDir(restored).filePath(QStringLiteral("data/baskets/page/note.html"))), noteContent);
    QCOMPARE(readFile(QDir(restored).filePath(QStringLiteral("data/baskets/page/attachment.bin"))), attachmentContent);
    QCOMPARE(readFile(QDir(restored).filePath(QStringLiteral("data/baskets/page/.hidden"))), hiddenContent);
}

int main(int argc, char **argv)
{
    // Backups include application settings as well as notes. Isolate every
    // configuration/data location before constructing the application.
    QTemporaryDir environment;
    if (!environment.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(environment.filePath(QStringLiteral("config"))));
    qputenv("XDG_DATA_HOME", QFile::encodeName(environment.filePath(QStringLiteral("data"))));
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("mathom-backup-test"));

    BackupTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "backuptest.moc"
