#include "secondarylanguageengine.h"

#include "secondarylanguagecatalog.h"
#include "secondarylanguageprofile.h"

namespace
{

struct Candidate
{
    int sourceLength = 0;
    int replaceLength = 0;

    QString replacement;
    QString languageId;
};

void addCandidate(
    QVector<Candidate> &candidates,
    const QString &prefix,
    const QString &trigger,
    const QString &current,
    const QString &replacement,
    const QString &languageId)
{
    if (current.isEmpty())
        return;

    if (!prefix.endsWith(current))
        return;

    Candidate candidate;
    candidate.sourceLength = current.size();
    candidate.replaceLength =
        current.size() + trigger.size();
    candidate.replacement = replacement;
    candidate.languageId = languageId;

    candidates.append(candidate);
}

}

SecondaryLanguageTransformation
SecondaryLanguageEngine::transform(
    const QString &textBeforeCursor,
    const QVector<SecondaryLanguageSelection> &selections)
{
    QVector<Candidate> candidates;

    for (const SecondaryLanguageSelection &selection :
         selections) {

        if (!selection.isValid())
            continue;

        const QString trigger =
            selection.trigger;

        if (!textBeforeCursor.endsWith(trigger))
            continue;

        QString error;

        const SecondaryLanguageProfile profile =
            SecondaryLanguageCatalog::profileById(
                selection.id,
                &error);

        if (!profile.isValid())
            continue;

        const QString prefix =
            textBeforeCursor.left(
                textBeforeCursor.size()
                - trigger.size());

        for (const SecondaryLanguageRule &rule :
             profile.rules) {

            if (rule.source.isEmpty()
                || rule.variants.isEmpty()) {
                continue;
            }

            /*
             * Cycle:
             *
             * source
             *   -> variante 1
             *   -> variante 2
             *   -> ...
             *   -> source
             */
            addCandidate(
                candidates,
                prefix,
                trigger,
                rule.source,
                rule.variants.first(),
                profile.id);

            for (int index = 0;
                 index < rule.variants.size();
                 ++index) {

                const QString &current =
                    rule.variants.at(index);

                const QString replacement =
                    index + 1
                            < rule.variants.size()
                    ? rule.variants.at(index + 1)
                    : rule.source;

                addCandidate(
                    candidates,
                    prefix,
                    trigger,
                    current,
                    replacement,
                    profile.id);
            }
        }
    }

    SecondaryLanguageTransformation result;

    if (candidates.isEmpty())
        return result;

    /*
     * Une séquence longue a priorité sur une courte.
     *
     * Exemple futur :
     *   oe + déclencheur
     * doit être testé avant
     *   e + déclencheur.
     */
    int longestSource = 0;

    for (const Candidate &candidate :
         candidates) {
        if (candidate.sourceLength
            > longestSource) {
            longestSource =
                candidate.sourceLength;
        }
    }

    QVector<Candidate> best;

    for (const Candidate &candidate :
         candidates) {
        if (candidate.sourceLength
            == longestSource) {
            best.append(candidate);
        }
    }

    const Candidate first =
        best.first();

    /*
     * Plusieurs profils peuvent éventuellement produire exactement
     * la même transformation : ce n'est pas une ambiguïté.
     *
     * En revanche, deux résultats différents pour la même saisie
     * rendent la transformation indéterminée.
     */
    for (const Candidate &candidate :
         best) {

        if (candidate.replaceLength
                != first.replaceLength
            || candidate.replacement
                != first.replacement) {

            result.ambiguous = true;
            return result;
        }
    }

    result.matched = true;
    result.replaceLength =
        first.replaceLength;
    result.replacement =
        first.replacement;
    result.languageId =
        first.languageId;

    return result;
}
