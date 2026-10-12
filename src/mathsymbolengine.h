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
};

class BASKET_EXPORT MathSymbolEngine
{
public:
    // A suffix-triggered alias; text before the trigger is left intact.
    static MathSymbolTransformation mathOnly(
        const QString &textBeforeCursor,
        const MathSymbolPreferences &preferences);

    // Resolve collisions with secondary languages by longest match.
    // Equal-length different results are ambiguous and remain untouched.
    static MathSymbolTransformation transform(
        const QString &textBeforeCursor,
        const MathSymbolPreferences &preferences,
        const QVector<SecondaryLanguageSelection> &languages);
};

#endif
