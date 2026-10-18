/**
 * SPDX-FileCopyrightText: (C) 2026 Thorinux
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "spreadsheetcontent.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPainter>
#include <QPalette>
#include <QSet>

#include <cmath>
#include <limits>

#include <KLocalizedString>

#include "basketscene.h"
#include "common.h"
#include "filter.h"
#include "htmlexporter.h"
#include "note.h"

namespace
{
constexpr qreal RowHeaderWidth = 38.0;
constexpr qreal ColumnHeaderHeight = 24.0;
constexpr qreal RowHeight = 26.0;
constexpr qreal MinimumSpreadsheetWidth = 340.0;

struct FormulaResult {
    bool ok = false;
    double value = 0.0;
    QString error;
};

QString formatFormulaNumber(double value)
{
    if (!std::isfinite(value))
        return QStringLiteral("#NUM!");

    if (std::fabs(value) < 1e-12)
        value = 0.0;

    if (std::fabs(value - std::round(value)) < 1e-12)
        return QLocale().toString(static_cast<qlonglong>(std::llround(value)));

    return QLocale().toString(value, 'g', 12);
}

bool parsePlainNumber(QString text, double *value)
{
    text = text.trimmed();
    if (text.isEmpty())
        return false;

    bool ok = false;
    double number = QLocale().toDouble(text, &ok);
    if (!ok) {
        QString normalized = text;
        normalized.replace(QLatin1Char(','), QLatin1Char('.'));
        number = QLocale::c().toDouble(normalized, &ok);
    }

    if (ok && value)
        *value = number;
    return ok;
}

quint64 cellKey(int row, int column)
{
    return (quint64(quint32(row)) << 32) | quint32(column);
}

bool parseCellReferenceToken(const QString &token, int *row, int *column)
{
    if (token.isEmpty())
        return false;

    int index = 0;
    int col = 0;
    bool hasColumn = false;

    while (index < token.size() && token.at(index).isLetter()) {
        const QChar upper = token.at(index).toUpper();
        if (upper < QLatin1Char('A') || upper > QLatin1Char('Z'))
            return false;
        col = col * 26 + (upper.unicode() - QLatin1Char('A').unicode() + 1);
        hasColumn = true;
        ++index;
    }

    if (!hasColumn || index >= token.size())
        return false;

    const int rowStart = index;
    while (index < token.size() && token.at(index).isDigit())
        ++index;

    if (rowStart == index || index != token.size())
        return false;

    bool ok = false;
    const int parsedRow = token.mid(rowStart).toInt(&ok);
    if (!ok || parsedRow <= 0)
        return false;

    if (row)
        *row = parsedRow - 1;
    if (column)
        *column = col - 1;
    return true;
}

class SpreadsheetFormulaParser;

FormulaResult evaluateNumericCell(const SpreadsheetContent *content, int row, int column, QSet<quint64> *stack, bool textAsZero);

class SpreadsheetFormulaParser
{
public:
    SpreadsheetFormulaParser(const SpreadsheetContent *content, const QString &expression, QSet<quint64> *stack)
        : m_content(content)
        , m_expression(expression)
        , m_stack(stack)
    {
    }

    FormulaResult parse()
    {
        FormulaResult result = parseExpression();
        skipSpaces();

        if (result.ok && m_pos != m_expression.size())
            return error(QStringLiteral("#ERROR!"));

        return result;
    }

private:
    FormulaResult parseExpression()
    {
        FormulaResult left = parseTerm();
        if (!left.ok)
            return left;

        while (true) {
            skipSpaces();
            if (!consume(QLatin1Char('+')) && !consume(QLatin1Char('-')))
                break;

            const QChar operation = m_expression.at(m_pos - 1);
            FormulaResult right = parseTerm();
            if (!right.ok)
                return right;

            if (operation == QLatin1Char('+'))
                left.value += right.value;
            else
                left.value -= right.value;
        }

        return left;
    }

    FormulaResult parseTerm()
    {
        FormulaResult left = parseFactor();
        if (!left.ok)
            return left;

        while (true) {
            skipSpaces();
            if (!consume(QLatin1Char('*')) && !consume(QLatin1Char('/')))
                break;

            const QChar operation = m_expression.at(m_pos - 1);
            FormulaResult right = parseFactor();
            if (!right.ok)
                return right;

            if (operation == QLatin1Char('*')) {
                left.value *= right.value;
            } else {
                if (std::fabs(right.value) < 1e-15)
                    return error(QStringLiteral("#DIV/0!"));
                left.value /= right.value;
            }
        }

        return left;
    }

    FormulaResult parseFactor()
    {
        skipSpaces();

        if (consume(QLatin1Char('+')))
            return parseFactor();

        if (consume(QLatin1Char('-'))) {
            FormulaResult value = parseFactor();
            if (value.ok)
                value.value = -value.value;
            return value;
        }

        if (consume(QLatin1Char('('))) {
            FormulaResult value = parseExpression();
            if (!value.ok)
                return value;
            skipSpaces();
            if (!consume(QLatin1Char(')')))
                return error(QStringLiteral("#ERROR!"));
            return value;
        }

        if (m_pos >= m_expression.size())
            return error(QStringLiteral("#ERROR!"));

        if (m_expression.at(m_pos).isDigit() || m_expression.at(m_pos) == QLatin1Char('.') || m_expression.at(m_pos) == QLatin1Char(','))
            return parseNumber();

        if (m_expression.at(m_pos).isLetter())
            return parseIdentifierOrCell();

        return error(QStringLiteral("#ERROR!"));
    }

    FormulaResult parseNumber()
    {
        const int start = m_pos;
        bool decimalSeparatorSeen = false;

        while (m_pos < m_expression.size()) {
            const QChar ch = m_expression.at(m_pos);
            if (ch.isDigit()) {
                ++m_pos;
                continue;
            }

            if ((ch == QLatin1Char('.') || ch == QLatin1Char(',')) && !decimalSeparatorSeen) {
                decimalSeparatorSeen = true;
                ++m_pos;
                continue;
            }

            break;
        }

        QString numberText = m_expression.mid(start, m_pos - start);
        numberText.replace(QLatin1Char(','), QLatin1Char('.'));

        bool ok = false;
        const double value = QLocale::c().toDouble(numberText, &ok);
        if (!ok)
            return error(QStringLiteral("#VALUE!"));

        return success(value);
    }

    FormulaResult parseIdentifierOrCell()
    {
        const int start = m_pos;
        while (m_pos < m_expression.size() && m_expression.at(m_pos).isLetter())
            ++m_pos;

        const QString name = m_expression.mid(start, m_pos - start);
        const int digitsStart = m_pos;
        while (m_pos < m_expression.size() && m_expression.at(m_pos).isDigit())
            ++m_pos;

        if (m_pos > digitsStart) {
            const QString reference = m_expression.mid(start, m_pos - start);
            int row = -1;
            int column = -1;
            if (!parseCellReferenceToken(reference, &row, &column))
                return error(QStringLiteral("#REF!"));
            return evaluateNumericCell(m_content, row, column, m_stack, false);
        }

        skipSpaces();
        if (!consume(QLatin1Char('(')))
            return error(QStringLiteral("#NAME?"));

        return parseFunction(name);
    }

    FormulaResult parseFunction(const QString &name)
    {
        const QString function = name.toUpper();
        if (function != QStringLiteral("SUM") && function != QStringLiteral("AVERAGE") && function != QStringLiteral("MIN")
            && function != QStringLiteral("MAX") && function != QStringLiteral("COUNT")) {
            return error(QStringLiteral("#NAME?"));
        }

        QVector<double> values;
        int numericCount = 0;

        skipSpaces();
        if (consume(QLatin1Char(')'))) {
            if (function == QStringLiteral("COUNT"))
                return success(0.0);
            return error(QStringLiteral("#VALUE!"));
        }

        while (true) {
            skipSpaces();
            const int savedPosition = m_pos;
            int firstRow = -1;
            int firstColumn = -1;

            if (parseCellReference(&firstRow, &firstColumn)) {
                skipSpaces();
                if (consume(QLatin1Char(':'))) {
                    skipSpaces();
                    int lastRow = -1;
                    int lastColumn = -1;
                    if (!parseCellReference(&lastRow, &lastColumn))
                        return error(QStringLiteral("#REF!"));

                    if (!validCell(firstRow, firstColumn) || !validCell(lastRow, lastColumn))
                        return error(QStringLiteral("#REF!"));

                    const int rowMin = qMin(firstRow, lastRow);
                    const int rowMax = qMax(firstRow, lastRow);
                    const int columnMin = qMin(firstColumn, lastColumn);
                    const int columnMax = qMax(firstColumn, lastColumn);

                    for (int row = rowMin; row <= rowMax; ++row) {
                        for (int column = columnMin; column <= columnMax; ++column) {
                            FormulaResult value = evaluateNumericCell(m_content, row, column, m_stack, true);
                            if (!value.ok) {
                                if (!value.error.isEmpty())
                                    return value;
                                continue;
                            }
                            values.append(value.value);
                            ++numericCount;
                        }
                    }
                } else {
                    if (!validCell(firstRow, firstColumn))
                        return error(QStringLiteral("#REF!"));
                    FormulaResult value = evaluateNumericCell(m_content, firstRow, firstColumn, m_stack, true);
                    if (!value.ok) {
                        if (!value.error.isEmpty())
                            return value;
                    } else {
                        values.append(value.value);
                        ++numericCount;
                    }
                }
            } else {
                m_pos = savedPosition;
                FormulaResult value = parseExpression();
                if (!value.ok)
                    return value;
                values.append(value.value);
                ++numericCount;
            }

            skipSpaces();
            if (consume(QLatin1Char(')')))
                break;

            if (!consume(QLatin1Char(';')))
                return error(QStringLiteral("#ERROR!"));
        }

        if (function == QStringLiteral("COUNT"))
            return success(numericCount);

        if (values.isEmpty())
            return error(QStringLiteral("#VALUE!"));

        if (function == QStringLiteral("SUM")) {
            double total = 0.0;
            for (double value : values)
                total += value;
            return success(total);
        }

        if (function == QStringLiteral("AVERAGE")) {
            double total = 0.0;
            for (double value : values)
                total += value;
            return numericCount > 0 ? success(total / numericCount) : error(QStringLiteral("#DIV/0!"));
        }

        double result = values.first();
        for (int index = 1; index < values.size(); ++index) {
            if (function == QStringLiteral("MIN"))
                result = qMin(result, values.at(index));
            else
                result = qMax(result, values.at(index));
        }
        return success(result);
    }

    bool parseCellReference(int *row, int *column)
    {
        const int start = m_pos;
        while (m_pos < m_expression.size() && m_expression.at(m_pos).isLetter())
            ++m_pos;

        const int digitsStart = m_pos;
        while (m_pos < m_expression.size() && m_expression.at(m_pos).isDigit())
            ++m_pos;

        if (start == digitsStart || digitsStart == m_pos) {
            m_pos = start;
            return false;
        }

        const QString token = m_expression.mid(start, m_pos - start);
        if (!parseCellReferenceToken(token, row, column)) {
            m_pos = start;
            return false;
        }
        return true;
    }

    bool validCell(int row, int column) const
    {
        return row >= 0 && row < m_content->rowCount() && column >= 0 && column < m_content->columnCount();
    }

    void skipSpaces()
    {
        while (m_pos < m_expression.size() && m_expression.at(m_pos).isSpace())
            ++m_pos;
    }

    bool consume(QChar character)
    {
        if (m_pos < m_expression.size() && m_expression.at(m_pos) == character) {
            ++m_pos;
            return true;
        }
        return false;
    }

    FormulaResult success(double value) const
    {
        FormulaResult result;
        result.ok = true;
        result.value = value;
        return result;
    }

    FormulaResult error(const QString &message) const
    {
        FormulaResult result;
        result.error = message;
        return result;
    }

    const SpreadsheetContent *m_content;
    QString m_expression;
    QSet<quint64> *m_stack;
    int m_pos = 0;
};

FormulaResult evaluateNumericCell(const SpreadsheetContent *content, int row, int column, QSet<quint64> *stack, bool textAsZero)
{
    FormulaResult result;

    if (row < 0 || row >= content->rowCount() || column < 0 || column >= content->columnCount()) {
        result.error = QStringLiteral("#REF!");
        return result;
    }

    const QString raw = content->cell(row, column).trimmed();
    if (raw.isEmpty()) {
        result.ok = true;
        result.value = 0.0;
        return result;
    }

    if (!raw.startsWith(QLatin1Char('='))) {
        double value = 0.0;
        if (parsePlainNumber(raw, &value)) {
            result.ok = true;
            result.value = value;
            return result;
        }

        if (textAsZero)
            return result;

        result.error = QStringLiteral("#VALUE!");
        return result;
    }

    const quint64 key = cellKey(row, column);
    if (stack->contains(key)) {
        result.error = QStringLiteral("#CYCLE!");
        return result;
    }

    stack->insert(key);
    SpreadsheetFormulaParser parser(content, raw.mid(1), stack);
    result = parser.parse();
    stack->remove(key);
    return result;
}
}

SpreadsheetItem::SpreadsheetItem(Note *parent, SpreadsheetContent *content)
    : QGraphicsItem(parent)
    , m_note(parent)
    , m_content(content)
{
}

QRectF SpreadsheetItem::boundingRect() const
{
    return QRectF(0, 0, m_width, m_height);
}

void SpreadsheetItem::setSize(qreal width, qreal height)
{
    if (qFuzzyCompare(m_width, width) && qFuzzyCompare(m_height, height))
        return;

    prepareGeometryChange();
    m_width = width;
    m_height = height;
    update();
}

void SpreadsheetItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (!m_note || !m_content)
        return;

    const int rows = qMax(1, m_content->rowCount());
    const int columns = qMax(1, m_content->columnCount());
    const qreal dataWidth = qMax<qreal>(1.0, m_width - RowHeaderWidth);
    const qreal cellWidth = dataWidth / columns;

    QColor background = m_note->backgroundColor();
    QColor foreground = m_note->textColor();
    QColor grid = foreground;
    grid.setAlpha(90);
    QColor header = background.darker(background.lightness() > 128 ? 106 : 125);

    painter->save();
    painter->setFont(m_note->font());
    painter->fillRect(boundingRect(), background);
    painter->fillRect(QRectF(0, 0, m_width, ColumnHeaderHeight), header);
    painter->fillRect(QRectF(0, 0, RowHeaderWidth, m_height), header);

    painter->setPen(grid);
    painter->drawRect(boundingRect().adjusted(0, 0, -1, -1));
    painter->drawLine(QPointF(RowHeaderWidth, 0), QPointF(RowHeaderWidth, m_height));
    painter->drawLine(QPointF(0, ColumnHeaderHeight), QPointF(m_width, ColumnHeaderHeight));

    const QFontMetrics metrics(m_note->font());

    for (int column = 0; column < columns; ++column) {
        const qreal x = RowHeaderWidth + column * cellWidth;
        painter->drawLine(QPointF(x, 0), QPointF(x, m_height));
        const QRectF headerRect(x + 3, 0, cellWidth - 6, ColumnHeaderHeight);
        painter->setPen(foreground);
        painter->drawText(headerRect, Qt::AlignCenter, SpreadsheetContent::columnName(column));
        painter->setPen(grid);
    }

    for (int row = 0; row < rows; ++row) {
        const qreal y = ColumnHeaderHeight + row * RowHeight;
        painter->drawLine(QPointF(0, y), QPointF(m_width, y));

        painter->setPen(foreground);
        painter->drawText(QRectF(2, y, RowHeaderWidth - 4, RowHeight), Qt::AlignCenter, QString::number(row + 1));

        for (int column = 0; column < columns; ++column) {
            const qreal x = RowHeaderWidth + column * cellWidth;
            const QRectF textRect(x + 4, y + 1, cellWidth - 8, RowHeight - 2);
            const QString value = metrics.elidedText(m_content->displayValue(row, column), Qt::ElideRight, qMax(0, int(textRect.width())));
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, value);
        }

        painter->setPen(grid);
    }

    painter->restore();
}

SpreadsheetContent::SpreadsheetContent(Note *parent, const QString &fileName, bool lazyLoad)
    : NoteContent(parent, NoteType::Spreadsheet, fileName)
    , m_item(parent, this)
{
    resetToDefault();

    if (parent) {
        parent->addToGroup(&m_item);
        m_item.setPos(parent->contentX(), Note::NOTE_MARGIN);
    }

    basket()->addWatchedFile(fullPath());
    loadFromFile(lazyLoad);
    refreshView();
}

SpreadsheetContent::~SpreadsheetContent()
{
    if (note())
        note()->removeFromGroup(&m_item);
}

void SpreadsheetContent::resetToDefault()
{
    m_rows = DefaultRows;
    m_columns = DefaultColumns;
    m_cells = QVector<QVector<QString>>(m_rows, QVector<QString>(m_columns));
}

QString SpreadsheetContent::cell(int row, int column) const
{
    if (row < 0 || row >= m_rows || column < 0 || column >= m_columns)
        return QString();

    return m_cells.at(row).at(column);
}

QString SpreadsheetContent::displayValue(int row, int column) const
{
    const QString raw = cell(row, column);
    if (!raw.trimmed().startsWith(QLatin1Char('=')))
        return raw;

    QSet<quint64> stack;
    const FormulaResult result = evaluateNumericCell(this, row, column, &stack, false);
    if (!result.ok)
        return result.error.isEmpty() ? QStringLiteral("#ERROR!") : result.error;

    return formatFormulaNumber(result.value);
}

void SpreadsheetContent::setTableData(int rows, int columns, const QVector<QVector<QString>> &cells)
{
    m_rows = qBound(1, rows, 200);
    m_columns = qBound(1, columns, 50);
    m_cells = QVector<QVector<QString>>(m_rows, QVector<QString>(m_columns));

    const int copyRows = qMin(m_rows, cells.size());
    for (int row = 0; row < copyRows; ++row) {
        const int copyColumns = qMin(m_columns, cells.at(row).size());
        for (int column = 0; column < copyColumns; ++column)
            m_cells[row][column] = cells.at(row).at(column);
    }

    refreshView();
}

QString SpreadsheetContent::columnName(int column)
{
    QString result;
    int value = column;

    do {
        result.prepend(QChar(QLatin1Char('A').unicode() + (value % 26)));
        value = value / 26 - 1;
    } while (value >= 0);

    return result;
}

QString SpreadsheetContent::toText(const QString &)
{
    QStringList lines;
    for (int row = 0; row < m_rows; ++row) {
        QStringList values;
        for (int column = 0; column < m_columns; ++column)
            values.append(cell(row, column));
        lines.append(values.join(QLatin1Char('\t')));
    }
    return lines.join(QLatin1Char('\n'));
}

QString SpreadsheetContent::toHtml(const QString &, const QString &)
{
    QString html = QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\"><thead><tr><th></th>");
    for (int column = 0; column < m_columns; ++column)
        html += QStringLiteral("<th>%1</th>").arg(columnName(column).toHtmlEscaped());
    html += QStringLiteral("</tr></thead><tbody>");

    for (int row = 0; row < m_rows; ++row) {
        html += QStringLiteral("<tr><th>%1</th>").arg(row + 1);
        for (int column = 0; column < m_columns; ++column)
            html += QStringLiteral("<td>%1</td>").arg(displayValue(row, column).toHtmlEscaped());
        html += QStringLiteral("</tr>");
    }

    html += QStringLiteral("</tbody></table>");
    return html;
}

bool SpreadsheetContent::useFile() const
{
    return true;
}

bool SpreadsheetContent::canBeSavedAs() const
{
    return true;
}

QString SpreadsheetContent::saveAsFilters() const
{
    return i18n("Mathom Spreadsheet (*.mcalc)");
}

bool SpreadsheetContent::match(const FilterData &data)
{
    return toText(QString()).contains(data.string, Qt::CaseInsensitive);
}

void SpreadsheetContent::exportToHTML(HTMLExporter *exporter, int)
{
    exporter->stream << toHtml(QString(), QString());
}

QString SpreadsheetContent::cssClass() const
{
    return QStringLiteral("spreadsheet");
}

qreal SpreadsheetContent::setWidthAndGetHeight(qreal width)
{
    const qreal itemWidth = qMax(MinimumSpreadsheetWidth, width - 1.0);
    const qreal itemHeight = ColumnHeaderHeight + RowHeight * m_rows;
    m_item.setSize(itemWidth, itemHeight);
    return itemHeight;
}

bool SpreadsheetContent::loadFromFile(bool)
{
    QByteArray raw;
    if (!FileStorage::loadFromFile(fullPath(), &raw) || raw.trimmed().isEmpty())
        return false;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject object = document.object();
    const int rows = qBound(1, object.value(QStringLiteral("rows")).toInt(DefaultRows), 200);
    const int columns = qBound(1, object.value(QStringLiteral("columns")).toInt(DefaultColumns), 50);
    QVector<QVector<QString>> cells(rows, QVector<QString>(columns));

    const QJsonArray rowsArray = object.value(QStringLiteral("cells")).toArray();
    for (int row = 0; row < qMin(rows, rowsArray.size()); ++row) {
        const QJsonArray columnsArray = rowsArray.at(row).toArray();
        for (int column = 0; column < qMin(columns, columnsArray.size()); ++column)
            cells[row][column] = columnsArray.at(column).toString();
    }

    setTableData(rows, columns, cells);
    return true;
}

bool SpreadsheetContent::saveToFile()
{
    QJsonObject object;
    object.insert(QStringLiteral("format"), QStringLiteral("mathom-spreadsheet"));
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("rows"), m_rows);
    object.insert(QStringLiteral("columns"), m_columns);

    QJsonArray rowsArray;
    for (int row = 0; row < m_rows; ++row) {
        QJsonArray columnsArray;
        for (int column = 0; column < m_columns; ++column)
            columnsArray.append(cell(row, column));
        rowsArray.append(columnsArray);
    }
    object.insert(QStringLiteral("cells"), rowsArray);

    return m_pendingSave.recordResult(FileStorage::saveToFile(fullPath(), QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

void SpreadsheetContent::fontChanged()
{
    refreshView();
}

QString SpreadsheetContent::editToolTipText() const
{
    return i18n("Edit this spreadsheet");
}

QPixmap SpreadsheetContent::feedbackPixmap(qreal width, qreal height)
{
    const QSizeF sourceSize = m_item.boundingRect().size();
    if (sourceSize.isEmpty())
        return QPixmap();

    const qreal scale = qMin<qreal>(1.0, qMin(width / sourceSize.width(), height / sourceSize.height()));
    const QSize target(qMax(1, qRound(sourceSize.width() * scale)), qMax(1, qRound(sourceSize.height() * scale)));

    QPixmap pixmap(target);
    pixmap.fill(note()->backgroundColor().darker(FEEDBACK_DARKING));

    QPainter painter(&pixmap);
    painter.scale(scale, scale);
    m_item.paint(&painter, nullptr, nullptr);
    painter.end();

    return pixmap;
}

void SpreadsheetContent::refreshView()
{
    const qreal currentWidth = m_item.boundingRect().width();
    m_item.setSize(qMax(MinimumSpreadsheetWidth, currentWidth), ColumnHeaderHeight + RowHeight * m_rows);
    m_item.update();
    contentChanged(MinimumSpreadsheetWidth);
}
