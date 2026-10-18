/**
 * SPDX-FileCopyrightText: (C) 2006 by Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "archive.h"

#include <QDebug>
#include <QDateTime>
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
#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KCompressionDevice>
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
#include "pendingsave.h"
#include "tag.h"
#include "tools.h"
#include "xmlwork.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <utility>

#include <basket_debug.h>

namespace {
constexpr qint64 copyBufferSize = 64 * 1024;
constexpr std::array<char, 1024> tarEndBlocks{};

bool failWithMessage(QString *errorMessage, const QString &message)
{
    if (errorMessage)
        *errorMessage = message;
    return false;
}

bool copyExactly(QIODevice &source, QIODevice &destination, qint64 size, bool *readFailed = nullptr)
{
    if (readFailed)
        *readFailed = false;
    std::array<char, copyBufferSize> buffer{};
    while (size > 0) {
        const qint64 count = source.read(buffer.data(), std::min<qint64>(buffer.size(), size));
        if (count <= 0) {
            if (readFailed)
                *readFailed = true;
            return false;
        }
        if (destination.write(buffer.data(), count) != count)
            return false;
        size -= count;
    }
    return size == 0;
}

bool tarNumber(const char *data, qsizetype length, qint64 &value)
{
    // KTar reads sizes as octal and can silently turn GNU base-256 sizes into
    // zero-length files. Refuse that unsupported representation before import.
    if (static_cast<unsigned char>(data[0]) & 0x80)
        return false;
    const QByteArray field = QByteArray(data, length).replace('\0', ' ').trimmed();
    if (field.isEmpty()) {
        value = 0;
        return true;
    }
    if (!std::all_of(field.cbegin(), field.cend(), [](char byte) { return byte >= '0' && byte <= '7'; }))
        return false;
    bool valid = false;
    value = field.toLongLong(&valid, 8);
    return valid && value >= 0;
}

// KTar accepts short final headers, skips some checksums and can stop before
// gzip's trailer. Validate the complete stream without creating an uncompressed
// temporary file, including empty tar archives with their two zero end blocks.
bool validateTarStream(KCompressionDevice &input)
{
    constexpr qint64 blockSize = 512;
    std::array<char, blockSize> header{};
    std::array<char, copyBufferSize> buffer{};
    bool hasEntry = false;
    const auto allZero = [](const char *data, qint64 size) {
        return std::all_of(data, data + size, [](char byte) { return byte == 0; });
    };
    while (true) {
        const qint64 headerSize = input.read(header.data(), blockSize);
        // Older KTar writers omit end blocks. An EOF at a complete member
        // boundary is compatible, but only after the gzip footer was verified.
        if (headerSize == 0)
            return hasEntry && input.atEnd() && input.error() == QFileDevice::NoError;
        if (headerSize != blockSize)
            return false;
        if (allZero(header.data(), blockSize)) {
            if (input.read(header.data(), blockSize) != blockSize || !allZero(header.data(), blockSize))
                return false;
            // Consume padding and the gzip footer so truncated streams and bad
            // CRCs cannot be mistaken for a successful end of the tar archive.
            qint64 count = 0;
            qint64 padding = 0;
            while ((count = input.read(buffer.data(), buffer.size())) > 0) {
                if (!allZero(buffer.data(), count))
                    return false;
                padding = (padding + count) % blockSize;
            }
            return count == 0 && padding == 0 && input.atEnd() && input.error() == QFileDevice::NoError;
        }

        qint64 checksum = 0, size = 0;
        if (!tarNumber(header.data() + 148, 8, checksum) || !tarNumber(header.data() + 124, 12, size))
            return false;
        qint64 unsignedChecksum = 8 * ' ', signedChecksum = 8 * ' ';
        for (qint64 i = 0; i < blockSize; ++i) {
            if (i >= 148 && i < 156)
                continue;
            unsignedChecksum += static_cast<unsigned char>(header[i]);
            signedChecksum += static_cast<signed char>(header[i]);
        }
        // Some old tar writers used signed bytes for their checksum.
        if (checksum != unsignedChecksum && checksum != signedChecksum)
            return false;
        hasEntry = true;
        const qint64 padding = (blockSize - size % blockSize) % blockSize;
        if (size > std::numeric_limits<qint64>::max() - padding)
            return false;
        qint64 remaining = size + padding;
        while (remaining > 0) {
            const qint64 count = input.read(buffer.data(), std::min<qint64>(buffer.size(), remaining));
            if (count <= 0)
                return false;
            remaining -= count;
        }
    }
}

mode_t archivePermissions(const QFileInfo &info, bool directory)
{
    mode_t permissions = directory ? 040000 : 0100000;
    if (info.permission(QFile::ReadOwner)) permissions |= 0400;
    if (info.permission(QFile::WriteOwner)) permissions |= 0200;
    if (info.permission(QFile::ExeOwner)) permissions |= 0100;
    if (info.permission(QFile::ReadGroup)) permissions |= 0040;
    if (info.permission(QFile::WriteGroup)) permissions |= 0020;
    if (info.permission(QFile::ExeGroup)) permissions |= 0010;
    if (info.permission(QFile::ReadOther)) permissions |= 0004;
    if (info.permission(QFile::WriteOther)) permissions |= 0002;
    if (info.permission(QFile::ExeOther)) permissions |= 0001;
    return permissions;
}

// KArchive's directory convenience method does not propagate child failures,
// and its file convenience method relies on an assertion for complete reads.
// Check every operation here, including in Release builds.
bool addFileToArchive(KTar &tar, const QString &path, const QString &entryName, QString *errorMessage = nullptr)
{
    const QFileInfo info(path);
    if (info.isSymLink()) {
        if (!tar.addLocalFile(path, entryName))
            return failWithMessage(errorMessage, i18n("Could not add %1 to the archive: %2", path, tar.errorString()));
        return true;
    }
    if (!info.isFile())
        return failWithMessage(errorMessage, i18n("The archive source file %1 is unavailable.", path));
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly))
        return failWithMessage(errorMessage, i18n("Could not read %1 for the archive: %2", path, source.errorString()));
    const qint64 size = source.size();
    if (size < 0 || !tar.prepareWriting(entryName, info.owner(), info.group(), size, archivePermissions(info, false),
                                       info.lastRead(), info.lastModified(), info.birthTime()))
        return failWithMessage(errorMessage, i18n("Could not add %1 to the archive: %2", path, tar.errorString()));

    std::array<char, copyBufferSize> buffer{};
    qint64 remaining = size;
    while (remaining > 0) {
        const qint64 count = source.read(buffer.data(), std::min<qint64>(buffer.size(), remaining));
        if (count <= 0)
            return failWithMessage(errorMessage, i18n("Could not read the complete file %1 for the archive.", path));
        if (!tar.writeData(buffer.data(), count))
            return failWithMessage(errorMessage, i18n("Could not add %1 to the archive: %2", path, tar.errorString()));
        remaining -= count;
    }
    if (source.error() != QFileDevice::NoError || source.size() != size)
        return failWithMessage(errorMessage, i18n("The file %1 changed or could not be read completely during export.", path));
    if (!tar.finishWriting(size))
        return failWithMessage(errorMessage, i18n("Could not finish adding %1 to the archive: %2", path, tar.errorString()));
    return true;
}

bool addDirectoryToArchive(KTar &tar, const QString &path, const QString &entryName, QString *errorMessage = nullptr)
{
    const QDir directory(path);
    const QFileInfo info(path);
    if (!directory.exists() || !directory.isReadable())
        return failWithMessage(errorMessage, i18n("The archive source directory %1 is unavailable.", path));
    if (!tar.writeDir(entryName, info.owner(), info.group(), archivePermissions(info, true), info.lastRead(), info.lastModified(), info.birthTime()))
        return failWithMessage(errorMessage, i18n("Could not add %1 to the archive: %2", path, tar.errorString()));
    const QFileInfoList entries = directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &entry : entries) {
        const QString childName = entryName + QLatin1Char('/') + entry.fileName();
        const bool added = entry.isDir() && !entry.isSymLink()
            ? addDirectoryToArchive(tar, entry.absoluteFilePath(), childName, errorMessage)
            : addFileToArchive(tar, entry.absoluteFilePath(), childName, errorMessage);
        if (!added)
            return false;
    }
    return true;
}

Archive::IOErrorCode extractDirectory(const KArchiveDirectory &root, const QString &destination)
{
    using Error = Archive::IOErrorCode;
    QList<QPair<const KArchiveDirectory *, QString>> directories{{&root, destination}};
    QList<QPair<const KArchiveFile *, QString>> files;
    QList<QPair<QString, QString>> links;
    while (!directories.isEmpty()) {
        const auto directory = directories.takeLast();
        if (!QDir().mkpath(directory.second))
            return Error::FailedToOpenResource;
        const QStringList entries = directory.first->entries();
        for (const QString &name : entries) {
            const KArchiveEntry *entry = directory.first->entry(name);
            // Entries must remain within the freshly created staging tree.
            if (!entry || name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") || name.contains(QLatin1Char('/')))
                return Error::CorruptedBasketArchive;
            const QString outputPath = QDir(directory.second).filePath(name);
            if (!entry->symLinkTarget().isEmpty())
                links.append({entry->symLinkTarget(), outputPath});
            else if (entry->isDirectory())
                directories.append({static_cast<const KArchiveDirectory *>(entry), outputPath});
            else if (entry->isFile())
                files.append({static_cast<const KArchiveFile *>(entry), outputPath});
            else
                return Error::CorruptedBasketArchive;
        }
    }

    // Read the tar in its stored order to avoid repeated decompression seeks.
    std::sort(files.begin(), files.end(), [](const auto &left, const auto &right) { return left.first->position() < right.first->position(); });
    for (const auto &file : std::as_const(files)) {
        std::unique_ptr<QIODevice> input(file.first->createDevice());
        if (!input || !input->isReadable() || file.first->size() < 0)
            return Error::CorruptedBasketArchive;
        QFile output(file.second);
        if (!output.open(QIODevice::WriteOnly))
            return Error::FailedToOpenResource;
        bool readFailed = false;
        if (!copyExactly(*input, output, file.first->size(), &readFailed))
            return readFailed ? Error::CorruptedBasketArchive : Error::FailedToOpenResource;
        if (!output.flush())
            return Error::FailedToOpenResource;
        auto permissions = output.permissions();
        if (file.first->permissions() & 0100) permissions |= QFile::ExeOwner;
        if (file.first->permissions() & 0010) permissions |= QFile::ExeGroup;
        if (file.first->permissions() & 0001) permissions |= QFile::ExeOther;
        if (!output.setPermissions(permissions))
            return Error::FailedToOpenResource;
    }
    // Create symlinks last: no extraction writes can then follow a link outside
    // the staging tree. A failed link also prevents destination replacement.
    for (const auto &link : std::as_const(links)) {
        if (!QFile::link(link.first, link.second))
            return Error::FailedToOpenResource;
    }
    return Error::NoError;
}

// All export paths use the same envelope and only replace the destination
// after its complete contents have been written successfully.
bool writeArchiveEnvelope(const QString &archivePath,
                          const QString &previewPath,
                          const QString &destination,
                          bool allBaskets,
                          QString *errorMessage = nullptr)
{
    QFile preview(previewPath);
    QFile archive(archivePath);
    if (!preview.open(QIODevice::ReadOnly))
        return failWithMessage(errorMessage, i18n("Could not read the archive preview: %1", preview.errorString()));
    if (!archive.open(QIODevice::ReadOnly))
        return failWithMessage(errorMessage, i18n("Could not read the temporary archive: %1", archive.errorString()));

    const qint64 previewSize = preview.size();
    const qint64 archiveSize = archive.size();
    if (previewSize < 0 || archiveSize <= 0)
        return failWithMessage(errorMessage, i18n("The temporary archive is incomplete."));

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly))
        return failWithMessage(errorMessage, i18n("Could not write the archive to %1: %2", destination, output.errorString()));

    QByteArray header("MathomNotes:archive\nversion:1.0\n");
    if (allBaskets)
        header += "scope:all\n";
    header += "preview*:" + QByteArray::number(previewSize) + '\n';
    const QByteArray archiveHeader = "archive*:" + QByteArray::number(archiveSize) + '\n';
    if (output.write(header) != header.size()
        || !copyExactly(preview, output, previewSize)
        || output.write(archiveHeader) != archiveHeader.size()
        || !copyExactly(archive, output, archiveSize)
        || preview.error() != QFileDevice::NoError
        || archive.error() != QFileDevice::NoError
        || preview.size() != previewSize
        || archive.size() != archiveSize) {
        output.cancelWriting();
        return failWithMessage(errorMessage, i18n("Could not write the complete archive to %1.", destination));
    }
    if (!output.commit())
        return failWithMessage(errorMessage, i18n("Could not finish saving the archive to %1: %2", destination, output.errorString()));
    return true;
}

bool savePreview(BasketScene *basket, const QString &previewPath)
{
    if (!basket)
        return QFile::copy(QStringLiteral(":/mathom/icons/app.png"), previewPath);

    QPixmap previewPixmap(basket->width(), basket->height());
    QPainter painter(&previewPixmap);
    NoteSelection *selection = basket->selectedNotes();
    basket->unselectAll();
    Note *focusedNote = basket->focusedNote();
    basket->setFocusedNote(nullptr);
    basket->doHoverEffects(nullptr, Note::None);
    basket->render(&painter);
    if (selection)
        basket->selectSelection(selection);
    basket->setFocusedNote(focusedNote);
    basket->doHoverEffects();
    painter.end();
    return previewPixmap.toImage().scaled(256, 256, Qt::KeepAspectRatio).save(previewPath, "PNG");
}
}

bool Archive::save(BasketScene *basket, bool withSubBaskets, const QString &destination, QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    if (!basket || !Global::bnpView)
        return failWithMessage(errorMessage, i18n("No Mathom-House is available to export."));
    return saveBaskets({basket}, withSubBaskets, destination, false, errorMessage);
}

bool Archive::saveAll(const QString &destination, QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    if (!Global::bnpView || Global::bnpView->topLevelItemCount() <= 0)
        return failWithMessage(errorMessage, i18n("No Mathom-House is available to export."));

    QList<BasketScene *> baskets;
    for (int i = 0; i < Global::bnpView->topLevelItemCount(); ++i)
        baskets.append(Global::bnpView->topLevelItem(i)->basket());
    return saveBaskets(baskets, true, destination, true, errorMessage);
}

bool Archive::saveBaskets(const QList<BasketScene *> &baskets,
                         bool recursive,
                         const QString &destination,
                         bool allBaskets,
                         QString *errorMessage)
{
    Global::bnpView->closeAllEditors();
    Global::bnpView->save();
    if (!PendingSave::retryPendingSaves())
        return failWithMessage(errorMessage, i18n("Some changes could not be saved. The archive was not created; please retry after resolving the save error."));

    QProgressDialog dialog;
    dialog.setWindowTitle(allBaskets ? i18n("Save All Mathom-Houses") : i18n("Save as Mathom-House Archive"));
    dialog.setLabelText(allBaskets ? i18n("Saving all Mathom-Houses. Please wait...") : i18n("Saving as Mathom-House archive. Please wait..."));
    dialog.setCancelButton(nullptr);
    dialog.setAutoClose(true);

    int basketTotal = 0;
    for (BasketScene *basket : baskets)
        basketTotal += 1 + (recursive ? Global::bnpView->basketCount(Global::bnpView->listViewItemForBasket(basket)) : 0);
    dialog.setRange(0, 2 + basketTotal);
    dialog.setValue(0);
    dialog.show();

    QTemporaryDir temporary;
    if (!temporary.isValid())
        return failWithMessage(errorMessage, i18n("Could not create a temporary directory for the archive."));
    const QString tempFolder = temporary.path() + QDir::separator();
    const QString tempDestination = temporary.filePath(QStringLiteral("archive.tar.gz"));
    KTar tar(tempDestination, QStringLiteral("application/x-gzip"));
    if (!tar.open(QIODevice::WriteOnly) || !tar.writeDir(QStringLiteral("baskets")))
        return failWithMessage(errorMessage, i18n("Could not create the temporary archive: %1", tar.errorString()));
    dialog.setValue(dialog.value() + 1);

    QStringList backgrounds;
    QList<Tag *> tags;
    for (BasketScene *basket : baskets) {
        if (!saveBasketToArchive(basket, recursive, &tar, backgrounds, tempFolder, &dialog, errorMessage))
            return false;
        listUsedTags(basket, recursive, tags);
    }

    QString data;
    QXmlStreamWriter treeStream(&data);
    XMLWork::setupXmlStream(treeStream, QStringLiteral("basketTree"));
    for (BasketScene *basket : baskets)
        Global::bnpView->saveSubHierarchy(Global::bnpView->listViewItemForBasket(basket), treeStream, recursive);
    treeStream.writeEndElement();
    treeStream.writeEndDocument();

    const QString treePath = temporary.filePath(QStringLiteral("baskets.xml"));
    if (!FileStorage::safelySaveToFile(treePath, data))
        return failWithMessage(errorMessage, i18n("Could not save the archive's Mathom-House hierarchy."));
    if (!addFileToArchive(tar, treePath, QStringLiteral("baskets/baskets.xml"), errorMessage))
        return false;

    const QString tagsPath = temporary.filePath(QStringLiteral("tags.xml"));
    if (!Tag::saveTagsTo(tags, tagsPath))
        return failWithMessage(errorMessage, i18n("Could not save the archive's tags."));
    if (!addFileToArchive(tar, tagsPath, QStringLiteral("tags.xml"), errorMessage))
        return false;

    const QString tempIconFile = temporary.filePath(QStringLiteral("icon.png"));
    for (Tag *tag : tags) {
        for (State *state : tag->states()) {
            QPixmap icon = KIconLoader::global()->loadIcon(state->emblem(), KIconLoader::Small, 16, KIconLoader::DefaultState, QStringList(), nullptr, true);
            if (!icon.isNull()) {
                if (!icon.save(tempIconFile, "PNG"))
                    return failWithMessage(errorMessage, i18n("Could not save a tag emblem for the archive."));
                const QString iconFileName = state->emblem().replace(QLatin1Char('/'), QLatin1Char('_'));
                if (!addFileToArchive(tar, tempIconFile, QStringLiteral("tag-emblems/") + iconFileName, errorMessage))
                    return false;
            }
        }
    }
    if (!tar.writeData(tarEndBlocks.data(), tarEndBlocks.size()) || !tar.close())
        return failWithMessage(errorMessage, i18n("Could not finish the temporary archive: %1", tar.errorString()));

    const QString previewPath = temporary.filePath(QStringLiteral("preview.png"));
    BasketScene *previewBasket = allBaskets ? Global::bnpView->currentBasket() : baskets.first();
    if (!savePreview(previewBasket, previewPath))
        return failWithMessage(errorMessage, i18n("Could not save the archive preview."));
    if (!writeArchiveEnvelope(tempDestination, previewPath, destination, allBaskets, errorMessage))
        return false;
    dialog.setValue(dialog.maximum());
    return true;
}

bool Archive::saveBasketToArchive(BasketScene *basket,
                                  bool recursive,
                                  KTar *tar,
                                  QStringList &backgrounds,
                                  const QString &tempFolder,
                                  QProgressDialog *progress,
                                  QString *errorMessage)
{
    // Basket needs to be loaded for tag exportation.
    if (!basket->isLoaded()) {
        basket->load();
        if (!basket->isLoaded())
            return failWithMessage(errorMessage, i18n("Could not load %1 for export.", basket->basketName()));
    }
    if (!PendingSave::retryPendingSaves(basket))
        return failWithMessage(errorMessage, i18n("The latest changes to %1 could not be saved for export.", basket->basketName()));

    if (!addDirectoryToArchive(*tar, basket->fullPath(), QStringLiteral("baskets/") + basket->folderName(), errorMessage))
        return false;

    const QString tempIconFile = tempFolder + QStringLiteral("icon.png");
    if (!basket->icon().isEmpty() && basket->icon() != QStringLiteral("basket")) {
        QPixmap icon = KIconLoader::global()->loadIcon(basket->icon(), KIconLoader::Small, 16, KIconLoader::DefaultState, QStringList(), nullptr, true);
        if (!icon.isNull()) {
            if (!icon.save(tempIconFile, "PNG"))
                return failWithMessage(errorMessage, i18n("Could not save the icon of %1 for the archive.", basket->basketName()));
            const QString iconFileName = basket->icon().replace(QLatin1Char('/'), QLatin1Char('_'));
            if (!addFileToArchive(*tar, tempIconFile, QStringLiteral("basket-icons/") + iconFileName, errorMessage))
                return false;
        }
    }

    // Export one background image only once, including every Page.
    const auto archiveBackgroundImage = [&](const QString &imageName) {
        if (imageName.isEmpty() || backgrounds.contains(imageName))
            return true;
        const QString backgroundPath = Global::backgroundManager->pathForImageName(imageName);
        if (!backgroundPath.isEmpty()) {
            if (!addFileToArchive(*tar, backgroundPath, QStringLiteral("backgrounds/") + imageName, errorMessage))
                return false;
            const QString previewPath = Global::backgroundManager->previewPathForImageName(imageName);
            if (!previewPath.isEmpty()
                && !addFileToArchive(*tar, previewPath, QStringLiteral("backgrounds/previews/") + imageName, errorMessage))
                return false;
            const QString configPath = backgroundPath + QStringLiteral(".config");
            if (QFileInfo::exists(configPath)
                && !addFileToArchive(*tar, configPath, QStringLiteral("backgrounds/") + imageName + QStringLiteral(".config"), errorMessage))
                return false;
        }
        backgrounds.append(imageName);
        return true;
    };
    // Compatibility with pre-Pages Mathom/BasKet data.
    if (!archiveBackgroundImage(basket->backgroundImageName()))
        return false;
    for (const BasketScene::PageInfo &page : basket->pages()) {
        if (!archiveBackgroundImage(page.backgroundImage))
            return false;
    }

    progress->setValue(progress->value() + 1);
    if (recursive) {
        BasketListViewItem *item = Global::bnpView->listViewItemForBasket(basket);
        for (int i = 0; i < item->childCount(); ++i) {
            if (!saveBasketToArchive(static_cast<BasketListViewItem *>(item->child(i))->basket(), true, tar, backgrounds, tempFolder, progress, errorMessage))
                return false;
        }
    }
    return true;
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

        // Import and rename tag emblems BEFORE loading them.
        if (!importTagEmblems(tempFolder)) {
            KMessageBox::error(nullptr, i18n("Could not save the imported archive's tags."), i18n("Mathom-House Archive Error"));
            break;
        }
        QMap<QString, QString> mergedStates = Tag::loadTags(tempFolder + QStringLiteral("tags.xml"));
        if (mergedStates.count() > 0) {
            Tag::saveTags();
        }

        // Import the Background Images:
        importArchivedBackgroundImages(tempFolder);

        // Import the Baskets:
        if (!renameBasketFolders(tempFolder, mergedStates)) {
            KMessageBox::error(nullptr, i18n("Could not prepare the imported Mathom-Houses."), i18n("Mathom-House Archive Error"));
            break;
        }

        Tools::deleteRecursively(tempFolder);
        break;
    }
}

Archive::IOErrorCode Archive::extractArchive(const QString &path, const QString &destination, const bool protectDestination)
{
    const QString destinationPath = QFileInfo(destination.isEmpty()
                                                  ? QFileInfo(path).path() + QDir::separator() + QFileInfo(path).baseName() + QStringLiteral("-source")
                                                  : destination).absoluteFilePath();
    const QFileInfo destinationInfo(destinationPath);
    if ((destinationInfo.exists() || destinationInfo.isSymLink()) && protectDestination)
        return IOErrorCode::DestinationExists;
    if (QDir(destinationPath).isRoot() || destinationInfo.isSymLink() || (destinationInfo.exists() && !destinationInfo.isDir()))
        return IOErrorCode::FailedToOpenResource;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return IOErrorCode::FailedToOpenResource;

    // Read headers directly from QFile so text buffering cannot move the
    // position past an embedded binary block.
    constexpr qint64 maximumHeaderSize = 64 * 1024;
    const auto readHeader = [&file](QByteArray &line) {
        line = file.readLine(maximumHeaderSize + 1);
        if (!line.endsWith('\n') || line.size() > maximumHeaderSize || file.error() != QFileDevice::NoError)
            return false;
        line.chop(1);
        if (line.endsWith('\r'))
            line.chop(1);
        return true;
    };
    QByteArray line;
    if (!readHeader(line))
        return IOErrorCode::NotABasketArchive;
    const bool nativeMathomArchive = line == "MathomNotes:archive";
    if (!nativeMathomArchive && line != "BasKetNP:archive")
        return IOErrorCode::NotABasketArchive;
    const QString expectedVersion = nativeMathomArchive ? QStringLiteral("1.0") : QStringLiteral("0.6.1");

    // Keep an existing destination intact until the whole envelope and tar
    // have been read and extracted successfully. The sibling staging directory
    // also keeps the final directory renames on the same filesystem.
    const QString parentPath = destinationInfo.absolutePath();
    if (!QDir().mkpath(parentPath))
        return IOErrorCode::FailedToOpenResource;
    QTemporaryDir temporary(parentPath + QStringLiteral("/.mathom-extract-XXXXXX"));
    if (!temporary.isValid())
        return IOErrorCode::FailedToOpenResource;
    const QString extractedPath = temporary.filePath(QStringLiteral("contents"));
    if (!QDir().mkpath(extractedPath))
        return IOErrorCode::FailedToOpenResource;

    QString version;
    QStringList readCompatibleVersions;
    QStringList writeCompatibleVersions;
    bool hasArchive = false;
    bool hasPreview = false;
    IOErrorCode result = IOErrorCode::NoError;
    while (!file.atEnd()) {
        if (!readHeader(line))
            return IOErrorCode::CorruptedBasketArchive;
        const int separator = line.indexOf(':');
        const QByteArray key = separator < 0 ? line : line.left(separator);
        const QByteArray value = separator < 0 ? QByteArray() : line.mid(separator + 1);
        if (key == "version") {
            version = QString::fromUtf8(value);
        } else if (key == "read-compatible") {
            readCompatibleVersions = QString::fromUtf8(value).split(QLatin1Char(';'));
        } else if (key == "write-compatible") {
            writeCompatibleVersions = QString::fromUtf8(value).split(QLatin1Char(';'));
        } else if (key.endsWith('*')) {
            bool validSize = false;
            const qint64 size = value.toLongLong(&validSize);
            if (!validSize || size < 0 || size > file.size() - file.pos())
                return IOErrorCode::CorruptedBasketArchive;

            if (key == "preview*") {
                if (hasPreview)
                    return IOErrorCode::CorruptedBasketArchive;
                const QString previewPath = QDir(extractedPath).filePath(QStringLiteral("preview.png"));
                // A forward-compatible envelope may put the preview after the
                // tar. Never follow a symlink extracted from that tar.
                if (QFileInfo(previewPath).isSymLink())
                    return IOErrorCode::CorruptedBasketArchive;
                QFile preview(previewPath);
                if (!preview.open(QIODevice::WriteOnly))
                    return IOErrorCode::FailedToOpenResource;
                if (!copyExactly(file, preview, size) || !preview.flush())
                    return IOErrorCode::FailedToOpenResource;
                hasPreview = true;
            } else if (key == "archive*") {
                if (hasArchive || size == 0)
                    return IOErrorCode::CorruptedBasketArchive;
                if (version != expectedVersion) {
                    if (!readCompatibleVersions.contains(expectedVersion) && !writeCompatibleVersions.contains(expectedVersion))
                        return IOErrorCode::IncompatibleBasketVersion;
                    if (!writeCompatibleVersions.contains(expectedVersion))
                        result = IOErrorCode::PossiblyCompatibleBasketVersion;
                }

                const QString archivePath = temporary.filePath(QStringLiteral("archive.tar.gz"));
                QFile archiveFile(archivePath);
                if (!archiveFile.open(QIODevice::WriteOnly))
                    return IOErrorCode::FailedToOpenResource;
                if (!copyExactly(file, archiveFile, size) || !archiveFile.flush())
                    return IOErrorCode::FailedToOpenResource;
                archiveFile.close();

                QFile compressedArchive(archivePath);
                if (!compressedArchive.open(QIODevice::ReadOnly))
                    return IOErrorCode::FailedToOpenResource;
                // KGzipFilter otherwise treats non-gzip input as plain bytes.
                if (compressedArchive.peek(3) != QByteArray("\x1f\x8b\x08", 3))
                    return IOErrorCode::CorruptedBasketArchive;
                // Both devices are stack-owned. Passing the stream to KTar
                // avoids its hidden uncompressed temporary file and allows a
                // failed extracted-file write to retain its actual error code.
                KCompressionDevice tarStream(&compressedArchive, false, KCompressionDevice::GZip);
                if (!tarStream.open(QIODevice::ReadOnly))
                    return IOErrorCode::FailedToOpenResource;
                if (!validateTarStream(tarStream))
                    return compressedArchive.error() == QFileDevice::NoError ? IOErrorCode::CorruptedBasketArchive
                                                                            : IOErrorCode::FailedToOpenResource;
                if (!tarStream.seek(0))
                    return IOErrorCode::FailedToOpenResource;
                KTar tar(&tarStream);
                if (!tar.open(QIODevice::ReadOnly) || !tar.directory())
                    return IOErrorCode::CorruptedBasketArchive;
                const IOErrorCode extractionResult = extractDirectory(*tar.directory(), extractedPath);
                if (extractionResult != IOErrorCode::NoError)
                    return extractionResult;
                if (!tar.close())
                    return IOErrorCode::FailedToOpenResource;
                hasArchive = true;
            } else if (!file.seek(file.pos() + size)) {
                // Unknown embedded data is allowed, but must be complete.
                return IOErrorCode::CorruptedBasketArchive;
            }
        }
    }
    if (file.error() != QFileDevice::NoError)
        return IOErrorCode::FailedToOpenResource;
    if (!hasArchive)
        return IOErrorCode::CorruptedBasketArchive;
    file.close();

    const QFileInfo currentDestination(destinationPath);
    if ((currentDestination.exists() || currentDestination.isSymLink()) && protectDestination)
        return IOErrorCode::DestinationExists;
    if (currentDestination.isSymLink() || (currentDestination.exists() && !currentDestination.isDir()))
        return IOErrorCode::FailedToOpenResource;

    const QString previousPath = temporary.filePath(QStringLiteral("previous"));
    const bool replaced = currentDestination.exists();
    if (replaced && !QDir().rename(destinationPath, previousPath))
        return IOErrorCode::FailedToOpenResource;
    if (!QDir().rename(extractedPath, destinationPath)) {
        if (replaced && !QDir().rename(previousPath, destinationPath)) {
            // If an external filesystem change also prevents rollback, retain
            // the original directory for recovery instead of deleting it.
            temporary.setAutoRemove(false);
            qCWarning(BASKET_LOG) << "Could not restore extraction destination; original retained at" << previousPath;
        }
        return IOErrorCode::FailedToOpenResource;
    }
    return result;
}

Archive::IOErrorCode
Archive::createArchiveFromSource(const QString &sourcePath, const QString &previewImage, const QString &destination, const bool protectDestination)
{
    QDir source(sourcePath);
    if (!source.exists() || !source.isReadable())
        return IOErrorCode::FailedToOpenResource;
    const QFileInfo destinationFile(destination);
    if ((destinationFile.exists() || destinationFile.isSymLink()) && protectDestination)
        return IOErrorCode::DestinationExists;

    QTemporaryDir temporary;
    if (!temporary.isValid())
        return IOErrorCode::FailedToOpenResource;
    const QString archivePath = temporary.filePath(QStringLiteral("archive.tar.gz"));
    KTar archive(archivePath, QStringLiteral("application/x-gzip"));
    if (!archive.open(QIODevice::WriteOnly))
        return IOErrorCode::FailedToOpenResource;

    // Include hidden metadata as well as ordinary files. preview.png is stored
    // once in the envelope, as in both native and legacy archives.
    const QFileInfoList entries = source.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName() == QStringLiteral("preview.png"))
            continue;
        const bool added = entry.isDir() && !entry.isSymLink()
            ? addDirectoryToArchive(archive, entry.absoluteFilePath(), entry.fileName())
            : addFileToArchive(archive, entry.absoluteFilePath(), entry.fileName());
        if (!added)
            return IOErrorCode::FailedToOpenResource;
    }
    if (!archive.writeData(tarEndBlocks.data(), tarEndBlocks.size()) || !archive.close())
        return IOErrorCode::FailedToOpenResource;

    // An omitted preview uses the application icon; an explicitly supplied
    // unreadable preview is an error rather than a silently incomplete export.
    const QString previewPath = previewImage.isEmpty() ? QStringLiteral(":/mathom/icons/app.png") : previewImage;
    if ((QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()) && protectDestination)
        return IOErrorCode::DestinationExists;
    return writeArchiveEnvelope(archivePath, previewPath, destination, false) ? IOErrorCode::NoError : IOErrorCode::FailedToOpenResource;
}

/**
 * When opening a basket archive that come from another computer,
 * it can contains tags that use icons (emblems) that are not present on that computer.
 * Fortunately, basket archives contains a copy of every used icons.
 * This method check for every emblems and import the missing ones.
 * It also modify the tags.xml copy for the emblems to point to the absolute path of the imported icons.
 */
bool Archive::importTagEmblems(const QString &extractionFolder)
{
    QDomDocument *document = XMLWork::openFile(QStringLiteral("basketTags"), extractionFolder + QStringLiteral("tags.xml"));
    if (document == nullptr)
        return true;
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
    const bool saved = FileStorage::safelySaveToFile(extractionFolder + QStringLiteral("tags.xml"), document->toString());
    delete document;
    return saved;
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

bool Archive::renameBasketFolders(const QString &extractionFolder, QMap<QString, QString> &mergedStates)
{
    QDomDocument *doc = XMLWork::openFile(QStringLiteral("basketTree"), extractionFolder + QStringLiteral("baskets/baskets.xml"));
    if (doc != nullptr) {
        QMap<QString, QString> folderMap;
        QDomElement docElem = doc->documentElement();
        QDomNode node = docElem.firstChild();
        if (!renameBasketFolder(extractionFolder, node, folderMap, mergedStates)) {
            // Only empty directories reserved by this import are removed.
            for (const QString &folder : std::as_const(folderMap))
                QDir().rmdir(Global::basketsFolder() + folder);
            delete doc;
            return false;
        }
        loadExtractedBaskets(extractionFolder, node, folderMap, nullptr);
        delete doc;
        return true;
    }
    return false;
}

bool Archive::renameBasketFolder(const QString &extractionFolder, QDomNode &basketNode, QMap<QString, QString> &folderMap, QMap<QString, QString> &mergedStates)
{
    QDomNode n = basketNode;
    while (!n.isNull()) {
        QDomElement element = n.toElement();
        if ((!element.isNull()) && element.tagName() == QStringLiteral("basket")) {
            QString folderName = element.attribute(QStringLiteral("folderName"));
            if (!folderName.isEmpty()) {
                // Find a folder name:
                QString newFolderName = BasketFactory::newFolderName();
                // Reserve the folder name:
                QDir dir;
                if (!dir.mkdir(Global::basketsFolder() + newFolderName))
                    return false;
                folderMap[folderName] = newFolderName;
                // Rename the merged tag ids:
                //              if (mergedStates.count() > 0) {
                if (!renameMergedStatesAndBasketIcon(extractionFolder + QStringLiteral("baskets/") + folderName + QStringLiteral(".basket"),
                                                      mergedStates,
                                                      extractionFolder))
                    return false;
                //              }
                // Child baskets:
                QDomNode node = element.firstChild();
                if (!renameBasketFolder(extractionFolder, node, folderMap, mergedStates))
                    return false;
            }
        }
        n = n.nextSibling();
    }
    return true;
}

bool Archive::renameMergedStatesAndBasketIcon(const QString &fullPath, QMap<QString, QString> &mergedStates, const QString &extractionFolder)
{
    QDomDocument *doc = XMLWork::openFile(QStringLiteral("basket"), fullPath);
    if (doc == nullptr)
        return true;
    QDomElement docElem = doc->documentElement();
    QDomElement properties = XMLWork::getElement(docElem, QStringLiteral("properties"));
    importBasketIcon(properties, extractionFolder);
    QDomElement notes = XMLWork::getElement(docElem, QStringLiteral("notes"));
    if (mergedStates.count() > 0)
        renameMergedStates(notes, mergedStates);
    const bool saved = FileStorage::safelySaveToFile(fullPath, doc->toString());
    delete doc;
    return saved;
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
