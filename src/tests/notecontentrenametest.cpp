/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Point 4 only: the file and its Mathom reference must agree after renaming.
 */

#include <notecontent.h>


#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
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
    void danglingSymlinkCollisionIsPreserved();
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

void NoteContentRenameTest::danglingSymlinkCollisionIsPreserved()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("source.txt"));
    const QString target = temp.filePath(QStringLiteral("target.txt"));

    // A dangling symlink occupies a name even though QFileInfo::exists()
    // returns false. It must never be overwritten by an attachment rename.
    QVERIFY(QFile::link(temp.filePath(QStringLiteral("absent.txt")), target));
    QVERIFY(QFileInfo(target).isSymLink());
    QVERIFY(writeBytes(source, QByteArrayLiteral("attachment data")));

    QString error;
    QCOMPARE(NoteContent::renameFileOnDisk(source, QStringLiteral("target.txt"), &error),
             QStringLiteral("target-2.txt"));
    QVERIFY(error.isEmpty());
    QVERIFY(QFileInfo(target).isSymLink());
    QCOMPARE(readBytes(temp.filePath(QStringLiteral("target-2.txt"))),
             QByteArrayLiteral("attachment data"));
    QVERIFY(!QFile::exists(source));
}

QTEST_MAIN(NoteContentRenameTest)
#include "notecontentrenametest.moc"
