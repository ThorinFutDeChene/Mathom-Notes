/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Point 4 only: the file and its Mathom reference must agree after renaming.
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
#include <QStatusBar>
#include <QTemporaryDir>
#include <QXmlStreamWriter>
#include <QtTest/QtTest>

namespace {
bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile output(path);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString &path)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        return {};
    return input.readAll();
}
}

class NoteContentRenameTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void renamePreservesBinaryData();
    void collisionCreatesUniqueFile();
    void missingSourcePreservesExistingFile();
    void invalidName_data();
    void invalidName();
    void renameFailureLeavesOriginal();
    void renameDirectoryCollision();
    void fileModelWatcherAndSavedReference();
};

void NoteContentRenameTest::renamePreservesBinaryData()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QByteArray original("contents\\0with binary data", 25);
    const QString before = temp.filePath(QStringLiteral("before.bin"));
    QVERIFY(writeBytes(before, original));

    QString error;
    QCOMPARE(NoteContent::renameFileOnDisk(before, QStringLiteral("after.bin"), &error),
             QStringLiteral("after.bin"));
    QVERIFY(error.isEmpty());
    QVERIFY(!QFile::exists(before));
    QCOMPARE(readBytes(temp.filePath(QStringLiteral("after.bin"))), original);

    // Asking again for the same filename must not create a -2 duplicate.
    QCOMPARE(NoteContent::renameFileOnDisk(temp.filePath(QStringLiteral("after.bin")),
                                           QStringLiteral("after.bin"), &error),
             QStringLiteral("after.bin"));
    QVERIFY(error.isEmpty());
    QCOMPARE(QDir(temp.path()).entryList(QDir::Files), QStringList{QStringLiteral("after.bin")});
}

void NoteContentRenameTest::collisionCreatesUniqueFile()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString original = temp.filePath(QStringLiteral("document.pdf"));
    const QString occupied = temp.filePath(QStringLiteral("rapport.pdf"));
    const QString occupied2 = temp.filePath(QStringLiteral("rapport-2.pdf"));
    QVERIFY(writeBytes(original, QByteArrayLiteral("document source")));
    QVERIFY(writeBytes(occupied, QByteArrayLiteral("document preexistant")));
    QVERIFY(writeBytes(occupied2, QByteArrayLiteral("autre document")));

    QString error;
    QCOMPARE(NoteContent::renameFileOnDisk(original, QStringLiteral("rapport.pdf"), &error),
             QStringLiteral("rapport-3.pdf"));
    QVERIFY(error.isEmpty());
    QVERIFY(!QFile::exists(original));
    QCOMPARE(readBytes(temp.filePath(QStringLiteral("rapport-3.pdf"))), QByteArrayLiteral("document source"));
    QCOMPARE(readBytes(occupied), QByteArrayLiteral("document preexistant"));
    QCOMPARE(readBytes(occupied2), QByteArrayLiteral("autre document"));
}

void NoteContentRenameTest::missingSourcePreservesExistingFile()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString occupied = temp.filePath(QStringLiteral("identique.txt"));
    QVERIFY(writeBytes(occupied, QByteArrayLiteral("ne pas effacer")));
    QString error;
    QVERIFY(NoteContent::renameFileOnDisk(temp.filePath(QStringLiteral("absent.txt")),
                                           QStringLiteral("identique.txt"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QCOMPARE(readBytes(occupied), QByteArrayLiteral("ne pas effacer"));
}

void NoteContentRenameTest::invalidName_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("empty") << QString();
    QTest::newRow("dot") << QStringLiteral(".");
    QTest::newRow("dotdot") << QStringLiteral("..");
    QTest::newRow("parent") << QStringLiteral("../outside.pdf");
    QTest::newRow("subfolder") << QStringLiteral("subdir/document.pdf");
    QTest::newRow("absolute") << QStringLiteral("/tmp/document.pdf");
    QTest::newRow("backslash") << QStringLiteral("dir\\document.pdf");
    QTest::newRow("null") << (QStringLiteral("doc") + QChar::Null + QStringLiteral(".pdf"));
}

void NoteContentRenameTest::invalidName()
{
    QFETCH(QString, name);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("important.pdf"));
    QVERIFY(writeBytes(source, QByteArrayLiteral("must survive")));
    QString error;
    QVERIFY(NoteContent::renameFileOnDisk(source, name, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QCOMPARE(readBytes(source), QByteArrayLiteral("must survive"));
}

void NoteContentRenameTest::renameFailureLeavesOriginal()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("important.pdf"));
    QVERIFY(writeBytes(source, QByteArrayLiteral("original intact")));
    QString error;
    QVERIFY(NoteContent::renameFileOnDisk(source, QString(4096, QLatin1Char('x')),
                                           &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QCOMPARE(readBytes(source), QByteArrayLiteral("original intact"));
}

void NoteContentRenameTest::renameDirectoryCollision()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir(temp.path()).mkdir(QStringLiteral("old")));
    QVERIFY(QDir(temp.path()).mkdir(QStringLiteral("new")));
    QVERIFY(writeBytes(temp.filePath(QStringLiteral("old/child.txt")), QByteArrayLiteral("old child")));
    QVERIFY(writeBytes(temp.filePath(QStringLiteral("new/child.txt")), QByteArrayLiteral("existing child")));
    QString error;
    QCOMPARE(NoteContent::renameFileOnDisk(temp.filePath(QStringLiteral("old")),
                                           QStringLiteral("new"), &error),
             QStringLiteral("new-2"));
    QVERIFY(error.isEmpty());
    QCOMPARE(readBytes(temp.filePath(QStringLiteral("new-2/child.txt"))), QByteArrayLiteral("old child"));
    QCOMPARE(readBytes(temp.filePath(QStringLiteral("new/child.txt"))), QByteArrayLiteral("existing child"));
}

void NoteContentRenameTest::fileModelWatcherAndSavedReference()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    Global::setCustomSavesFolder(temp.path());
    Global::basketConfig = KSharedConfig::openConfig(temp.filePath(QStringLiteral("mathomrc")), KConfig::SimpleConfig);

    QStatusBar statusBar;
    KActionCollection actions(static_cast<QObject *>(nullptr));
    BNPView view(nullptr, nullptr, &actions, new BasketStatusBar(&statusBar));
    BasketScene basket(nullptr, QStringLiteral("rename-test"));
    QVERIFY(QDir().mkpath(basket.fullPath()));
    const QString path = basket.fullPathForFileName(QStringLiteral("source.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("content to preserve")));

    Note note(&basket);
    auto *content = new FileContent(&note, QStringLiteral("source.txt"));
    auto *watcher = basket.findChild<KDirWatch *>();
    QVERIFY(watcher);
    QVERIFY(watcher->contains(path));

    QString error;
    QVERIFY(content->trySetFileName(QStringLiteral("target.txt"), &error));
    QVERIFY(error.isEmpty());
    QCOMPARE(content->fileName(), QStringLiteral("target.txt"));
    QVERIFY(!watcher->contains(path));
    QVERIFY(watcher->contains(content->fullPath()));
    QCOMPARE(readBytes(content->fullPath()), QByteArrayLiteral("content to preserve"));

    const QString occupied = basket.fullPathForFileName(QStringLiteral("occupied.txt"));
    QVERIFY(writeBytes(occupied, QByteArrayLiteral("not overwritten")));
    QVERIFY(content->trySetFileName(QStringLiteral("occupied.txt"), &error));
    QCOMPARE(content->fileName(), QStringLiteral("occupied-2.txt"));
    QVERIFY(!watcher->contains(basket.fullPathForFileName(QStringLiteral("target.txt"))));
    QVERIFY(watcher->contains(content->fullPath()));
    QCOMPARE(readBytes(occupied), QByteArrayLiteral("not overwritten"));
    QCOMPARE(readBytes(content->fullPath()), QByteArrayLiteral("content to preserve"));

    QString saved;
    QXmlStreamWriter writer(&saved);
    content->saveToNode(writer);
    QVERIFY(saved.contains(QStringLiteral(">occupied-2.txt</content>")));

    const QString accepted = content->fullPath();
    QVERIFY(!content->trySetFileName(QString(4096, QLatin1Char('x')), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(content->fullPath(), accepted);
    QCOMPARE(content->fileName(), QStringLiteral("occupied-2.txt"));
    QVERIFY(watcher->contains(accepted));
    QCOMPARE(readBytes(accepted), QByteArrayLiteral("content to preserve"));
    QCOMPARE(readBytes(occupied), QByteArrayLiteral("not overwritten"));
}

QTEST_MAIN(NoteContentRenameTest)
#include "notecontentrenametest.moc"
