/**
 * SPDX-FileCopyrightText: (C) 2010 Brian C. Milco <bcmilco@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HISTORY_H
#define HISTORY_H

#include <QPointer>
#include <QStringList>
#include <QUndoCommand>

class BasketScene;
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
        QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    int id() const override;
    bool mergeWith(
        const QUndoCommand *other) override;

private:
    QPointer<Note> m_note;
    QString m_oldState;
    QString m_newState;
    bool m_richText;
    bool m_firstRedo = true;
    qint64 m_timestampMs = 0;
};

#endif // HISTORY_H
