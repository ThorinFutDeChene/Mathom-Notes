#ifndef MATHSYMBOLCATALOG_H
#define MATHSYMBOLCATALOG_H

#include "basket_export.h"
#include <QString>
#include <QVector>

struct BASKET_EXPORT MathSymbolDefinition
{
    QString symbol;
    QString category;
    QString label;
    QString defaultShortcut;

    QString configKey() const
    {
        return QStringLiteral("symbol_")
            + QString::fromLatin1(symbol.toUtf8().toHex());
    }
};

class BASKET_EXPORT MathSymbolCatalog
{
public:
    static QVector<MathSymbolDefinition> entries();
};

#endif
