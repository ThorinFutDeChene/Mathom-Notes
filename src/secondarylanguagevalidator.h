#ifndef SECONDARYLANGUAGEVALIDATOR_H
#define SECONDARYLANGUAGEVALIDATOR_H

#include "basket_export.h"
#include "secondarylanguagesettings.h"

#include <QString>
#include <QVector>

enum class SecondaryLanguageConflictType
{
    SameTrigger,
    PrefixTrigger
};

struct BASKET_EXPORT SecondaryLanguageConflict
{
    SecondaryLanguageConflictType type =
        SecondaryLanguageConflictType::SameTrigger;

    QString firstLanguageId;
    QString secondLanguageId;

    QString firstTrigger;
    QString secondTrigger;

    QString sequence;
};

class BASKET_EXPORT SecondaryLanguageValidator
{
public:
    static QVector<SecondaryLanguageConflict> conflicts(
        const QVector<SecondaryLanguageSelection> &selections);

    static bool isValid(
        const QVector<SecondaryLanguageSelection> &selections);
};

#endif
