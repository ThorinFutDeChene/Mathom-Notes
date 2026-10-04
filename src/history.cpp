/**
 * SPDX-FileCopyrightText: (C) 2010 Brian C. Milco <bcmilco@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "history.h"
#include "global.h"

#include "basketscene.h"
#include "bnpview.h"

#include <KLocalizedString>

HistorySetBasket::HistorySetBasket(BasketScene *basket, QUndoCommand *parent)
    : QUndoCommand(parent)
{
    setText(i18n("Set current location to %1", basket->basketName()));
    m_folderNameOld = Global::bnpView->currentBasket()->folderName();
    m_folderNameNew = basket->folderName();
}

void HistorySetBasket::undo()
{
    BasketScene *oldBasket = Global::bnpView->basketForFolderName(m_folderNameOld);
    Global::bnpView->setCurrentBasket(oldBasket);
}

void HistorySetBasket::redo()
{
    BasketScene *curBasket = Global::bnpView->basketForFolderName(m_folderNameNew);
    Global::bnpView->setCurrentBasket(curBasket);
}

/** Global Page modification history */

PageRenameCommand::PageRenameCommand(
    BasketScene *basket,
    const QString &pageId,
    const QString &oldTitle,
    const QString &newTitle,
    QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_basket(basket)
    , m_pageId(pageId)
    , m_oldTitle(oldTitle)
    , m_newTitle(newTitle)
{
    setText(
        i18n(
            "Rename Page \"%1\" to \"%2\"",
            oldTitle,
            newTitle));
}

void PageRenameCommand::undo()
{
    if (!m_basket)
        return;

    m_basket->renamePage(
        m_pageId,
        m_oldTitle);
}

void PageRenameCommand::redo()
{
    if (!m_basket)
        return;

    m_basket->renamePage(
        m_pageId,
        m_newTitle);
}


PageReorderCommand::PageReorderCommand(
    BasketScene *basket,
    const QStringList &oldOrder,
    const QStringList &newOrder,
    QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_basket(basket)
    , m_oldOrder(oldOrder)
    , m_newOrder(newOrder)
{
    setText(i18n("Reorder Pages"));
}

void PageReorderCommand::undo()
{
    if (!m_basket)
        return;

    m_basket->reorderPages(
        m_oldOrder);
}

void PageReorderCommand::redo()
{
    if (!m_basket)
        return;

    m_basket->reorderPages(
        m_newOrder);
}

