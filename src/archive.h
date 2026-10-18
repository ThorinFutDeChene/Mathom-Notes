/**
 * SPDX-FileCopyrightText: (C) 2006 by Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "basket_export.h"

#include <QList>
#include <QMap>

class BasketScene;
class Tag;

class QString;
// class QStringList;
class QDomNode;
class QProgressDialog;
class QDomElement;

class KTar;

/**
 * @author Sébastien Laoût <slaout@linux62.org>
 */
class Archive
{
public:
    // Return true only after the complete archive has been committed.
    // On failure, errorMessage receives a translated diagnostic if supplied.
    static bool save(BasketScene *basket, bool withSubBaskets, const QString &destination, QString *errorMessage = nullptr);
    static bool saveAll(const QString &destination, QString *errorMessage = nullptr);
    static void open(const QString &path);

    /**
     * @brief The IOErrorCode enum granularly indicates whether encoding or decoding of baskets archive succeeded
     */
    enum class IOErrorCode : quint8 {
        NoError,
        NotABasketArchive,
        CorruptedBasketArchive,
        DestinationExists,
        IncompatibleBasketVersion,
        PossiblyCompatibleBasketVersion,
        FailedToOpenResource,
    };

    /**
     * @brief extractArchive decodes native .mathom archives and legacy .baskets archives
     * @param path to the .baskets file
     * @param destination into which the basket archive should be extracted
     * @param protectDestination decides whether the destination will be replaced if it has been present
     * \todo protectDestination likely should be an enum, too, to be more descriptive
     * @return NoError or PossiblyCompatibleBasketVersion after complete extraction; otherwise a failure code.
     */
    BASKET_EXPORT static IOErrorCode extractArchive(const QString &path, const QString &destination, const bool protectDestination = true);

    /**
     * @brief createArchiveFromSource encodes a Mathom archive source directory into a .mathom file
     *
     * Be aware, this function currently does not validate the sourcePath's structure. The caller of this function needs to make sure the basket directory is a
     * valid source.
     *
     * @param sourcePath specifies the directory to be encoded
     * @param previewImage specifies an optional .png image
     * @param destination specifies where the encoded .mathom file should be saved
     * @param protectDestination decides whether the destination will be replaced if it has been present
     * @return NoError only after the complete archive has been committed. Other codes indicate failure.
     */
    BASKET_EXPORT static IOErrorCode createArchiveFromSource(const QString &sourcePath,
                                                             const QString &previewImage,
                                                             const QString &destination = QString(),
                                                             const bool protectDestination = true);

private:
    // Convenient Methods for Saving:
    static bool saveBaskets(const QList<BasketScene *> &baskets, bool recursive, const QString &destination, bool allBaskets, QString *errorMessage);
    static bool saveBasketToArchive(BasketScene *basket,
                                    bool recursive,
                                    KTar *tar,
                                    QStringList &backgrounds,
                                    const QString &tempFolder,
                                    QProgressDialog *progress,
                                    QString *errorMessage);
    static void listUsedTags(BasketScene *basket, bool recursive, QList<Tag *> &list);
    // Convenient Methods for Loading:
    static bool renameBasketFolders(const QString &extractionFolder, QMap<QString, QString> &mergedStates);
    static bool
    renameBasketFolder(const QString &extractionFolder, QDomNode &basketNode, QMap<QString, QString> &folderMap, QMap<QString, QString> &mergedStates);
    static bool renameMergedStatesAndBasketIcon(const QString &fullPath, QMap<QString, QString> &mergedStates, const QString &extractionFolder);
    static void renameMergedStates(QDomNode notes, QMap<QString, QString> &mergedStates);
    static void importBasketIcon(QDomElement properties, const QString &extractionFolder);
    static void loadExtractedBaskets(const QString &extractionFolder, QDomNode &basketNode, QMap<QString, QString> &folderMap, BasketScene *parent);
    static bool importTagEmblems(const QString &extractionFolder);
    static void importArchivedBackgroundImages(const QString &extractionFolder);
};

#endif
