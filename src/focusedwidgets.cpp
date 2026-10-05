/**
 * SPDX-FileCopyrightText: (C) 2003 by Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "focusedwidgets.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QWheelEvent>

#include "basketscene.h"
#include "bnpview.h"
#include "global.h"
#include "settings.h"

#ifdef KeyPress
#undef KeyPress
#endif

/** class FocusedTextEdit */

FocusedTextEdit::FocusedTextEdit(bool disableUpdatesOnKeyPress, QWidget *parent)
    : KTextEdit(parent)
    , m_disableUpdatesOnKeyPress(disableUpdatesOnKeyPress)
{
    connect(this, &FocusedTextEdit::selectionChanged, this, &FocusedTextEdit::onSelectionChanged);
}

FocusedTextEdit::~FocusedTextEdit() = default;

void FocusedTextEdit::paste(QClipboard::Mode mode)
{
    const QMimeData *md = QApplication::clipboard()->mimeData(mode);
    if (md != nullptr) {
        insertFromMimeData(md);
    }
}

void FocusedTextEdit::clearMultiSelection()
{
    if (m_multiSelectionCursors.isEmpty())
        return;

    m_multiSelectionCursors.clear();
    setExtraSelections(QList<QTextEdit::ExtraSelection>());
}

void FocusedTextEdit::addMultiSelection(const QTextCursor &cursor)
{
    if (!cursor.hasSelection())
        return;

    for (const QTextCursor &existing : m_multiSelectionCursors) {
        if (existing.selectionStart() == cursor.selectionStart()
            && existing.selectionEnd() == cursor.selectionEnd()) {
            return;
        }
    }

    m_multiSelectionCursors.append(cursor);
}

void FocusedTextEdit::refreshMultiSelectionHighlights()
{
    QList<QTextEdit::ExtraSelection> selections;

    for (const QTextCursor &cursor : m_multiSelectionCursors) {
        QTextEdit::ExtraSelection selection;
        selection.cursor = cursor;
        selection.format.setBackground(
            palette().brush(QPalette::Highlight));
        selection.format.setForeground(
            palette().brush(QPalette::HighlightedText));
        selections.append(selection);
    }

    setExtraSelections(selections);
}

void FocusedTextEdit::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton
        && (event->modifiers() & Qt::ControlModifier)) {

        // Preserve the selection that existed before Ctrl+double-click.
        const QTextCursor current = textCursor();
        if (current.hasSelection())
            addMultiSelection(current);

        QTextCursor word =
            cursorForPosition(event->position().toPoint());

        word.select(QTextCursor::WordUnderCursor);

        if (word.hasSelection()) {
            addMultiSelection(word);

            // Keep the most recently added word as the active QTextCursor.
            // The other selections remain visible through ExtraSelection.
            setTextCursor(word);
            refreshMultiSelectionHighlights();

            event->accept();
            return;
        }
    }

    clearMultiSelection();
    KTextEdit::mouseDoubleClickEvent(event);
}

void FocusedTextEdit::mergeFormatIntoSelection(
    const QTextCharFormat &format)
{
    if (m_multiSelectionCursors.isEmpty()) {
        mergeCurrentCharFormat(format);
        return;
    }

    const QTextCursor original = textCursor();

    for (QTextCursor cursor : m_multiSelectionCursors)
        cursor.mergeCharFormat(format);

    setTextCursor(original);
    refreshMultiSelectionHighlights();
}

void FocusedTextEdit::applyFontFamily(const QString &family)
{
    QTextCharFormat format;
    format.setFontFamilies(QStringList{family});
    mergeFormatIntoSelection(format);
}

void FocusedTextEdit::applyFontPointSize(qreal size)
{
    if (size <= 0)
        return;

    QTextCharFormat format;
    format.setFontPointSize(size);
    mergeFormatIntoSelection(format);
}

void FocusedTextEdit::applyTextColor(const QColor &color)
{
    QTextCharFormat format;
    format.setForeground(color);
    mergeFormatIntoSelection(format);
}

void FocusedTextEdit::applyFontWeight(int weight)
{
    QTextCharFormat format;
    format.setFontWeight(weight);
    mergeFormatIntoSelection(format);
}

void FocusedTextEdit::applyFontItalic(bool italic)
{
    QTextCharFormat format;
    format.setFontItalic(italic);
    mergeFormatIntoSelection(format);
}

void FocusedTextEdit::applyFontUnderline(bool underline)
{
    QTextCharFormat format;
    format.setFontUnderline(underline);
    mergeFormatIntoSelection(format);
}

void FocusedTextEdit::applyVerticalAlignment(QTextCharFormat::VerticalAlignment alignment)
{
    QTextCharFormat format;
    format.setVerticalAlignment(alignment);
    mergeFormatIntoSelection(format);
}

void FocusedTextEdit::keyPressEvent(QKeyEvent *event)
{
    // A normal typing/navigation action leaves multi-selection mode.
    // Ctrl shortcuts are kept so formatting shortcuts can still operate
    // on the selected words.
    const bool shortcutModifier =
        event->modifiers()
        & (Qt::ControlModifier
           | Qt::AltModifier
           | Qt::MetaModifier);

    const bool modifierKey =
        event->key() == Qt::Key_Control
        || event->key() == Qt::Key_Shift
        || event->key() == Qt::Key_Alt
        || event->key() == Qt::Key_Meta;

    if (hasMultiSelection()
        && !shortcutModifier
        && !modifierKey) {
        clearMultiSelection();
    }

    if (event->key() == Qt::Key_Escape) {
        Q_EMIT escapePressed();
        return;
    }

    if (m_disableUpdatesOnKeyPress) {
        setUpdatesEnabled(false);
    }

    KTextEdit::keyPressEvent(event);

    // Workaround (for ensuring the cursor to be visible): signal not emitted when pressing those keys:
    if (event->key() == Qt::Key_Home || event->key() == Qt::Key_End || event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) {
        Q_EMIT cursorPositionChanged();
    }

    if (m_disableUpdatesOnKeyPress) {
        setUpdatesEnabled(true);
        if (!document()->isEmpty()) {
            ensureCursorVisible();
        }
    }
}

void FocusedTextEdit::wheelEvent(QWheelEvent *event)
{
    // If we're already scrolled all the way to the top or bottom, we pass the
    // wheel event onto the basket.
    QScrollBar *sb = verticalScrollBar();
    if ((event->angleDelta().y() > 0 && sb->value() > sb->minimum()) || (event->angleDelta().y() < 0 && sb->value() < sb->maximum())) {
        KTextEdit::wheelEvent(event);
    }
    // else
    //    Global::bnpView->currentBasket()->graphicsView()->wheelEvent(event);
}

void FocusedTextEdit::enterEvent(QEnterEvent *event)
{
    Q_EMIT mouseEntered();
    KTextEdit::enterEvent(event);
}

void FocusedTextEdit::insertFromMimeData(const QMimeData *source)
{
    // When user always wants plaintext pasting, if both HTML and text data is
    // present, only send plain text data (the provided source is readonly and I
    // also can't just pass it to QMimeData constructor as the latter is 'private')
    if (Settings::pasteAsPlainText() && source->hasHtml() && source->hasText()) {
        QMimeData alteredSource;
        alteredSource.setData(QStringLiteral("text/plain"), source->data(QStringLiteral("text/plain")));
        KTextEdit::insertFromMimeData(&alteredSource);
    } else {
        KTextEdit::insertFromMimeData(source);
    }
}

void FocusedTextEdit::onSelectionChanged()
{
    if (textCursor().selectedText().length() > 0) {
        QMimeData *md = createMimeDataFromSelection();
        QApplication::clipboard()->setMimeData(md, QClipboard::Selection);
    }
}

/** class FocusWidgetFilter */
FocusWidgetFilter::FocusWidgetFilter(QWidget *parent)
    : QObject(parent)
{
    if (parent != nullptr) {
        parent->installEventFilter(this);
    }
}

bool FocusWidgetFilter::eventFilter(QObject *, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress: {
        auto *ke = dynamic_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Return:
            Q_EMIT returnPressed();
            return true;
        case Qt::Key_Escape:
            Q_EMIT escapePressed();
            return true;
        default:
            return false;
        };
    }
    case QEvent::Enter:
        Q_EMIT mouseEntered();
        Q_FALLTHROUGH();
    default:
        return false;
    };
}

#include "moc_focusedwidgets.cpp"
