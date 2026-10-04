/**
 * SPDX-FileCopyrightText: (C) 2010 Brian C. Milco <bcmilco@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "history.h"
#include "global.h"
#include "gitwrapper.h"

#include "basketscene.h"
#include "bnpview.h"
#include "note.h"

#include <KLocalizedString>

#include <QDateTime>

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

/** Global Mathom text modification history */

MathomTextEditCommand::MathomTextEditCommand(
    Note *note,
    const QString &oldState,
    const QString &newState,
    bool richText,
    QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_note(note)
    , m_oldState(oldState)
    , m_newState(newState)
    , m_richText(richText)
    , m_timestampMs(
          QDateTime::currentMSecsSinceEpoch())
{
    setText(i18n("Edit Mathom"));
}

void MathomTextEditCommand::undo()
{
    if (!m_note || !m_note->basket())
        return;

    m_note->basket()->applyTextEditSnapshot(
        m_note,
        m_oldState,
        m_richText);
}

void MathomTextEditCommand::redo()
{
    /*
     * QUndoStack::push() automatically calls redo().
     * The user's modification is already present when the command
     * is pushed, so the first redo must do nothing.
     */
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }

    if (!m_note || !m_note->basket())
        return;

    m_note->basket()->applyTextEditSnapshot(
        m_note,
        m_newState,
        m_richText);
}

int MathomTextEditCommand::id() const
{
    return 0x4d54;
}

bool MathomTextEditCommand::mergeWith(
    const QUndoCommand *command)
{
    const auto *other =
        dynamic_cast<const MathomTextEditCommand *>(
            command);

    if (!other)
        return false;

    if (m_note != other->m_note
        || m_richText != other->m_richText) {
        return false;
    }

    const qint64 elapsed =
        other->m_timestampMs - m_timestampMs;

    /*
     * Consecutive keystrokes are one natural editing operation.
     * A pause creates a new Undo step.
     */
    if (elapsed < 0 || elapsed > 1200)
        return false;

    m_newState = other->m_newState;
    m_timestampMs = other->m_timestampMs;

    return true;
}

/** Global Mathom-House / Shelf properties history */

BasketPropertiesCommand::BasketPropertiesCommand(
    BasketScene *basket,
    const BasketPropertiesState &oldState,
    const BasketPropertiesState &newState,
    QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_basket(basket)
    , m_oldState(oldState)
    , m_newState(newState)
{
    setText(
        i18n(
            "Modify properties of \"%1\"",
            oldState.name));
}

void BasketPropertiesCommand::apply(
    const BasketPropertiesState &state)
{
    if (!m_basket)
        return;

    m_basket->setShortcut(
        state.shortcut,
        state.shortcutAction);

    m_basket->setTabColor(
        state.tabColor,
        state.tabColorAutomatic);

    /*
     * Keep this last: setShelfIdentity() emits propertiesChanged(),
     * which refreshes the organization tree and persists baskets.xml.
     */
    m_basket->setShelfIdentity(
        state.icon,
        state.name);

    m_basket->save();
    GitWrapper::commitBasket(m_basket);
}

void BasketPropertiesCommand::undo()
{
    apply(m_oldState);
}

void BasketPropertiesCommand::redo()
{
    apply(m_newState);
}

