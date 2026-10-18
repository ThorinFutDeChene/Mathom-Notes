/**
 * SPDX-FileCopyrightText: (C) 2021 Sebastian Engel <dev@sebastianengel.eu>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <KTar>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <archive.h>

namespace {
bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}
}

class ArchiveTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();

    void testExtractArchive();
    void testCreateArchive();
    void testContentComparison_data();
    void testContentComparison();
    void testUnreadableContent();

    void cleanupTestCase();

private:
    bool compareDirTree(const QString &toTestPath, const QString &referencePath);
    bool compareDirHashes(const QString &toTestPath, const QString &referencePath);
    QStringList createDirTree(const QString &path);

    QTemporaryDir m_sampleSourceDir;
};

QTEST_MAIN(ArchiveTest)

void ArchiveTest::testExtractArchive()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString referencePath = QFINDTESTDATA("archive/");

    // Test the Archive::extractArchive function
    QString referenceSource = m_sampleSourceDir.filePath(QStringLiteral("sample_source"));
    QString testArchive = referencePath + QStringLiteral("sample.baskets");
    QString testPath = tempDir.filePath(QStringLiteral("sample"));
    Archive::IOErrorCode ioCode = Archive::extractArchive(testArchive, testPath, false);

    QVERIFY2(ioCode == Archive::IOErrorCode::NoError, "An issue occurred while extracting .baskets archive");
    QVERIFY2(compareDirTree(testPath, referenceSource), "Extracted .baskets archive is not identical with the reference source");
    QVERIFY2(compareDirHashes(testPath, referenceSource), "Extracted .baskets archive content is not identical with the reference source (hashes different)");

    // Test the Archive::extractArchive function with incompatible file
    testArchive = referencePath + QStringLiteral("notABasket.baskets");
    testPath = QStringLiteral("notABasket/");
    ioCode = Archive::extractArchive(testArchive, testPath, false);

    QVERIFY2(ioCode == Archive::IOErrorCode::NotABasketArchive, ".baskets was not recoqnized as incompatible baskets file");

    // Test the Archive::extractArchive function with incompatible version
    testArchive = referencePath + QStringLiteral("incompatible.baskets");
    testPath = QStringLiteral("incompatible/");
    ioCode = Archive::extractArchive(testArchive, testPath, false);

    QVERIFY2(ioCode == Archive::IOErrorCode::IncompatibleBasketVersion, ".baskets was not recoqnized as incompatible baskets file");

    /// \todo prepare more tests
    // Test the Archive::extractArchive function with corrupt file
    // Test the Archive::extractArchive function with 0-length preview
    // Test the Archive::extractArchive function with 0-length tar archive
    // Test the Archive::extractArchive function with non integer values as size
}

void ArchiveTest::testCreateArchive()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString referencePath = QFINDTESTDATA("archive/");

    // Test the Archive::createArchiveFromSource function

    QString referenceSource = referencePath + QStringLiteral("sample.baskets");
    QString testArchive = tempDir.filePath(QStringLiteral("test.mathom"));

    QString testSourcePath = m_sampleSourceDir.filePath(QStringLiteral("sample_source/"));
    Archive::IOErrorCode ioCode = Archive::createArchiveFromSource(testSourcePath, testSourcePath + QStringLiteral("preview.png"), testArchive);

    QVERIFY2(ioCode == Archive::IOErrorCode::NoError, "An issue occurred while creating a .mathom archive");

    QFile nativeArchive(testArchive);
    QVERIFY(nativeArchive.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(nativeArchive.readLine().trimmed(), QByteArray("MathomNotes:archive"));
    nativeArchive.close();

    const QString extractedPath = tempDir.filePath(QStringLiteral("extracted-native"));
    ioCode = Archive::extractArchive(testArchive, extractedPath, false);
    QVERIFY2(ioCode == Archive::IOErrorCode::NoError, "The newly created .mathom archive could not be read back");
    QVERIFY2(compareDirTree(extractedPath, testSourcePath), "Extracted .mathom archive is not identical with the source");
    QVERIFY2(compareDirHashes(extractedPath, testSourcePath), "Extracted .mathom archive content differs from the source");

    /// \todo find a simple way to compare created archive. KTar could write specific meta data...

    //    auto hashRef = fileHash(referenceSource, QCryptographicHash::Sha256);
    //    auto hashTest = fileHash(testArchive, QCryptographicHash::Sha256);

    //    QVERIFY2(hashRef == hashTest, "Created .baskets archive is not identical with the reference (hashes different)");
}

void ArchiveTest::testContentComparison_data()
{
    QTest::addColumn<QString>("change");
    QTest::addColumn<bool>("matches");

    QTest::newRow("identical") << QString() << true;
    QTest::newRow("one-byte-changed") << QStringLiteral("byte") << false;
    QTest::newRow("missing-file") << QStringLiteral("missing") << false;
    QTest::newRow("extra-file") << QStringLiteral("extra") << false;
    QTest::newRow("renamed-file") << QStringLiteral("rename") << false;
    QTest::newRow("hidden-file-changed") << QStringLiteral("hidden") << false;
    QTest::newRow("same-concatenation-different-boundaries") << QStringLiteral("boundaries") << false;
    QTest::newRow("extra-empty-directory") << QStringLiteral("empty-directory") << false;
    QTest::newRow("file-became-directory") << QStringLiteral("type-change") << false;
}

void ArchiveTest::testContentComparison()
{
    QFETCH(QString, change);
    QFETCH(bool, matches);

    QTemporaryDir reference;
    QTemporaryDir actual;
    QVERIFY(reference.isValid());
    QVERIFY(actual.isValid());

    for (const QString &root : {reference.path(), actual.path()}) {
        QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("a.txt")), QByteArray("ab")));
        QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("b.txt")), QByteArray("c")));
        QVERIFY(writeFile(QDir(root).filePath(QStringLiteral(".basket")), QByteArray("metadata")));
        QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("empty-directory"))));
    }

    if (change == QStringLiteral("byte"))
        QVERIFY(writeFile(actual.filePath(QStringLiteral("a.txt")), QByteArray("ac")));
    else if (change == QStringLiteral("missing"))
        QVERIFY(QFile::remove(actual.filePath(QStringLiteral("a.txt"))));
    else if (change == QStringLiteral("extra"))
        QVERIFY(writeFile(actual.filePath(QStringLiteral("extra.txt")), QByteArray("extra")));
    else if (change == QStringLiteral("rename"))
        QVERIFY(QFile::rename(actual.filePath(QStringLiteral("a.txt")), actual.filePath(QStringLiteral("renamed.txt"))));
    else if (change == QStringLiteral("hidden"))
        QVERIFY(writeFile(actual.filePath(QStringLiteral(".basket")), QByteArray("changed!")));
    else if (change == QStringLiteral("boundaries")) {
        // Same concatenated bytes ("abc"), but different content in each file.
        QVERIFY(writeFile(actual.filePath(QStringLiteral("a.txt")), QByteArray("a")));
        QVERIFY(writeFile(actual.filePath(QStringLiteral("b.txt")), QByteArray("bc")));
    } else if (change == QStringLiteral("empty-directory"))
        QVERIFY(QDir().mkpath(actual.filePath(QStringLiteral("another-empty-directory"))));
    else if (change == QStringLiteral("type-change")) {
        QVERIFY(QFile::remove(actual.filePath(QStringLiteral("a.txt"))));
        QVERIFY(QDir().mkpath(actual.filePath(QStringLiteral("a.txt"))));
    }

    QCOMPARE(compareDirHashes(actual.path(), reference.path()), matches);
    QVERIFY(!compareDirHashes(actual.filePath(QStringLiteral("nonexistent")), reference.filePath(QStringLiteral("nonexistent"))));
}

void ArchiveTest::testUnreadableContent()
{
#ifndef Q_OS_UNIX
    QSKIP("This test requires a dangling symbolic link.");
#else
    QTemporaryDir reference;
    QTemporaryDir actual;
    QVERIFY(reference.isValid());
    QVERIFY(actual.isValid());
    QVERIFY(writeFile(reference.filePath(QStringLiteral("note.txt")), QByteArray("content")));
    QVERIFY(QFile::link(actual.filePath(QStringLiteral("missing")), actual.filePath(QStringLiteral("note.txt"))));
    QVERIFY(compareDirTree(actual.path(), reference.path()));
    QVERIFY(!compareDirHashes(actual.path(), reference.path()));
#endif
}

void ArchiveTest::initTestCase()
{
    QVERIFY(m_sampleSourceDir.isValid());

    const QString referenceData = QFINDTESTDATA("archive/sample_source.tar.gz");

    KTar archive(referenceData, QStringLiteral("application/x-gzip"));

    // Open the archive
    QVERIFY(archive.open(QIODevice::ReadOnly));
    QString destination = m_sampleSourceDir.path();
    QVERIFY(archive.directory()->copyTo(destination, true));
    QVERIFY(archive.close());
}

void ArchiveTest::cleanupTestCase()
{
}

QStringList ArchiveTest::createDirTree(const QString &path)
{
    QStringList relativePaths;
    const QDir root(path);
    QDirIterator it(root.absolutePath(),
                    QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        relativePaths.append(root.relativeFilePath(it.next()));
    relativePaths.sort();
    return relativePaths;
}

bool ArchiveTest::compareDirTree(const QString &toTestPath, const QString &referencePath)
{
    // Two missing roots must never be accepted as two identical empty archives.
    return QDir(toTestPath).exists() && QDir(referencePath).exists()
        && createDirTree(toTestPath) == createDirTree(referencePath);
}

bool ArchiveTest::compareDirHashes(const QString &toTestPath, const QString &referencePath)
{
    if (!compareDirTree(toTestPath, referencePath))
        return false;

    const QDir actualDir(toTestPath);
    const QDir referenceDir(referencePath);
    for (const QString &relativePath : createDirTree(referencePath)) {
        const QFileInfo actualInfo(actualDir.filePath(relativePath));
        const QFileInfo referenceInfo(referenceDir.filePath(relativePath));

        // Do not report an unsupported or unreadable symlink as identical.
        if (actualInfo.isSymLink() || referenceInfo.isSymLink())
            return false;
        if (actualInfo.isDir() != referenceInfo.isDir())
            return false;
        if (actualInfo.isDir())
            continue;
        if (!actualInfo.isFile() || !referenceInfo.isFile())
            return false;

        QFile actual(actualInfo.absoluteFilePath());
        QFile reference(referenceInfo.absoluteFilePath());
        if (!actual.open(QIODevice::ReadOnly) || !reference.open(QIODevice::ReadOnly))
            return false;

        // Hash every corresponding file separately: aggregating all bytes
        // could miss content moved across file boundaries.
        QCryptographicHash actualHash(QCryptographicHash::Sha256);
        QCryptographicHash referenceHash(QCryptographicHash::Sha256);
        if (!actualHash.addData(&actual) || !referenceHash.addData(&reference)
            || actual.error() != QFileDevice::NoError || reference.error() != QFileDevice::NoError
            || actualHash.result() != referenceHash.result())
            return false;
    }
    return true;
}

#include "archivetest.moc"
/* vim: set et sts=4 sw=4 ts=8 tw=0 : */
