/**
 * SPDX-FileCopyrightText: (C) 2010 Brian C. Milco <bcmilco@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HISTORY_H
#define HISTORY_H

#include <QColor>
#include <QKeySequence>
#include <QPointer>
#include <QStringList>
#include <QUndoCommand>

class BasketScene;
class BasketListViewItem;
class BNPView;
class Note;

class HistorySetBasket : public QUndoCommand
{
public:
    explicit HistorySetBasket(BasketScene *basket, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    QString m_folderNameOld;
    QString m_folderNameNew;
};


/**
 * Global modification history.
 *
 * Unlike HistorySetBasket, these commands represent modifications to the
 * user's data and therefore belong to the application-wide Undo/Redo stack.
 */
class PageRenameCommand : public QUndoCommand
{
public:
    PageRenameCommand(
        BasketScene *basket,
        const QString &pageId,
        const QString &oldTitle,
        const QString &newTitle,
        QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    QPointer<BasketScene> m_basket;
    QString m_pageId;
    QString m_oldTitle;
    QString m_newTitle;
};


class PageReorderCommand : public QUndoCommand
{
public:
    PageReorderCommand(
        BasketScene *basket,
        const QStringList &oldOrder,
        const QStringList &newOrder,
        QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    QPointer<BasketScene> m_basket;
    QStringList m_oldOrder;
    QStringList m_newOrder;
};


class MathomTextEditCommand : public QUndoCommand
{
public:
    MathomTextEditCommand(
        Note *note,
        const QString &oldState,
        const QString &newState,
        bool richText,
        bool newMathom,
        bool oldStateWasEmpty,
        QUndoCommand *parent = nullptr);

    ~MathomTextEditCommand() override;

    void undo() override;
    void redo() override;

    int id() const override;
    bool mergeWith(
        const QUndoCommand *other) override;

private:
    QPointer<BasketScene> m_basket;
    QPointer<Note> m_note;

    // Exact location of a newly created Mathom.
    QPointer<Note> m_parentNote;
    QPointer<Note> m_previousNote;
    QPointer<Note> m_nextNote;

    QString m_oldState;
    QString m_newState;

    bool m_richText;
    bool m_newMathom = false;
    bool m_oldStateWasEmpty = false;
    bool m_mathomSuspended = false;

    bool m_firstRedo = true;
    qint64 m_timestampMs = 0;
};


struct BasketPropertiesState
{
    QString icon;
    QString name;
    QColor tabColor;
    bool tabColorAutomatic = true;
    QKeySequence shortcut;
    int shortcutAction = 0;
};


struct DeletedMathomPosition
{
    QPointer<Note> note;
    QPointer<Note> parent;
    QPointer<Note> previous;
    QPointer<Note> next;
};


class MathomDeleteCommand : public QUndoCommand
{
public:
    MathomDeleteCommand(
        BasketScene *basket,
        const QList<Note *> &notes,
        QUndoCommand *parent = nullptr);

    ~MathomDeleteCommand() override;

    void undo() override;
    void redo() override;

private:
    QPointer<BasketScene> m_basket;
    QList<DeletedMathomPosition> m_positions;
    bool m_deleted = false;
};


class BasketCreateCommand : public QUndoCommand
{
public:
    BasketCreateCommand(
        BNPView *view,
        BasketScene *basket,
        QUndoCommand *parent = nullptr);

    ~BasketCreateCommand() override;

    void undo() override;
    void redo() override;

private:
    BNPView *m_view = nullptr;
    QPointer<BasketScene> m_basket;
    QPointer<BasketScene> m_parentBasket;

    BasketListViewItem *m_detachedItem = nullptr;

    int m_index = -1;
    bool m_firstRedo = true;
    bool m_detached = false;
};


class BasketDeleteCommand : public QUndoCommand
{
public:
    BasketDeleteCommand(
        BNPView *view,
        BasketScene *basket,
        QUndoCommand *parent = nullptr);

    ~BasketDeleteCommand() override;

    void undo() override;
    void redo() override;

private:
    BNPView *m_view = nullptr;

    QPointer<BasketScene> m_basket;
    QPointer<BasketScene> m_parentBasket;

    BasketListViewItem *m_detachedItem = nullptr;

    QString m_folderName;
    int m_index = -1;
    bool m_deleted = false;
};


class BasketPropertiesCommand : public QUndoCommand
{
public:
    BasketPropertiesCommand(
        BasketScene *basket,
        const BasketPropertiesState &oldState,
        const BasketPropertiesState &newState,
        QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    void apply(
        const BasketPropertiesState &state);

    QPointer<BasketScene> m_basket;
    BasketPropertiesState m_oldState;
    BasketPropertiesState m_newState;
};

#endif // HISTORY_H
