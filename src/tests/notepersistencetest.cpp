/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <basketscene.h>
#include <basketstatusbar.h>
#include <bnpview.h>
#include <global.h>
#include <note.h>
#include <notecontent.h>
#include <pendingsave.h>

#include <KActionCollection>
#include <KDirWatch>
#include <KSharedConfig>

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QMimeData>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {
bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.flush();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

class NotePersistenceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void failedSaveRetainsLatestContent_data();
    void failedSaveRetainsLatestContent();
    void fileTransfersWaitForPendingContent();
    void cleanup()
    {
        QApplication::clipboard()->clear();
        QVERIFY(!PendingSave::hasPendingSaves());
    }

private:
    // Global::savesFolder caches its first path, so it must remain valid for all rows.
    QTemporaryDir m_profile;
};

void NotePersistenceTest::initTestCase()
{
    QVERIFY(m_profile.isValid());
    QStandardPaths::setTestModeEnabled(true);
    Global::setCustomSavesFolder(m_profile.path());
    Global::basketConfig = KSharedConfig::openConfig(m_profile.filePath(QStringLiteral("mathomrc")), KConfig::SimpleConfig);
}

void NotePersistenceTest::failedSaveRetainsLatestContent_data()
{
    QTest::addColumn<bool>("richText");
    QTest::newRow("text") << false;
    QTest::newRow("html") << true;
}

void NotePersistenceTest::failedSaveRetainsLatestContent()
{
    QFETCH(bool, richText);
    QStatusBar statusBar;
    KActionCollection actions(nullptr);
    BNPView view(nullptr, nullptr, &actions, new BasketStatusBar(&statusBar));
    const QString folderName = richText ? QStringLiteral("pending-html") : QStringLiteral("pending-text");
    BasketScene *basket = view.loadBasket(folderName);
    QVERIFY(basket);
    const QString directory = QDir::cleanPath(basket->fullPath());
    QVERIFY(QDir().mkpath(directory));
    const QString fileName = richText ? QStringLiteral("note.html") : QStringLiteral("note.txt");
    const QString path = basket->fullPathForFileName(fileName);
    const QString original = richText ? QStringLiteral("<p>Original enregistré</p>") : QStringLiteral("Original enregistré");
    QVERIFY(writeFile(path, original.toUtf8()));

    auto *note = new Note(basket);
    NoteContent *content = richText ? static_cast<NoteContent *>(new HtmlContent(note, fileName, true))
                                    : static_cast<NoteContent *>(new TextContent(note, fileName, true));
    basket->appendNoteAfter(note, nullptr);
    QCOMPARE(basket->noteForFullPath(path), note);
    auto *watcher = basket->findChild<KDirWatch *>();
    QVERIFY(watcher);
    QVERIFY(watcher->contains(path));

    const auto setContent = [content, richText](const QString &value) {
        if (richText)
            static_cast<HtmlContent *>(content)->setHtml(value, true);
        else
            static_cast<TextContent *>(content)->setText(value, true);
    };
    const auto currentContent = [content, richText] {
        return richText ? static_cast<HtmlContent *>(content)->html() : static_cast<TextContent *>(content)->text();
    };
    QCOMPARE(currentContent(), original);
    QVERIFY(!content->hasPendingSave());

    // A regular file at the directory's old path makes the failure deterministic,
    // even for root, while retaining the original bytes in the moved directory.
    const QString heldDirectory = directory + QStringLiteral("-unavailable");
    QVERIFY(QDir().rename(directory, heldDirectory));
    const QByteArray obstruction("Storage temporarily unavailable");
    QVERIFY(writeFile(directory, obstruction));
    const QString firstChange = richText ? QStringLiteral("<p>Première modification</p>") : QStringLiteral("Première modification");
    setContent(firstChange);
    QVERIFY(!content->saveToFile());
    QVERIFY(content->hasPendingSave());
    QVERIFY(PendingSave::hasPendingSaves(note));
    QCOMPARE(readFile(QDir(heldDirectory).filePath(fileName)), original.toUtf8());
    QCOMPARE(readFile(directory), obstruction);

    const QString latestChange = richText ? QStringLiteral("<p>Dernière modification : été <b>42</b></p>")
                                         : QStringLiteral("Dernière modification : été\n42");
    setContent(latestChange);

    // Invoke only these slots directly. No event loop is entered, so neither
    // BNPView::lateInit nor unrelated startup tasks can run in this fixture.
    QVERIFY(QMetaObject::invokeMethod(basket, "watchedFileModified", Qt::DirectConnection, Q_ARG(QString, path)));
    QVERIFY(QMetaObject::invokeMethod(basket, "updateModifiedNotes", Qt::DirectConnection));
    QCOMPARE(currentContent(), latestChange);
    QVERIFY(content->hasPendingSave());
    QVERIFY(QMetaObject::invokeMethod(basket, "watchedFileDeleted", Qt::DirectConnection, Q_ARG(QString, path)));
    QCOMPARE(basket->noteForFullPath(path), note);
    QVERIFY(watcher->contains(path));
    QCOMPARE(currentContent(), latestChange);

    QVERIFY(QFile::remove(directory));
    QVERIFY(QDir().rename(heldDirectory, directory));
    QVERIFY(PendingSave::retryPendingSaves(note));
    QVERIFY(!content->hasPendingSave());
    QVERIFY(!PendingSave::hasPendingSaves(note));
    QCOMPARE(readFile(path), latestChange.toUtf8());
    QCOMPARE(currentContent(), latestChange);
    QCOMPARE(content->fileName(), fileName);
    QVERIFY(watcher->contains(path));
}

void NotePersistenceTest::fileTransfersWaitForPendingContent()
{
    QStatusBar statusBar;
    KActionCollection actions(nullptr);
    BNPView view(nullptr, nullptr, &actions, new BasketStatusBar(&statusBar));
    BasketScene *basket = view.loadBasket(QStringLiteral("pending-transfer"));
    QVERIFY(basket);
    const QString directory = QDir::cleanPath(basket->fullPath());
    QVERIFY(QDir().mkpath(directory));
    const QString fileName = QStringLiteral("transfer.txt");
    const QString path = basket->fullPathForFileName(fileName);
    const QByteArray original("Original file contents");
    QVERIFY(writeFile(path, original));

    auto *note = new Note(basket);
    auto *content = new TextContent(note, fileName, true);
    basket->appendNoteAfter(note, nullptr);
    note->setSelected(true);
    QClipboard *clipboard = QApplication::clipboard();
    basket->noteCopy();
    const QString nativeFormat = QStringLiteral("application/x-basket-note");
    QVERIFY(clipboard->mimeData()->hasFormat(nativeFormat));
    const QByteArray previousClipboard = clipboard->mimeData()->data(nativeFormat);
    const QString previousText = clipboard->text();
    const QString previousCutFile = QDir(Global::tempCutFolder()).filePath(QStringLiteral("previous-cut.txt"));
    const QByteArray previousCut("Previous clipboard attachment");
    QVERIFY(writeFile(previousCutFile, previousCut));

    const QString heldDirectory = directory + QStringLiteral("-unavailable");
    QVERIFY(QDir().rename(directory, heldDirectory));
    QVERIFY(writeFile(directory, QByteArray("Storage unavailable")));
    content->setText(QStringLiteral("First unsaved change"), true);
    QVERIFY(!content->saveToFile());
    const QString latest = QStringLiteral("Latest change must survive a refused cut");
    content->setText(latest, true);
    QSignalSpy errors(&view, &BNPView::showErrorMessage);

    // These are the real copy/cut entry points. Refusal must happen before
    // clipboard replacement, a KIO move, or destruction of the pending owner.
    basket->noteCopy();
    QVERIFY(!errors.isEmpty());
    basket->noteCut();
    QVERIFY(errors.size() >= 2);
    QCOMPARE(basket->noteForFullPath(path), note);
    QCOMPARE(content->text(), latest);
    QVERIFY(content->hasPendingSave());
    QVERIFY(note->isSelected());
    QCOMPARE(clipboard->mimeData()->data(nativeFormat), previousClipboard);
    QCOMPARE(clipboard->text(), previousText);
    QCOMPARE(readFile(previousCutFile), previousCut);
    QCOMPARE(readFile(QDir(heldDirectory).filePath(fileName)), original);

    // Recovery is attempted by copy itself, without a timer or a global event
    // loop. Only the latest model may become the clipboard's file-backed data.
    QVERIFY(QFile::remove(directory));
    QVERIFY(QDir().rename(heldDirectory, directory));
    basket->noteCopy();
    QVERIFY(!content->hasPendingSave());
    QCOMPARE(readFile(path), latest.toUtf8());
    QCOMPARE(clipboard->text(), latest);
    QVERIFY(clipboard->mimeData()->hasFormat(nativeFormat));
    QCOMPARE(basket->noteForFullPath(path), note);
}

QTEST_MAIN(NotePersistenceTest)
#include "notepersistencetest.moc"
