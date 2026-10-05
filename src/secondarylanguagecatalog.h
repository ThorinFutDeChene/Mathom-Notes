#ifndef SECONDARYLANGUAGECATALOG_H
#define SECONDARYLANGUAGECATALOG_H

#include "basket_export.h"

#include "secondarylanguageprofile.h"

#include <QString>
#include <QStringList>
#include <QVector>

class BASKET_EXPORT SecondaryLanguageCatalog
{
public:
    static QVector<SecondaryLanguageProfile> profiles(
        QStringList *errors = nullptr);

    static SecondaryLanguageProfile profileById(
        const QString &id,
        QString *errorMessage = nullptr);
};

#endif
