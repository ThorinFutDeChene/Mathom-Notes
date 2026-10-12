#ifndef MATHSYMBOLENGINE_H
#define MATHSYMBOLENGINE_H

#include "basket_export.h"
#include "mathsymbolsettings.h"
#include "secondarylanguagesettings.h"

#include <QString>
#include <QVector>

struct BASKET_EXPORT MathSymbolTransformation
{
    bool matched = false;
    bool ambiguous = false;
    int replaceLength = 0;
    QString replacement;

    // Only rich-text Mathoms support a true stacked fraction.
    // 'replacement' always contains a plain-Unicode fallback for text Mathoms.
    bool verticalFraction = false;
    QString numerator;
    QString denominator;
};

class BASKET_EXPORT MathSymbolEngine
{
public:
    static MathSymbolTransformation mathOnly(
        const QString &textBeforeCursor,
        const MathSymbolPreferences &preferences);

    // The longest matching sequence wins. Equal-length conflicting results
    // are reported as ambiguous and are intentionally not substituted.
    static MathSymbolTransformation transform(
        const QString &textBeforeCursor,
        const MathSymbolPreferences &preferences,
        const QVector<SecondaryLanguageSelection> &languages);
};

#endif
