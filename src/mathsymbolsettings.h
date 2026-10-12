#ifndef MATHSYMBOLSETTINGS_H
#define MATHSYMBOLSETTINGS_H

#include "basket_export.h"

#include <QString>
#include <QVector>

struct BASKET_EXPORT MathSymbolShortcut
{
    QString symbol;
    QString input;
};

struct BASKET_EXPORT MathSymbolPreferences
{
    QString trigger;
    QVector<MathSymbolShortcut> shortcuts;
};

class BASKET_EXPORT MathSymbolSettings
{
public:
    // French defaults are applied only when an alias has never been saved.
    static MathSymbolPreferences load();
    static QString trigger();
    static MathSymbolPreferences defaults();
    static void save(const MathSymbolPreferences &preferences);
};

#endif
