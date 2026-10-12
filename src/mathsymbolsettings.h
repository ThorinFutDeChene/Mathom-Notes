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

// French suggestions only: every field may be customized or left blank.
struct BASKET_EXPORT MathScientificPreferences
{
    QString fractionSeparator = QStringLiteral("/");
    QString exponentKeyword = QStringLiteral("exposant");
    QString subscriptKeyword = QStringLiteral("indice");
    bool verticalFractions = true;
};

struct BASKET_EXPORT MathSymbolPreferences
{
    QString trigger;
    QVector<MathSymbolShortcut> shortcuts;
    MathScientificPreferences scientific;
};

class BASKET_EXPORT MathSymbolSettings
{
public:
    static MathSymbolPreferences load();
    static QString trigger();
    static MathSymbolPreferences defaults();
    static void save(const MathSymbolPreferences &preferences);
};

#endif
