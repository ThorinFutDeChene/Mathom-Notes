/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mathsymbolengine.h"

#include "secondarylanguageengine.h"

#include <QRegularExpression>

namespace {

QString asSuperscript(const QString &text)
{
    static const QString glyphs[] = {
        QString::fromUtf8("⁰"), QString::fromUtf8("¹"),
        QString::fromUtf8("²"), QString::fromUtf8("³"),
        QString::fromUtf8("⁴"), QString::fromUtf8("⁵"),
        QString::fromUtf8("⁶"), QString::fromUtf8("⁷"),
        QString::fromUtf8("⁸"), QString::fromUtf8("⁹")
    };
    QString result;
    for (const QChar character : text) {
        const ushort value = character.unicode();
        if (value >= '0' && value <= '9')
            result += glyphs[value - '0'];
        else if (character == QLatin1Char('-'))
            result += QString::fromUtf8("⁻");
        else if (character == QLatin1Char('+'))
            result += QString::fromUtf8("⁺");
    }
    return result;
}

QString asSubscript(const QString &text)
{
    static const QString glyphs[] = {
        QString::fromUtf8("₀"), QString::fromUtf8("₁"),
        QString::fromUtf8("₂"), QString::fromUtf8("₃"),
        QString::fromUtf8("₄"), QString::fromUtf8("₅"),
        QString::fromUtf8("₆"), QString::fromUtf8("₇"),
        QString::fromUtf8("₈"), QString::fromUtf8("₉")
    };
    QString result;
    for (const QChar character : text) {
        const ushort value = character.unicode();
        if (value >= '0' && value <= '9')
            result += glyphs[value - '0'];
        else if (character == QLatin1Char('-'))
            result += QString::fromUtf8("₋");
        else if (character == QLatin1Char('+'))
            result += QString::fromUtf8("₊");
    }
    return result;
}

QString diagonalFraction(const QString &numerator, const QString &denominator)
{
    // Unicode has convenient single-character fractions for these cases.
    if (numerator == QStringLiteral("1") && denominator == QStringLiteral("2")) return QString::fromUtf8("½");
    if (numerator == QStringLiteral("1") && denominator == QStringLiteral("3")) return QString::fromUtf8("⅓");
    if (numerator == QStringLiteral("2") && denominator == QStringLiteral("3")) return QString::fromUtf8("⅔");
    if (numerator == QStringLiteral("1") && denominator == QStringLiteral("4")) return QString::fromUtf8("¼");
    if (numerator == QStringLiteral("3") && denominator == QStringLiteral("4")) return QString::fromUtf8("¾");
    if (numerator == QStringLiteral("1") && denominator == QStringLiteral("5")) return QString::fromUtf8("⅕");
    if (numerator == QStringLiteral("2") && denominator == QStringLiteral("5")) return QString::fromUtf8("⅖");
    if (numerator == QStringLiteral("3") && denominator == QStringLiteral("5")) return QString::fromUtf8("⅗");
    if (numerator == QStringLiteral("4") && denominator == QStringLiteral("5")) return QString::fromUtf8("⅘");
    if (numerator == QStringLiteral("1") && denominator == QStringLiteral("6")) return QString::fromUtf8("⅙");
    if (numerator == QStringLiteral("5") && denominator == QStringLiteral("6")) return QString::fromUtf8("⅚");
    if (numerator == QStringLiteral("1") && denominator == QStringLiteral("8")) return QString::fromUtf8("⅛");
    if (numerator == QStringLiteral("3") && denominator == QStringLiteral("8")) return QString::fromUtf8("⅜");
    if (numerator == QStringLiteral("5") && denominator == QStringLiteral("8")) return QString::fromUtf8("⅝");
    if (numerator == QStringLiteral("7") && denominator == QStringLiteral("8")) return QString::fromUtf8("⅞");
    if (numerator == QStringLiteral("1") && denominator == QStringLiteral("10")) return QString::fromUtf8("⅒");
    return asSuperscript(numerator) + QString::fromUtf8("⁄") + asSubscript(denominator);
}

struct Candidate
{
    int replaceLength = 0;
    QString replacement;
    bool verticalFraction = false;
    QString numerator;
    QString denominator;
};

void addNumericCandidate(
    QVector<Candidate> &candidates, const QString &prefix,
    const QString &trigger, const QString &keyword,
    const bool superscript)
{
    if (keyword.isEmpty())
        return;
    const QRegularExpression expression(
        QStringLiteral("([+-]?[0-9]+)%1$")
            .arg(QRegularExpression::escape(keyword)));
    const QRegularExpressionMatch match = expression.match(prefix);
    if (!match.hasMatch())
        return;
    const QString numeric = match.captured(1);
    candidates.append({static_cast<int>(match.capturedLength(0) + trigger.size()),
                       superscript ? asSuperscript(numeric) : asSubscript(numeric),
                       false, QString(), QString()});
}

} // namespace

MathSymbolTransformation MathSymbolEngine::mathOnly(
    const QString &textBeforeCursor,
    const MathSymbolPreferences &preferences)
{
    MathSymbolTransformation result;
    const QString trigger = preferences.trigger;
    if (trigger.isEmpty() || !textBeforeCursor.endsWith(trigger))
        return result;

    const QString prefix = textBeforeCursor.left(textBeforeCursor.size() - trigger.size());
    QVector<Candidate> candidates;
    for (const MathSymbolShortcut &mapping : preferences.shortcuts) {
        if (!mapping.input.isEmpty() && !mapping.symbol.isEmpty()
            && prefix.endsWith(mapping.input)) {
            candidates.append({static_cast<int>(mapping.input.size() + trigger.size()),
                               mapping.symbol, false, QString(), QString()});
        }
    }

    const MathScientificPreferences &scientific = preferences.scientific;
    if (!scientific.fractionSeparator.isEmpty()) {
        const QRegularExpression fractionExpression(
            QStringLiteral("([+-]?[0-9]+)%1([0-9]+)$")
                .arg(QRegularExpression::escape(scientific.fractionSeparator)));
        const QRegularExpressionMatch match = fractionExpression.match(prefix);
        if (match.hasMatch()) {
            const QString numerator = match.captured(1);
            const QString denominator = match.captured(2);
            candidates.append({static_cast<int>(match.capturedLength(0) + trigger.size()),
                               diagonalFraction(numerator, denominator),
                               scientific.verticalFractions, numerator, denominator});
        }
    }
    addNumericCandidate(candidates, prefix, trigger,
                        scientific.exponentKeyword, true);
    addNumericCandidate(candidates, prefix, trigger,
                        scientific.subscriptKeyword, false);

    int longest = 0;
    for (const Candidate &candidate : candidates)
        longest = qMax(longest, candidate.replaceLength);
    if (longest <= 0)
        return result;

    Candidate first;
    bool found = false;
    for (const Candidate &candidate : candidates) {
        if (candidate.replaceLength != longest)
            continue;
        if (!found) {
            first = candidate;
            found = true;
        } else if (candidate.replacement != first.replacement
                   || candidate.verticalFraction != first.verticalFraction
                   || candidate.numerator != first.numerator
                   || candidate.denominator != first.denominator) {
            result.ambiguous = true;
        }
    }
    result.replaceLength = longest;
    if (result.ambiguous)
        return result;
    result.matched = true;
    result.replacement = first.replacement;
    result.verticalFraction = first.verticalFraction;
    result.numerator = first.numerator;
    result.denominator = first.denominator;
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
    if (math.replaceLength == 0)
        return {language.matched, language.ambiguous,
                language.replaceLength, language.replacement};
    if (language.replaceLength == 0)
        return math;
    if (math.replaceLength > language.replaceLength)
        return math;
    if (language.replaceLength > math.replaceLength)
        return {language.matched, language.ambiguous,
                language.replaceLength, language.replacement};
    // On an exact tie, never choose arbitrarily between distinct results.
    if (math.ambiguous || language.ambiguous
        || math.replacement != language.replacement) {
        MathSymbolTransformation ambiguous;
        ambiguous.ambiguous = true;
        ambiguous.replaceLength = math.replaceLength;
        return ambiguous;
    }
    return math;
}
