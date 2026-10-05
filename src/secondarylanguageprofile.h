#ifndef SECONDARYLANGUAGEPROFILE_H
#define SECONDARYLANGUAGEPROFILE_H

#include "basket_export.h"

#include <QString>
#include <QStringList>
#include <QVector>

struct SecondaryLanguageRule
{
    QString source;
    QStringList variants;
};

class BASKET_EXPORT SecondaryLanguageProfile
{
public:
    QString id;
    QString name;
    QString defaultTrigger;
    QVector<SecondaryLanguageRule> rules;

    static SecondaryLanguageProfile loadFromResource(
        const QString &resourcePath,
        QString *errorMessage = nullptr);

    bool isValid() const;
};

#endif
