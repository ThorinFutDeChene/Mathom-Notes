#include "secondarylanguagecatalog.h"

#include <QDir>
#include <QSet>

#include <algorithm>

QVector<SecondaryLanguageProfile>
SecondaryLanguageCatalog::profiles(
    QStringList *errors)
{
    QVector<SecondaryLanguageProfile> result;
    QSet<QString> ids;

    const QDir directory(
        QStringLiteral(
            ":/mathom/secondary-languages"));

    const QStringList files =
        directory.entryList(
            {QStringLiteral("*.json")},
            QDir::Files,
            QDir::Name);

    for (const QString &fileName : files) {
        QString error;

        const SecondaryLanguageProfile profile =
            SecondaryLanguageProfile::loadFromResource(
                directory.filePath(fileName),
                &error);

        if (!profile.isValid()) {
            if (errors) {
                errors->append(
                    QStringLiteral("%1 : %2")
                        .arg(fileName, error));
            }
            continue;
        }

        if (ids.contains(profile.id)) {
            if (errors) {
                errors->append(
                    QStringLiteral(
                        "%1 : identifiant de langue dupliqué \"%2\"")
                        .arg(fileName, profile.id));
            }
            continue;
        }

        ids.insert(profile.id);
        result.append(profile);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const SecondaryLanguageProfile &left,
           const SecondaryLanguageProfile &right) {
            return QString::localeAwareCompare(
                       left.name,
                       right.name)
                < 0;
        });

    return result;
}

SecondaryLanguageProfile
SecondaryLanguageCatalog::profileById(
    const QString &id,
    QString *errorMessage)
{
    QStringList errors;

    const QVector<SecondaryLanguageProfile> allProfiles =
        profiles(&errors);

    for (const SecondaryLanguageProfile &profile :
         allProfiles) {
        if (profile.id == id)
            return profile;
    }

    if (errorMessage) {
        if (!errors.isEmpty()) {
            *errorMessage =
                errors.join(QLatin1Char('\n'));
        } else {
            *errorMessage =
                QStringLiteral(
                    "Profil de langue introuvable : %1")
                    .arg(id);
        }
    }

    return {};
}
