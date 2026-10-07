/**
 * SPDX-FileCopyrightText: (C) 2003 Sébastien Laoût <slaout@linux62.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "htmlexporter.h"

#include "basketlistview.h"
#include "basketscene.h"
#include "bnpview.h"
#include "common.h"
#include "config.h"
#include "global.h"
#include "linklabel.h"
#include "note.h"
#include "notecontent.h"
#include "tools.h"

#include <KAboutData>
#include <KConfig>
#include <KConfigGroup>
#include <KIconLoader>
#include <KLocalizedString>
#include <KMessageBox>

#include <KIO/CopyJob> //For KIO::copy
#include <KIO/FileCopyJob> //For KIO::file_copy
#include <KIO/Job>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QList>
#include <QPainter>
#include <QPixmap>
#include <QProgressDialog>
#include <QTextStream>

#include <basket_debug.h>
#include <basket_version.h>

HTMLExporter::HTMLExporter(BasketScene *basket)
    : dialog(new QProgressDialog())
{
    if (!basket)
        return;

    QDir dir;

    KConfigGroup config =
        Global::config()->group(
            QStringLiteral("Export to HTML"));

    const QString folder =
        config.readEntry(
            "lastFolder",
            QDir::homePath())
        + QLatin1Char('/');

    QString destination =
        folder
        + QString(basket->basketName())
              .replace(QLatin1Char('/'), QLatin1Char('_'))
        + QStringLiteral(".html");

    const QString filter =
        QStringLiteral("*.html *.htm|")
        + i18n("HTML Documents")
        + QStringLiteral("\n*|")
        + i18n("All Files");

    for (bool askAgain = true; askAgain;) {
        destination =
            QFileDialog::getSaveFileName(
                nullptr,
                i18n("Export to HTML"),
                destination,
                filter);

        if (destination.isEmpty())
            return;

        if (!destination.endsWith(
                QStringLiteral(".html"),
                Qt::CaseInsensitive)
            && !destination.endsWith(
                QStringLiteral(".htm"),
                Qt::CaseInsensitive)) {
            destination += QStringLiteral(".html");
        }

        if (dir.exists(destination)) {
            const int result =
                KMessageBox::questionTwoActionsCancel(
                    nullptr,
                    QStringLiteral("<qt>")
                        + i18n(
                            "The file <b>%1</b> already exists. "
                            "Do you really want to overwrite it?",
                            QUrl::fromLocalFile(destination)
                                .fileName()),
                    i18n("Overwrite File?"),
                    KGuiItem(
                        i18n("&Overwrite"),
                        QStringLiteral("document-save")),
                    KStandardGuiItem::discard());

            if (result == KMessageBox::Cancel)
                return;

            if (result == KMessageBox::Ok)
                askAgain = false;
        } else {
            askAgain = false;
        }
    }

    dialog->setWindowTitle(i18n("Export to HTML"));
    dialog->setLabelText(
        i18n("Exporting the Mathom-House to HTML. Please wait..."));
    dialog->setCancelButton(nullptr);
    dialog->setAutoClose(true);
    dialog->show();

    config.writeEntry(
        "lastFolder",
        QUrl::fromLocalFile(destination)
            .adjusted(QUrl::RemoveFilename)
            .path());
    config.sync();

    prepareExport(basket, destination);
    exportBasket(basket, false);

    dialog->setValue(dialog->value() + 1);

    m_succeeded =
        QFile::exists(filePath);
}

HTMLExporter::~HTMLExporter() = default;

void HTMLExporter::prepareExport(
    BasketScene *basket,
    const QString &fullPath)
{
    dialog->setRange(
        0,
        1
            + documentCount(basket)
            + 1);

    dialog->setValue(0);
    qApp->processEvents();

    filePath = fullPath;
    fileName =
        QUrl::fromLocalFile(fullPath)
            .fileName();

    exportedBasket = basket;
    currentBasket = nullptr;

    BasketListViewItem *item =
        Global::bnpView
            ? Global::bnpView->listViewItemForBasket(basket)
            : nullptr;

    withBasketTree =
        item
        && item->childCount() > 0;

    filesFolderPath =
        i18nc(
            "HTML export folder (files)",
            "%1_files",
            filePath)
        + QLatin1Char('/');

    Tools::deleteRecursively(filesFolderPath);

    QDir dir;

    dir.mkpath(filesFolderPath);

    iconsFolderPath =
        filesFolderPath
        + i18nc(
            "HTML export folder (icons)",
            "icons")
        + QLatin1Char('/');

    imagesFolderPath =
        filesFolderPath
        + i18nc(
            "HTML export folder (images)",
            "images")
        + QLatin1Char('/');

    basketsFolderPath =
        filesFolderPath
        + i18nc(
            "HTML export folder (Mathom-Houses)",
            "mathom-houses")
        + QLatin1Char('/');

    dir.mkpath(iconsFolderPath);
    dir.mkpath(imagesFolderPath);
    dir.mkpath(basketsFolderPath);

    dialog->setValue(dialog->value() + 1);
}

QString HTMLExporter::defaultPageId(
    BasketScene *basket) const
{
    if (!basket
        || basket->pages().isEmpty()) {
        return {};
    }

    const QString current =
        basket->currentPageId();

    for (const BasketScene::PageInfo &page :
         basket->pages()) {
        if (page.id == current)
            return current;
    }

    return basket->pages().first().id;
}

QString HTMLExporter::pageDocumentFileName(
    BasketScene *basket,
    const QString &pageId) const
{
    QString basketName =
        basket
            ? basket->folderName()
            : QStringLiteral("mathom");

    if (basketName.endsWith(QLatin1Char('/')))
        basketName.chop(1);

    return basketName
        + QStringLiteral("-page-")
        + pageId
        + QStringLiteral(".html");
}

QString HTMLExporter::pageDocumentPath(
    BasketScene *basket,
    bool isSubBasket,
    const QString &pageId,
    bool isDefaultPage) const
{
    if (isDefaultPage) {
        if (!isSubBasket)
            return filePath;

        QString basketName =
            basket->folderName();

        if (basketName.endsWith(QLatin1Char('/')))
            basketName.chop(1);

        return basketsFolderPath
            + basketName
            + QStringLiteral(".html");
    }

    return basketsFolderPath
        + pageDocumentFileName(
            basket,
            pageId);
}

QString HTMLExporter::pageDocumentLink(
    BasketScene *basket,
    const QString &pageId,
    bool targetIsDefaultPage) const
{
    if (targetIsDefaultPage) {
        if (basket == exportedBasket) {
            return m_currentDocumentInBasketsFolder
                ? QStringLiteral("../../") + fileName
                : QStringLiteral("#");
        }

        QString basketName =
            basket->folderName();

        if (basketName.endsWith(QLatin1Char('/')))
            basketName.chop(1);

        const QString target =
            basketName
            + QStringLiteral(".html");

        return m_currentDocumentInBasketsFolder
            ? target
            : basketsFolderName + target;
    }

    const QString target =
        pageDocumentFileName(
            basket,
            pageId);

    return m_currentDocumentInBasketsFolder
        ? target
        : basketsFolderName + target;
}

QString HTMLExporter::linkToBasket(
    BasketScene *basket) const
{
    if (!basket)
        return QStringLiteral("#");

    if (basket == exportedBasket) {
        return m_currentDocumentInBasketsFolder
            ? QStringLiteral("../../") + fileName
            : QStringLiteral("#");
    }

    QString basketName =
        basket->folderName();

    if (basketName.endsWith(QLatin1Char('/')))
        basketName.chop(1);

    const QString target =
        basketName
        + QStringLiteral(".html");

    return m_currentDocumentInBasketsFolder
        ? target
        : basketsFolderName + target;
}

int HTMLExporter::documentCount(
    BasketScene *basket) const
{
    if (!basket)
        return 0;

    if (!basket->isLoaded())
        basket->load();

    int count =
        qMax(
            1,
            basket->pages().size());

    if (!Global::bnpView)
        return count;

    BasketListViewItem *item =
        Global::bnpView
            ->listViewItemForBasket(basket);

    if (!item)
        return count;

    for (int index = 0;
         index < item->childCount();
         ++index) {
        auto *childItem =
            static_cast<BasketListViewItem *>(
                item->child(index));

        count +=
            documentCount(
                childItem->basket());
    }

    return count;
}

bool HTMLExporter::noteBelongsToCurrentPage(
    Note *note) const
{
    if (!note || !currentBasket)
        return false;

    const QString pageId =
        currentBasket->currentPageId();

    if (pageId.isEmpty())
        return true;

    if (note->content())
        return note->pageId() == pageId;

    if (currentBasket->currentPageOwnsLayout()
        && note->parentNote() == nullptr
        && note->pageId().isEmpty()) {
        return false;
    }

    return note->pageId().isEmpty()
        || note->pageId() == pageId;
}

bool HTMLExporter::shouldExportNote(
    Note *note) const
{
    if (!noteBelongsToCurrentPage(note))
        return false;

    if (note->isGroup()
        && !note->isColumn()
        && exportableDirectChildCount(note) == 0) {
        return false;
    }

    return true;
}

int HTMLExporter::exportableDirectChildCount(
    Note *note) const
{
    if (!note)
        return 0;

    int count = 0;

    for (Note *child = note->firstChild();
         child;
         child = child->next()) {
        if (shouldExportNote(child))
            ++count;
    }

    return count;
}

void HTMLExporter::exportBasket(
    BasketScene *basket,
    bool isSubBasket)
{
    if (!basket)
        return;

    if (!basket->isLoaded())
        basket->load();

    const QString originalPageId =
        basket->currentPageId();

    const QString defaultId =
        defaultPageId(basket);

    if (basket->pages().isEmpty()) {
        exportBasketPage(
            basket,
            isSubBasket,
            QString(),
            true);
    } else {
        if (basket->currentPageId()
            != defaultId) {
            basket->setCurrentPageId(
                defaultId);
        }

        exportBasketPage(
            basket,
            isSubBasket,
            defaultId,
            true);

        for (const BasketScene::PageInfo &page :
             basket->pages()) {
            if (page.id == defaultId)
                continue;

            basket->setCurrentPageId(
                page.id);

            exportBasketPage(
                basket,
                isSubBasket,
                page.id,
                false);
        }

        if (!originalPageId.isEmpty()
            && basket->currentPageId()
                != originalPageId) {
            basket->setCurrentPageId(
                originalPageId);
        }
    }

    if (!Global::bnpView)
        return;

    BasketListViewItem *item =
        Global::bnpView
            ->listViewItemForBasket(basket);

    if (!item)
        return;

    for (int index = 0;
         index < item->childCount();
         ++index) {
        auto *childItem =
            static_cast<BasketListViewItem *>(
                item->child(index));

        exportBasket(
            childItem->basket(),
            true);
    }
}

void HTMLExporter::exportBasketPage(
    BasketScene *basket,
    bool isSubBasket,
    const QString &pageId,
    bool isDefaultPage)
{
    currentBasket = basket;

    QString pageTitle;

    for (const BasketScene::PageInfo &page :
         basket->pages()) {
        if (page.id == pageId) {
            pageTitle = page.title;
            break;
        }
    }

    QString documentTitle =
        basket->basketName();

    if (!pageTitle.isEmpty()) {
        documentTitle +=
            QStringLiteral(" — ")
            + pageTitle;
    }

    const QColor backgroundSetting =
        basket->currentPageBackgroundColorSetting();

    const QColor textSetting =
        basket->currentPageTextColorSetting();

    const bool hasBackgroundColor =
        backgroundSetting.isValid();

    const bool hasTextColor =
        textSetting.isValid();

    backgroundColorName =
        hasBackgroundColor
            ? basket->backgroundColor()
                  .name()
                  .toLower()
                  .mid(1)
            : QStringLiteral("transparent");

    // The historical root document lives next to the chosen .html file.
    // Every shelf document and every additional Page lives in the
    // mathom-houses directory.
    m_currentDocumentInBasketsFolder =
        isSubBasket
        || !isDefaultPage;

    filesFolderPath =
        i18nc(
            "HTML export folder (files)",
            "%1_files",
            filePath)
        + QLatin1Char('/');

    basketFilePath =
        pageDocumentPath(
            basket,
            isSubBasket,
            pageId,
            isDefaultPage);

    if (m_currentDocumentInBasketsFolder) {
        filesFolderName =
            QStringLiteral("../");

        const QString documentBaseName =
            QFileInfo(basketFilePath)
                .completeBaseName();

        dataFolderName =
            documentBaseName
            + QLatin1Char('-')
            + i18nc(
                "HTML export folder (data)",
                "data")
            + QLatin1Char('/');

        dataFolderPath =
            basketsFolderPath
            + dataFolderName;

        basketsFolderName =
            QString();
    } else {
        filesFolderName =
            i18nc(
                "HTML export folder (files)",
                "%1_files",
                QUrl::fromLocalFile(filePath)
                    .fileName())
            + QLatin1Char('/');

        dataFolderName =
            filesFolderName
            + i18nc(
                "HTML export folder (data)",
                "data")
            + QLatin1Char('/');

        dataFolderPath =
            filesFolderPath
            + i18nc(
                "HTML export folder (data)",
                "data")
            + QLatin1Char('/');

        basketsFolderName =
            filesFolderName
            + i18nc(
                "HTML export folder (Mathom-Houses)",
                "mathom-houses")
            + QLatin1Char('/');
    }

    iconsFolderName =
        (m_currentDocumentInBasketsFolder
             ? QStringLiteral("../")
             : filesFolderName)
        + i18nc(
            "HTML export folder (icons)",
            "icons")
        + QLatin1Char('/');

    imagesFolderName =
        (m_currentDocumentInBasketsFolder
             ? QStringLiteral("../")
             : filesFolderName)
        + i18nc(
            "HTML export folder (images)",
            "images")
        + QLatin1Char('/');

    qCDebug(BASKET_LOG) << "Exporting ================================================";
    qCDebug(BASKET_LOG) << "  filePath:" << filePath;
    qCDebug(BASKET_LOG) << "  basketFilePath:" << basketFilePath;
    qCDebug(BASKET_LOG) << "  filesFolderPath:" << filesFolderPath;
    qCDebug(BASKET_LOG) << "  filesFolderName:" << filesFolderName;
    qCDebug(BASKET_LOG) << "  iconsFolderPath:" << iconsFolderPath;
    qCDebug(BASKET_LOG) << "  iconsFolderName:" << iconsFolderName;
    qCDebug(BASKET_LOG) << "  imagesFolderPath:" << imagesFolderPath;
    qCDebug(BASKET_LOG) << "  imagesFolderName:" << imagesFolderName;
    qCDebug(BASKET_LOG) << "  dataFolderPath:" << dataFolderPath;
    qCDebug(BASKET_LOG) << "  dataFolderName:" << dataFolderName;
    qCDebug(BASKET_LOG) << "  basketsFolderPath:" << basketsFolderPath;
    qCDebug(BASKET_LOG) << "  basketsFolderName:" << basketsFolderName;

    // Create the data folder for this basket:
    QDir dir;
    dir.mkpath(dataFolderPath);

    // Generate basket icons:
    QString basketIcon16 = iconsFolderName + copyIcon(basket->icon(), 16);
    QString basketIcon32 = iconsFolderName + copyIcon(basket->icon(), 32);

    // Generate the [+] image for groups:
    QPixmap expandGroup(Note::EXPANDER_WIDTH, Note::EXPANDER_HEIGHT);
    if (hasBackgroundColor) {
        expandGroup.fill(basket->backgroundColor());
    } else {
        expandGroup.fill(QColor(Qt::GlobalColor::transparent));
    }
    QPainter painter(&expandGroup);
    if (hasBackgroundColor) {
        Note::drawExpander(&painter, 0, 0, basket->backgroundColor(), /*expand=*/true, basket);
    } else {
        Note::drawExpander(&painter, 0, 0, QColor(Qt::GlobalColor::transparent), /*expand=*/true, basket);
    }
    painter.end();
    if (hasBackgroundColor) {
        expandGroup.save(imagesFolderPath + QStringLiteral("expand_group_") + backgroundColorName + QStringLiteral(".png"), "PNG");
    } else {
        expandGroup.save(imagesFolderPath + QStringLiteral("expand_group_transparent.png"), "PNG");
    }

    // Generate the [-] image for groups:
    QPixmap foldGroup(Note::EXPANDER_WIDTH, Note::EXPANDER_HEIGHT);
    if (hasBackgroundColor) {
        foldGroup.fill(basket->backgroundColor());
    } else {
        foldGroup.fill(QColor(Qt::GlobalColor::transparent));
    }
    painter.begin(&foldGroup);
    if (hasBackgroundColor) {
        Note::drawExpander(&painter, 0, 0, basket->backgroundColor(), /*expand=*/false, basket);
    } else {
        Note::drawExpander(&painter, 0, 0, QColor(Qt::GlobalColor::transparent), /*expand=*/false, basket);
    }

    painter.end();
    if (hasBackgroundColor) {
        foldGroup.save(imagesFolderPath + QStringLiteral("fold_group_") + backgroundColorName + QStringLiteral(".png"), "PNG");
    } else {
        foldGroup.save(imagesFolderPath + QStringLiteral("fold_group_transparent.png"), "PNG");
    }

    // Open the file to write:
    QFile file(basketFilePath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    stream.setDevice(&file);

    // Output the header:
    QString borderColor;
    if (hasBackgroundColor && hasTextColor) {
        borderColor = Tools::mixColor(basket->backgroundColor(), basket->textColor()).name();
    } else if (hasBackgroundColor) {
        borderColor = Tools::mixColor(basket->backgroundColor(), QColor(Qt::GlobalColor::black)).name();
    } else if (hasTextColor) {
        borderColor = Tools::mixColor(QColor(Qt::GlobalColor::white), basket->textColor()).name();
    } else {
        borderColor =
            Tools::mixColor(
                basket->palette().color(QPalette::Base),
                basket->palette().color(QPalette::Text))
                .name();
    }
    stream << "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\">\n"
              "<html>\n"
              " <head>\n"
              "  <meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\">\n"
              "  <meta http-equiv=\"content-type\" content=\"text/html; charset=utf-8\"><meta name=\"Generator\" content=\""
           << QGuiApplication::applicationDisplayName() << " " << BASKET_VERSION_STRING << " " << KAboutData::applicationData().homepage()
           << "\">\n"
              "  <style type=\"text/css\">\n"
              //      "   @media print {\n"
              //      "    span.printable { display: inline; }\n"
              //      "   }\n"
              "   body { margin: 10px; font: 11px sans-serif; }\n" // TODO: Use user font
              "   h1 { text-align: center; }\n"
              "   .pageTitle { margin: 0 0 6px 0; font-size: 120%; }\n"
              "   .pages { margin: 0 0 8px 0; padding: 6px; border: 1px solid #bbb; border-radius: 5px; }\n"
              "   .pages a, .pages span { display: inline-block; margin: 2px 4px 2px 0; padding: 3px 6px; text-decoration: none; }\n"
              "   .pages .current { font-weight: bold; border-bottom: 2px solid currentColor; }\n"
              "   img { border: none; vertical-align: middle; }\n";
    if (withBasketTree) {
        stream << "   .tree { margin: 0; padding: 1px 0 1px 1px; width: 150px; _width: 149px; overflow: hidden; float: left; }\n"
                  "   .tree ul { margin: 0 0 0 10px; padding: 0; }\n"
                  "   .tree li { padding: 0; margin: 0; list-style: none; }\n"
                  "   .tree a { display: block; padding: 1px; height: 16px; text-decoration: none;\n"
                  "             white-space: nowrap; word-wrap: normal; text-wrap: suppress; color: black; }\n"
                  "   .tree span { -moz-border-radius: 6px; display: block; float: left;\n"
                  "                line-height: 16px; height: 16px; vertical-align: middle; padding: 0 1px; }\n"
                  "   .tree img { vertical-align: top; padding-right: 1px; }\n"
                  "   .tree .current { background-color: "
               << qApp->palette().color(QPalette::Highlight).name()
               << "; "
                  "-moz-border-radius: 3px 0 0 3px; border-radius: 3px 0 0 3px; color: "
               << qApp->palette().color(QPalette::Highlight).name()
               << "; }\n"
                  "   .basketSurrounder { margin-left: 152px; _margin: 0; _float: right; }\n";
    }

    stream << "   .basket { ";
    if (hasBackgroundColor) {
        stream << "background-color: " << basket->backgroundColor().name() << "; ";
    }
    stream << "border: solid " << borderColor
           << " 1px; "
              "font: "
           << Tools::cssFontDefinition(basket->QGraphicsScene::font()) << "; ";
    if (hasTextColor) {
        stream << "color: " << basket->textColor().name() << "; ";
    }
    stream << "padding: 1px; width: 100%; }\n"
              "   table.basket { border-collapse: collapse; }\n"
              "   .basket * { padding: 0; margin: 0; }\n"
              "   .basket table { width: 100%; border-spacing: 0; _border-collapse: collapse; }\n"
              "   .column { vertical-align: top; }\n"
              "   .columnHandle { width: "
           << Note::RESIZER_WIDTH << "px; background: transparent url('" << imagesFolderName << "column_handle_";
    if (hasBackgroundColor) {
        stream << backgroundColorName;
    } else {
        stream << "transparent";
    }
    stream << ".png') repeat-y; }\n"
              "   .group { margin: 0; padding: 0; border-collapse: collapse; width: 100% }\n"
              "   .groupHandle { margin: 0; width: "
           << Note::GROUP_WIDTH
           << "px; text-align: center; }\n"
              "   .note { padding: 1px 2px; ";
    if (hasBackgroundColor) {
        stream << "background-color : " << basket->backgroundColor().name() << "; ";
    }
    stream << "width: 100%; }\n"
              "   .tags { width: 1px; white-space: nowrap; }\n"
              "   .tags img { padding-right: 2px; }\n";
    if (hasTextColor) {
        stream << LinkLook::soundLook->toCSS(QStringLiteral("sound"), basket->textColor())
               << LinkLook::fileLook->toCSS(QStringLiteral("file"), basket->textColor())
               << LinkLook::localLinkLook->toCSS(QStringLiteral("local"), basket->textColor())
               << LinkLook::networkLinkLook->toCSS(QStringLiteral("network"), basket->textColor())
               << LinkLook::launcherLook->toCSS(QStringLiteral("launcher"), basket->textColor())
               << LinkLook::crossReferenceLook->toCSS(QStringLiteral("cross_reference"), basket->textColor());
    } else {
        stream << LinkLook::soundLook->toCSS(QStringLiteral("sound"), QColor(Qt::GlobalColor::black))
               << LinkLook::fileLook->toCSS(QStringLiteral("file"), QColor(Qt::GlobalColor::black))
               << LinkLook::localLinkLook->toCSS(QStringLiteral("local"), QColor(Qt::GlobalColor::black))
               << LinkLook::networkLinkLook->toCSS(QStringLiteral("network"), QColor(Qt::GlobalColor::black))
               << LinkLook::launcherLook->toCSS(QStringLiteral("launcher"), QColor(Qt::GlobalColor::black))
               << LinkLook::crossReferenceLook->toCSS(QStringLiteral("cross_reference"), QColor(Qt::GlobalColor::black));
    }
    stream << "   .unknown { margin: 1px 2px; border: 1px solid " << borderColor << "; -moz-border-radius: 4px; }\n";
    QList<State *> states = basket->usedStates();
    QString statesCss;
    for (State::List::Iterator it = states.begin(); it != states.end(); ++it)
        statesCss += (*it)->toCSS(imagesFolderPath, imagesFolderName, basket->QGraphicsScene::font());
    stream << statesCss << "   .credits { text-align: right; margin: 3px 0 0 0; _margin-top: -17px; font-size: 80%; color: " << borderColor
           << "; }\n"
              "  </style>\n"
              "  <title>"
           << Tools::textToHTMLWithoutP(documentTitle)
           << "</title>\n"
              "  <link rel=\"shortcut icon\" type=\"image/png\" href=\""
           << basketIcon16 << "\">\n";
    // Create the column handle image:
    QPixmap columnHandle(Note::RESIZER_WIDTH, 50);
    painter.begin(&columnHandle);
    if (hasBackgroundColor) {
        Note::drawInactiveResizer(&painter, 0, 0, columnHandle.height(), basket->backgroundColor(), /*column=*/true);
    } else {
        Note::drawInactiveResizer(&painter, 0, 0, columnHandle.height(), QColor(Qt::GlobalColor::black), /*column=*/true);
    }
    painter.end();

    if (hasBackgroundColor) {
        columnHandle.save(imagesFolderPath + QStringLiteral("column_handle_") + backgroundColorName + QStringLiteral(".png"), "PNG");
    } else {
        columnHandle.save(imagesFolderPath + QStringLiteral("column_handle_transparent.png"), "PNG");
    }

    stream << " </head>\n"
              " <body>\n"
              "  <h1><img src=\""
           << QUrl(basketIcon32).toString() << R"(" width="32" height="32" alt=""> )" << Tools::textToHTMLWithoutP(basket->basketName()) << "</h1>\n";

    if (withBasketTree)
        writeBasketTree(basket);

    // If filtering, only export filtered notes, inform to the user:
    // TODO: Filtering tags too!!
    // TODO: Make sure only filtered notes are exported!
    //  if (decoration()->filterData().isFiltering)
    //      stream <<
    //          "  <p>" << i18n("Notes matching the filter &quot;%1&quot;:", Tools::textToHTMLWithoutP(decoration()->filterData().string)) << "</p>\n";

    stream << "  <div class=\"basketSurrounder\">\n";

    if (!pageTitle.isEmpty()) {
        stream
            << "   <h2 class=\"pageTitle\">"
            << Tools::textToHTMLWithoutP(pageTitle)
            << "</h2>\n";
    }

    writePageNavigation(
        basket,
        pageId,
        isSubBasket,
        isDefaultPage);

    stream << R"(   <div class="basket" style="position: relative; min-width: 100%; min-height: calc(100vh - 100px); )";
    if (!basket->isColumnsLayout()) {
        stream << "height: " << basket->sceneRect().height() << "px; width: " << basket->sceneRect().width() << "px; ";
    }
    stream << "\">\n";

    if (basket->isColumnsLayout()) {
        stream << "   <table>\n"
                  "    <tr>\n";
    }

    for (Note *note = basket->firstNote(); note; note = note->next()) {
        exportNote(note, /*indent=*/(basket->isFreeLayout() ? 4 : 5));
    }

    // Output the footer:
    if (basket->isColumnsLayout()) {
        stream << "    </tr>\n"
                  "   </table>\n";
    }

    stream << "   </div>\n";

    stream << QStringLiteral(
                  "  </div>\n"
                  "  <p class=\"credits\">%1</p>\n")
                  .arg(i18n("Made with <a href=\"%1\">%2</a> %3, a tool to organize mathoms and keep information at hand.",
                            KAboutData::applicationData().homepage(),
                            QGuiApplication::applicationDisplayName(),
                            QStringLiteral(BASKET_VERSION_STRING)));

    stream << " </body>\n"
              "</html>\n";

    file.close();
    stream.setDevice(nullptr);
    dialog->setValue(dialog->value() + 1); // Basket exportation finished

}

void HTMLExporter::exportNote(Note *note, int indent)
{
    if (!shouldExportNote(note))
        return;

    QString spaces;

    if (note->isColumn()) {
        QString width;
        // Disabled intentionally: this historical calculation mixes pixel
        // values with an HTML percentage width and can produce invalid
        // proportions (often clamped to 100%). Keep it as a basis for a
        // future, properly tested proportional column export.
        if (false) {
            // As we cannot be precise in CSS (say eg. "width: 50%-40px;"),
            // we output a percentage that is approximately correct.
            // For instance, we compute the currently used percentage of width in the basket
            // and try make it the same on a 1024*768 display in a Web browser:
            int availableSpaceForColumnsInThisBasket = note->basket()->sceneRect().width() - (note->basket()->columnsCount() - 1) * Note::RESIZER_WIDTH;
            int availableSpaceForColumnsInBrowser = 1024 /* typical screen width */
                - 25 /* window border and scrollbar width */
                - 2 * 5 /* page margin */
                - (note->basket()->columnsCount() - 1) * Note::RESIZER_WIDTH;
            if (availableSpaceForColumnsInThisBasket <= 0)
                availableSpaceForColumnsInThisBasket = 1;
            int widthValue = (int)(availableSpaceForColumnsInBrowser * (double)note->groupWidth() / availableSpaceForColumnsInThisBasket);
            if (widthValue <= 0)
                widthValue = 1;
            if (widthValue > 100)
                widthValue = 100;
            width = QStringLiteral(" width=\"%1%\"").arg(QString::number(widthValue));
        }
        stream << spaces.fill(QLatin1Char(' '), indent) << "<td class=\"column\"" << width << ">\n";

        // Export child notes:
        for (Note *child = note->firstChild(); child; child = child->next()) {
            if (!shouldExportNote(child))
                continue;

            stream << spaces.fill(QLatin1Char(' '), indent + 1);
            exportNote(child, indent + 1);
            stream << '\n';
        }

        stream << spaces.fill(QLatin1Char(' '), indent) << "</td>\n";
        if (note->hasResizer())
            stream << spaces.fill(QLatin1Char(' '), indent) << "<td class=\"columnHandle\"></td>\n";
        return;
    }

    QString freeStyle;
    if (note->isFree())
        freeStyle = QStringLiteral(" style=\"position: absolute; left: ") + QString::number(note->x()) + QStringLiteral("px; top: ")
            + QString::number(note->y()) + QStringLiteral("px; width: ") + QString::number(note->groupWidth()) + QStringLiteral("px\"");

    if (note->isGroup()) {
        stream << '\n'
               << spaces.fill(QLatin1Char(' '), indent) << "<table" << freeStyle
               << ">\n"; // Note content is expected to be on the same HTML line, but NOT groups
        int i = 0;
        const int exportedChildCount =
            exportableDirectChildCount(note);

        for (Note *child = note->firstChild(); child; child = child->next()) {
            if (!shouldExportNote(child))
                continue;

            stream << spaces.fill(QLatin1Char(' '), indent);
            if (i == 0)
                stream << R"( <tr><td class="groupHandle"><img src=")" << QUrl(imagesFolderName).toString()
                       << (note->isFolded() ? "expand_group_" : "fold_group_") << backgroundColorName << ".png"
                       << "\" width=\"" << Note::EXPANDER_WIDTH << "\" height=\"" << Note::EXPANDER_HEIGHT << "\"></td>\n";
            else if (i == 1)
                stream << R"( <tr><td class="freeSpace" rowspan=")" << exportedChildCount << "\"></td>\n";
            else
                stream << " <tr>\n";
            stream << spaces.fill(QLatin1Char(' '), indent) << "  <td>";
            exportNote(child, indent + 3);
            stream << "</td>\n" << spaces.fill(QLatin1Char(' '), indent) << " </tr>\n";
            ++i;
        }
        stream << '\n' << spaces.fill(QLatin1Char(' '), indent) << "</table>\n" /*<< spaces.fill(' ', indent - 1)*/;
    } else {
        // Additional class for the content (link, netword, color...):
        QString additionalClasses = note->content()->cssClass();
        if (!additionalClasses.isEmpty())
            additionalClasses = QLatin1Char(' ') + additionalClasses;
        // Assign the style of each associated tags:
        for (State::List::Iterator it = note->states().begin(); it != note->states().end(); ++it)
            additionalClasses += QStringLiteral(" tag_") + (*it)->id();
        // stream << spaces.fill(' ', indent);
        stream << "<table class=\"note" << additionalClasses << "\"" << freeStyle << "><tr>";
        if (note->emblemsCount() > 0) {
            stream << "<td class=\"tags\"><nobr>";
            for (State::List::Iterator it = note->states().begin(); it != note->states().end(); ++it)
                if (!(*it)->emblem().isEmpty()) {
                    int emblemSize = 16;
                    QString iconFileName = copyIcon((*it)->emblem(), emblemSize);
                    stream << "<img src=\"" << QUrl(iconsFolderName + iconFileName).toString() << "\" width=\"" << emblemSize << "\" height=\"" << emblemSize
                           << "\" alt=\"" << (*it)->textEquivalent() << "\" title=\"" << (*it)->fullName() << "\">";
                }
            stream << "</nobr></td>";
        }
        stream << "<td>";
        note->content()->exportToHTML(this, indent);
        stream << "</td></tr></table>";
    }
}

void HTMLExporter::writePageNavigation(
    BasketScene *basket,
    const QString &pageId,
    bool isSubBasket,
    bool isDefaultPage)
{
    Q_UNUSED(isSubBasket)
    Q_UNUSED(isDefaultPage)

    if (!basket
        || basket->pages().size() <= 1) {
        return;
    }

    const QString defaultId =
        defaultPageId(basket);

    stream << "   <nav class=\"pages\" aria-label=\""
           << i18n("Pages")
           << "\">\n";

    for (const BasketScene::PageInfo &page :
         basket->pages()) {
        const QString title =
            Tools::textToHTMLWithoutP(page.title);

        if (page.id == pageId) {
            stream
                << "    <span class=\"current\">"
                << title
                << "</span>\n";
            continue;
        }

        const bool targetIsDefault =
            page.id == defaultId;

        stream
            << "    <a href=\""
            << QUrl(
                   pageDocumentLink(
                       basket,
                       page.id,
                       targetIsDefault))
                   .toString()
            << "\">"
            << title
            << "</a>\n";
    }

    stream << "   </nav>\n";
}

void HTMLExporter::writeBasketTree(BasketScene *currentBasket)
{
    stream << "  <ul class=\"tree\">\n";
    writeBasketTree(currentBasket, exportedBasket, 3);
    stream << "  </ul>\n";
}

void HTMLExporter::writeBasketTree(BasketScene *currentBasket, BasketScene *basket, int indent)
{
    // Compute variable HTML code:
    QString spaces;
    QString cssClass = (basket == currentBasket ? QStringLiteral(" class=\"current\"") : QString());
    const QString link =
        linkToBasket(basket);
    QString spanStyle = QStringLiteral(" style=\"");
    if (basket->backgroundColorSetting().isValid()) {
        spanStyle += QStringLiteral("background-color: ") + basket->backgroundColor().name() + QStringLiteral("; ");
    }
    if (basket->textColorSetting().isValid()) {
        spanStyle += QStringLiteral("color: ") + basket->textColor().name() + QStringLiteral("; ");
    }
    spanStyle += QStringLiteral("\"");

    // Write the basket tree line:
    stream << spaces.fill(QLatin1Char(' '), indent) << "<li><a" << cssClass << " href=\"" << link
           << "\">"
              "<span"
           << spanStyle << " title=\"" << Tools::textToHTMLWithoutP(basket->basketName())
           << "\">"
              "<img src=\""
           << iconsFolderName << copyIcon(basket->icon(), 16) << R"(" width="16" height="16" alt="">)" << Tools::textToHTMLWithoutP(basket->basketName())
           << "</span></a>";

    // Write the sub-baskets lines & end the current one:
    BasketListViewItem *item = Global::bnpView->listViewItemForBasket(basket);
    if (item->childCount() >= 0) {
        stream << "\n" << spaces.fill(QLatin1Char(' '), indent) << " <ul>\n";
        for (int i = 0; i < item->childCount(); i++)
            writeBasketTree(currentBasket, ((BasketListViewItem *)item->child(i))->basket(), indent + 2);
        stream << spaces.fill(QLatin1Char(' '), indent) << " </ul>\n" << spaces.fill(QLatin1Char(' '), indent) << "</li>\n";
    } else {
        stream << "</li>\n";
    }
}

/** Save an icon to a folder.
 * If an icon with the same name already exist in the destination,
 * it is assumed the icon is already copied, so no action is took.
 * It is optimized so that you can have an empty folder receiving the icons
 * and call copyIcon() each time you encounter one during export process.
 */
QString HTMLExporter::copyIcon(const QString &iconName, int size)
{
    if (iconName.isEmpty()) {
        return {};
    }

    // Sometimes icon can be "favicons/www.kde.org", we replace the '/' with a '_'
    QString fileName = iconName; // QString::replace() isn't const, so I must copy the string before
    fileName = QStringLiteral("ico") + QString::number(size) + QLatin1Char('_') + fileName.replace(QLatin1Char('/'), QLatin1Char('_')) + QStringLiteral(".png");
    QString fullPath = iconsFolderPath + fileName;
    if (!QFile::exists(fullPath)) {
        KIconLoader::global()->loadIcon(iconName, KIconLoader::Desktop, size).save(fullPath, "PNG");
    }
    return fileName;
}

/** Done: Sometimes we can call two times copyFile() with the same srcPath and dataFolderPath
 *       (eg. when exporting basket to HTML with two links to same filename
 *            (but not necessary same path, as in "/home/foo.txt" and "/foo.txt") )
 *       The first copy isn't yet started, so the dest file isn't created and this method
 *       returns the same filename !!!!!!!!!!!!!!!!!!!!
 */
QString HTMLExporter::copyFile(const QString &srcPath, bool createIt)
{
    QString fileName = Tools::fileNameForNewFile(QUrl::fromLocalFile(srcPath).fileName(), dataFolderPath);
    QString fullPath = dataFolderPath + fileName;

    if (!currentBasket->isEncrypted()) {
        if (createIt) {
            // We create the file to be sure another very near call to copyFile() won't choose the same name:
            QFile file(QUrl::fromLocalFile(fullPath).path());
            if (file.open(QIODevice::WriteOnly))
                file.close();
            // And then we copy the file AND overwriting the file we just created:
            KIO::file_copy(QUrl::fromLocalFile(srcPath), QUrl::fromLocalFile(fullPath), 0666, KIO::HideProgressInfo | KIO::Resume | KIO::Overwrite);
        } else {
            /*KIO::CopyJob *copyJob = */ KIO::copy(QUrl::fromLocalFile(srcPath), QUrl::fromLocalFile(fullPath), KIO::DefaultFlags); // Do it as before
        }
    } else {
        QByteArray array;
        bool success = FileStorage::loadFromFile(srcPath, &array);

        if (success) {
            saveToFile(fullPath, array);
        } else {
            qCDebug(BASKET_LOG) << "Unable to load encrypted file " << srcPath;
        }
    }

    return fileName;
}

void HTMLExporter::saveToFile(const QString &fullPath, const QByteArray &array)
{
    QFile file(QUrl::fromLocalFile(fullPath).path());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(array.toStdString().c_str(), array.size());
        file.close();
    } else {
        qCDebug(BASKET_LOG) << "Unable to open file for writing: " << fullPath;
    }
}
