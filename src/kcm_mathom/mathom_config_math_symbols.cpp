#include "settings.h"
#include "global.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

class MathSymbolsPage : public AbstractSettingsPage
{
    Q_OBJECT

public:
    explicit MathSymbolsPage(
        QObject *parent,
        const KPluginMetaData &data = KPluginMetaData())
        : AbstractSettingsPage(parent, data)
    {
        auto *layout = new QVBoxLayout(widget());

        auto *description = new QLabel(
            i18n("Configure mathematical symbols "
                 "and their personalized input shortcuts."),
            widget());

        description->setWordWrap(true);
        layout->addWidget(description);

        auto *form = new QFormLayout();

        m_trigger = new QLineEdit(widget());
        m_trigger->setMaximumWidth(120);

        m_trigger->setPlaceholderText(
            i18n("Example: £"));

        m_trigger->setToolTip(
            i18n("Character used to trigger mathematical "
                 "symbol substitutions."));

        form->addRow(
            i18n("Mathematical input trigger:"),
            m_trigger);

        layout->addLayout(form);

        auto *information = new QLabel(
            i18n("Each mathematical symbol will be able "
                 "to use a personalized input shortcut. "
                 "For example, p£ can insert π."),
            widget());

        information->setWordWrap(true);
        layout->addWidget(information);

        layout->addStretch();

        connect(
            m_trigger,
            &QLineEdit::textChanged,
            this,
            &KCModule::markAsChanged);

        load();
    }

    void load() override
    {
        if (!Global::config())
            return;

        KConfigGroup group(
            Global::config(),
            QStringLiteral("Mathematical Symbols"));

        m_trigger->setText(
            group.readEntry(
                QStringLiteral("trigger"),
                QString()));

        setNeedsSave(false);
    }

    void save() override
    {
        if (!Global::config())
            return;

        KConfigGroup group(
            Global::config(),
            QStringLiteral("Mathematical Symbols"));

        group.writeEntry(
            QStringLiteral("trigger"),
            m_trigger->text());

        Global::config()->sync();

        setNeedsSave(false);
    }

    void defaults() override
    {
        m_trigger->clear();
        markAsChanged();
    }

    void cancel() override
    {
        load();
    }

private:
    QLineEdit *m_trigger = nullptr;
};

K_PLUGIN_CLASS_WITH_JSON(
    MathSymbolsPage,
    "mathom_config_math_symbols.json")

#include "mathom_config_math_symbols.moc"
