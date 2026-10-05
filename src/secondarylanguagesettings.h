#ifndef SECONDARYLANGUAGESETTINGS_H
#define SECONDARYLANGUAGESETTINGS_H

#include "basket_export.h"

#include <QString>
#include <QVector>

struct BASKET_EXPORT SecondaryLanguageSelection
{
    QString id;
    QString trigger;

    bool isValid() const
    {
        return !id.isEmpty()
            && !trigger.isEmpty();
    }
};

class BASKET_EXPORT SecondaryLanguageSettings
{
public:
    static QVector<SecondaryLanguageSelection> load();

    static void save(
        const QVector<SecondaryLanguageSelection> &selections);
};

#endif
