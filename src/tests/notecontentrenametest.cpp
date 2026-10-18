/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <basketscene.h>
#include <basketstatusbar.h>
#include <bnpview.h>
#include <global.h>
#include <note.h>
#include <notecontent.h>

#include <KActionCollection>
#include <KDirWatch>
#include <KSharedConfig>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QXmlStreamWriter>
#include <QtTest/QtTest>

namespace
{
bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

class NoteContentRenameTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void renameFile();
    void collisionPreservesBothFiles();
    void missingSourcePreservesDestination();
    void invalidName_data();
    void invalidName();
    void filesystemFailurePreservesSource();
    void renameAttachedDirectory();
    void modelAndWatcherUseActualName();
};

void NoteContentRenameTest::renameFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray contents("attachment\0with binary data", 27);
    const QString source = directory.filePath(QStringLiteral("original.txt"));
    QVERIFY(writeFile(source, contents));

    QString error = QStringLiteral("previous error");
    const QString actualName = NoteContent::renameFileOnDisk(source, QStringLiteral("renamed.txt"), &error);

    QCOMPARE(actualName, QStringLiteral("renamed.txt"));
    QVERIFY(error.isEmpty());
    QVERIFY(!QFile::exists(source));
    QCOMPARE(readFile(directory.filePath(actualName)), contents);

    // A repeated save of the accepted name must not generate another suffix.
    QCOMPARE(NoteContent::renameFileOnDisk(directory.filePath(actualName), actualName, &error), actualName);
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList{actualName});
}

void NoteContentRenameTest::collisionPreservesBothFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("original.txt"));
    const QString occupied = directory.filePath(QStringLiteral("target.txt"));
    QVERIFY(writeFile(source, QByteArrayLiteral("original attachment")));
    QVERIFY(writeFile(occupied, QByteArrayLiteral("other attachment")));

    QString error;
    const QString actualName = NoteContent::renameFileOnDisk(source, QStringLiteral("target.txt"), &error);

    QCOMPARE(actualName, QStringLiteral("target-2.txt"));
    QVERIFY(error.isEmpty());
    QVERIFY(!QFile::exists(source));
    QCOMPARE(readFile(directory.filePath(actualName)), QByteArrayLiteral("original attachment"));
    QCOMPARE(readFile(occupied), QByteArrayLiteral("other attachment"));
}

void NoteContentRenameTest::missingSourcePreservesDestination()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString occupied = directory.filePath(QStringLiteral("target.txt"));
    QVERIFY(writeFile(occupied, QByteArrayLiteral("keep this attachment")));

    QString error;
    QVERIFY(NoteContent::renameFileOnDisk(directory.filePath(QStringLiteral("missing.txt")), QStringLiteral("target.txt"), &error).isEmpty());

    QVERIFY(!error.isEmpty());
    QCOMPARE(readFile(occupied), QByteArrayLiteral("keep this attachment"));
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList{QStringLiteral("target.txt")});
}

void NoteContentRenameTest::invalidName_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("empty") << QString();
    QTest::newRow("current-directory") << QStringLiteral(".");
    QTest::newRow("parent-directory") << QStringLiteral("..");
    QTest::newRow("relative-path") << QStringLiteral("../outside.txt");
    QTest::newRow("subdirectory") << QStringLiteral("subdirectory/target.txt");
    QTest::newRow("absolute-path") << QStringLiteral("/target.txt");
    QTest::newRow("embedded-null") << (QStringLiteral("target") + QChar(QChar::Null) + QStringLiteral(".txt"));
}

void NoteContentRenameTest::invalidName()
{
    QFETCH(QString, name);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("original.txt"));
    QVERIFY(writeFile(source, QByteArrayLiteral("keep the original")));

    QString error;
    QVERIFY(NoteContent::renameFileOnDisk(source, name, &error).isEmpty());

    QVERIFY(!error.isEmpty());
    QCOMPARE(readFile(source), QByteArrayLiteral("keep the original"));
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList{QStringLiteral("original.txt")});
}

void NoteContentRenameTest::filesystemFailurePreservesSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("original.txt"));
    QVERIFY(writeFile(source, QByteArrayLiteral("still available")));

    // Exceed the filesystem's filename limit; this fails even when tests run as root.
    QString error;
    QVERIFY(NoteContent::renameFileOnDisk(source, QString(4096, QLatin1Char('x')), &error).isEmpty());

    QVERIFY(!error.isEmpty());
    QCOMPARE(readFile(source), QByteArrayLiteral("still available"));
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList{QStringLiteral("original.txt")});
}

void NoteContentRenameTest::renameAttachedDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("source")));
    QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("target")));
    QVERIFY(writeFile(directory.filePath(QStringLiteral("source/child.txt")), QByteArrayLiteral("child attachment")));
    QVERIFY(writeFile(directory.filePath(QStringLiteral("target/other.txt")), QByteArrayLiteral("other directory")));

    QString error;
    const QString actualName = NoteContent::renameFileOnDisk(directory.filePath(QStringLiteral("source")), QStringLiteral("target"), &error);

    QCOMPARE(actualName, QStringLiteral("target-2"));
    QVERIFY(error.isEmpty());
    QVERIFY(!QDir(directory.filePath(QStringLiteral("source"))).exists());
    QCOMPARE(readFile(directory.filePath(actualName + QStringLiteral("/child.txt"))), QByteArrayLiteral("child attachment"));
    QCOMPARE(readFile(directory.filePath(QStringLiteral("target/other.txt"))), QByteArrayLiteral("other directory"));
}

void NoteContentRenameTest::modelAndWatcherUseActualName()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    QStandardPaths::setTestModeEnabled(true);
    Global::setCustomSavesFolder(profile.path());
    Global::basketConfig = KSharedConfig::openConfig(profile.filePath(QStringLiteral("mathomrc")), KConfig::SimpleConfig);

    QStatusBar statusBar;
    KActionCollection actions(static_cast<QObject *>(nullptr));
    BNPView view(nullptr, nullptr, &actions, new BasketStatusBar(&statusBar));
    BasketScene basket(nullptr, QStringLiteral("rename-test"));
    QVERIFY(QDir().mkpath(basket.fullPath()));
    const QString originalPath = basket.fullPathForFileName(QStringLiteral("original.txt"));
    QVERIFY(writeFile(originalPath, QByteArrayLiteral("my attachment")));

    // No event loop is entered here: the real widgets are exercised without running
    // BNPView::lateInit, which loads the user's document tree during normal startup.
    Note note(&basket);
    auto *content = new FileContent(&note, QStringLiteral("original.txt"));
    auto *watcher = basket.findChild<KDirWatch *>();
    QVERIFY(watcher);
    QVERIFY(watcher->contains(originalPath));

    QString error;
    QVERIFY(content->trySetFileName(QStringLiteral("renamed.txt"), &error));
    QCOMPARE(content->fileName(), QStringLiteral("renamed.txt"));
    QVERIFY(error.isEmpty());
    QVERIFY(!watcher->contains(originalPath));
    QVERIFY(watcher->contains(content->fullPath()));
    QCOMPARE(readFile(content->fullPath()), QByteArrayLiteral("my attachment"));

    const QString renamedPath = content->fullPath();
    const QString occupiedPath = basket.fullPathForFileName(QStringLiteral("target.txt"));
    QVERIFY(writeFile(occupiedPath, QByteArrayLiteral("other attachment")));
    QVERIFY(content->trySetFileName(QStringLiteral("target.txt"), &error));
    QCOMPARE(content->fileName(), QStringLiteral("target-2.txt"));
    QVERIFY(!watcher->contains(renamedPath));
    QVERIFY(watcher->contains(content->fullPath()));
    QCOMPARE(readFile(content->fullPath()), QByteArrayLiteral("my attachment"));
    QCOMPARE(readFile(occupiedPath), QByteArrayLiteral("other attachment"));

    QString xml;
    QXmlStreamWriter stream(&xml);
    content->saveToNode(stream);
    QVERIFY(xml.contains(QStringLiteral(">target-2.txt</content>")));

    const QString acceptedPath = content->fullPath();
    QVERIFY(!content->trySetFileName(QString(4096, QLatin1Char('x')), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(content->fileName(), QStringLiteral("target-2.txt"));
    QCOMPARE(content->fullPath(), acceptedPath);
    QVERIFY(watcher->contains(acceptedPath));
    QCOMPARE(readFile(acceptedPath), QByteArrayLiteral("my attachment"));
    QCOMPARE(readFile(occupiedPath), QByteArrayLiteral("other attachment"));

    // History transfers already move files and must retain a setter without I/O.
    content->setFileName(QStringLiteral("model-only.txt"));
    QCOMPARE(readFile(acceptedPath), QByteArrayLiteral("my attachment"));
    QVERIFY(!QFile::exists(content->fullPath()));
}

QTEST_MAIN(NoteContentRenameTest)

#include "notecontentrenametest.moc"
