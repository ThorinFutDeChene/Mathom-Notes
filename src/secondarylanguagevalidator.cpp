#include "secondarylanguagevalidator.h"

#include "secondarylanguagecatalog.h"
#include "secondarylanguageprofile.h"

#include <QHash>
#include <QSet>

namespace
{

QHash<QString, QString> transitions(
    const SecondaryLanguageProfile &profile)
{
    QHash<QString, QString> result;

    for (const SecondaryLanguageRule &rule :
         profile.rules) {

        if (rule.source.isEmpty()
            || rule.variants.isEmpty()) {
            continue;
        }

        result.insert(
            rule.source,
            rule.variants.first());

        if (rule.cycle) {
            for (int index = 0;
                 index < rule.variants.size();
                 ++index) {

                const QString replacement =
                    index + 1 < rule.variants.size()
                    ? rule.variants.at(index + 1)
                    : rule.source;

                result.insert(
                    rule.variants.at(index),
                    replacement);
            }
        }
    }

    return result;
}

}

QVector<SecondaryLanguageConflict>
SecondaryLanguageValidator::conflicts(
    const QVector<SecondaryLanguageSelection> &selections)
{
    QVector<SecondaryLanguageConflict> result;
    QSet<QString> recorded;

    for (int firstIndex = 0;
         firstIndex < selections.size();
         ++firstIndex) {

        const SecondaryLanguageSelection &first =
            selections.at(firstIndex);

        if (!first.isValid())
            continue;

        const SecondaryLanguageProfile firstProfile =
            SecondaryLanguageCatalog::profileById(
                first.id);

        if (!firstProfile.isValid())
            continue;

        const QHash<QString, QString> firstTransitions =
            transitions(firstProfile);

        for (int secondIndex = firstIndex + 1;
             secondIndex < selections.size();
             ++secondIndex) {

            const SecondaryLanguageSelection &second =
                selections.at(secondIndex);

            if (!second.isValid())
                continue;

            const SecondaryLanguageProfile secondProfile =
                SecondaryLanguageCatalog::profileById(
                    second.id);

            if (!secondProfile.isValid())
                continue;

            const QHash<QString, QString> secondTransitions =
                transitions(secondProfile);

            /*
             * CAS 1 :
             * Les deux langues utilisent exactement
             * le même déclencheur.
             */
            if (first.trigger == second.trigger) {

                for (auto it = firstTransitions.constBegin();
                     it != firstTransitions.constEnd();
                     ++it) {

                    if (!secondTransitions.contains(it.key()))
                        continue;

                    if (secondTransitions.value(it.key())
                        == it.value()) {
                        continue;
                    }

                    const QString sequence =
                        it.key() + first.trigger;

                    const QString uniqueKey =
                        QStringLiteral("same|%1|%2|%3")
                            .arg(
                                first.id,
                                second.id,
                                sequence);

                    if (recorded.contains(uniqueKey))
                        continue;

                    recorded.insert(uniqueKey);

                    result.append({
                        SecondaryLanguageConflictType::SameTrigger,
                        first.id,
                        second.id,
                        first.trigger,
                        second.trigger,
                        sequence
                    });
                }

                continue;
            }

            /*
             * CAS 2 :
             * Un déclencheur est le préfixe de l'autre.
             *
             * Exemple :
             *
             * Espagnol : x
             * Allemand : xx
             *
             * ax peut être transformé avant que l'utilisateur
             * ait le temps de terminer axx.
             */
            const SecondaryLanguageSelection *shortSelection =
                nullptr;

            const SecondaryLanguageSelection *longSelection =
                nullptr;

            const QHash<QString, QString> *shortTransitions =
                nullptr;

            const QHash<QString, QString> *longTransitions =
                nullptr;

            if (second.trigger.startsWith(first.trigger)) {
                shortSelection = &first;
                longSelection = &second;
                shortTransitions = &firstTransitions;
                longTransitions = &secondTransitions;
            } else if (first.trigger.startsWith(second.trigger)) {
                shortSelection = &second;
                longSelection = &first;
                shortTransitions = &secondTransitions;
                longTransitions = &firstTransitions;
            } else {
                continue;
            }

            for (auto longIt = longTransitions->constBegin();
                 longIt != longTransitions->constEnd();
                 ++longIt) {

                QString bestShortInput;

                for (auto shortIt =
                         shortTransitions->constBegin();
                     shortIt !=
                         shortTransitions->constEnd();
                     ++shortIt) {

                    if (!longIt.key().endsWith(
                            shortIt.key())) {
                        continue;
                    }

                    if (shortIt.key().size()
                        > bestShortInput.size()) {
                        bestShortInput =
                            shortIt.key();
                    }
                }

                if (bestShortInput.isEmpty())
                    continue;

                const QString sequence =
                    longIt.key()
                    + longSelection->trigger;

                const QString uniqueKey =
                    QStringLiteral("prefix|%1|%2|%3")
                        .arg(
                            shortSelection->id,
                            longSelection->id,
                            sequence);

                if (recorded.contains(uniqueKey))
                    continue;

                recorded.insert(uniqueKey);

                result.append({
                    SecondaryLanguageConflictType::PrefixTrigger,
                    shortSelection->id,
                    longSelection->id,
                    shortSelection->trigger,
                    longSelection->trigger,
                    sequence
                });
            }
        }
    }

    return result;
}

bool SecondaryLanguageValidator::isValid(
    const QVector<SecondaryLanguageSelection> &selections)
{
    return conflicts(selections).isEmpty();
}
