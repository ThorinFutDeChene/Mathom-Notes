#ifndef MATHSCIENTIFICFORMAT_H
#define MATHSCIENTIFICFORMAT_H

#include "basket_export.h"

#include <QBrush>
#include <QString>
#include <QTextCursor>

// A text-based fraction table: both numeric components remain selectable,
// editable and are serialized with QTextDocument::toHtml().
class BASKET_EXPORT MathScientificFormat
{
public:
    static void insertVerticalFraction(QTextCursor &cursor,
                                       const QString &numerator,
                                       const QString &denominator,
                                       const QBrush &separatorColor);
};

#endif
