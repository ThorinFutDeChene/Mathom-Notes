#include "secondarylanguagesettings.h"

#include "global.h"
#include "secondarylanguagecatalog.h"

#include <KConfigGroup>

#include <QSet>
#include <QStringList>

namespace
{

QString triggerKey(const QString &id)
{
    return QStringLiteral("trigger_%1").arg(id);
}

}

QVector<SecondaryLanguageSelection>
SecondaryLanguageSettings::load()
{
    QVector<SecondaryLanguageSelection> result;

    if (!Global::config())
        return result;

    KConfigGroup group(
        Global::config(),
        QStringLiteral("Secondary Languages"));

    const QStringList enabledLanguages =
        group.readEntry(
            QStringLiteral("enabledLanguages"),
            QStringList());

    QSet<QString> seen;

    for (const QString &configuredId :
         enabledLanguages) {

        const QString id =
            configuredId.trimmed();

        if (id.isEmpty()
            || seen.contains(id)) {
            continue;
        }

        QString profileError;

        const SecondaryLanguageProfile profile =
            SecondaryLanguageCatalog::profileById(
                id,
                &profileError);

        /*
         * Ignore obsolete or invalid language IDs.
         * This keeps an old configuration harmless if a profile
         * disappears in a future version.
         */
        if (!profile.isValid())
            continue;

        QString trigger =
            group.readEntry(
                triggerKey(id),
                QString());

        if (trigger.isEmpty())
            trigger = profile.defaultTrigger;

        if (trigger.isEmpty())
            continue;

        result.append(
            SecondaryLanguageSelection{
                id,
                trigger
            });

        seen.insert(id);
    }

    return result;
}

void SecondaryLanguageSettings::save(
    const QVector<SecondaryLanguageSelection> &selections)
{
    if (!Global::config())
        return;

    KConfigGroup group(
        Global::config(),
        QStringLiteral("Secondary Languages"));

    const QStringList oldIds =
        group.readEntry(
            QStringLiteral("enabledLanguages"),
            QStringList());

    QStringList newIds;
    QSet<QString> seen;

    for (const SecondaryLanguageSelection &selection :
         selections) {

        const QString id =
            selection.id.trimmed();

        if (id.isEmpty()
            || selection.trigger.isEmpty()
            || seen.contains(id)) {
            continue;
        }

        QString profileError;

        const SecondaryLanguageProfile profile =
            SecondaryLanguageCatalog::profileById(
                id,
                &profileError);

        if (!profile.isValid())
            continue;

        newIds.append(id);
        seen.insert(id);

        group.writeEntry(
            triggerKey(id),
            selection.trigger);
    }

    /*
     * Remove trigger entries belonging to languages which have
     * been disabled by the user.
     */
    for (const QString &oldId : oldIds) {
        if (!seen.contains(oldId)) {
            group.deleteEntry(
                triggerKey(oldId));
        }
    }

    group.writeEntry(
        QStringLiteral("enabledLanguages"),
        newIds);

    Global::config()->sync();
}
