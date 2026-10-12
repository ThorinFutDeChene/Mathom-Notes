/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mathsymbolengine.h"

#include "secondarylanguageengine.h"

MathSymbolTransformation MathSymbolEngine::mathOnly(
    const QString &textBeforeCursor,
    const MathSymbolPreferences &preferences)
{
    MathSymbolTransformation result;
    const QString trigger = preferences.trigger;
    if (trigger.isEmpty() || !textBeforeCursor.endsWith(trigger))
        return result;

    const QString prefix = textBeforeCursor.left(
        textBeforeCursor.size() - trigger.size());
    int longest = 0;

    for (const MathSymbolShortcut &mapping : preferences.shortcuts) {
        if (mapping.input.isEmpty() || mapping.symbol.isEmpty()
            || !prefix.endsWith(mapping.input))
            continue;

        const int length = mapping.input.size();
        if (length < longest)
            continue;
        if (length > longest) {
            longest = length;
            result.replacement = mapping.symbol;
            result.ambiguous = false;
        } else if (result.replacement != mapping.symbol) {
            result.ambiguous = true;
        }
    }

    if (longest > 0) {
        result.replaceLength = longest + trigger.size();
        result.matched = !result.ambiguous;
    }
    return result;
}

MathSymbolTransformation MathSymbolEngine::transform(
    const QString &textBeforeCursor,
    const MathSymbolPreferences &preferences,
    const QVector<SecondaryLanguageSelection> &languages)
{
    const MathSymbolTransformation math = mathOnly(textBeforeCursor, preferences);
    const SecondaryLanguageTransformation language =
        SecondaryLanguageEngine::transform(textBeforeCursor, languages);

    if (math.replaceLength == 0) {
        return {language.matched, language.ambiguous,
                language.replaceLength, language.replacement};
    }
    if (!language.matched && !language.ambiguous)
        return math;

    if (math.replaceLength > language.replaceLength)
        return math;
    if (language.replaceLength > math.replaceLength) {
        return {language.matched, language.ambiguous,
                language.replaceLength, language.replacement};
    }

    if (math.ambiguous || language.ambiguous
        || math.replacement != language.replacement) {
        MathSymbolTransformation ambiguous;
        ambiguous.ambiguous = true;
        ambiguous.replaceLength = math.replaceLength;
        return ambiguous;
    }
    return math;
}
