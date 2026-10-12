/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mathscientificformat.h"

#include <QFontMetrics>
#include <QTextBlockFormat>
#include <QTextFrameFormat>
#include <QTextLength>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableCellFormat>
#include <QTextTableFormat>

void MathScientificFormat::insertVerticalFraction(
    QTextCursor &cursor, const QString &numerator,
    const QString &denominator, const QBrush &separatorColor)
{
    if (numerator.isEmpty() || denominator.isEmpty())
        return;

    const QTextCharFormat activeFormat = cursor.charFormat();
    const QFontMetrics metrics(activeFormat.font());
    const int width = qMax(30, qMax(metrics.horizontalAdvance(numerator),
                                   metrics.horizontalAdvance(denominator)) + 14);

    // QTextTable is rich text, not an image. Its contents survive HTML
    // serialization and remain editable after the Mathom is reopened.
    QTextTableFormat tableFormat;
    tableFormat.setBorder(0);
    tableFormat.setCellPadding(2);
    tableFormat.setCellSpacing(0);
    tableFormat.setAlignment(Qt::AlignLeft);
    tableFormat.setWidth(QTextLength(QTextLength::FixedLength, width));

    cursor.removeSelectedText();
    QTextTable *table = cursor.insertTable(2, 1, tableFormat);
    if (!table)
        return;

    QTextTableCell numeratorCell = table->cellAt(0, 0);
    QTextTableCell denominatorCell = table->cellAt(1, 0);

    QTextTableCellFormat numeratorFormat =
        numeratorCell.format().toTableCellFormat();
    numeratorFormat.setBottomBorder(1.5);
    numeratorFormat.setBottomBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    numeratorFormat.setBottomBorderBrush(separatorColor);
    numeratorCell.setFormat(numeratorFormat);

    QTextCursor numeratorCursor = numeratorCell.firstCursorPosition();
    numeratorCursor.insertText(numerator, activeFormat);
    QTextBlockFormat centered = numeratorCursor.blockFormat();
    centered.setAlignment(Qt::AlignHCenter);
    numeratorCursor.setBlockFormat(centered);

    QTextCursor denominatorCursor = denominatorCell.firstCursorPosition();
    denominatorCursor.insertText(denominator, activeFormat);
    centered = denominatorCursor.blockFormat();
    centered.setAlignment(Qt::AlignHCenter);
    denominatorCursor.setBlockFormat(centered);

    // Move the editing position to the block following the table when Qt
    // provides one, rather than leaving the cursor inside the denominator.
    QTextCursor after = table->lastCursorPosition();
    after.movePosition(QTextCursor::NextBlock);
    cursor = after;
}
