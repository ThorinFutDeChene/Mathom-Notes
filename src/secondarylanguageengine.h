#ifndef SECONDARYLANGUAGEENGINE_H
#define SECONDARYLANGUAGEENGINE_H

#include "basket_export.h"
#include "secondarylanguagesettings.h"

#include <QString>
#include <QVector>

struct BASKET_EXPORT SecondaryLanguageTransformation
{
    bool matched = false;
    bool ambiguous = false;

    int replaceLength = 0;

    QString replacement;
    QString languageId;
};

class BASKET_EXPORT SecondaryLanguageEngine
{
public:
    static SecondaryLanguageTransformation transform(
        const QString &textBeforeCursor,
        const QVector<SecondaryLanguageSelection> &selections);
};

#endif
