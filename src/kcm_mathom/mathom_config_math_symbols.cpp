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
#include <QFont>
#include <QFrame>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
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
            updateWarnings();
        });
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
        for (const MathSymbolShortcut &mapping : settings.shortcuts) {
            if (QTreeWidgetItem *item = symbolItem(mapping.symbol))
                item->setText(2, mapping.input);
        }
        syncSelection();
        updatePreview();
        updateWarnings();
        setNeedsSave(false);
    }

    void save() override
    {
        MathSymbolPreferences settings;
        settings.trigger = m_trigger->text();
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
        for (const MathSymbolShortcut &mapping : settings.shortcuts) {
            if (QTreeWidgetItem *item = symbolItem(mapping.symbol))
                item->setText(2, mapping.input);
        }
        syncSelection();
        updatePreview();
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
