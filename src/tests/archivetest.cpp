/**
 * SPDX-FileCopyrightText: (C) 2021 Sebastian Engel <dev@sebastianengel.eu>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <KTar>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <archive.h>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#endif

namespace {
bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.flush();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

#ifdef Q_OS_UNIX
// Exercise real short writes without filling a disk or relying on permissions
// that root can bypass. Always restore both process settings on scope exit.
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
    void testExtractCorruption_data();
    void testExtractCorruption();
    void testExtractionPreservesDestination();
    void testExtractionDestinationFailures();
    void testCompatibleHeaders_data();
    void testCompatibleHeaders();
    void testCreateFailuresPreserveDestination();
    void testEmptyPreview();
    void testArchiveWriteFailurePreservesDestination();
    void testExtractionWriteFailurePreservesDestination();

private:
    bool compareDirTree(const QString &toTestPath, const QString &referencePath);
    bool compareDirHashes(const QString &toTestPath, const QString &referencePath);
    QStringList createDirTree(const QString &path);
    QTemporaryDir m_sampleSourceDir;
};

QTEST_MAIN(ArchiveTest)

void ArchiveTest::initTestCase()
{
    QVERIFY(m_sampleSourceDir.isValid());
    const QString referenceData = QFINDTESTDATA("archive/sample_source.tar.gz");
    KTar archive(referenceData, QStringLiteral("application/x-gzip"));
    QVERIFY(archive.open(QIODevice::ReadOnly));
    QVERIFY(archive.directory()->copyTo(m_sampleSourceDir.path(), true));
    QVERIFY(archive.close());
}

void ArchiveTest::testExtractArchive()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString referencePath = QFINDTESTDATA("archive/");
    const QString referenceSource = m_sampleSourceDir.filePath(QStringLiteral("sample_source"));
    const QString testPath = temporary.filePath(QStringLiteral("sample"));
    QCOMPARE(Archive::extractArchive(referencePath + QStringLiteral("sample.baskets"), testPath), Archive::IOErrorCode::NoError);
    QVERIFY2(compareDirTree(testPath, referenceSource), "Legacy archive paths differ from the reference");
    QVERIFY2(compareDirHashes(testPath, referenceSource), "Legacy archive contents differ from the reference");

    QCOMPARE(Archive::extractArchive(referencePath + QStringLiteral("notABasket.baskets"), temporary.filePath(QStringLiteral("invalid"))),
             Archive::IOErrorCode::NotABasketArchive);
    QCOMPARE(Archive::extractArchive(referencePath + QStringLiteral("incompatible.baskets"), temporary.filePath(QStringLiteral("incompatible"))),
             Archive::IOErrorCode::IncompatibleBasketVersion);
    QCOMPARE(Archive::extractArchive(temporary.filePath(QStringLiteral("missing.mathom")), temporary.filePath(QStringLiteral("missing"))),
             Archive::IOErrorCode::FailedToOpenResource);
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("missing"))));
}

void ArchiveTest::testCreateArchive()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = m_sampleSourceDir.filePath(QStringLiteral("sample_source"));
    const QString archivePath = temporary.filePath(QStringLiteral("test.mathom"));
    QCOMPARE(Archive::createArchiveFromSource(source, QDir(source).filePath(QStringLiteral("preview.png")), archivePath), Archive::IOErrorCode::NoError);

    QFile archive(archivePath);
    QVERIFY(archive.open(QIODevice::ReadOnly));
    QCOMPARE(archive.readLine(), QByteArray("MathomNotes:archive\n"));
    archive.close();

    const QString extractedPath = temporary.filePath(QStringLiteral("extracted"));
    QCOMPARE(Archive::extractArchive(archivePath, extractedPath), Archive::IOErrorCode::NoError);
    QVERIFY2(compareDirTree(extractedPath, source), "Native archive paths differ from the source");
    QVERIFY2(compareDirHashes(extractedPath, source), "Native round trip changed a file's contents");
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
    QTest::newRow("hidden-metadata-changed") << QStringLiteral("hidden") << false;
    QTest::newRow("same-concatenated-bytes-different-files") << QStringLiteral("boundaries") << false;
}

void ArchiveTest::testContentComparison()
{
    QFETCH(QString, change);
    QFETCH(bool, matches);
    QTemporaryDir reference;
    QTemporaryDir actual;
    QVERIFY(reference.isValid());
    QVERIFY(actual.isValid());
    for (const QString &path : {reference.path(), actual.path()}) {
        QVERIFY(writeFile(QDir(path).filePath(QStringLiteral("a.txt")), QByteArray("ab")));
        QVERIFY(writeFile(QDir(path).filePath(QStringLiteral("b.txt")), QByteArray("c")));
        QVERIFY(writeFile(QDir(path).filePath(QStringLiteral(".basket")), QByteArray("metadata")));
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
        QVERIFY(writeFile(actual.filePath(QStringLiteral("a.txt")), QByteArray("a")));
        QVERIFY(writeFile(actual.filePath(QStringLiteral("b.txt")), QByteArray("bc")));
    }
    QCOMPARE(compareDirHashes(actual.path(), reference.path()), matches);
    QVERIFY(!compareDirHashes(actual.filePath(QStringLiteral("absent")), reference.filePath(QStringLiteral("absent"))));
}

void ArchiveTest::testUnreadableContent()
{
#ifndef Q_OS_UNIX
    QSKIP("This test uses a Unix dangling symbolic link to make a file unreadable, including when tests run as root.");
#else
    QTemporaryDir reference;
    QTemporaryDir actual;
    QVERIFY(reference.isValid());
    QVERIFY(actual.isValid());
    QVERIFY(writeFile(reference.filePath(QStringLiteral("note.txt")), QByteArray("content")));
    QVERIFY(QFile::link(actual.filePath(QStringLiteral("missing")), actual.filePath(QStringLiteral("note.txt"))));
    // The relative filenames match, but opening the dangling target must fail.
    QVERIFY(compareDirTree(actual.path(), reference.path()));
    QVERIFY(!compareDirHashes(actual.path(), reference.path()));
#endif
}

void ArchiveTest::testExtractCorruption_data()
{
    QTest::addColumn<QByteArray>("contents");
    const QByteArray header("MathomNotes:archive\nversion:1.0\n");
    QTest::newRow("header-only") << header;
    QTest::newRow("non-integer-size") << header + "preview*:abc\n";
    QTest::newRow("negative-size") << header + "preview*:-1\n";
    QTest::newRow("oversized-preview") << header + "preview*:1000\nsmall";
    QTest::newRow("size-overflow") << header + "preview*:18446744073709551615\n";
    QTest::newRow("missing-tar") << header + "preview*:0\n";
    QTest::newRow("empty-tar") << header + "archive*:0\n";
    QTest::newRow("invalid-tar") << header + "archive*:3\nbad";
    QTest::newRow("truncated-unknown-block") << header + "future*:100\nshort";
    QByteArray legacy = readFile(QFINDTESTDATA("archive/sample.baskets"));
    QVERIFY(!legacy.isEmpty());
    QTest::newRow("duplicate-archive") << legacy + "archive*:0\n";
    QTest::newRow("duplicate-preview") << legacy + "preview*:0\n";
    legacy.chop(1);
    QTest::newRow("truncated-real-archive") << legacy;
}

void ArchiveTest::testExtractCorruption()
{
    QFETCH(QByteArray, contents);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString archivePath = temporary.filePath(QStringLiteral("invalid.mathom"));
    const QString destination = temporary.filePath(QStringLiteral("extracted"));
    QVERIFY(writeFile(archivePath, contents));
    QCOMPARE(Archive::extractArchive(archivePath, destination), Archive::IOErrorCode::CorruptedBasketArchive);
    QVERIFY(!QFileInfo::exists(destination));
}

void ArchiveTest::testExtractionPreservesDestination()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString destination = temporary.filePath(QStringLiteral("existing"));
    const QString sentinel = QDir(destination).filePath(QStringLiteral("keep.txt"));
    const QByteArray previous("Existing extraction must survive a failed replacement.");
    QVERIFY(QDir().mkpath(destination));
    QVERIFY(writeFile(sentinel, previous));
    const QString invalidArchive = temporary.filePath(QStringLiteral("invalid.mathom"));
    QVERIFY(writeFile(invalidArchive, QByteArray("MathomNotes:archive\nversion:1.0\n")));
    QCOMPARE(Archive::extractArchive(invalidArchive, destination, false), Archive::IOErrorCode::CorruptedBasketArchive);
    QCOMPARE(readFile(sentinel), previous);
    QCOMPARE(Archive::extractArchive(temporary.filePath(QStringLiteral("absent")), destination, false), Archive::IOErrorCode::FailedToOpenResource);
    QCOMPARE(readFile(sentinel), previous);

    const QString archivePath = QFINDTESTDATA("archive/sample.baskets");
    QCOMPARE(Archive::extractArchive(archivePath, destination), Archive::IOErrorCode::DestinationExists);
    QCOMPARE(readFile(sentinel), previous);
    QCOMPARE(Archive::extractArchive(archivePath, destination, false), Archive::IOErrorCode::NoError);
    QVERIFY(!QFileInfo::exists(sentinel));
    QVERIFY(compareDirHashes(destination, m_sampleSourceDir.filePath(QStringLiteral("sample_source"))));
}

void ArchiveTest::testExtractionDestinationFailures()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString occupied = temporary.filePath(QStringLiteral("occupied"));
    const QByteArray previous("A regular file is not an extraction directory.");
    QVERIFY(writeFile(occupied, previous));
    const QString archivePath = QFINDTESTDATA("archive/sample.baskets");
    QCOMPARE(Archive::extractArchive(archivePath, occupied, false), Archive::IOErrorCode::FailedToOpenResource);
    QCOMPARE(readFile(occupied), previous);
    QCOMPARE(Archive::extractArchive(archivePath, occupied + QStringLiteral("/child")), Archive::IOErrorCode::FailedToOpenResource);
    QCOMPARE(readFile(occupied), previous);
}

void ArchiveTest::testCompatibleHeaders_data()
{
    QTest::addColumn<QByteArray>("versionHeaders");
    QTest::addColumn<int>("expectedResult");
    QTest::newRow("read-compatible") << QByteArray("version:9.9.9\nread-compatible:0.6.1\n")
                                    << static_cast<int>(Archive::IOErrorCode::PossiblyCompatibleBasketVersion);
    QTest::newRow("write-compatible") << QByteArray("version:9.9.9\nwrite-compatible:0.6.1\n")
                                     << static_cast<int>(Archive::IOErrorCode::NoError);
    QTest::newRow("unknown-binary-extension") << QByteArray("version:0.6.1\nfuture*:5\nabcde")
                                             << static_cast<int>(Archive::IOErrorCode::NoError);
}

void ArchiveTest::testCompatibleHeaders()
{
    QFETCH(QByteArray, versionHeaders);
    QFETCH(int, expectedResult);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QByteArray contents = readFile(QFINDTESTDATA("archive/sample.baskets"));
    QVERIFY(contents.contains("version:0.6.1\n"));
    contents.replace("version:0.6.1\n", versionHeaders);
    const QString archivePath = temporary.filePath(QStringLiteral("compatible.baskets"));
    const QString destination = temporary.filePath(QStringLiteral("extracted"));
    QVERIFY(writeFile(archivePath, contents));
    QCOMPARE(static_cast<int>(Archive::extractArchive(archivePath, destination)), expectedResult);
    QVERIFY(compareDirHashes(destination, m_sampleSourceDir.filePath(QStringLiteral("sample_source"))));
}

void ArchiveTest::testCreateFailuresPreserveDestination()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = m_sampleSourceDir.filePath(QStringLiteral("sample_source"));
    const QString preview = QDir(source).filePath(QStringLiteral("preview.png"));
    const QString destination = temporary.filePath(QStringLiteral("existing.mathom"));
    const QByteArray previous("Existing archive must survive a failed replacement.");
    QVERIFY(writeFile(destination, previous));
    QCOMPARE(Archive::createArchiveFromSource(source, preview, destination), Archive::IOErrorCode::DestinationExists);
    QCOMPARE(readFile(destination), previous);
    QCOMPARE(Archive::createArchiveFromSource(temporary.filePath(QStringLiteral("missing-source")), preview, destination, false),
             Archive::IOErrorCode::FailedToOpenResource);
    QCOMPARE(readFile(destination), previous);
    QCOMPARE(Archive::createArchiveFromSource(source, temporary.filePath(QStringLiteral("missing-preview.png")), destination, false),
             Archive::IOErrorCode::FailedToOpenResource);
    QCOMPARE(readFile(destination), previous);

    const QString directoryTarget = temporary.filePath(QStringLiteral("directory.mathom"));
    QVERIFY(QDir().mkpath(directoryTarget));
    const QString sentinel = QDir(directoryTarget).filePath(QStringLiteral("keep.txt"));
    QVERIFY(writeFile(sentinel, previous));
    QCOMPARE(Archive::createArchiveFromSource(source, preview, directoryTarget, false), Archive::IOErrorCode::FailedToOpenResource);
    QCOMPARE(readFile(sentinel), previous);
    QCOMPARE(Archive::createArchiveFromSource(source, preview, destination + QStringLiteral("/child.mathom"), false), Archive::IOErrorCode::FailedToOpenResource);
    QCOMPARE(readFile(destination), previous);

    QCOMPARE(Archive::createArchiveFromSource(source, preview, destination, false), Archive::IOErrorCode::NoError);
    const QString extracted = temporary.filePath(QStringLiteral("replaced"));
    QCOMPARE(Archive::extractArchive(destination, extracted), Archive::IOErrorCode::NoError);
    QVERIFY(compareDirHashes(extracted, source));
}

void ArchiveTest::testEmptyPreview()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = temporary.filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral(".metadata")), QByteArray("Hidden metadata")));
    const QString preview = QDir(source).filePath(QStringLiteral("preview.png"));
    QVERIFY(writeFile(preview, QByteArray()));
    const QString archivePath = temporary.filePath(QStringLiteral("empty-preview.mathom"));
    const QString extracted = temporary.filePath(QStringLiteral("extracted"));
    QCOMPARE(Archive::createArchiveFromSource(source, preview, archivePath), Archive::IOErrorCode::NoError);
    QCOMPARE(Archive::extractArchive(archivePath, extracted), Archive::IOErrorCode::NoError);
    QVERIFY(compareDirHashes(extracted, source));
}

void ArchiveTest::testArchiveWriteFailurePreservesDestination()
{
#ifndef Q_OS_UNIX
    QSKIP("This test uses the Unix file-size limit to provoke a real short write.");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = temporary.filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("note.txt")), QByteArray("content")));
    const QString preview = QDir(source).filePath(QStringLiteral("preview.png"));
    QVERIFY(writeFile(preview, QByteArray(64 * 1024, 'p')));
    const QString destination = temporary.filePath(QStringLiteral("existing.mathom"));
    const QByteArray previous("Previous archive");
    QVERIFY(writeFile(destination, previous));
    {
        ScopedFileSizeLimit limit(8192);
        QVERIFY(limit.isActive());
        // The small tar fits; the larger envelope fails while writing its preview.
        QCOMPARE(Archive::createArchiveFromSource(source, preview, destination, false), Archive::IOErrorCode::FailedToOpenResource);
    }
    QCOMPARE(readFile(destination), previous);
#endif
}

void ArchiveTest::testExtractionWriteFailurePreservesDestination()
{
#ifndef Q_OS_UNIX
    QSKIP("This test uses the Unix file-size limit to provoke a real short write.");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = temporary.filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("large.txt")), QByteArray(1024 * 1024, 'x')));
    const QString preview = QDir(source).filePath(QStringLiteral("preview.png"));
    QVERIFY(writeFile(preview, QByteArray()));
    const QString archivePath = temporary.filePath(QStringLiteral("compressed.mathom"));
    QCOMPARE(Archive::createArchiveFromSource(source, preview, archivePath), Archive::IOErrorCode::NoError);
    QVERIFY(QFileInfo(archivePath).size() < 8192);
    const QString destination = temporary.filePath(QStringLiteral("existing"));
    QVERIFY(QDir().mkpath(destination));
    const QString sentinel = QDir(destination).filePath(QStringLiteral("keep.txt"));
    const QByteArray previous("Previous extraction");
    QVERIFY(writeFile(sentinel, previous));
    {
        ScopedFileSizeLimit limit(8192);
        QVERIFY(limit.isActive());
        // The compressed staging file fits, but its extracted file cannot.
        QCOMPARE(Archive::extractArchive(archivePath, destination, false), Archive::IOErrorCode::FailedToOpenResource);
    }
    QCOMPARE(readFile(sentinel), previous);
    QVERIFY(!QFileInfo::exists(QDir(destination).filePath(QStringLiteral("large.txt"))));
#endif
}

QStringList ArchiveTest::createDirTree(const QString &path)
{
    QStringList paths;
    const QDir directory(path);
    QDirIterator iterator(directory.absolutePath(), QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (iterator.hasNext())
        paths.append(directory.relativeFilePath(iterator.next()));
    paths.sort();
    return paths;
}

bool ArchiveTest::compareDirTree(const QString &toTestPath, const QString &referencePath)
{
    return QDir(toTestPath).exists() && QDir(referencePath).exists() && createDirTree(toTestPath) == createDirTree(referencePath);
}

bool ArchiveTest::compareDirHashes(const QString &toTestPath, const QString &referencePath)
{
    if (!compareDirTree(toTestPath, referencePath))
        return false;
    // Hash each file separately, with its relative path already matched. A
    // concatenated hash would miss bytes moved across file boundaries.
    const QStringList paths = createDirTree(referencePath);
    for (const QString &path : paths) {
        QFile actual(QDir(toTestPath).filePath(path));
        QFile reference(QDir(referencePath).filePath(path));
        if (!actual.open(QIODevice::ReadOnly) || !reference.open(QIODevice::ReadOnly))
            return false;
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
