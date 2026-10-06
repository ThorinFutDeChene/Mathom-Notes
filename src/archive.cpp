/**
 * SPDX-FileCopyrightText: (C) 2006 by Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "archive.h"

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
    IOErrorCode retCode = IOErrorCode::NoError;

    QString l_destination;

    // derive name of the extraction directory
    if (destination.isEmpty()) {
        // have the decoded baskets the same name as the archive
        l_destination = QFileInfo(path).path() + QDir::separator() + QFileInfo(path).baseName() + QStringLiteral("-source");
    } else {
        l_destination = QDir::cleanPath(destination);
    }

    QDir dir(l_destination);

    // do nothing when writeProtected
    if (dir.exists() && protectDestination) {
        return IOErrorCode::DestinationExists;
    }

    // Create directory and delete its content in case it was not empty
    if (!dir.removeRecursively()) {
        return IOErrorCode::FailedToOpenResource;
    }
    dir.mkpath(QStringLiteral("."));

    const qint64 BUFFER_SIZE = 1024;

    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        // stream.setEncoding(QStringConverter::Latin1);
        QString line = stream.readLine();
        const bool nativeMathomArchive =
            line == QStringLiteral("MathomNotes:archive");
        const bool legacyBasketArchive =
            line == QStringLiteral("BasKetNP:archive");

        if (!nativeMathomArchive && !legacyBasketArchive) {
            file.close();
            dir.removeRecursively();
            return IOErrorCode::NotABasketArchive;
        }

        const QString expectedVersion =
            nativeMathomArchive
                ? QStringLiteral("1.0")
                : QStringLiteral("0.6.1");

        QString version;
        QStringList readCompatibleVersions;
        QStringList writeCompatibleVersions;
        while (!stream.atEnd()) {
            // Get Key/Value Pair From the Line to Read:
            line = stream.readLine();
            int index = line.indexOf(QLatin1Char(':'));
            QString key;
            QString value;
            if (index >= 0) {
                key = line.left(index);
                value = line.right(line.length() - index - 1);
            } else {
                key = line;
                value = QString();
            }
            if (key == QStringLiteral("version")) {
                version = value;
            } else if (key == QStringLiteral("read-compatible")) {
                readCompatibleVersions = value.split(QLatin1Char(';'));
            } else if (key == QStringLiteral("write-compatible")) {
                writeCompatibleVersions = value.split(QLatin1Char(';'));
            } else if (key == QStringLiteral("preview*")) {
                bool ok;
                const qint64 size = value.toULong(&ok);
                if (!ok) {
                    file.close();
                    dir.removeRecursively();
                    return IOErrorCode::CorruptedBasketArchive;
                }
                // Get the preview file:
                QFile previewFile(dir.absolutePath() + QDir::separator() + QStringLiteral("preview.png"));
                if (previewFile.open(QIODevice::WriteOnly)) {
                    std::array<char, BUFFER_SIZE> buffer{};
                    qint64 remainingBytes = size;
                    qint64 sizeRead = 0;
                    file.seek(stream.pos());

                    while ((sizeRead = file.read(buffer.data(), std::min(BUFFER_SIZE, remainingBytes))) > 0) {
                        previewFile.write(buffer.data(), sizeRead);
                        remainingBytes -= sizeRead;
                    }
                    previewFile.close();
                }
                stream.seek(stream.pos() + size);
            } else if (key == QStringLiteral("archive*")) {
                if (version != expectedVersion && readCompatibleVersions.contains(expectedVersion)
                    && !writeCompatibleVersions.contains(expectedVersion)) {
                    retCode = IOErrorCode::PossiblyCompatibleBasketVersion;
                }
                if (version != expectedVersion && !readCompatibleVersions.contains(expectedVersion)
                    && !writeCompatibleVersions.contains(expectedVersion)) {
                    file.close();
                    dir.removeRecursively();
                    return IOErrorCode::IncompatibleBasketVersion;
                }

                bool ok;
                qint64 size = value.toULong(&ok);
                if (!ok) {
                    file.close();
                    dir.removeRecursively();
                    return IOErrorCode::CorruptedBasketArchive;
                }

                // Get the archive file and extract it to destination:
                QTemporaryDir tempDir;
                if (!tempDir.isValid()) {
                    return IOErrorCode::FailedToOpenResource;
                }
                QString tempArchive = tempDir.path() + QDir::separator() + QStringLiteral("temp-archive.tar.gz");
                QFile archiveFile(tempArchive);
                file.seek(stream.pos());

                if (!archiveFile.open(QIODevice::WriteOnly)) {
                    file.close();
                    dir.removeRecursively();
                    return IOErrorCode::FailedToOpenResource;
                }

                char *buffer = new char[BUFFER_SIZE];
                qint64 sizeRead;

                while ((sizeRead = file.read(
                            buffer,
                            std::min(BUFFER_SIZE, size))) > 0) {
                    archiveFile.write(buffer, sizeRead);
                    size -= sizeRead;
                }

                archiveFile.close();
                delete[] buffer;

                KTar tar(
                    tempArchive,
                    QStringLiteral("application/x-gzip"));

                if (!tar.open(QIODevice::ReadOnly)) {
                    file.close();
                    dir.removeRecursively();
                    return IOErrorCode::CorruptedBasketArchive;
                }

                tar.directory()->copyTo(l_destination);
                tar.close();

                stream.seek(file.pos());
            } else if (key.endsWith(QLatin1Char('*'))) {
                // We do not know what it is, but we should read the embedded-file in
                // order to discard it:
                bool ok;
                qint64 size = value.toULong(&ok);
                if (!ok) {
                    file.close();
                    dir.removeRecursively();
                    return IOErrorCode::CorruptedBasketArchive;
                }
                // Get the archive file:
                char *buffer = new char[BUFFER_SIZE];
                qint64 sizeRead;
                while ((sizeRead = file.read(buffer, std::min(BUFFER_SIZE, size))) > 0) {
                    size -= sizeRead;
                }
                delete[] buffer;
            } else {
                // We do not know what it is, and we do not care.
            }
            // Analyze the Value, if Understood:
        }
        file.close();
    }

    return retCode;
}

Archive::IOErrorCode
Archive::createArchiveFromSource(const QString &sourcePath, const QString &previewImage, const QString &destination, const bool protectDestination)
{
    QDir source(sourcePath);
    QFileInfo destinationFile(destination);

    // sourcePath must be a valid directory
    if (!source.exists()) {
        return IOErrorCode::FailedToOpenResource;
    }

    // destinationFile must not previously exist;
    if (destinationFile.exists() && protectDestination) {
        return IOErrorCode::DestinationExists;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        return IOErrorCode::FailedToOpenResource;
    }

    // Create the temporary archive file:
    QString tempDestinationFile = tempDir.path() + QDir::separator() + QStringLiteral("temp-archive.tar.gz");
    KTar archive(tempDestinationFile, QStringLiteral("application/x-gzip"));

    // Prepare the archive for writing.
    if (!archive.open(QIODevice::WriteOnly)) {
        // Failed to open file.
        archive.close();
        return IOErrorCode::FailedToOpenResource;
    }

    // Add files and directories to tar archive
    auto sourceFiles = source.entryList(QDir::Files);
    sourceFiles.removeOne(QStringLiteral("preview.png"));
    std::for_each(sourceFiles.constBegin(), sourceFiles.constEnd(), [&](const QString &entry) {
        archive.addLocalFile(source.absolutePath() + QDir::separator() + entry, entry);
    });
    const auto sourceDirectories = source.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::for_each(sourceDirectories.constBegin(), sourceDirectories.constEnd(), [&](const QString &entry) {
        archive.addLocalDirectory(source.absolutePath() + QDir::separator() + entry, entry);
    });

    archive.close();

    // use generic basket icon as preview if no valid image supplied
    /// \todo write a way to create preview the way it's done in Archive::save
    QString previewImagePath = previewImage;
    if (previewImage.isEmpty() && !QFileInfo(previewImage).exists()) {
        previewImagePath = QStringLiteral(":/mathom/icons/app.png");
    }

    // Finally Save to the Real Destination file:
    QFile file(destination);
    if (file.open(QIODevice::WriteOnly)) {
        ulong previewSize = QFile(previewImagePath).size();
        ulong archiveSize = QFile(tempDestinationFile).size();
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
        QFile previewFile(previewImagePath);
        if (previewFile.open(QIODevice::ReadOnly)) {
            while ((sizeRead = previewFile.read(buffer, BUFFER_SIZE)) > 0)
                file.write(buffer, sizeRead);
        }
        stream << "archive*:" << archiveSize << "\n";
        stream.flush();

        // Copy the Archive File:
        QFile archiveFile(tempDestinationFile);
        if (archiveFile.open(QIODevice::ReadOnly)) {
            while ((sizeRead = archiveFile.read(buffer, BUFFER_SIZE)) > 0)
                file.write(buffer, sizeRead);
        }
        // Clean Up:
        delete[] buffer;
        buffer = nullptr;
        file.close();
    }

    return IOErrorCode::NoError;
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
