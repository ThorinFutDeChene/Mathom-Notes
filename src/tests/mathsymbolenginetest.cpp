/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mathsymbolcatalog.h"
#include "mathsymbolengine.h"
#include "mathsymbolsettings.h"

#include <QtTest>

class MathSymbolEngineTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void defaultsAreFrench()
    {
        const auto preferences = MathSymbolSettings::defaults();
        QCOMPARE(preferences.trigger, QString());
        QCOMPARE(preferences.shortcuts.size(), qsizetype(147));
        auto shortcutFor = [&preferences](const QString &symbol) {
            for (const auto &item : preferences.shortcuts) {
                if (item.symbol == symbol)
                    return item.input;
            }
            return QString();
        };
        QCOMPARE(shortcutFor(QStringLiteral("√")), QStringLiteral("racine"));
        QCOMPARE(shortcutFor(QStringLiteral("π")), QStringLiteral("pi"));
        QCOMPARE(shortcutFor(QStringLiteral("∞")), QStringLiteral("infini"));
        QCOMPARE(shortcutFor(QStringLiteral("≤")), QStringLiteral("inferieurouegal"));
    }

    void substitutionOnlyAtTerminator()
    {
        auto preferences = MathSymbolSettings::defaults();
        preferences.trigger = QStringLiteral("$");
        QVERIFY(!MathSymbolEngine::mathOnly(QStringLiteral("racine"), preferences).matched);
        const auto match = MathSymbolEngine::mathOnly(
            QStringLiteral("La racine$"), preferences);
        QVERIFY(match.matched);
        QCOMPARE(match.replacement, QStringLiteral("√"));
        QCOMPARE(match.replaceLength, 7);
        QVERIFY(!MathSymbolEngine::mathOnly(QStringLiteral("racine£"), preferences).matched);
    }

    void personalizedAliasesAndAnyLength()
    {
        auto preferences = MathSymbolSettings::defaults();
        preferences.trigger = QStringLiteral("£");
        for (auto &shortcut : preferences.shortcuts) {
            if (shortcut.symbol == QStringLiteral("π"))
                shortcut.input = QStringLiteral("monmotparticulier");
        }
        const auto match = MathSymbolEngine::mathOnly(
            QStringLiteral("monmotparticulier£"), preferences);
        QVERIFY(match.matched);
        QCOMPARE(match.replacement, QStringLiteral("π"));
    }

    void duplicatesNeverChooseArbitrarily()
    {
        auto preferences = MathSymbolSettings::defaults();
        preferences.trigger = QStringLiteral("$");
        for (auto &shortcut : preferences.shortcuts) {
            if (shortcut.symbol == QStringLiteral("π") || shortcut.symbol == QStringLiteral("√"))
                shortcut.input = QStringLiteral("commun");
        }
        const auto match = MathSymbolEngine::mathOnly(QStringLiteral("commun$"), preferences);
        QVERIFY(!match.matched);
        QVERIFY(match.ambiguous);
    }

    void disabledAliasStaysDisabled()
    {
        auto preferences = MathSymbolSettings::defaults();
        preferences.trigger = QStringLiteral("$");
        for (auto &shortcut : preferences.shortcuts) {
            if (shortcut.symbol == QStringLiteral("√"))
                shortcut.input.clear();
        }
        QVERIFY(!MathSymbolEngine::mathOnly(QStringLiteral("racine$"), preferences).matched);
    }
};

QTEST_GUILESS_MAIN(MathSymbolEngineTest)
#include "mathsymbolenginetest.moc"
