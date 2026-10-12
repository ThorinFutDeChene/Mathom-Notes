/* Mathom Notes - mathematical symbol configuration.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "settings.h"

#include "mathsymbolcatalog.h"
#include "mathsymbolsettings.h"
#include "secondarylanguagecatalog.h"
#include "secondarylanguagesettings.h"

#include <KLocalizedString>
#include <KPluginFactory>

#include <QAbstractItemView>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QFont>
#include <QFrame>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

class MathSymbolsPage : public AbstractSettingsPage
{
    Q_OBJECT
public:
    explicit MathSymbolsPage(
        QObject *parent, const KPluginMetaData &data = KPluginMetaData())
        : AbstractSettingsPage(parent, data)
    {
        // The settings fields must remain literal even when their content
        // resembles a configured input sequence.
        widget()->setProperty("mathomSecondaryLanguageDisabled", true);

        auto *layout = new QVBoxLayout(widget());
        auto *description = new QLabel(
            i18n("Choose an input trigger and a personal word or sequence "
                 "for each mathematical symbol. French words are suggested "
                 "by default; all shortcuts can be changed or cleared."), widget());
        description->setWordWrap(true);
        layout->addWidget(description);

        auto *triggerRow = new QHBoxLayout();
        auto *triggerLabel = new QLabel(i18n("Mathematical input trigger:"), widget());
        m_trigger = new QLineEdit(widget());
        m_trigger->setMaximumWidth(150);
        m_trigger->setPlaceholderText(i18n("Example: £"));
        m_trigger->setToolTip(i18n("This character completes an alias, e.g. racine£."));
        triggerRow->addWidget(triggerLabel);
        triggerRow->addWidget(m_trigger);
        triggerRow->addStretch();
        layout->addLayout(triggerRow);

        auto *expressions = new QGroupBox(i18n("Scientific expressions"), widget());
        auto *expressionForm = new QFormLayout(expressions);

        m_fractionStyle = new QComboBox(expressions);
        m_fractionStyle->addItem(i18n("Vertical (stacked)"), true);
        m_fractionStyle->addItem(i18n("Diagonal (compact)"), false);
        expressionForm->addRow(i18n("Fraction display:"), m_fractionStyle);

        m_fractionSeparator = new QLineEdit(expressions);
        m_fractionSeparator->setPlaceholderText(QStringLiteral("/"));
        m_fractionSeparator->setToolTip(i18n("Input placed between numerator and denominator; / by default."));
        expressionForm->addRow(i18n("Fraction separator:"), m_fractionSeparator);

        m_exponentKeyword = new QLineEdit(expressions);
        m_exponentKeyword->setPlaceholderText(QStringLiteral("exposant"));
        expressionForm->addRow(i18n("Exponent keyword:"), m_exponentKeyword);

        m_subscriptKeyword = new QLineEdit(expressions);
        m_subscriptKeyword->setPlaceholderText(QStringLiteral("indice"));
        expressionForm->addRow(i18n("Subscript keyword:"), m_subscriptKeyword);

        auto *formatNote = new QLabel(
            i18n("Stacked fractions are supported in rich-text Mathoms. "
                 "Plain-text Mathoms and single-line fields use diagonal Unicode fractions "
                 "to keep their content editable and persistent."), expressions);
        formatNote->setWordWrap(true);
        expressionForm->addRow(formatNote);
        m_sciencePreview = new QLabel(expressions);
        m_sciencePreview->setWordWrap(true);
        expressionForm->addRow(m_sciencePreview);
        layout->addWidget(expressions);

        m_conflictPanel = new QFrame(widget());
        m_conflictPanel->setFrameShape(QFrame::StyledPanel);
        auto *conflictLayout = new QHBoxLayout(m_conflictPanel);
        auto *conflictIcon = new QLabel(m_conflictPanel);
        conflictIcon->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-warning")).pixmap(24, 24));
        m_conflictText = new QLabel(m_conflictPanel);
        m_conflictText->setWordWrap(true);
        conflictLayout->addWidget(conflictIcon);
        conflictLayout->addWidget(m_conflictText, 1);
        layout->addWidget(m_conflictPanel);
        m_conflictPanel->hide();

        m_search = new QLineEdit(widget());
        m_search->setPlaceholderText(i18n("Find a symbol, a name or an input word..."));
        m_search->setClearButtonEnabled(true);
        layout->addWidget(m_search);

        m_tree = new QTreeWidget(widget());
        m_tree->setColumnCount(3);
        m_tree->setHeaderLabels({i18n("Symbol"), i18n("Meaning"), i18n("Input word or sequence")});
        m_tree->setRootIsDecorated(true);
        m_tree->setUniformRowHeights(true);
        m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
        m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_tree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
        m_tree->header()->resizeSection(2, 190);
        layout->addWidget(m_tree, 1);

        QString currentCategory;
        QTreeWidgetItem *category = nullptr;
        const QVector<MathSymbolDefinition> definitions = MathSymbolCatalog::entries();
        for (const MathSymbolDefinition &definition : definitions) {
            if (!category || currentCategory != definition.category) {
                currentCategory = definition.category;
                category = new QTreeWidgetItem(m_tree, {definition.category});
                category->setFirstColumnSpanned(true);
                category->setFlags(Qt::ItemIsEnabled);
                QFont font = category->font(0);
                font.setBold(true);
                category->setFont(0, font);
            }
            auto *item = new QTreeWidgetItem(category,
                {definition.symbol, definition.label, definition.defaultShortcut});
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setData(0, Qt::UserRole, definition.configKey());
        }
        m_tree->expandAll();

        m_selectedLabel = new QLabel(i18n("Select a symbol to edit its input word."), widget());
        layout->addWidget(m_selectedLabel);
        m_shortcut = new QLineEdit(widget());
        m_shortcut->setPlaceholderText(
            i18n("Any length: pi, racine, inferieurouegal..."));
        m_shortcut->setClearButtonEnabled(true);
        m_shortcut->setEnabled(false);
        layout->addWidget(m_shortcut);
        m_preview = new QLabel(widget());
        m_preview->setWordWrap(true);
        layout->addWidget(m_preview);

        connect(m_trigger, &QLineEdit::textChanged, this, [this]() {
            markAsChanged();
            updatePreview();
            updateSciencePreview();
            updateWarnings();
        });
        connect(m_fractionStyle, &QComboBox::currentIndexChanged, this, [this]() {
            markAsChanged();
            updateSciencePreview();
        });
        for (QLineEdit *editor : {m_fractionSeparator, m_exponentKeyword, m_subscriptKeyword}) {
            connect(editor, &QLineEdit::textChanged, this, [this]() {
                markAsChanged();
                updateSciencePreview();
                updateWarnings();
            });
        }
        connect(m_search, &QLineEdit::textChanged, this,
                [this](const QString &value) { filterCatalog(value); });
        connect(m_tree, &QTreeWidget::currentItemChanged, this,
                [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                    const QSignalBlocker blocker(m_shortcut);
                    const bool selected = item && item->parent();
                    m_shortcut->setEnabled(selected);
                    m_shortcut->setText(selected ? item->text(2) : QString());
                    m_selectedLabel->setText(selected
                        ? i18n("Input word for %1 (%2):", item->text(0), item->text(1))
                        : i18n("Select a symbol to edit its input word."));
                    updatePreview();
                });
        connect(m_shortcut, &QLineEdit::textChanged, this,
                [this](const QString &value) {
                    auto *item = m_tree->currentItem();
                    if (!item || !item->parent())
                        return;
                    item->setText(2, value);
                    updatePreview();
                    updateWarnings();
                    markAsChanged();
                });

        load();
    }

    void load() override
    {
        const MathSymbolPreferences settings = MathSymbolSettings::load();
        {
            const QSignalBlocker blocker(m_trigger);
            m_trigger->setText(settings.trigger);
        }
        {
            const QSignalBlocker blocker(m_fractionStyle);
            m_fractionStyle->setCurrentIndex(settings.scientific.verticalFractions ? 0 : 1);
        }
        {
            const QSignalBlocker a(m_fractionSeparator);
            const QSignalBlocker b(m_exponentKeyword);
            const QSignalBlocker c(m_subscriptKeyword);
            m_fractionSeparator->setText(settings.scientific.fractionSeparator);
            m_exponentKeyword->setText(settings.scientific.exponentKeyword);
            m_subscriptKeyword->setText(settings.scientific.subscriptKeyword);
        }
        for (const MathSymbolShortcut &mapping : settings.shortcuts) {
            if (QTreeWidgetItem *item = symbolItem(mapping.symbol))
                item->setText(2, mapping.input);
        }
        syncSelection();
        updatePreview();
        updateSciencePreview();
        updateWarnings();
        setNeedsSave(false);
    }

    void save() override
    {
        MathSymbolPreferences settings;
        settings.trigger = m_trigger->text();
        settings.scientific.verticalFractions = m_fractionStyle->currentData().toBool();
        settings.scientific.fractionSeparator = m_fractionSeparator->text();
        settings.scientific.exponentKeyword = m_exponentKeyword->text();
        settings.scientific.subscriptKeyword = m_subscriptKeyword->text();
        for (int categoryIndex = 0; categoryIndex < m_tree->topLevelItemCount(); ++categoryIndex) {
            auto *category = m_tree->topLevelItem(categoryIndex);
            for (int index = 0; index < category->childCount(); ++index) {
                auto *item = category->child(index);
                settings.shortcuts.append({item->text(0), item->text(2)});
            }
        }
        // Warnings are advisory: preserve the user's decision, even on conflict.
        MathSymbolSettings::save(settings);
        setNeedsSave(false);
    }

    void defaults() override
    {
        const MathSymbolPreferences settings = MathSymbolSettings::defaults();
        m_trigger->setText(settings.trigger);
        m_fractionStyle->setCurrentIndex(settings.scientific.verticalFractions ? 0 : 1);
        m_fractionSeparator->setText(settings.scientific.fractionSeparator);
        m_exponentKeyword->setText(settings.scientific.exponentKeyword);
        m_subscriptKeyword->setText(settings.scientific.subscriptKeyword);
        for (const MathSymbolShortcut &mapping : settings.shortcuts) {
            if (QTreeWidgetItem *item = symbolItem(mapping.symbol))
                item->setText(2, mapping.input);
        }
        syncSelection();
        updatePreview();
        updateSciencePreview();
        updateWarnings();
        markAsChanged();
    }

    void cancel() override { load(); }

private:
    QTreeWidgetItem *symbolItem(const QString &symbol) const
    {
        for (int categoryIndex = 0; categoryIndex < m_tree->topLevelItemCount(); ++categoryIndex) {
            auto *category = m_tree->topLevelItem(categoryIndex);
            for (int index = 0; index < category->childCount(); ++index) {
                if (category->child(index)->text(0) == symbol)
                    return category->child(index);
            }
        }
        return nullptr;
    }

    void syncSelection()
    {
        const QSignalBlocker blocker(m_shortcut);
        auto *item = m_tree->currentItem();
        m_shortcut->setEnabled(item && item->parent());
        m_shortcut->setText(item && item->parent() ? item->text(2) : QString());
    }

    void filterCatalog(const QString &value)
    {
        const QString needle = value.trimmed();
        for (int categoryIndex = 0; categoryIndex < m_tree->topLevelItemCount(); ++categoryIndex) {
            auto *category = m_tree->topLevelItem(categoryIndex);
            const bool categoryMatches = category->text(0).contains(needle, Qt::CaseInsensitive);
            bool visibleCategory = false;
            for (int index = 0; index < category->childCount(); ++index) {
                auto *item = category->child(index);
                const bool visible = categoryMatches
                    || item->text(0).contains(needle, Qt::CaseInsensitive)
                    || item->text(1).contains(needle, Qt::CaseInsensitive)
                    || item->text(2).contains(needle, Qt::CaseInsensitive);
                item->setHidden(!visible);
                visibleCategory |= visible;
            }
            category->setHidden(!visibleCategory);
            if (!needle.isEmpty() && visibleCategory)
                category->setExpanded(true);
        }
    }

    void updatePreview()
    {
        auto *item = m_tree->currentItem();
        if (!item || !item->parent()) {
            m_preview->setText(i18n("Select a symbol to preview its input sequence."));
            return;
        }
        if (m_shortcut->text().isEmpty()) {
            m_preview->setText(i18n("No input word assigned (this symbol is disabled for direct typing)."));
        } else if (m_trigger->text().isEmpty()) {
            m_preview->setText(i18n("Choose an input trigger to enable direct typing."));
        } else {
            m_preview->setText(i18n("Input preview: %1 → %2",
                                     m_shortcut->text() + m_trigger->text(), item->text(0)));
        }
    }

    void updateSciencePreview()
    {
        const QString trig = m_trigger->text();
        if (trig.isEmpty()) {
            m_sciencePreview->setText(i18n("Choose the mathematical trigger to activate scientific input."));
            return;
        }
        const QString exampleFraction = QStringLiteral("8") + m_fractionSeparator->text()
            + QStringLiteral("153") + trig;
        const QString exampleExponent = QStringLiteral("x10") + m_exponentKeyword->text() + trig;
        const QString exampleSubscript = QStringLiteral("H2") + m_subscriptKeyword->text() + trig
            + QStringLiteral("O");
        m_sciencePreview->setText(i18n("Examples: %1 (fraction), %2 (exponent), %3 (subscript)",
            exampleFraction, exampleExponent, exampleSubscript));
    }

    void updateWarnings()
    {
        QStringList warnings;
        const QString trigger = m_trigger->text();
        if (!trigger.isEmpty()) {
            const auto languages = SecondaryLanguageSettings::load();
            for (const SecondaryLanguageSelection &language : languages) {
                if (language.trigger.isEmpty())
                    continue;
                if (language.trigger == trigger
                    || language.trigger.startsWith(trigger)
                    || trigger.startsWith(language.trigger)) {
                    const auto profile = SecondaryLanguageCatalog::profileById(language.id);
                    warnings.append(i18n("Trigger \"%1\" overlaps secondary language %2 (\"%3\").",
                        trigger, profile.isValid() ? profile.name : language.id, language.trigger));
                }
            }
        }

        if (!m_exponentKeyword->text().isEmpty()
            && m_exponentKeyword->text() == m_subscriptKeyword->text()) {
            warnings.append(i18n("Exponent and subscript share the same keyword; numeric input is ambiguous."));
        }
        QHash<QString, QStringList> aliases;
        for (int categoryIndex = 0; categoryIndex < m_tree->topLevelItemCount(); ++categoryIndex) {
            auto *category = m_tree->topLevelItem(categoryIndex);
            for (int index = 0; index < category->childCount(); ++index) {
                auto *item = category->child(index);
                if (!item->text(2).isEmpty())
                    aliases[item->text(2)].append(item->text(0));
            }
        }
        for (auto it = aliases.cbegin(); it != aliases.cend(); ++it) {
            if (it.value().size() > 1) {
                warnings.append(i18n("Input word \"%1\" is assigned to %2.",
                    it.key(), it.value().join(QStringLiteral(", "))));
            }
        }
        // Explicit aliases may clash with generated fraction/exponent/indice
        // commands. The warning is advisory; the engine handles ambiguity.
        const QString fractionPattern = m_fractionSeparator->text().isEmpty()
            ? QString() : QStringLiteral("^[+-]?[0-9]+")
                  + QRegularExpression::escape(m_fractionSeparator->text())
                  + QStringLiteral("[0-9]+$");
        const QString exponentPattern = m_exponentKeyword->text().isEmpty()
            ? QString() : QStringLiteral("^[+-]?[0-9]+")
                  + QRegularExpression::escape(m_exponentKeyword->text())
                  + QStringLiteral("$");
        const QString subscriptPattern = m_subscriptKeyword->text().isEmpty()
            ? QString() : QStringLiteral("^[+-]?[0-9]+")
                  + QRegularExpression::escape(m_subscriptKeyword->text())
                  + QStringLiteral("$");
        for (auto it = aliases.cbegin(); it != aliases.cend(); ++it) {
            const auto patternMatches = [&it](const QString &pattern) {
                return !pattern.isEmpty()
                    && QRegularExpression(pattern).match(it.key()).hasMatch();
            };
            if (patternMatches(fractionPattern) || patternMatches(exponentPattern)
                || patternMatches(subscriptPattern)) {
                warnings.append(i18n("Input word \"%1\" overlaps a scientific expression; "
                                     "you can keep it, but the result may be ambiguous.", it.key()));
            }
        }
        warnings.sort();
        if (warnings.isEmpty()) {
            m_conflictText->clear();
            m_conflictPanel->hide();
            return;
        }
        m_conflictText->setText(
            i18n("Potential input conflicts (you may keep your choices):")
            + QStringLiteral("\n") + warnings.join(QStringLiteral("\n")));
        m_conflictPanel->show();
    }

    QLineEdit *m_trigger = nullptr;
    QComboBox *m_fractionStyle = nullptr;
    QLineEdit *m_fractionSeparator = nullptr;
    QLineEdit *m_exponentKeyword = nullptr;
    QLineEdit *m_subscriptKeyword = nullptr;
    QLabel *m_sciencePreview = nullptr;
    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_selectedLabel = nullptr;
    QLineEdit *m_shortcut = nullptr;
    QLabel *m_preview = nullptr;
    QFrame *m_conflictPanel = nullptr;
    QLabel *m_conflictText = nullptr;
};

K_PLUGIN_CLASS_WITH_JSON(MathSymbolsPage, "mathom_config_math_symbols.json")
#include "mathom_config_math_symbols.moc"
