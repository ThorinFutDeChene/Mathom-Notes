/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mathsymbolsettings.h"

#include "global.h"
#include "mathsymbolcatalog.h"

#include <KConfigGroup>

MathSymbolPreferences MathSymbolSettings::defaults()
{
    MathSymbolPreferences preferences;
    for (const MathSymbolDefinition &definition : MathSymbolCatalog::entries()) {
        preferences.shortcuts.append({definition.symbol, definition.defaultShortcut});
    }
    // The trigger remains the user's decision; never borrow one from a language.
    return preferences;
}

QString MathSymbolSettings::trigger()
{
    if (!Global::config())
        return QString();
    KConfigGroup general(Global::config(), QStringLiteral("Mathematical Symbols"));
    return general.readEntry(QStringLiteral("trigger"), QString());
}

MathSymbolPreferences MathSymbolSettings::load()
{
    MathSymbolPreferences preferences = defaults();
    if (!Global::config())
        return preferences;

    KConfigGroup general(Global::config(), QStringLiteral("Mathematical Symbols"));
    KConfigGroup mappings(Global::config(), QStringLiteral("Mathematical Symbol Shortcuts"));
    preferences.trigger = general.readEntry(QStringLiteral("trigger"), QString());
    preferences.scientific.fractionSeparator = general.readEntry(
        QStringLiteral("fractionSeparator"), preferences.scientific.fractionSeparator);
    preferences.scientific.exponentKeyword = general.readEntry(
        QStringLiteral("exponentKeyword"), preferences.scientific.exponentKeyword);
    preferences.scientific.subscriptKeyword = general.readEntry(
        QStringLiteral("subscriptKeyword"), preferences.scientific.subscriptKeyword);
    const QString style = general.readEntry(
        QStringLiteral("fractionStyle"), QStringLiteral("vertical"));
    preferences.scientific.verticalFractions = style != QStringLiteral("diagonal");

    const QVector<MathSymbolDefinition> definitions = MathSymbolCatalog::entries();
    for (qsizetype index = 0; index < definitions.size(); ++index) {
        // An explicitly empty alias disables that mapping.
        preferences.shortcuts[index].input = mappings.readEntry(
            definitions[index].configKey(), definitions[index].defaultShortcut);
    }
    return preferences;
}

void MathSymbolSettings::save(const MathSymbolPreferences &preferences)
{
    if (!Global::config())
        return;

    KConfigGroup general(Global::config(), QStringLiteral("Mathematical Symbols"));
    KConfigGroup mappings(Global::config(), QStringLiteral("Mathematical Symbol Shortcuts"));
    general.writeEntry(QStringLiteral("trigger"), preferences.trigger);
    general.writeEntry(QStringLiteral("fractionSeparator"),
                       preferences.scientific.fractionSeparator);
    general.writeEntry(QStringLiteral("exponentKeyword"),
                       preferences.scientific.exponentKeyword);
    general.writeEntry(QStringLiteral("subscriptKeyword"),
                       preferences.scientific.subscriptKeyword);
    general.writeEntry(QStringLiteral("fractionStyle"),
                       preferences.scientific.verticalFractions
                           ? QStringLiteral("vertical") : QStringLiteral("diagonal"));

    const QVector<MathSymbolDefinition> definitions = MathSymbolCatalog::entries();
    for (const MathSymbolDefinition &definition : definitions) {
        QString input = definition.defaultShortcut;
        for (const MathSymbolShortcut &shortcut : preferences.shortcuts) {
            if (shortcut.symbol == definition.symbol) {
                input = shortcut.input;
                break;
            }
        }
        mappings.writeEntry(definition.configKey(), input);
    }
    Global::config()->sync();
}
