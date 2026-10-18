/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "backup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

namespace
{

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
    void preservesExistingBackupWhenPromotionFails();
    void reportsDestinationErrorWithoutPartialArchive();
    void replacesExistingBackupWithRestorableContent();
};

void BackupTest::preservesExistingBackupWhenSnapshotFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString destination = directory.filePath(QStringLiteral("previous.mathom-backup"));
    const QByteArray previousContent = QByteArray("previous backup must survive") + '\0' + QByteArray("unchanged");
    QVERIFY(writeFile(destination, previousContent));

    BackupWorker backup(destination, directory.filePath(QStringLiteral("missing-source")));
    backup.run();

    QVERIFY(!backup.success());
    QVERIFY(!backup.errorString().isEmpty());
    QCOMPARE(readFile(destination), previousContent);
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden),
             QStringList{QStringLiteral("previous.mathom-backup")});
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
