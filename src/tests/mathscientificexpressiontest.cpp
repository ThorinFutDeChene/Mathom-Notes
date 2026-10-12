/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mathsymbolengine.h"
#include "mathsymbolsettings.h"

#include <QtTest>

class MathScientificExpressionTest : public QObject
{
    Q_OBJECT

private:
    static MathSymbolPreferences settings(const bool vertical = true)
    {
        MathSymbolPreferences p = MathSymbolSettings::defaults();
        p.trigger = QStringLiteral("£");
        p.scientific.verticalFractions = vertical;
        return p;
    }

private Q_SLOTS:
    void frenchDefaults()
    {
        const auto p = MathSymbolSettings::defaults();
        QVERIFY(p.scientific.verticalFractions);
        QCOMPARE(p.scientific.fractionSeparator, QStringLiteral("/"));
        QCOMPARE(p.scientific.exponentKeyword, QStringLiteral("exposant"));
        QCOMPARE(p.scientific.subscriptKeyword, QStringLiteral("indice"));
    }

    void fractionsDiagonal()
    {
        const auto p = settings(false);
        const auto result = MathSymbolEngine::mathOnly(QStringLiteral("8/153£"), p);
        QVERIFY(result.matched);
        QVERIFY(!result.verticalFraction);
        QCOMPARE(result.replacement, QStringLiteral("⁸⁄₁₅₃"));
        QCOMPARE(result.replaceLength, 6);
        const auto tenth = MathSymbolEngine::mathOnly(QStringLiteral("1/10£"), p);
        QCOMPARE(tenth.replacement, QStringLiteral("⅒"));
        const auto half = MathSymbolEngine::mathOnly(QStringLiteral("1/2£"), p);
        QCOMPARE(half.replacement, QStringLiteral("½"));
    }

    void fractionVerticalAndPlainFallback()
    {
        const auto result = MathSymbolEngine::mathOnly(
            QStringLiteral("8/153£"), settings(true));
        QVERIFY(result.matched);
        QVERIFY(result.verticalFraction);
        QCOMPARE(result.numerator, QStringLiteral("8"));
        QCOMPARE(result.denominator, QStringLiteral("153"));
        QCOMPARE(result.replacement, QStringLiteral("⁸⁄₁₅₃"));
    }

    void fractionsDoNotCompute()
    {
        const auto p = settings(false);
        const auto result = MathSymbolEngine::mathOnly(QStringLiteral("1/0£"), p);
        QVERIFY(result.matched);
        QCOMPARE(result.replacement, QStringLiteral("¹⁄₀"));
    }

    void longFractionsWithoutArbitraryLimits()
    {
        const auto p = settings(false);
        const QString input = QString(300, QLatin1Char('8')) + QStringLiteral("/")
            + QString(300, QLatin1Char('3')) + QStringLiteral("£");
        const auto result = MathSymbolEngine::mathOnly(input, p);
        QVERIFY(result.matched);
        QCOMPARE(result.replaceLength, static_cast<int>(input.size()));
        QVERIFY(result.replacement.startsWith(QString::fromUtf8("⁸⁸⁸")));
        QVERIFY(result.replacement.endsWith(QString::fromUtf8("₃₃₃")));
    }

    void exponentAndSubscript()
    {
        const auto p = settings();
        const auto exp = MathSymbolEngine::mathOnly(QStringLiteral("x10exposant£"), p);
        QVERIFY(exp.matched);
        QCOMPARE(exp.replacement, QStringLiteral("¹⁰"));
        const auto sub = MathSymbolEngine::mathOnly(QStringLiteral("H2indice£O"), p);
        QVERIFY(!sub.matched);  // conversion is triggered before typing the O
        const auto sub2 = MathSymbolEngine::mathOnly(QStringLiteral("H2indice£"), p);
        QCOMPARE(sub2.replacement, QStringLiteral("₂"));
        QCOMPARE(MathSymbolEngine::mathOnly(QStringLiteral("-125exposant£"), p).replacement,
                 QStringLiteral("⁻¹²⁵"));
        QCOMPARE(MathSymbolEngine::mathOnly(QStringLiteral("10indice£"), p).replacement,
                 QStringLiteral("₁₀"));
    }

    void customKeywordsAndDisabledRule()
    {
        auto p = settings();
        p.scientific.fractionSeparator = QStringLiteral("sur");
        p.scientific.exponentKeyword = QStringLiteral("puissance");
        p.scientific.subscriptKeyword.clear();
        QCOMPARE(MathSymbolEngine::mathOnly(QStringLiteral("8sur153£"), p).numerator,
                 QStringLiteral("8"));
        QCOMPARE(MathSymbolEngine::mathOnly(QStringLiteral("12puissance£"), p).replacement,
                 QStringLiteral("¹²"));
        QVERIFY(!MathSymbolEngine::mathOnly(QStringLiteral("2indice£"), p).matched);
        QVERIFY(!MathSymbolEngine::mathOnly(QStringLiteral("8/153£"), p).matched);
    }

    void noPrematureSubstitution()
    {
        const auto p = settings();
        QVERIFY(!MathSymbolEngine::mathOnly(QStringLiteral("8/153"), p).matched);
        QVERIFY(!MathSymbolEngine::mathOnly(QStringLiteral("3indice$"), p).matched);
    }

    void sameKeywordsDoNotChooseArbitrarily()
    {
        auto p = settings();
        p.scientific.subscriptKeyword = p.scientific.exponentKeyword;
        const auto r = MathSymbolEngine::mathOnly(QStringLiteral("10exposant£"), p);
        QVERIFY(!r.matched);
        QVERIFY(r.ambiguous);
    }

    void longerSymbolAliasWins()
    {
        auto p = settings(false);
        p.shortcuts.append({QStringLiteral("π"), QStringLiteral("expr8/153")});
        const auto r = MathSymbolEngine::mathOnly(QStringLiteral("expr8/153£"), p);
        QVERIFY(r.matched);
        QCOMPARE(r.replacement, QStringLiteral("π"));
    }
};

QTEST_GUILESS_MAIN(MathScientificExpressionTest)
#include "mathscientificexpressiontest.moc"
