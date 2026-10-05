#include "secondarylanguagespage.h"

#include "secondarylanguagecatalog.h"
#include "secondarylanguagesettings.h"
#include "secondarylanguagevalidator.h"

#include <KLocalizedString>

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

SecondaryLanguagesPage::SecondaryLanguagesPage(
    QObject *parent,
    const KPluginMetaData &data)
    : AbstractSettingsPage(parent, data)
{
    auto *mainLayout =
        new QVBoxLayout(widget());

    auto *description =
        new QLabel(
            i18n(
                "Ajoutez les langues secondaires que vous utilisez. "
                "Chaque langue possède son propre déclencheur de saisie. "
                "Le déclencheur proposé par défaut est « xx »."),
            widget());

    description->setWordWrap(true);
    mainLayout->addWidget(description);

    auto *scrollArea =
        new QScrollArea(widget());

    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *rowsWidget =
        new QWidget(scrollArea);

    m_rowsLayout =
        new QVBoxLayout(rowsWidget);

    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(8);
    m_rowsLayout->addStretch();

    scrollArea->setWidget(rowsWidget);
    mainLayout->addWidget(scrollArea, 1);

    m_conflictLabel =
        new QLabel(widget());

    m_conflictLabel->setWordWrap(true);
    m_conflictLabel->hide();

    mainLayout->addWidget(m_conflictLabel);

    m_addButton =
        new QPushButton(
            QStringLiteral("+"),
            widget());

    m_addButton->setMinimumHeight(52);
    m_addButton->setToolTip(
        i18n("Ajouter une langue secondaire"));

    mainLayout->addWidget(m_addButton);

    connect(
        m_addButton,
        &QPushButton::clicked,
        this,
        &SecondaryLanguagesPage::chooseLanguage);

    load();
}

void SecondaryLanguagesPage::clearRows()
{
    for (const LanguageRow &row : m_rows) {
        if (row.widget)
            row.widget->deleteLater();
    }

    m_rows.clear();
}

void SecondaryLanguagesPage::load()
{
    clearRows();

    const QVector<SecondaryLanguageSelection> selections =
        SecondaryLanguageSettings::load();

    for (const SecondaryLanguageSelection &selection :
         selections) {
        addLanguage(
            selection.id,
            selection.trigger);
    }

    updateAddButton();
    updateConflictWarning();

    setNeedsSave(false);
}

void SecondaryLanguagesPage::save()
{
    SecondaryLanguageSettings::save(
        currentSelections());

    setNeedsSave(false);
}

void SecondaryLanguagesPage::defaults()
{
    clearRows();

    updateAddButton();
    updateConflictWarning();

    markAsChanged();
}

void SecondaryLanguagesPage::cancel()
{
    load();
}

void SecondaryLanguagesPage::chooseLanguage()
{
    const QVector<SecondaryLanguageProfile> profiles =
        SecondaryLanguageCatalog::profiles();

    QVector<SecondaryLanguageProfile> available;

    for (const SecondaryLanguageProfile &profile :
         profiles) {

        bool alreadyPresent = false;

        for (const LanguageRow &row : m_rows) {
            if (row.id == profile.id) {
                alreadyPresent = true;
                break;
            }
        }

        if (!alreadyPresent)
            available.append(profile);
    }

    if (available.isEmpty())
        return;

    QStringList names;

    for (const SecondaryLanguageProfile &profile :
         available) {
        names.append(profile.name);
    }

    bool ok = false;

    const QString selectedName =
        QInputDialog::getItem(
            widget(),
            i18n("Ajouter une langue"),
            i18n("Langue :"),
            names,
            0,
            false,
            &ok);

    if (!ok || selectedName.isEmpty())
        return;

    for (const SecondaryLanguageProfile &profile :
         available) {

        if (profile.name != selectedName)
            continue;

        addLanguage(
            profile.id,
            profile.defaultTrigger);

        markAsChanged();
        break;
    }

    updateAddButton();
    updateConflictWarning();
}

void SecondaryLanguagesPage::addLanguage(
    const QString &id,
    const QString &trigger)
{
    const SecondaryLanguageProfile profile =
        SecondaryLanguageCatalog::profileById(id);

    if (!profile.isValid())
        return;

    for (const LanguageRow &existing : m_rows) {
        if (existing.id == id)
            return;
    }

    auto *rowWidget =
        new QWidget(widget());

    auto *layout =
        new QHBoxLayout(rowWidget);

    layout->setContentsMargins(0, 0, 0, 0);

    auto *name =
        new QLabel(
            profile.name,
            rowWidget);

    name->setMinimumWidth(160);

    auto *triggerLabel =
        new QLabel(
            i18n("Déclencheur :"),
            rowWidget);

    auto *triggerEdit =
        new QLineEdit(
            trigger,
            rowWidget);

    triggerEdit->setMaximumWidth(120);

    auto *removeButton =
        new QPushButton(
            i18n("Supprimer"),
            rowWidget);

    layout->addWidget(name);
    layout->addStretch();
    layout->addWidget(triggerLabel);
    layout->addWidget(triggerEdit);
    layout->addWidget(removeButton);

    /*
     * Insérer avant le stretch placé en fin de layout.
     */
    m_rowsLayout->insertWidget(
        m_rowsLayout->count() - 1,
        rowWidget);

    m_rows.append({
        id,
        rowWidget,
        triggerEdit
    });

    connect(
        triggerEdit,
        &QLineEdit::textChanged,
        this,
        [this]() {
            updateConflictWarning();
            markAsChanged();
        });

    connect(
        removeButton,
        &QPushButton::clicked,
        this,
        [this, id]() {
            removeLanguage(id);
        });
}

void SecondaryLanguagesPage::removeLanguage(
    const QString &id)
{
    for (int index = 0;
         index < m_rows.size();
         ++index) {

        if (m_rows.at(index).id != id)
            continue;

        QWidget *rowWidget =
            m_rows.at(index).widget;

        m_rows.removeAt(index);

        if (rowWidget)
            rowWidget->deleteLater();

        markAsChanged();

        updateAddButton();
        updateConflictWarning();

        return;
    }
}

QVector<SecondaryLanguageSelection>
SecondaryLanguagesPage::currentSelections() const
{
    QVector<SecondaryLanguageSelection> result;

    for (const LanguageRow &row : m_rows) {

        if (!row.trigger)
            continue;

        result.append({
            row.id,
            row.trigger->text()
        });
    }

    return result;
}

void SecondaryLanguagesPage::updateAddButton()
{
    const int availableCount =
        SecondaryLanguageCatalog::profiles().size();

    m_addButton->setEnabled(
        m_rows.size() < availableCount);
}

void SecondaryLanguagesPage::updateConflictWarning()
{
    const QVector<SecondaryLanguageConflict> conflicts =
        SecondaryLanguageValidator::conflicts(
            currentSelections());

    if (conflicts.isEmpty()) {
        m_conflictLabel->clear();
        m_conflictLabel->hide();
        return;
    }

    QStringList messages;

    for (const SecondaryLanguageConflict &conflict :
         conflicts) {

        const SecondaryLanguageProfile first =
            SecondaryLanguageCatalog::profileById(
                conflict.firstLanguageId);

        const SecondaryLanguageProfile second =
            SecondaryLanguageCatalog::profileById(
                conflict.secondLanguageId);

        const QString firstName =
            first.isValid()
            ? first.name
            : conflict.firstLanguageId;

        const QString secondName =
            second.isValid()
            ? second.name
            : conflict.secondLanguageId;

        if (conflict.type
            == SecondaryLanguageConflictType::SameTrigger) {

            messages.append(
                i18n(
                    "Conflit entre %1 et %2 pour « %3 ».",
                    firstName,
                    secondName,
                    conflict.sequence));

        } else {

            messages.append(
                i18n(
                    "Déclencheurs incompatibles : "
                    "%1 utilise « %2 » et %3 utilise « %4 ».",
                    firstName,
                    conflict.firstTrigger,
                    secondName,
                    conflict.secondTrigger));
        }
    }

    m_conflictLabel->setText(
        QStringLiteral("⚠ ")
        + messages.join(
            QStringLiteral("\n")));

    m_conflictLabel->show();
}
