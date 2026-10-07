/**
 * SPDX-FileCopyrightText: (C) 2003 Sébastien Laoût <slaout@linux62.org>
 * SPDX-FileCopyrightText: (C) 2026 Thorinux
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HTMLEXPORTER_H
#define HTMLEXPORTER_H

#include <QHash>
#include <QScopedPointer>
#include <QString>
#include <QTextStream>

class QProgressDialog;

class BasketScene;
class Note;

/**
 * Export one Mathom-House and all of its shelves as a self-contained
 * static HTML mini-site.
 *
 * Each Mathom Page gets its own HTML document.  The Page that was current
 * when the export started keeps the historical Mathom-House file name, so
 * old cross references remain useful; the other Pages are stored next to the
 * exported shelf documents.
 *
 * @author Sébastien Laoût
 */
class HTMLExporter
{
public:
    explicit HTMLExporter(BasketScene *basket);
    ~HTMLExporter();

    bool succeeded() const
    {
        return m_succeeded;
    }

    QString linkToBasket(BasketScene *basket) const;

private:
    void prepareExport(BasketScene *basket, const QString &fullPath);
    void exportBasket(BasketScene *basket, bool isSubBasket);
    void exportBasketPage(
        BasketScene *basket,
        bool isSubBasket,
        const QString &pageId,
        bool isDefaultPage);
    void exportNote(Note *note, int indent);

    void writeBasketTree(BasketScene *currentBasket);
    void writeBasketTree(
        BasketScene *currentBasket,
        BasketScene *basket,
        int indent);

    void writePageNavigation(
        BasketScene *basket,
        const QString &pageId,
        bool isSubBasket,
        bool isDefaultPage);

    QString defaultPageId(BasketScene *basket) const;
    QString pageDocumentFileName(
        BasketScene *basket,
        const QString &pageId) const;
    QString pageDocumentPath(
        BasketScene *basket,
        bool isSubBasket,
        const QString &pageId,
        bool isDefaultPage) const;
    QString pageDocumentLink(
        BasketScene *basket,
        const QString &pageId,
        bool targetIsDefaultPage) const;

    int documentCount(BasketScene *basket) const;
    bool noteBelongsToCurrentPage(Note *note) const;
    bool shouldExportNote(Note *note) const;
    int exportableDirectChildCount(Note *note) const;

    void saveToFile(
        const QString &fullPath,
        const QByteArray &array);

public:
    QString copyIcon(const QString &iconName, int size);
    QString copyFile(const QString &srcPath, bool createIt);

public: // Used by NoteContent HTML exporters.
    // Absolute path of the file name the user chose:
    QString filePath; // eg.: "/home/seb/foo.html"
    QString fileName; // eg.: "foo.html"

    // Absolute & relative paths for the current document:
    QString basketFilePath;
    QString filesFolderPath;
    QString filesFolderName;
    QString iconsFolderPath;
    QString iconsFolderName;
    QString imagesFolderPath;
    QString imagesFolderName;
    QString dataFolderPath;
    QString dataFolderName;
    QString basketsFolderPath;
    QString basketsFolderName;

    // Various properties of the currently exporting Page:
    QString backgroundColorName;

    // Variables used by every export method:
    QTextStream stream;
    BasketScene *exportedBasket = nullptr;
    BasketScene *currentBasket = nullptr;
    bool withBasketTree = false;
    QScopedPointer<QProgressDialog> dialog;

private:
    QHash<BasketScene *, QString> m_defaultPageIds;
    bool m_currentDocumentInBasketsFolder = false;
    bool m_failed = false;
    bool m_succeeded = false;
};

#endif // HTMLEXPORTER_H
