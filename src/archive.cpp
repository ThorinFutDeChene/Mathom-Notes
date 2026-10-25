/**
 * SPDX-FileCopyrightText: (C) 2006 by Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "archive.h"

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QList>
#include <QMap>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QProgressDialog>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QXmlStreamWriter>
#include <QtXml/QDomDocument>

#include <KAboutData>
#include <KIconLoader>
#include <KLocalizedString>
#include <KMainWindow> //For Global::MainWindow()
#include <KMessageBox>
#include <KTar>

#include "backgroundmanager.h"
#include "basketfactory.h"
#include "basketlistview.h"
#include "basketscene.h"
#include "bnpview.h"
#include "common.h"
#include "formatimporter.h"
#include "global.h"
#include "mathomicons.h"
#include "tag.h"
#include "tools.h"
#include "xmlwork.h"

#include <algorithm>
#include <array>

#include <basket_debug.h>

namespace {

// Keep the existing .mathom envelope byte-for-byte compatible with the
// historic exporter, while rejecting short reads, failed writes and commits.
// QSaveFile does not destroy an existing archive when a replacement fails.
bool writeMathomEnvelope(const QString &tarPath,
                         const QString &previewPath,
                         const QString &destination,
                         bool allMathomHouses)
{
    QFile compressed(tarPath);
    if (!compressed.open(QIODevice::ReadOnly))
        return false;

    QFile preview(previewPath);
    qint64 previewSize = 0;
    if (!previewPath.isEmpty()) {
        if (!preview.open(QIODevice::ReadOnly))
            return false;
        previewSize = preview.size();
        if (previewSize < 0)
            return false;
    }

    const qint64 compressedSize = compressed.size();
    if (compressedSize <= 0 || destination.isEmpty())
        return false;

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly))
        return false;

    const auto writeBytes = [&](const QByteArray &bytes) {
        return output.write(bytes) == bytes.size();
    };
    const auto copyBytes = [&](QFile &input, qint64 expectedSize) {
        std::array<char, 65536> buffer{};
        qint64 written = 0;
        while (written < expectedSize) {
            const qint64 count = input.read(
                buffer.data(), std::min<qint64>(buffer.size(), expectedSize - written));
            if (count <= 0 || output.write(buffer.data(), count) != count)
                return false;
            written += count;
        }
        return input.error() == QFileDevice::NoError && written == expectedSize;
    };

    const QByteArray header =
        QByteArrayLiteral("MathomNotes:archive\nversion:1.0\n")
        + (allMathomHouses ? QByteArrayLiteral("scope:all\n") : QByteArray())
        + QByteArrayLiteral("preview*:") + QByteArray::number(previewSize) + '\n';
    const QByteArray tarHeader =
        QByteArrayLiteral("archive*:") + QByteArray::number(compressedSize) + '\n';

    if (!writeBytes(header)
        || (previewSize > 0 && !copyBytes(preview, previewSize))
        || !writeBytes(tarHeader)
        || !copyBytes(compressed, compressedSize)) {
        output.cancelWriting();
        return false;
    }

    return output.commit();
}

} // namespace

void Archive::save(BasketScene *basket, bool withSubBaskets, const QString &destination)
{
    QDir dir;
    QProgressDialog dialog;
    dialog.setWindowTitle(i18n("Save as Mathom-House Archive"));
    dialog.setLabelText(i18n("Saving as Mathom-House archive. Please wait..."));
    dialog.setCancelButton(nullptr);
    dialog.setAutoClose(true);

    dialog.setRange(0,
                    /*Preparation:*/ 1 + /*Finishing:*/ 1 + /*Basket:*/ 1
                        + /*SubBaskets:*/ (withSubBaskets ? Global::bnpView->basketCount(Global::bnpView->listViewItemForBasket(basket)) : 0));
    dialog.setValue(0);
    dialog.show();

    // Create a clean temporary folder. A previous interrupted export must
    // never leave stale files that affect the next archive.
    QString tempFolder = Global::savesFolder() + QStringLiteral("temp-archive/");
    Tools::deleteRecursively(tempFolder);
    dir.mkpath(tempFolder);

    // Create the temporary archive file:
    QString tempDestination = tempFolder + QStringLiteral("temp-archive.tar.gz");
    KTar tar(tempDestination, QStringLiteral("application/x-gzip"));
    if (!tar.open(QIODevice::WriteOnly)) {
        KMessageBox::error(
            nullptr,
            i18n("Failed to create the temporary Mathom-House archive."),
            i18n("Mathom-House Archive Error"));
        QFile::remove(tempDestination);
        dir.rmdir(tempFolder);
        return;
    }

    tar.writeDir(QStringLiteral("baskets"), QString(), QString());

    dialog.setValue(dialog.value() + 1); // Preparation finished

    qCDebug(BASKET_LOG) << "Preparation finished out of " << dialog.maximum();

    // Copy the baskets data into the archive:
    QStringList backgrounds;
    Archive::saveBasketToArchive(basket, withSubBaskets, &tar, backgrounds, tempFolder, &dialog);

    // Create a Small baskets.xml Document:
    QString data;
    QXmlStreamWriter stream(&data);
    XMLWork::setupXmlStream(stream, QStringLiteral("basketTree"));
    Global::bnpView->saveSubHierarchy(Global::bnpView->listViewItemForBasket(basket), stream, withSubBaskets);
    stream.writeEndElement();
    stream.writeEndDocument();
    FileStorage::safelySaveToFile(tempFolder + QStringLiteral("baskets.xml"), data);
    tar.addLocalFile(tempFolder + QStringLiteral("baskets.xml"), QStringLiteral("baskets/baskets.xml"));
    dir.remove(tempFolder + QStringLiteral("baskets.xml"));

    // Save a Small tags.xml Document:
    QList<Tag *> tags;
    listUsedTags(basket, withSubBaskets, tags);
    Tag::saveTagsTo(tags, tempFolder + QStringLiteral("tags.xml"));
    tar.addLocalFile(tempFolder + QStringLiteral("tags.xml"), QStringLiteral("tags.xml"));
    dir.remove(tempFolder + QStringLiteral("tags.xml"));

    // Save Tag Emblems (in case they are loaded on a computer that do not have those icons):
    QString tempIconFile = tempFolder + QStringLiteral("icon.png");
    for (Tag::List::iterator it = tags.begin(); it != tags.end(); ++it) {
        State::List states = (*it)->states();
        for (State::List::iterator it2 = states.begin(); it2 != states.end(); ++it2) {
            State *state = (*it2);
            QPixmap icon = KIconLoader::global()->loadIcon(state->emblem(), KIconLoader::Small, 16, KIconLoader::DefaultState, QStringList(), nullptr, true);
            if (!icon.isNull()) {
                icon.save(tempIconFile, "PNG");
                QString iconFileName = state->emblem().replace(QLatin1Char('/'), QLatin1Char('_'));
                tar.addLocalFile(tempIconFile, QStringLiteral("tag-emblems/") + iconFileName);
            }
        }
    }
    dir.remove(tempIconFile);

    // Finish Tar.Gz Exportation:
    tar.close();

    // Computing the File Preview:
    BasketScene *previewBasket = basket; // FIXME: Use the first non-empty basket!
    // QPixmap previewPixmap(previewBasket->visibleWidth(), previewBasket->visibleHeight());
    QPixmap previewPixmap(previewBasket->width(), previewBasket->height());
    QPainter painter(&previewPixmap);
    // Save old state, and make the look clean ("smile, you are filmed!"):
    NoteSelection *selection = previewBasket->selectedNotes();
    previewBasket->unselectAll();
    Note *focusedNote = previewBasket->focusedNote();
    previewBasket->setFocusedNote(nullptr);
    previewBasket->doHoverEffects(nullptr, Note::None);
    // Take the screenshot:
    previewBasket->render(&painter);
    // Go back to the old look. selectedNotes() legitimately returns
    // nullptr when nothing was selected.
    if (selection)
        previewBasket->selectSelection(selection);
    previewBasket->setFocusedNote(focusedNote);
    previewBasket->doHoverEffects();
    // End and save our splandid painting:
    painter.end();
    QImage previewImage = previewPixmap.toImage();
    const int PREVIEW_SIZE = 256;
    previewImage = previewImage.scaled(PREVIEW_SIZE, PREVIEW_SIZE, Qt::KeepAspectRatio);
    previewImage.save(tempFolder + QStringLiteral("preview.png"), "PNG");

    // Finally Save to the Real Destination file:
    QFile file(destination);
    if (file.open(QIODevice::WriteOnly)) {
        ulong previewSize = QFile(tempFolder + QStringLiteral("preview.png")).size();
        ulong archiveSize = QFile(tempDestination).size();
        QTextStream stream(&file);
        // stream.setEncoding(QStringConverter::Latin1);
        stream << "MathomNotes:archive\n"
               << "version:1.0\n"
               //             << "read-compatible:0.6.1\n"
               //             << "write-compatible:0.6.1\n"
               << "preview*:" << previewSize << "\n";

        stream.flush();
        // Copy the Preview File:
        const unsigned long BUFFER_SIZE = 1024;
        char *buffer = new char[BUFFER_SIZE];
        long sizeRead;
        QFile previewFile(tempFolder + QStringLiteral("preview.png"));
        if (previewFile.open(QIODevice::ReadOnly)) {
            while ((sizeRead = previewFile.read(buffer, BUFFER_SIZE)) > 0)
                file.write(buffer, sizeRead);
        }
        stream << "archive*:" << archiveSize << "\n";
        stream.flush();

        // Copy the Archive File:
        QFile archiveFile(tempDestination);
        if (archiveFile.open(QIODevice::ReadOnly)) {
            while ((sizeRead = archiveFile.read(buffer, BUFFER_SIZE)) > 0)
                file.write(buffer, sizeRead);
        }
        // Clean Up:
        delete[] buffer;
        buffer = nullptr;
        file.close();
    }

    dialog.setValue(dialog.value() + 1); // Finishing finished
    qCDebug(BASKET_LOG) << "Finishing finished";

    // Clean Up Everything:
    dir.remove(tempFolder + QStringLiteral("preview.png"));
    dir.remove(tempDestination);
    dir.rmdir(tempFolder);
}


void Archive::saveAll(const QString &destination)
{
    if (!Global::bnpView || Global::bnpView->topLevelItemCount() <= 0)
        return;

    QDir dir;
    QProgressDialog dialog;
    dialog.setWindowTitle(i18n("Save All Mathom-Houses"));
    dialog.setLabelText(i18n("Saving all Mathom-Houses. Please wait..."));
    dialog.setCancelButton(nullptr);
    dialog.setAutoClose(true);

    int basketTotal = 0;
    for (int i = 0; i < Global::bnpView->topLevelItemCount(); ++i) {
        BasketListViewItem *item = Global::bnpView->topLevelItem(i);
        basketTotal += 1 + Global::bnpView->basketCount(item);
    }

    dialog.setRange(0, 2 + basketTotal);
    dialog.setValue(0);
    dialog.show();

    const QString tempFolder =
        Global::savesFolder() + QStringLiteral("temp-archive/");

    Tools::deleteRecursively(tempFolder);
    dir.mkpath(tempFolder);

    const QString tempDestination =
        tempFolder + QStringLiteral("temp-archive.tar.gz");

    KTar tar(tempDestination, QStringLiteral("application/x-gzip"));
    if (!tar.open(QIODevice::WriteOnly)) {
        KMessageBox::error(
            nullptr,
            i18n("Failed to create the temporary Mathom archive."),
            i18n("Mathom Archive Error"));
        Tools::deleteRecursively(tempFolder);
        return;
    }

    tar.writeDir(QStringLiteral("baskets"), QString(), QString());
    dialog.setValue(dialog.value() + 1);

    QStringList backgrounds;
    QList<Tag *> tags;

    for (int i = 0; i < Global::bnpView->topLevelItemCount(); ++i) {
        BasketListViewItem *item = Global::bnpView->topLevelItem(i);
        BasketScene *basket = item->basket();

        saveBasketToArchive(
            basket,
            true,
            &tar,
            backgrounds,
            tempFolder,
            &dialog);

        listUsedTags(basket, true, tags);
    }

    QString data;
    QXmlStreamWriter treeStream(&data);
    XMLWork::setupXmlStream(treeStream, QStringLiteral("basketTree"));

    for (int i = 0; i < Global::bnpView->topLevelItemCount(); ++i) {
        Global::bnpView->saveSubHierarchy(
            Global::bnpView->topLevelItem(i),
            treeStream,
            true);
    }

    treeStream.writeEndElement();
    treeStream.writeEndDocument();

    const QString treePath =
        tempFolder + QStringLiteral("baskets.xml");

    FileStorage::safelySaveToFile(treePath, data);
    tar.addLocalFile(treePath, QStringLiteral("baskets/baskets.xml"));
    dir.remove(treePath);

    const QString tagsPath =
        tempFolder + QStringLiteral("tags.xml");

    Tag::saveTagsTo(tags, tagsPath);
    tar.addLocalFile(tagsPath, QStringLiteral("tags.xml"));
    dir.remove(tagsPath);

    const QString tempIconFile =
        tempFolder + QStringLiteral("icon.png");

    for (Tag *tag : tags) {
        for (State *state : tag->states()) {
            QPixmap icon =
                KIconLoader::global()->loadIcon(
                    state->emblem(),
                    KIconLoader::Small,
                    16,
                    KIconLoader::DefaultState,
                    QStringList(),
                    nullptr,
                    true);

            if (!icon.isNull()) {
                icon.save(tempIconFile, "PNG");
                QString iconFileName =
                    state->emblem().replace(
                        QLatin1Char('/'),
                        QLatin1Char('_'));

                tar.addLocalFile(
                    tempIconFile,
                    QStringLiteral("tag-emblems/")
                        + iconFileName);
            }
        }
    }

    dir.remove(tempIconFile);
    tar.close();

    BasketScene *previewBasket =
        Global::bnpView->currentBasket();

    QString previewPath =
        tempFolder + QStringLiteral("preview.png");

    if (previewBasket) {
        QPixmap previewPixmap(
            previewBasket->width(),
            previewBasket->height());

        QPainter painter(&previewPixmap);
        NoteSelection *selection =
            previewBasket->selectedNotes();

        previewBasket->unselectAll();

        Note *focusedNote =
            previewBasket->focusedNote();

        previewBasket->setFocusedNote(nullptr);
        previewBasket->doHoverEffects(nullptr, Note::None);
        previewBasket->render(&painter);

        if (selection)
            previewBasket->selectSelection(selection);

        previewBasket->setFocusedNote(focusedNote);
        previewBasket->doHoverEffects();
        painter.end();

        previewPixmap.toImage()
            .scaled(256, 256, Qt::KeepAspectRatio)
            .save(previewPath, "PNG");
    }

    QFile file(destination);
    if (file.open(QIODevice::WriteOnly)) {
        const ulong previewSize =
            QFile(previewPath).size();

        const ulong archiveSize =
            QFile(tempDestination).size();

        QTextStream output(&file);
        output << "MathomNotes:archive\n"
               << "version:1.0\n"
               << "scope:all\n"
               << "preview*:" << previewSize << "\n";
        output.flush();

        const unsigned long bufferSize = 1024;
        char *buffer = new char[bufferSize];
        long sizeRead;

        QFile previewFile(previewPath);
        if (previewFile.open(QIODevice::ReadOnly)) {
            while ((sizeRead =
                        previewFile.read(
                            buffer,
                            bufferSize)) > 0) {
                file.write(buffer, sizeRead);
            }
        }

        output << "archive*:" << archiveSize << "\n";
        output.flush();

        QFile archiveFile(tempDestination);
        if (archiveFile.open(QIODevice::ReadOnly)) {
            while ((sizeRead =
                        archiveFile.read(
                            buffer,
                            bufferSize)) > 0) {
                file.write(buffer, sizeRead);
            }
        }

        delete[] buffer;
        file.close();
    }

    dialog.setValue(dialog.maximum());
    Tools::deleteRecursively(tempFolder);
}


void Archive::saveBasketToArchive(BasketScene *basket,
                                  bool recursive,
                                  KTar *tar,
                                  QStringList &backgrounds,
                                  const QString &tempFolder,
                                  QProgressDialog *progress)
{
    // Basket need to be loaded for tags exportation.
    // We load it NOW so that the progress bar really reflect the state of the exportation:
    if (!basket->isLoaded()) {
        basket->load();
    }

    QDir dir;
    // Save basket data:
    tar->addLocalDirectory(basket->fullPath(), QStringLiteral("baskets/") + basket->folderName());
    // Save basket icon:
    QString tempIconFile = tempFolder + QStringLiteral("icon.png");
    if (!basket->icon().isEmpty() && basket->icon() != QStringLiteral("basket")) {
        QPixmap icon =
            KIconLoader::global()
                ->loadIcon(basket->icon(), KIconLoader::Small, 16, KIconLoader::DefaultState, QStringList(), /*path_store=*/nullptr, /*canReturnNull=*/true);
        if (!icon.isNull()) {
            icon.save(tempIconFile, "PNG");
            QString iconFileName = basket->icon().replace(QLatin1Char('/'), QLatin1Char('_'));
            tar->addLocalFile(tempIconFile, QStringLiteral("basket-icons/") + iconFileName);
        }
    }
    // Export one background image only once for the whole archive.
    // Page background images are stored in the .basket file by name, so
    // every referenced image must also be present in the archive.
    const auto archiveBackgroundImage =
        [&](const QString &imageName) {
            if (imageName.isEmpty()
                || backgrounds.contains(imageName)) {
                return;
            }

            const QString backgroundPath =
                Global::backgroundManager
                    ->pathForImageName(imageName);

            if (!backgroundPath.isEmpty()) {
                tar->addLocalFile(
                    backgroundPath,
                    QStringLiteral("backgrounds/")
                        + imageName);

                const QString previewPath =
                    Global::backgroundManager
                        ->previewPathForImageName(
                            imageName);

                if (!previewPath.isEmpty()) {
                    tar->addLocalFile(
                        previewPath,
                        QStringLiteral(
                            "backgrounds/previews/")
                            + imageName);
                }

                const QString configPath =
                    backgroundPath
                    + QStringLiteral(".config");

                if (QFileInfo::exists(configPath)) {
                    tar->addLocalFile(
                        configPath,
                        QStringLiteral("backgrounds/")
                            + imageName
                            + QStringLiteral(".config"));
                }
            }

            backgrounds.append(imageName);
        };

    // Compatibility with pre-Pages Mathom/BasKet data.
    archiveBackgroundImage(
        basket->backgroundImageName());

    // Since 0.1.11, appearance belongs to Pages.
    // Export every Page background, not only the currently displayed one.
    for (const BasketScene::PageInfo &page :
         basket->pages()) {
        archiveBackgroundImage(
            page.backgroundImage);
    }

    progress->setValue(progress->value() + 1); // Basket exportation finished
    qCDebug(BASKET_LOG) << basket->basketName() << " finished";

    // Recursively save child baskets:
    BasketListViewItem *item = Global::bnpView->listViewItemForBasket(basket);
    if (recursive) {
        for (int i = 0; i < item->childCount(); i++) {
            saveBasketToArchive(((BasketListViewItem *)item->child(i))->basket(), recursive, tar, backgrounds, tempFolder, progress);
        }
    }
}

void Archive::listUsedTags(BasketScene *basket, bool recursive, QList<Tag *> &list)
{
    basket->listUsedTags(list);
    BasketListViewItem *item = Global::bnpView->listViewItemForBasket(basket);
    if (recursive) {
        for (int i = 0; i < item->childCount(); i++) {
            listUsedTags(((BasketListViewItem *)item->child(i))->basket(), recursive, list);
        }
    }
}

void Archive::open(const QString &path)
{
    // Use the temporary folder:
    QString tempFolder = Global::savesFolder() + QStringLiteral("temp-archive/");

    switch (extractArchive(path, tempFolder, false)) {
    case IOErrorCode::FailedToOpenResource:
        KMessageBox::error(nullptr, i18n("Failed to open a file resource."), i18n("Mathom-House Archive Error"));
        break;
    case IOErrorCode::NotABasketArchive:
        KMessageBox::error(nullptr, i18n("This file is not a Mathom-House archive."), i18n("Mathom-House Archive Error"));
        break;
    case IOErrorCode::CorruptedBasketArchive:
        KMessageBox::error(nullptr, i18n("This file is corrupted. It can not be opened."), i18n("Mathom-House Archive Error"));
        break;
    case IOErrorCode::DestinationExists:
        KMessageBox::error(nullptr, i18n("Extraction path already exists."), i18n("Mathom-House Archive Error"));
        break;
    case IOErrorCode::IncompatibleBasketVersion:
        KMessageBox::error(nullptr,
                           i18n("This file was created with a recent version of %1."
                                "Please upgrade to a newer version to be able to open that file.",
                                QGuiApplication::applicationDisplayName()),
                           i18n("Mathom-House Archive Error"));
        break;
    case IOErrorCode::PossiblyCompatibleBasketVersion:
        KMessageBox::information(nullptr,
                                 i18n("This file was created with a recent version of %1. "
                                      "It can be opened but not every information will be available to you. "
                                      "For instance, some mathoms may be missing because they are of a type only available in new versions. "
                                      "When saving the file back, consider to save it to another file, to preserve the original one.",
                                      QGuiApplication::applicationDisplayName()),
                                 i18n("Mathom-House Archive Error"));
        [[fallthrough]];
    case IOErrorCode::NoError:
        if (Global::activeMainWindow()) {
            Global::activeMainWindow()->raise();
        }
        // Import the Tags:

        importTagEmblems(tempFolder); // Import and rename tag emblems BEFORE loading them!
        QMap<QString, QString> mergedStates = Tag::loadTags(tempFolder + QStringLiteral("tags.xml"));
        if (mergedStates.count() > 0) {
            Tag::saveTags();
        }

        // Import the Background Images:
        importArchivedBackgroundImages(tempFolder);

        // Import the Baskets:
        renameBasketFolders(tempFolder, mergedStates);

        Tools::deleteRecursively(tempFolder);
        break;
    }
}

Archive::IOErrorCode Archive::extractArchive(const QString &path, const QString &destination, const bool protectDestination)
{
    QString extractionPath;
    if (destination.isEmpty()) {
        extractionPath = QFileInfo(path).path() + QDir::separator()
            + QFileInfo(path).baseName() + QStringLiteral("-source");
    } else {
        extractionPath = QDir::cleanPath(destination);
    }

    QDir dir(extractionPath);
    QFile file(path);
    // Do not delete an extraction folder just because the input is missing.
    if (!file.open(QIODevice::ReadOnly))
        return IOErrorCode::FailedToOpenResource;

    if (dir.exists() && protectDestination)
        return IOErrorCode::DestinationExists;

    if (!dir.removeRecursively() || !dir.mkpath(QStringLiteral(".")))
        return IOErrorCode::FailedToOpenResource;

    // All failures after creating an extraction directory remove the partial
    // contents, so no caller can mistake an incomplete extraction for success.
    const auto fail = [&](IOErrorCode code) {
        file.close();
        dir.removeRecursively();
        return code;
    };

    QTextStream stream(&file);
    const QString magic = stream.readLine();
    const bool native = magic == QStringLiteral("MathomNotes:archive");
    const bool legacy = magic == QStringLiteral("BasKetNP:archive");
    if (!native && !legacy)
        return fail(IOErrorCode::NotABasketArchive);

    const QString expectedVersion =
        native ? QStringLiteral("1.0") : QStringLiteral("0.6.1");
    QString version;
    QStringList readCompatibleVersions;
    QStringList writeCompatibleVersions;
    IOErrorCode result = IOErrorCode::NoError;
    bool extractedArchive = false;
    constexpr qint64 bufferSize = 1024;

    // Read exactly the number of payload bytes declared by the envelope.
    // All stream positions are restored before parsing the following key.
    const auto consumePayload = [&](qint64 size, QFile *output) {
        const qint64 offset = stream.pos();
        if (offset < 0 || size < 0 || size > file.size() - offset
            || !file.seek(offset)) {
            return false;
        }
        std::array<char, bufferSize> buffer{};
        while (size > 0) {
            const qint64 count =
                file.read(buffer.data(), std::min(bufferSize, size));
            if (count <= 0)
                return false;
            if (output && output->write(buffer.data(), count) != count)
                return false;
            size -= count;
        }
        if (output && !output->flush())
            return false;
        return stream.seek(file.pos())
            && file.error() == QFileDevice::NoError;
    };

    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        const int colon = line.indexOf(QLatin1Char(':'));
        const QString key = colon < 0 ? line : line.left(colon);
        const QString value = colon < 0 ? QString() : line.mid(colon + 1);

        if (key == QStringLiteral("version")) {
            version = value;
        } else if (key == QStringLiteral("read-compatible")) {
            readCompatibleVersions = value.split(QLatin1Char(';'));
        } else if (key == QStringLiteral("write-compatible")) {
            writeCompatibleVersions = value.split(QLatin1Char(';'));
        } else if (key == QStringLiteral("preview*")) {
            bool ok = false;
            const qint64 size = value.toLongLong(&ok);
            if (!ok || size < 0)
                return fail(IOErrorCode::CorruptedBasketArchive);

            QFile preview(dir.filePath(QStringLiteral("preview.png")));
            if (!preview.open(QIODevice::WriteOnly))
                return fail(IOErrorCode::FailedToOpenResource);
            if (!consumePayload(size, &preview))
                return fail(preview.error() == QFileDevice::NoError
                                ? IOErrorCode::CorruptedBasketArchive
                                : IOErrorCode::FailedToOpenResource);
            preview.close();
        } else if (key == QStringLiteral("archive*")) {
            if (version != expectedVersion
                && readCompatibleVersions.contains(expectedVersion)
                && !writeCompatibleVersions.contains(expectedVersion)) {
                result = IOErrorCode::PossiblyCompatibleBasketVersion;
            } else if (version != expectedVersion
                       && !readCompatibleVersions.contains(expectedVersion)
                       && !writeCompatibleVersions.contains(expectedVersion)) {
                return fail(IOErrorCode::IncompatibleBasketVersion);
            }

            bool ok = false;
            const qint64 size = value.toLongLong(&ok);
            if (!ok || size <= 0)
                return fail(IOErrorCode::CorruptedBasketArchive);

            QTemporaryDir temporary;
            if (!temporary.isValid())
                return fail(IOErrorCode::FailedToOpenResource);
            const QString compressedPath =
                temporary.filePath(QStringLiteral("temp-archive.tar.gz"));
            QFile compressed(compressedPath);
            if (!compressed.open(QIODevice::WriteOnly))
                return fail(IOErrorCode::FailedToOpenResource);
            if (!consumePayload(size, &compressed)) {
                return fail(compressed.error() == QFileDevice::NoError
                                ? IOErrorCode::CorruptedBasketArchive
                                : IOErrorCode::FailedToOpenResource);
            }
            compressed.close();

            KTar tar(compressedPath, QStringLiteral("application/x-gzip"));
            if (!tar.open(QIODevice::ReadOnly))
                return fail(IOErrorCode::CorruptedBasketArchive);
            if (!tar.directory() || !tar.directory()->copyTo(extractionPath)) {
                tar.close();
                return fail(IOErrorCode::FailedToOpenResource);
            }
            if (!tar.close())
                return fail(IOErrorCode::CorruptedBasketArchive);
            extractedArchive = true;
        } else if (key.endsWith(QLatin1Char('*'))) {
            // Unknown binary extension: skip only when its declared size fits.
            bool ok = false;
            const qint64 size = value.toLongLong(&ok);
            if (!ok || size < 0 || !consumePayload(size, nullptr))
                return fail(IOErrorCode::CorruptedBasketArchive);
        }
    }

    if (!extractedArchive || file.error() != QFileDevice::NoError)
        return fail(IOErrorCode::CorruptedBasketArchive);
    file.close();
    return result;
}

Archive::IOErrorCode
Archive::createArchiveFromSource(const QString &sourcePath, const QString &previewImage, const QString &destination, const bool protectDestination)
{
    const QDir source(sourcePath);
    if (!source.exists() || destination.isEmpty())
        return IOErrorCode::FailedToOpenResource;

    if (protectDestination && QFileInfo::exists(destination))
        return IOErrorCode::DestinationExists;

    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return IOErrorCode::FailedToOpenResource;

    const QString tempArchive = tempDir.filePath(QStringLiteral("temp-archive.tar.gz"));
    KTar archive(tempArchive, QStringLiteral("application/x-gzip"));
    if (!archive.open(QIODevice::WriteOnly))
        return IOErrorCode::FailedToOpenResource;

    // Only change error propagation, not the existing archive directory tree.
    QStringList sourceFiles = source.entryList(QDir::Files);
    sourceFiles.removeOne(QStringLiteral("preview.png"));
    for (const QString &entry : sourceFiles) {
        if (!archive.addLocalFile(source.filePath(entry), entry)) {
            archive.close();
            return IOErrorCode::FailedToOpenResource;
        }
    }
    for (const QString &entry : source.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!archive.addLocalDirectory(source.filePath(entry), entry)) {
            archive.close();
            return IOErrorCode::FailedToOpenResource;
        }
    }
    if (!archive.close())
        return IOErrorCode::FailedToOpenResource;

    // Keep the same default preview, but do not claim success if a caller
    // explicitly supplied an unreadable preview image.
    const QString defaultPreview = QStringLiteral(":/mathom/icons/app.png");
    const QString previewPath = previewImage.isEmpty()
        ? (QFile::exists(defaultPreview) ? defaultPreview : QString())
        : previewImage;

    return writeMathomEnvelope(tempArchive, previewPath, destination, false)
        ? IOErrorCode::NoError
        : IOErrorCode::FailedToOpenResource;
}

/**
 * When opening a basket archive that come from another computer,
 * it can contains tags that use icons (emblems) that are not present on that computer.
 * Fortunately, basket archives contains a copy of every used icons.
 * This method check for every emblems and import the missing ones.
 * It also modify the tags.xml copy for the emblems to point to the absolute path of the imported icons.
 */
void Archive::importTagEmblems(const QString &extractionFolder)
{
    QDomDocument *document = XMLWork::openFile(QStringLiteral("basketTags"), extractionFolder + QStringLiteral("tags.xml"));
    if (document == nullptr)
        return;
    QDomElement docElem = document->documentElement();

    QDir dir;
    dir.mkdir(Global::savesFolder() + QStringLiteral("tag-emblems/"));
    FormatImporter copier; // Only used to copy files synchronously

    QDomNode node = docElem.firstChild();
    while (!node.isNull()) {
        QDomElement element = node.toElement();
        if ((!element.isNull()) && element.tagName() == QStringLiteral("tag")) {
            QDomNode subNode = element.firstChild();
            while (!subNode.isNull()) {
                QDomElement subElement = subNode.toElement();
                if ((!subElement.isNull()) && subElement.tagName() == QStringLiteral("state")) {
                    QString emblemName = XMLWork::getElementText(subElement, QStringLiteral("emblem"));
                    if (!emblemName.isEmpty()) {
                        QPixmap emblem =
                            KIconLoader::global()
                                ->loadIcon(emblemName, KIconLoader::NoGroup, 16, KIconLoader::DefaultState, QStringList(), nullptr, /*canReturnNull=*/true);
                        // The icon does not exists on that computer, import it:
                        if (emblem.isNull()) {
                            // Of the emblem path was eg. "/home/seb/emblem.png", it was exported as "tag-emblems/_home_seb_emblem.png".
                            // So we need to copy that image to "~/.local/share/basket/tag-emblems/emblem.png":
                            int slashIndex = emblemName.lastIndexOf(QLatin1Char('/'));
                            QString emblemFileName = (slashIndex < 0 ? emblemName : emblemName.right(slashIndex - 2));
                            QString source = extractionFolder + QStringLiteral("tag-emblems/") + emblemName.replace(QLatin1Char('/'), QLatin1Char('/'));
                            QString destination = Global::savesFolder() + QStringLiteral("tag-emblems/") + emblemFileName;
                            if (!dir.exists(destination) && dir.exists(source))
                                copier.copyFolder(source, destination);
                            // Replace the emblem path in the tags.xml copy:
                            QDomElement emblemElement = XMLWork::getElement(subElement, QStringLiteral("emblem"));
                            subElement.removeChild(emblemElement);
                            XMLWork::addElement(*document, subElement, QStringLiteral("emblem"), destination);
                        }
                    }
                }
                subNode = subNode.nextSibling();
            }
        }
        node = node.nextSibling();
    }
    FileStorage::safelySaveToFile(extractionFolder + QStringLiteral("tags.xml"), document->toString());
}

void Archive::importArchivedBackgroundImages(const QString &extractionFolder)
{
    FormatImporter copier; // Only used to copy files synchronously
    QString destFolder = Global::backgroundsFolder();
    QDir().mkpath(destFolder); // does not exist at the first run when addWelcomeBaskets is called

    QDir dir(extractionFolder + QStringLiteral("backgrounds/"),
             /*nameFilder=*/QStringLiteral("*.png"),
             /*sortSpec=*/QDir::Name | QDir::IgnoreCase,
             /*filterSpec=*/QDir::Files | QDir::NoSymLinks);
    QStringList files = dir.entryList();
    for (QStringList::Iterator it = files.begin(); it != files.end(); ++it) {
        QString image = *it;
        if (!Global::backgroundManager->exists(image)) {
            // Copy images:
            QString imageSource = extractionFolder + QStringLiteral("backgrounds/") + image;
            QString imageDest = destFolder + image;
            copier.copyFolder(imageSource, imageDest);
            // Copy configuration file:
            QString configSource = extractionFolder + QStringLiteral("backgrounds/") + image + QStringLiteral(".config");
            QString configDest = destFolder + image;
            if (dir.exists(configSource))
                copier.copyFolder(configSource, configDest);
            // Copy preview:
            QString previewSource = extractionFolder + QStringLiteral("backgrounds/previews/") + image;
            QString previewDest = destFolder + QStringLiteral("previews/") + image;
            if (dir.exists(previewSource)) {
                dir.mkdir(destFolder + QStringLiteral("previews/")); // Make sure the folder exists!
                copier.copyFolder(previewSource, previewDest);
            }
            // Append image to database:
            Global::backgroundManager->addImage(imageDest);
        }
    }
}

void Archive::renameBasketFolders(const QString &extractionFolder, QMap<QString, QString> &mergedStates)
{
    QDomDocument *doc = XMLWork::openFile(QStringLiteral("basketTree"), extractionFolder + QStringLiteral("baskets/baskets.xml"));
    if (doc != nullptr) {
        QMap<QString, QString> folderMap;
        QDomElement docElem = doc->documentElement();
        QDomNode node = docElem.firstChild();
        renameBasketFolder(extractionFolder, node, folderMap, mergedStates);
        loadExtractedBaskets(extractionFolder, node, folderMap, nullptr);
    }
}

void Archive::renameBasketFolder(const QString &extractionFolder, QDomNode &basketNode, QMap<QString, QString> &folderMap, QMap<QString, QString> &mergedStates)
{
    QDomNode n = basketNode;
    while (!n.isNull()) {
        QDomElement element = n.toElement();
        if ((!element.isNull()) && element.tagName() == QStringLiteral("basket")) {
            QString folderName = element.attribute(QStringLiteral("folderName"));
            if (!folderName.isEmpty()) {
                // Find a folder name:
                QString newFolderName = BasketFactory::newFolderName();
                folderMap[folderName] = newFolderName;
                // Reserve the folder name:
                QDir dir;
                dir.mkdir(Global::basketsFolder() + newFolderName);
                // Rename the merged tag ids:
                //              if (mergedStates.count() > 0) {
                renameMergedStatesAndBasketIcon(extractionFolder + QStringLiteral("baskets/") + folderName + QStringLiteral(".basket"),
                                                mergedStates,
                                                extractionFolder);
                //              }
                // Child baskets:
                QDomNode node = element.firstChild();
                renameBasketFolder(extractionFolder, node, folderMap, mergedStates);
            }
        }
        n = n.nextSibling();
    }
}

void Archive::renameMergedStatesAndBasketIcon(const QString &fullPath, QMap<QString, QString> &mergedStates, const QString &extractionFolder)
{
    QDomDocument *doc = XMLWork::openFile(QStringLiteral("basket"), fullPath);
    if (doc == nullptr)
        return;
    QDomElement docElem = doc->documentElement();
    QDomElement properties = XMLWork::getElement(docElem, QStringLiteral("properties"));
    importBasketIcon(properties, extractionFolder);
    QDomElement notes = XMLWork::getElement(docElem, QStringLiteral("notes"));
    if (mergedStates.count() > 0)
        renameMergedStates(notes, mergedStates);
    FileStorage::safelySaveToFile(fullPath, /*"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n" + */ doc->toString());
}

void Archive::importBasketIcon(QDomElement properties, const QString &extractionFolder)
{
    const QString iconName = XMLWork::getElementText(properties, QStringLiteral("icon"));
    if (iconName.isEmpty() || iconName == QStringLiteral("basket"))
        return;

    // A Mathom archive is self-contained: when it contains a snapshot of
    // the hierarchy icon, always restore that snapshot instead of deciding
    // from the icon theme available on the current machine. This avoids the
    // lab/native discrepancy where KIconLoader could resolve an icon in the
    // Flatpak SDK while the installed Debian runtime could not render it.
    QString archivedIconName = iconName;
    archivedIconName.replace(QLatin1Char('/'), QLatin1Char('_'));

    const QString source =
        extractionFolder + QStringLiteral("basket-icons/") + archivedIconName;

    if (!QFileInfo::exists(source))
        return; // Keep the semantic/theme icon name as a normal fallback.

    QDir().mkpath(MathomIcons::customIconsFolder());

    QString destinationFileName = archivedIconName;
    if (QFileInfo(destinationFileName).suffix().isEmpty())
        destinationFileName += QStringLiteral(".png");

    const QString destination =
        MathomIcons::customIconsFolder() + destinationFileName;

    if (!QFileInfo::exists(destination)) {
        QFile::copy(source, destination);
    }

    if (!QFileInfo::exists(destination))
        return; // Copy failed: never destroy the original icon identifier.

    QDomElement iconElement = XMLWork::getElement(properties, QStringLiteral("icon"));
    properties.removeChild(iconElement);
    QDomDocument document = properties.ownerDocument();
    XMLWork::addElement(document, properties, QStringLiteral("icon"), destination);
}

void Archive::renameMergedStates(QDomNode notes, QMap<QString, QString> &mergedStates)
{
    QDomNode n = notes.firstChild();
    while (!n.isNull()) {
        QDomElement element = n.toElement();
        if (!element.isNull()) {
            if (element.tagName() == QStringLiteral("group")) {
                renameMergedStates(n, mergedStates);
            } else if (element.tagName() == QStringLiteral("note")) {
                QString tags = XMLWork::getElementText(element, QStringLiteral("tags"));
                if (!tags.isEmpty()) {
                    QStringList tagNames = tags.split(QLatin1Char(';'));
                    for (QStringList::Iterator it = tagNames.begin(); it != tagNames.end(); ++it) {
                        QString &tag = *it;
                        if (mergedStates.contains(tag)) {
                            tag = mergedStates[tag];
                        }
                    }
                    QString newTags = tagNames.join(QStringLiteral(";"));
                    QDomElement tagsElement = XMLWork::getElement(element, QStringLiteral("tags"));
                    element.removeChild(tagsElement);
                    QDomDocument document = element.ownerDocument();
                    XMLWork::addElement(document, element, QStringLiteral("tags"), newTags);
                }
            }
        }
        n = n.nextSibling();
    }
}

void Archive::loadExtractedBaskets(const QString &extractionFolder, QDomNode &basketNode, QMap<QString, QString> &folderMap, BasketScene *parent)
{
    bool basketSetAsCurrent = (parent != nullptr);
    QDomNode n = basketNode;
    while (!n.isNull()) {
        QDomElement element = n.toElement();
        if ((!element.isNull()) && element.tagName() == QStringLiteral("basket")) {
            QString folderName = element.attribute(QStringLiteral("folderName"));
            if (!folderName.isEmpty()) {
                // Move the basket folder to its destination, while renaming it uniquely:
                QString newFolderName = folderMap[folderName];
                FormatImporter copier;
                // The folder has been "reserved" by creating it. Avoid asking the user to override:
                QDir dir;
                dir.rmdir(Global::basketsFolder() + newFolderName);
                copier.moveFolder(extractionFolder + QStringLiteral("baskets/") + folderName, Global::basketsFolder() + newFolderName);
                // Append and load the basket in the tree:
                BasketScene *basket = Global::bnpView->loadBasket(newFolderName);
                BasketListViewItem *basketItem =
                    Global::bnpView->appendBasket(basket, (basket && parent ? Global::bnpView->listViewItemForBasket(parent) : nullptr));
                basketItem->setExpanded(!XMLWork::trueOrFalse(element.attribute(QStringLiteral("folded"), QStringLiteral("false")), false));
                QDomElement properties = XMLWork::getElement(element, QStringLiteral("properties"));
                importBasketIcon(properties, extractionFolder); // Rename the icon fileName if necessary

                // Top-level Mathom-House names should never silently collide.
                // Propose "Name (2)", "Name (3)", ... and let the user edit
                // the suggestion before the imported Mathom-House is loaded.
                if (parent == nullptr) {
                    const QString importedName =
                        XMLWork::getElementText(
                            properties,
                            QStringLiteral("name"));

                    if (!importedName.isEmpty()) {
                        QStringList usedNames;

                        for (int i = 0;
                             i < Global::bnpView->topLevelItemCount();
                             ++i) {
                            BasketListViewItem *existingItem =
                                Global::bnpView->topLevelItem(i);

                            if (existingItem
                                && existingItem->basket()
                                && existingItem->basket() != basket) {
                                usedNames << existingItem->basket()->basketName();
                            }
                        }

                        if (usedNames.contains(
                                importedName,
                                Qt::CaseInsensitive)) {
                            int suffix = 2;
                            QString suggestedName;

                            do {
                                suggestedName =
                                    QStringLiteral("%1 (%2)")
                                        .arg(importedName)
                                        .arg(suffix++);
                            } while (usedNames.contains(
                                suggestedName,
                                Qt::CaseInsensitive));

                            bool accepted = false;

                            QString chosenName =
                                QInputDialog::getText(
                                    Global::activeMainWindow(),
                                    i18n("Mathom-House Name Conflict"),
                                    i18n(
                                        "A Mathom-House named \"%1\" already exists.\n"
                                        "Rename the imported Mathom-House or keep the suggested name:",
                                        importedName),
                                    QLineEdit::Normal,
                                    suggestedName,
                                    &accepted);

                            if (!accepted
                                || chosenName.trimmed().isEmpty()) {
                                chosenName = suggestedName;
                            }

                            chosenName = chosenName.trimmed();

                            while (usedNames.contains(
                                chosenName,
                                Qt::CaseInsensitive)) {
                                chosenName =
                                    QStringLiteral("%1 (%2)")
                                        .arg(importedName)
                                        .arg(suffix++);
                            }

                            QDomElement nameElement =
                                XMLWork::getElement(
                                    properties,
                                    QStringLiteral("name"));

                            if (!nameElement.isNull()) {
                                while (!nameElement.firstChild().isNull())
                                    nameElement.removeChild(
                                        nameElement.firstChild());

                                nameElement.appendChild(
                                    properties.ownerDocument()
                                        .createTextNode(chosenName));
                            } else {
                                QDomDocument document =
                                    properties.ownerDocument();

                                XMLWork::addElement(
                                    document,
                                    properties,
                                    QStringLiteral("name"),
                                    chosenName);
                            }
                        }
                    }
                }

                basket->loadProperties(properties);
                // Open the first basket of the archive:
                if (!basketSetAsCurrent) {
                    Global::bnpView->setCurrentBasket(basket);
                    basketSetAsCurrent = true;
                }
                QDomNode node = element.firstChild();
                loadExtractedBaskets(extractionFolder, node, folderMap, basket);
            }
        }
        n = n.nextSibling();
    }
}
