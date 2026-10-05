#include "secondarylanguageprofile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

SecondaryLanguageProfile
SecondaryLanguageProfile::loadFromResource(
    const QString &resourcePath,
    QString *errorMessage)
{
    SecondaryLanguageProfile profile;

    QFile file(resourcePath);

    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Impossible d'ouvrir le profil : %1")
                    .arg(resourcePath);
        }
        return profile;
    }

    QJsonParseError parseError;

    const QJsonDocument document =
        QJsonDocument::fromJson(
            file.readAll(),
            &parseError);

    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {

        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Profil JSON invalide : %1")
                    .arg(parseError.errorString());
        }

        return profile;
    }

    const QJsonObject root =
        document.object();

    profile.id =
        root.value(QStringLiteral("id"))
            .toString();

    profile.name =
        root.value(QStringLiteral("name"))
            .toString();

    profile.defaultTrigger =
        root.value(QStringLiteral("defaultTrigger"))
            .toString();

    const QJsonArray rules =
        root.value(QStringLiteral("rules"))
            .toArray();

    for (const QJsonValue &ruleValue : rules) {
        if (!ruleValue.isObject())
            continue;

        const QJsonObject ruleObject =
            ruleValue.toObject();

        SecondaryLanguageRule rule;

        rule.source =
            ruleObject
                .value(QStringLiteral("source"))
                .toString();

        const QJsonArray variants =
            ruleObject
                .value(QStringLiteral("variants"))
                .toArray();

        for (const QJsonValue &variant : variants) {
            if (variant.isString())
                rule.variants.append(
                    variant.toString());
        }

        if (!rule.source.isEmpty()
            && !rule.variants.isEmpty()) {
            profile.rules.append(rule);
        }
    }

    if (!profile.isValid()
        && errorMessage) {
        *errorMessage =
            QStringLiteral(
                "Le profil de langue est incomplet ou invalide.");
    }

    return profile;
}

bool SecondaryLanguageProfile::isValid() const
{
    return !id.isEmpty()
        && !name.isEmpty()
        && !defaultTrigger.isEmpty()
        && !rules.isEmpty();
}
