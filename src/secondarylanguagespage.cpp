#include "secondarylanguagespage.h"

#include "secondarylanguagecatalog.h"
#include "secondarylanguagesettings.h"
#include "secondarylanguagevalidator.h"

#include <KLocalizedString>

#include <QFont>
#include <QFrame>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSize>
#include <QStyle>
#include <QToolButton>
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

    /*
     * Etat vide : présenté comme un véritable écran d'accueil
     * plutôt qu'une grande zone blanche.
     */
    m_emptyStateWidget =
        new QWidget(widget());

    auto *emptyLayout =
        new QVBoxLayout(m_emptyStateWidget);

    emptyLayout->setContentsMargins(
        24,
        24,
        24,
        24);

    emptyLayout->setSpacing(10);
    emptyLayout->setAlignment(
        Qt::AlignCenter);

    auto *emptyIcon =
        new QLabel(m_emptyStateWidget);

    emptyIcon->setPixmap(
        QIcon::fromTheme(
            QStringLiteral(
                "preferences-desktop-locale"))
            .pixmap(QSize(48, 48)));

    emptyIcon->setAlignment(
        Qt::AlignCenter);

    auto *emptyTitle =
        new QLabel(
            i18n(
                "Aucune langue secondaire configurée"),
            m_emptyStateWidget);

    QFont emptyTitleFont =
        emptyTitle->font();

    emptyTitleFont.setBold(true);
    emptyTitleFont.setPointSize(
        emptyTitleFont.pointSize() + 2);

    emptyTitle->setFont(
        emptyTitleFont);

    emptyTitle->setAlignment(
        Qt::AlignCenter);

    auto *emptyDescription =
        new QLabel(
            i18n(
                "Ajoutez une langue pour utiliser rapidement "
                "ses caractères spécifiques pendant la prise de notes."),
            m_emptyStateWidget);

    emptyDescription->setWordWrap(true);
    emptyDescription->setAlignment(
        Qt::AlignCenter);

    emptyDescription->setMaximumWidth(
        480);

    emptyLayout->addWidget(
        emptyIcon);

    emptyLayout->addWidget(
        emptyTitle);

    emptyLayout->addWidget(
        emptyDescription);

    mainLayout->addWidget(
        m_emptyStateWidget,
        1);

    /*
     * Liste des langues configurées.
     */
    m_scrollArea =
        new QScrollArea(widget());

    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(
        QFrame::NoFrame);

    auto *rowsWidget =
        new QWidget(m_scrollArea);

    m_rowsLayout =
        new QVBoxLayout(rowsWidget);

    m_rowsLayout->setContentsMargins(
        0,
        4,
        0,
        4);

    m_rowsLayout->setSpacing(10);
    m_rowsLayout->addStretch();

    m_scrollArea->setWidget(rowsWidget);

    mainLayout->addWidget(
        m_scrollArea,
        1);

    /*
     * Bandeau de conflit.
     *
     * Aucun code couleur imposé : les couleurs utilisées sont celles
     * du thème Qt/KDE de l'utilisateur.
     */
    m_conflictPanel =
        new QFrame(widget());

    m_conflictPanel->setFrameShape(
        QFrame::StyledPanel);

    m_conflictPanel->setStyleSheet(
        QStringLiteral(
            "QFrame {"
            " border: 1px solid palette(highlight);"
            " border-radius: 6px;"
            " background: palette(alternate-base);"
            "}"));

    auto *conflictLayout =
        new QHBoxLayout(m_conflictPanel);

    conflictLayout->setContentsMargins(
        12,
        10,
        12,
        10);

    conflictLayout->setSpacing(10);

    auto *conflictIcon =
        new QLabel(m_conflictPanel);

    conflictIcon->setPixmap(
        QIcon::fromTheme(
            QStringLiteral("dialog-warning"))
            .pixmap(QSize(24, 24)));

    conflictIcon->setAlignment(
        Qt::AlignTop | Qt::AlignHCenter);

    m_conflictLabel =
        new QLabel(m_conflictPanel);

    m_conflictLabel->setWordWrap(true);

    conflictLayout->addWidget(
        conflictIcon);

    conflictLayout->addWidget(
        m_conflictLabel,
        1);

    m_conflictPanel->hide();

    mainLayout->addWidget(
        m_conflictPanel);

    /*
     * Action d'ajout volontairement compacte et explicite.
     * Contrairement à l'ancienne grande barre avec un simple "+",
     * elle doit être immédiatement identifiable comme une action.
     */
    auto *addButtonLayout =
        new QHBoxLayout();

    addButtonLayout->setContentsMargins(
        0,
        10,
        0,
        4);

    addButtonLayout->addStretch();

    m_addButton =
        new QPushButton(
            QIcon::fromTheme(
                QStringLiteral("list-add")),
            i18n("Ajouter une langue"),
            widget());

    m_addButton->setMinimumHeight(42);
    m_addButton->setMinimumWidth(260);
    m_addButton->setIconSize(
        QSize(22, 22));

    m_addButton->setToolTip(
        i18n("Ajouter une langue secondaire"));

    addButtonLayout->addWidget(
        m_addButton);

    addButtonLayout->addStretch();

    mainLayout->addLayout(
        addButtonLayout);

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

    QDialog dialog(widget());

    dialog.setWindowTitle(
        i18n("Ajouter une langue"));

    dialog.setMinimumSize(
        480,
        420);

    auto *layout =
        new QVBoxLayout(&dialog);

    layout->setContentsMargins(
        18,
        18,
        18,
        18);

    layout->setSpacing(12);

    /*
     * En-tête.
     */
    auto *title =
        new QLabel(
            i18n("Ajouter une langue secondaire"),
            &dialog);

    QFont titleFont =
        title->font();

    titleFont.setBold(true);
    titleFont.setPointSize(
        titleFont.pointSize() + 2);

    title->setFont(titleFont);

    layout->addWidget(title);

    auto *description =
        new QLabel(
            i18n(
                "Choisissez la langue dont vous souhaitez "
                "utiliser rapidement les caractères spécifiques."),
            &dialog);

    description->setWordWrap(true);

    layout->addWidget(description);

    /*
     * Recherche.
     */
    auto *search =
        new QLineEdit(&dialog);

    search->setPlaceholderText(
        i18n("Rechercher une langue…"));

    search->setClearButtonEnabled(true);

    layout->addWidget(search);

    /*
     * Liste des langues disponibles.
     */
    auto *languageList =
        new QListWidget(&dialog);

    languageList->setSelectionMode(
        QAbstractItemView::SingleSelection);

    languageList->setSpacing(2);

    const QIcon languageIcon =
        QIcon::fromTheme(
            QStringLiteral(
                "preferences-desktop-locale"));

    for (const SecondaryLanguageProfile &profile :
         available) {

        auto *item =
            new QListWidgetItem(
                languageIcon,
                profile.name,
                languageList);

        item->setData(
            Qt::UserRole,
            profile.id);

        item->setToolTip(
            i18n(
                "%1 — déclencheur par défaut : %2",
                profile.name,
                profile.defaultTrigger));
    }

    layout->addWidget(
        languageList,
        1);

    /*
     * Boutons.
     */
    auto *buttons =
        new QDialogButtonBox(
            QDialogButtonBox::Ok
            | QDialogButtonBox::Cancel,
            &dialog);

    auto *addButton =
        buttons->button(
            QDialogButtonBox::Ok);

    addButton->setText(
        i18n("Ajouter"));

    addButton->setIcon(
        QIcon::fromTheme(
            QStringLiteral("list-add")));

    addButton->setEnabled(false);

    layout->addWidget(buttons);

    connect(
        buttons,
        &QDialogButtonBox::accepted,
        &dialog,
        &QDialog::accept);

    connect(
        buttons,
        &QDialogButtonBox::rejected,
        &dialog,
        &QDialog::reject);

    /*
     * Activer "Ajouter" uniquement lorsqu'une langue
     * est réellement sélectionnée.
     */
    connect(
        languageList,
        &QListWidget::itemSelectionChanged,
        &dialog,
        [languageList, addButton]() {
            addButton->setEnabled(
                languageList->currentItem()
                != nullptr);
        });

    /*
     * Double-clic = ajout immédiat.
     */
    connect(
        languageList,
        &QListWidget::itemDoubleClicked,
        &dialog,
        [&dialog](
            QListWidgetItem *,
            int) {
            dialog.accept();
        });

    /*
     * Filtrage instantané.
     */
    connect(
        search,
        &QLineEdit::textChanged,
        &dialog,
        [languageList, addButton](
            const QString &text) {

            const QString filter =
                text.trimmed();

            QListWidgetItem *firstVisible =
                nullptr;

            for (int index = 0;
                 index < languageList->count();
                 ++index) {

                QListWidgetItem *item =
                    languageList->item(index);

                const bool visible =
                    filter.isEmpty()
                    || item->text().contains(
                        filter,
                        Qt::CaseInsensitive);

                item->setHidden(!visible);

                if (visible
                    && !firstVisible) {
                    firstVisible = item;
                }
            }

            /*
             * Une recherche modifie le contexte :
             * on évite de conserver une sélection devenue masquée.
             */
            if (languageList->currentItem()
                && languageList
                       ->currentItem()
                       ->isHidden()) {
                languageList->clearSelection();
                languageList->setCurrentItem(
                    nullptr);
            }

            addButton->setEnabled(
                languageList->currentItem()
                != nullptr);
        });

    /*
     * Sélectionner naturellement la première langue,
     * tout en laissant la zone de recherche recevoir le focus.
     */
    if (languageList->count() > 0) {
        languageList->setCurrentRow(0);
        addButton->setEnabled(true);
    }

    search->setFocus();

    if (dialog.exec()
        != QDialog::Accepted) {
        return;
    }

    QListWidgetItem *selectedItem =
        languageList->currentItem();

    if (!selectedItem)
        return;

    const QString selectedId =
        selectedItem
            ->data(Qt::UserRole)
            .toString();

    for (const SecondaryLanguageProfile &profile :
         available) {

        if (profile.id != selectedId)
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
        new QFrame(widget());

    rowWidget->setFrameShape(
        QFrame::StyledPanel);

    rowWidget->setFrameShadow(
        QFrame::Plain);

    /*
     * Etat normal / conflit.
     *
     * La propriété dynamique mathomConflict sera mise à jour
     * par updateConflictWarning().
     */
    rowWidget->setProperty(
        "mathomConflict",
        false);

    rowWidget->setStyleSheet(
        QStringLiteral(
            "QFrame {"
            " border: 1px solid palette(mid);"
            " border-radius: 6px;"
            "}"
            "QFrame[mathomConflict=\"true\"] {"
            " border: 2px solid palette(highlight);"
            " background: palette(alternate-base);"
            "}"));

    auto *layout =
        new QHBoxLayout(rowWidget);

    layout->setContentsMargins(
        12,
        10,
        10,
        10);

    layout->setSpacing(10);

    auto *name =
        new QLabel(
            profile.name,
            rowWidget);

    QFont nameFont =
        name->font();

    nameFont.setBold(true);
    name->setFont(nameFont);

    auto *triggerLabel =
        new QLabel(
            i18n("Déclencheur"),
            rowWidget);

    auto *triggerEdit =
        new QLineEdit(
            trigger,
            rowWidget);

    triggerEdit->setMinimumWidth(90);
    triggerEdit->setMaximumWidth(130);
    triggerEdit->setAlignment(
        Qt::AlignCenter);

    triggerEdit->setPlaceholderText(
        QStringLiteral("xx"));

    triggerEdit->setClearButtonEnabled(
        true);

    auto *removeButton =
        new QToolButton(rowWidget);

    removeButton->setIcon(
        QIcon::fromTheme(
            QStringLiteral("edit-delete")));

    removeButton->setToolTip(
        i18n(
            "Supprimer %1",
            profile.name));

    removeButton->setAccessibleName(
        i18n(
            "Supprimer %1",
            profile.name));

    removeButton->setAutoRaise(true);

    layout->addWidget(
        name,
        1);

    layout->addWidget(
        triggerLabel);

    layout->addWidget(
        triggerEdit);

    layout->addWidget(
        removeButton);

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

    const bool isEmpty =
        m_rows.isEmpty();

    m_addButton->setEnabled(
        m_rows.size() < availableCount);

    m_addButton->setText(
        isEmpty
        ? i18n("Ajouter une langue")
        : i18n("Ajouter une autre langue"));

    if (m_emptyStateWidget)
        m_emptyStateWidget->setVisible(isEmpty);

    if (m_scrollArea)
        m_scrollArea->setVisible(!isEmpty);
}

void SecondaryLanguagesPage::updateConflictWarning()
{
    /*
     * Commencer par remettre toutes les cartes dans leur état normal.
     */
    for (const LanguageRow &row : m_rows) {

        if (!row.widget)
            continue;

        row.widget->setProperty(
            "mathomConflict",
            false);

        row.widget->setToolTip(
            QString());

        row.widget->style()->unpolish(
            row.widget);

        row.widget->style()->polish(
            row.widget);
    }

    const QVector<SecondaryLanguageConflict> conflicts =
        SecondaryLanguageValidator::conflicts(
            currentSelections());

    if (conflicts.isEmpty()) {

        m_conflictLabel->clear();

        if (m_conflictPanel)
            m_conflictPanel->hide();

        return;
    }

    QStringList messages;
    QSet<QString> conflictingLanguages;

    for (const SecondaryLanguageConflict &conflict :
         conflicts) {

        conflictingLanguages.insert(
            conflict.firstLanguageId);

        conflictingLanguages.insert(
            conflict.secondLanguageId);

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
                    "%1 et %2 produisent des résultats différents "
                    "avec « %3 ». Modifiez le déclencheur de l'une "
                    "des deux langues.",
                    firstName,
                    secondName,
                    conflict.sequence));

        } else {

            messages.append(
                i18n(
                    "Le déclencheur « %1 » de %2 entre en conflit "
                    "avec « %3 » de %4. Modifiez l'un des deux "
                    "déclencheurs.",
                    conflict.firstTrigger,
                    firstName,
                    conflict.secondTrigger,
                    secondName));
        }
    }

    /*
     * Mettre visuellement en évidence uniquement les langues
     * réellement impliquées.
     */
    for (const LanguageRow &row : m_rows) {

        if (!row.widget)
            continue;

        const bool conflicted =
            conflictingLanguages.contains(
                row.id);

        row.widget->setProperty(
            "mathomConflict",
            conflicted);

        if (conflicted) {
            row.widget->setToolTip(
                i18n(
                    "Cette langue possède un conflit de déclencheur."));
        }

        /*
         * Les propriétés utilisées dans une feuille de style Qt
         * nécessitent un repolish pour être immédiatement visibles.
         */
        row.widget->style()->unpolish(
            row.widget);

        row.widget->style()->polish(
            row.widget);

        row.widget->update();
    }

    m_conflictLabel->setText(
        QStringLiteral("<b>")
        + i18n("Conflit de saisie")
        + QStringLiteral("</b><br>")
        + messages.join(
            QStringLiteral("<br>")));

    if (m_conflictPanel)
        m_conflictPanel->show();
}

