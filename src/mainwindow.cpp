/**
 * SPDX-FileCopyrightText: (C) 2003 Sébastien Laoût <slaout@linux62.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mainwindow.h"
#include "accessibilitysettings.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
// #include <QDesktopWidget>
#include <QLabel>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <KAboutData>
#include <KActionCollection>
#include <KConfig>
#include <KConfigGroup>
#include <KEditToolBar>
#include <KLocalizedString>
#include <KMessageBox>
#include <KShortcutsDialog>
#include <KToggleAction>

#include "basketstatusbar.h"
#include "bnpview.h"
#include "diagnosticmanager.h"
#include "global.h"
#include "mathomicons.h"
#include "settings.h"

#include <basket_debug.h>

/** Container */

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
    , m_settings(nullptr)
    , m_quit(false)
{
    auto *bar = new BasketStatusBar(statusBar());
    m_baskets = new BNPView(this, this, actionCollection(), bar);
    setCentralWidget(m_baskets);

    setupActions();
    statusBar()->show();
    statusBar()->setSizeGripEnabled(true);

    setAutoSaveSettings(QStringLiteral("MainWindow"), true);

    m_actShowStatusbar->setChecked(statusBar()->isVisible());
    connect(m_baskets, &BNPView::setWindowCaption, this, &MainWindow::setWindowTitle);

    setStandardToolBarMenuEnabled(true);

    createGUI(QStringLiteral("mathomui.rc"));
    KConfigGroup group = KSharedConfig::openConfig()->group(autoSaveGroup());
    applyMainWindowSettings(group);
}

MainWindow::~MainWindow()
{
    KConfigGroup group = KSharedConfig::openConfig()->group(autoSaveGroup());
    saveMainWindowSettings(group);
    delete m_settings;
    delete m_baskets;
}

void MainWindow::setupActions()
{
    actQuit = KStandardAction::quit(this, &MainWindow::quit, actionCollection());
    QAction *a = nullptr;
    a = actionCollection()->addAction(QStringLiteral("minimizeRestore"), this, &MainWindow::minimizeRestore);
    a->setText(i18n("Minimize"));
    a->setIcon(QIcon::fromTheme(QString()));
    a->setShortcut(0);

    /** Settings : ************************************************************/
    m_actShowStatusbar = KStandardAction::showStatusbar(this, &MainWindow::toggleStatusBar, actionCollection());


    (void)KStandardAction::keyBindings(this, &MainWindow::showShortcutsSettingsDialog, actionCollection());

    (void)KStandardAction::configureToolbars(this, &MainWindow::configureToolbars, actionCollection());

    actAppConfig = KStandardAction::preferences(this, &MainWindow::showSettingsDialog, actionCollection());

    QAction *updateSettingsAction =
        actionCollection()->addAction(QStringLiteral("options_update_settings"), this, &MainWindow::showUpdateSettingsDialog);
    updateSettingsAction->setText(i18n("Update Settings..."));
    updateSettingsAction->setIcon(QIcon::fromTheme(QStringLiteral("system-software-update")));

    /*
     * Accessibility profiles
     *
     * Profiles can be combined. Each profile is a preset that
     * activates accessibility modules. The rendering layer only
     * receives the resulting effective configuration.
     */
    auto addAccessibilityProfileAction =
        [this](const QString &actionName,
               const QString &label,
               const QString &configKey) -> QAction *
    {
        auto config = KSharedConfig::openConfig();
        KConfigGroup group(config, QStringLiteral("Accessibility Profiles"));

        auto *action = new QAction(this);
        action->setText(label);
        action->setCheckable(true);
        action->setChecked(group.readEntry(configKey, false));

        actionCollection()->addAction(actionName, action);

        connect(action, &QAction::toggled, this, [configKey](bool enabled) {
            auto config = KSharedConfig::openConfig();
            KConfigGroup group(config, QStringLiteral("Accessibility Profiles"));

            group.writeEntry(configKey, enabled);
            config->sync();

            // Apply profile changes immediately to open Mathom editors.
            AccessibilitySettings::refreshAllDisplays();
        });

        return action;
    };

    addAccessibilityProfileAction(
        QStringLiteral("profile_dyslexia"),
        i18n("Dyslexia"),
        QStringLiteral("dyslexia"));

    addAccessibilityProfileAction(
        QStringLiteral("profile_dysorthography"),
        i18n("Dysorthography"),
        QStringLiteral("dysorthography"));

    addAccessibilityProfileAction(
        QStringLiteral("profile_dysgraphia"),
        i18n("Dysgraphia"),
        QStringLiteral("dysgraphia"));

    addAccessibilityProfileAction(
        QStringLiteral("profile_dyspraxia"),
        i18n("Dyspraxia / DCD"),
        QStringLiteral("dyspraxia"));

    addAccessibilityProfileAction(
        QStringLiteral("profile_dysphasia"),
        i18n("Dysphasia / DLD"),
        QStringLiteral("dysphasia"));

    addAccessibilityProfileAction(
        QStringLiteral("profile_dyscalculia"),
        i18n("Dyscalculia"),
        QStringLiteral("dyscalculia"));

    addAccessibilityProfileAction(
        QStringLiteral("profile_adhd"),
        i18n("ADHD"),
        QStringLiteral("adhd"));

    QAction *customProfileAction =
        actionCollection()->addAction(
            QStringLiteral("profile_custom"),
            this,
            &MainWindow::showCustomAccessibilityDialog);

    customProfileAction->setText(
        i18n("Custom..."));

    QAction *aboutMathomAction =
        actionCollection()->addAction(
            QStringLiteral("help_about_mathom"),
            this,
            &MainWindow::showAboutMathomDialog);

    aboutMathomAction->setText(
        i18n("About Mathom Notes"));

    aboutMathomAction->setIcon(
        MathomIcons::application());


    QAction *diagnosticsAction =
        actionCollection()->addAction(QStringLiteral("help_open_diagnostics"), this, []() {
            DiagnosticManager::instance().openDiagnosticsFolder();
        });
    diagnosticsAction->setText(i18n("Open Diagnostics Folder"));
    diagnosticsAction->setIcon(QIcon::fromTheme(QStringLiteral("folder-open")));

    QAction *aboutThorinuxAction =
        actionCollection()->addAction(QStringLiteral("help_about_thorinux"), this, &MainWindow::showAboutThorinuxDialog);
    aboutThorinuxAction->setText(i18n("About Thorinux Systems"));
    aboutThorinuxAction->setIcon(QIcon::fromTheme(QStringLiteral("help-about")));
}

SettingsDialog *MainWindow::settings()
{
    return m_settings;
}

void MainWindow::toggleStatusBar()
{
    if (statusBar()->isVisible())
        statusBar()->hide();
    else
        statusBar()->show();

    KConfigGroup group = KSharedConfig::openConfig()->group(autoSaveGroup());
    saveMainWindowSettings(group);
}

void MainWindow::configureToolbars()
{
    KConfigGroup group = KSharedConfig::openConfig()->group(autoSaveGroup());
    saveMainWindowSettings(group);

    KEditToolBar dlg(actionCollection(), this);
    connect(&dlg, &KEditToolBar::newToolBarConfig, this, &MainWindow::slotNewToolbarConfig);
    dlg.exec();
}

void MainWindow::slotNewToolbarConfig() // This is called when OK or Apply is clicked
{
    createGUI(QStringLiteral("mathomui.rc"));

    // createGUI() can recreate the XMLGUI menus. Reconnect the dynamic
    // Tags menu to the newly created QMenu instance.
    m_baskets->connectTagsMenu();

    KConfigGroup group = KSharedConfig::openConfig()->group(autoSaveGroup());
    applyMainWindowSettings(group);
}

void MainWindow::showSettingsDialog()
{
    if (!m_settings)
        m_settings = new SettingsDialog(qApp->activeWindow());

    if (Global::activeMainWindow()) {
        m_settings->exec();
        return;
    }

    m_settings->show();
}

void MainWindow::showCustomAccessibilityDialog()
{
    QDialog dialog(this);

    dialog.setWindowTitle(
        i18n("Custom profile"));

    auto *layout =
        new QVBoxLayout(&dialog);

    auto *description =
        new QLabel(
            i18n(
                "Select the aids to apply. "
                "Modules can be combined freely."),
            &dialog);

    description->setWordWrap(true);
    layout->addWidget(description);


    auto config =
        KSharedConfig::openConfig();

    KConfigGroup custom(
        config,
        QStringLiteral(
            "Accessibility Custom Modules"));


    /*
     * ============================================================
     * TYPOGRAPHIE
     * ============================================================
     */

    auto *typographyGroup =
        new QGroupBox(
            i18n("Typography"),
            &dialog);

    auto *typographyLayout =
        new QVBoxLayout(typographyGroup);


    auto *adaptedFont =
        new QCheckBox(
            i18n("Adapted font"),
            typographyGroup);

    adaptedFont->setChecked(
        custom.readEntry(
            QStringLiteral("adaptedFont"),
            false));

    typographyLayout->addWidget(
        adaptedFont);


    auto *largerText =
        new QCheckBox(
            i18n("Larger text"),
            typographyGroup);

    largerText->setChecked(
        custom.readEntry(
            QStringLiteral("largerText"),
            false));

    typographyLayout->addWidget(
        largerText);


    auto *letterSpacing =
        new QCheckBox(
            i18n("Letter spacing"),
            typographyGroup);

    letterSpacing->setChecked(
        custom.readEntry(
            QStringLiteral("letterSpacing"),
            false));

    typographyLayout->addWidget(
        letterSpacing);


    auto *wordSpacing =
        new QCheckBox(
            i18n("Word spacing"),
            typographyGroup);

    wordSpacing->setChecked(
        custom.readEntry(
            QStringLiteral("wordSpacing"),
            false));

    typographyLayout->addWidget(
        wordSpacing);


    auto *lineSpacing =
        new QCheckBox(
            i18n("Increased line spacing"),
            typographyGroup);

    lineSpacing->setChecked(
        custom.readEntry(
            QStringLiteral("lineSpacing"),
            false));

    typographyLayout->addWidget(
        lineSpacing);


    /*
     * MODULE PREVU - ESPACEMENT DES PARAGRAPHES
     *
     * A decommenter lorsque le module sera operationnel.
     */

    // auto *paragraphSpacing =
    //     new QCheckBox(
    //         i18n("Espacement des paragraphes"),
    //         typographyGroup);
    //
    // paragraphSpacing->setChecked(
    //     custom.readEntry(
    //         QStringLiteral("paragraphSpacing"),
    //         false));
    //
    // typographyLayout->addWidget(
    //     paragraphSpacing);


    layout->addWidget(
        typographyGroup);


    /*
     * ============================================================
     * AIDES A LA LECTURE
     * ============================================================
     *
     * Interface deja preparee.
     * Les lignes seront decommmentees module par module.
     */

    auto *readingGroup =
        new QGroupBox(
            i18n("Reading aids"),
            &dialog);

    auto *readingLayout =
        new QVBoxLayout(readingGroup);


    /*
     * LireCouleur : coloration syllabique
     */

    auto *syllableColoring =
        new QCheckBox(
            i18n("Syllable coloring"),
            readingGroup);

    syllableColoring->setChecked(
        custom.readEntry(
            QStringLiteral("syllableColoring"),
            false));

    readingLayout->addWidget(
        syllableColoring);


    /*
     * LireCouleur : coloration des phonemes
     */

    auto *phonemeColoring =
        new QCheckBox(
            i18n("Phoneme coloring"),
            readingGroup);

    phonemeColoring->setChecked(
        custom.readEntry(
            QStringLiteral("phonemeColoring"),
            false));

    phonemeColoring->setToolTip(
        i18n(
            "Visual highlighting of common "
            "grapheme-sound correspondences in French."));

    readingLayout->addWidget(
        phonemeColoring);


    /*
     * Mise en evidence des graphemes
     */

    auto *graphemeHighlight =
        new QCheckBox(
            i18n("Grapheme highlighting"),
            readingGroup);

    graphemeHighlight->setChecked(
        custom.readEntry(
            QStringLiteral("graphemeHighlight"),
            false));

    graphemeHighlight->setToolTip(
        i18n(
            "Underlines groups of letters that form "
            "common reading units."));

    readingLayout->addWidget(
        graphemeHighlight);


    /*
     * Confusions visuelles b/d, p/q, etc.
     */

    // auto *confusableLetters =
    //     new QCheckBox(
    //         i18n("Aide aux lettres confondables"),
    //         readingGroup);
    //
    // confusableLetters->setChecked(
    //     custom.readEntry(
    //         QStringLiteral("confusableLetters"),
    //         false));
    //
    // readingLayout->addWidget(
    //     confusableLetters);


    /*
     * Alternance visuelle des lignes
     */

    // auto *alternatingLines =
    //     new QCheckBox(
    //         i18n("Alternance visuelle des lignes"),
    //         readingGroup);
    //
    // alternatingLines->setChecked(
    //     custom.readEntry(
    //         QStringLiteral("alternatingLines"),
    //         false));
    //
    // readingLayout->addWidget(
    //     alternatingLines);


    /*
     * Guide de lecture
     */

    // auto *readingGuide =
    //     new QCheckBox(
    //         i18n("Guide de lecture"),
    //         readingGroup);
    //
    // readingGuide->setChecked(
    //     custom.readEntry(
    //         QStringLiteral("readingGuide"),
    //         false));
    //
    // readingLayout->addWidget(
    //     readingGuide);


    /*
     * Ligne active
     */

    // auto *activeLineHighlight =
    //     new QCheckBox(
    //         i18n("Mettre en évidence la ligne active"),
    //         readingGroup);
    //
    // activeLineHighlight->setChecked(
    //     custom.readEntry(
    //         QStringLiteral("activeLineHighlight"),
    //         false));
    //
    // readingLayout->addWidget(
    //     activeLineHighlight);


    /*
     * Attenuation du reste du texte
     */

    // auto *dimOtherLines =
    //     new QCheckBox(
    //         i18n("Atténuer les autres lignes"),
    //         readingGroup);
    //
    // dimOtherLines->setChecked(
    //     custom.readEntry(
    //         QStringLiteral("dimOtherLines"),
    //         false));
    //
    // readingLayout->addWidget(
    //     dimOtherLines);


    layout->addWidget(
        readingGroup);


    /*
     * ============================================================
     * LECTURE VOCALE
     * ============================================================
     */

    // auto *speechGroup =
    //     new QGroupBox(
    //         i18n("Lecture vocale"),
    //         &dialog);
    //
    // auto *speechLayout =
    //     new QVBoxLayout(speechGroup);


    // auto *textToSpeech =
    //     new QCheckBox(
    //         i18n("Activer la lecture vocale"),
    //         speechGroup);
    //
    // textToSpeech->setChecked(
    //     custom.readEntry(
    //         QStringLiteral("textToSpeech"),
    //         false));
    //
    // speechLayout->addWidget(
    //     textToSpeech);


    // auto *speechTracking =
    //     new QCheckBox(
    //         i18n("Suivre visuellement le texte lu"),
    //         speechGroup);
    //
    // speechTracking->setChecked(
    //     custom.readEntry(
    //         QStringLiteral("speechTracking"),
    //         false));
    //
    // speechLayout->addWidget(
    //     speechTracking);


    // layout->addWidget(
    //     speechGroup);


    /*
     * ============================================================
     * CONCENTRATION / ERGONOMIE
     * ============================================================
     */

    // auto *focusGroup =
    //     new QGroupBox(
    //         i18n("Concentration et ergonomie"),
    //         &dialog);
    //
    // auto *focusLayout =
    //     new QVBoxLayout(focusGroup);


    // auto *reducedDistractions =
    //     new QCheckBox(
    //         i18n("Réduire les distractions"),
    //         focusGroup);
    //
    // reducedDistractions->setChecked(
    //     custom.readEntry(
    //         QStringLiteral("reducedDistractions"),
    //         false));
    //
    // focusLayout->addWidget(
    //     reducedDistractions);


    // auto *largerControls =
    //     new QCheckBox(
    //         i18n("Agrandir les contrôles"),
    //         focusGroup);
    //
    // largerControls->setChecked(
    //     custom.readEntry(
    //         QStringLiteral("largerControls"),
    //         false));
    //
    // focusLayout->addWidget(
    //     largerControls);


    // layout->addWidget(
    //     focusGroup);


    /*
     * ============================================================
     * BOUTONS
     * ============================================================
     */

    auto *buttons =
        new QDialogButtonBox(
            QDialogButtonBox::Ok
            | QDialogButtonBox::Cancel,
            &dialog);

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


    if (dialog.exec()
        != QDialog::Accepted) {

        return;
    }


    /*
     * ============================================================
     * ENREGISTREMENT DES MODULES ACTIFS
     * ============================================================
     */

    custom.writeEntry(
        QStringLiteral("adaptedFont"),
        adaptedFont->isChecked());

    custom.writeEntry(
        QStringLiteral("largerText"),
        largerText->isChecked());

    custom.writeEntry(
        QStringLiteral("letterSpacing"),
        letterSpacing->isChecked());

    custom.writeEntry(
        QStringLiteral("wordSpacing"),
        wordSpacing->isChecked());

    custom.writeEntry(
        QStringLiteral("lineSpacing"),
        lineSpacing->isChecked());


    /*
     * FUTURS MODULES
     *
     * Les cles correspondent deja exactement a celles du moteur
     * AccessibilitySettings.
     */

    // custom.writeEntry(
    //     QStringLiteral("paragraphSpacing"),
    //     paragraphSpacing->isChecked());

    custom.writeEntry(
        QStringLiteral("syllableColoring"),
        syllableColoring->isChecked());

    custom.writeEntry(
        QStringLiteral("phonemeColoring"),
        phonemeColoring->isChecked());

    custom.writeEntry(
        QStringLiteral("graphemeHighlight"),
        graphemeHighlight->isChecked());

    // custom.writeEntry(
    //     QStringLiteral("confusableLetters"),
    //     confusableLetters->isChecked());

    // custom.writeEntry(
    //     QStringLiteral("alternatingLines"),
    //     alternatingLines->isChecked());

    // custom.writeEntry(
    //     QStringLiteral("readingGuide"),
    //     readingGuide->isChecked());

    // custom.writeEntry(
    //     QStringLiteral("activeLineHighlight"),
    //     activeLineHighlight->isChecked());

    // custom.writeEntry(
    //     QStringLiteral("dimOtherLines"),
    //     dimOtherLines->isChecked());

    // custom.writeEntry(
    //     QStringLiteral("textToSpeech"),
    //     textToSpeech->isChecked());

    // custom.writeEntry(
    //     QStringLiteral("speechTracking"),
    //     speechTracking->isChecked());

    // custom.writeEntry(
    //     QStringLiteral("reducedDistractions"),
    //     reducedDistractions->isChecked());

    // custom.writeEntry(
    //     QStringLiteral("largerControls"),
    //     largerControls->isChecked());


    /*
     * Personnalise est considere actif des qu'au moins
     * un module actuellement disponible est selectionne.
     */

    const bool customEnabled =
        adaptedFont->isChecked()
        || largerText->isChecked()
        || letterSpacing->isChecked()
        || wordSpacing->isChecked()
        || lineSpacing->isChecked()
        || syllableColoring->isChecked()
        || phonemeColoring->isChecked()
        || graphemeHighlight->isChecked();

    KConfigGroup profiles(
        config,
        QStringLiteral(
            "Accessibility Profiles"));

    profiles.writeEntry(
        QStringLiteral("custom"),
        customEnabled);

    config->sync();

    AccessibilitySettings::refreshAllDisplays();
}


void MainWindow::showUpdateSettingsDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(i18n("Mathom Update Settings"));

    auto *layout = new QVBoxLayout(&dialog);

    auto *description = new QLabel(
        i18n("By default, Mathom installs only stable releases. "
             "Enable the option below to also receive development versions "
             "when they are newer than the latest stable release."),
        &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *allowDevelopment =
        new QCheckBox(i18n("Allow development versions of Mathom"), &dialog);
    allowDevelopment->setChecked(Settings::allowDevelopmentUpdates());
    layout->addWidget(allowDevelopment);

    auto *warning = new QLabel(
        i18n("Development versions are intended for testing and may contain bugs."),
        &dialog);
    warning->setWordWrap(true);
    layout->addWidget(warning);

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    Settings::setAllowDevelopmentUpdates(allowDevelopment->isChecked());
    Settings::saveConfig();
}

void MainWindow::showAboutMathomDialog()
{
    QDialog dialog(this);

    dialog.setWindowTitle(
        i18n("About Mathom Notes"));

    dialog.setWindowIcon(
        MathomIcons::application());

    dialog.resize(760, 520);

    auto *layout =
        new QVBoxLayout(&dialog);

    auto *tabs =
        new QTabWidget(&dialog);

    layout->addWidget(tabs);


    auto makePage =
        [&tabs](const QString &html)
    {
        auto *browser =
            new QTextBrowser(tabs);

        browser->setOpenExternalLinks(true);
        browser->setHtml(html);

        return browser;
    };


    const QString version =
        KAboutData::applicationData().version();


    /*
     * A PROPOS
     */
    const QString aboutHtml =
        i18n(
            "<h2>Mathom Notes %1</h2>"
            "<p>Mathom Notes is free software for note-taking "
            "and knowledge organization.</p>"
            "<p>The project is developed and maintained by "
            "<b>Thorinux Systems</b>.</p>"
            "<p>Mathom Notes is based on the free software project "
            "<b>BasKet Note Pads</b>.</p>"
            "<p>Mathom Notes uses <b>Qt 6</b> and "
            "<b>KDE Frameworks 6</b> provided by the system.</p>"
            "<p><b>Project website:</b> "
            "<a href=\"https://github.com/ThorinFutDeChene/Mathom-Notes\">"
            "GitHub - Mathom Notes</a></p>"
            "<p><b>Contact:</b> "
            "<a href=\"mailto:contact@thorinux.fr\">"
            "contact@thorinux.fr</a></p>",
            version);

    tabs->addTab(
        makePage(aboutHtml),
        i18n("About"));


    /*
     * COLLABORATEURS
     */
    const QString collaboratorsHtml =
        i18n(
            "<h2>Contributors</h2>"
            "<p>Mathom Notes is developed with a commitment to "
            "transparency regarding the people and tools involved "
            "in the project.</p>"

            "<h3>Fabrice PEREYRON</h3>"
            "<p>Project design, development, testing, "
            "maintenance and direction of Mathom Notes.</p>"

            "<h3>ChatGPT (OpenAI)</h3>"
            "<p>Development assistance, code analysis, "
            "debugging, documentation, technical structuring "
            "and design assistance.</p>"

            "<p><i>ChatGPT is used as an assistance tool. "
            "The design, functional choices, validations "
            "and maintenance of the project remain the responsibility "
            "of the Mathom Notes developer.</i></p>");

    tabs->addTab(
        makePage(collaboratorsHtml),
        i18n("Contributors"));


    /*
     * SOUTIENS PARTICULIERS
     */
    const QString supportersHtml =
        i18n(
            "<h2>Individual Supporters</h2>"
            "<p>This section thanks the people who have financially supported "
            "Mathom Notes as individuals.</p>"
            "<p>The names or pseudonyms of contributors who have "
            "chosen to appear in Mathom Notes will be displayed here.</p>"
            "<p>The amount of their contribution is not published.</p>"
            "<p>Thank you to everyone who contributes "
            "to the development and sustainability of the project.</p>");

    tabs->addTab(
        makePage(supportersHtml),
        i18n("Individual Supporters"));


    /*
     * PARTENAIRES FINANCIERS
     */
    const QString partnersHtml =
        i18n(
            "<h2>Financial Partners</h2>"
            "<p>This section is reserved for companies, associations "
            "and organizations providing financial support to the "
            "Mathom Notes project.</p>"
            "<p>Depending on the terms of the partnership, their name, "
            "logo and a link to their website may be "
            "displayed here.</p>");

    tabs->addTab(
        makePage(partnersHtml),
        i18n("Financial Partners"));


    /*
     * COLLECTE ULULE
     */
    const QString campaignHtml =
        i18n(
            "<h2>Ulule Campaign</h2>"
            "<p>Mathom Notes is intended to remain free software "
            "available free of charge.</p>"
            "<p>The Ulule campaign helps fund its "
            "development, accessibility tools, "
            "documentation and distribution.</p>"
            "<p>The final campaign information, "
            "its link and results will be added here.</p>");

    tabs->addTab(
        makePage(campaignHtml),
        i18n("Ulule Campaign"));


    /*
     * LICENCE ET ATTRIBUTION
     */
    const QString licenseHtml =
        i18n(
            "<h2>License</h2>"
            "<p><b>Mathom Notes</b> is distributed under the "
            "<b>GNU GPL version 2 or later</b>.</p>"
            "<p>Copyright © 2026 Thorinux Systems.</p>"
            "<p>Mathom Notes is a fork of "
            "<b>BasKet Note Pads</b>.</p>"
            "<p>The copyrights, licenses and attributions of the original "
            "project are preserved in the source code and "
            "the project license files.</p>");

    tabs->addTab(
        makePage(licenseHtml),
        i18n("License"));


    auto *buttons =
        new QDialogButtonBox(
            QDialogButtonBox::Close,
            &dialog);

    connect(
        buttons,
        &QDialogButtonBox::rejected,
        &dialog,
        &QDialog::reject);

    connect(
        buttons,
        &QDialogButtonBox::accepted,
        &dialog,
        &QDialog::accept);

    layout->addWidget(buttons);

    dialog.exec();
}


void MainWindow::showAboutThorinuxDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(i18n("About Thorinux Systems"));
    dialog.resize(680, 430);

    auto *layout = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs);

    auto makePage = [&tabs](const QString &html) {
        auto *browser = new QTextBrowser(tabs);
        browser->setOpenExternalLinks(true);
        browser->setHtml(html);
        return browser;
    };

    const QString aboutHtml =
        i18n(
            "<h3>Thorinux Systems</h3>"
            "<p>Thorinux Systems develops and maintains free and open-source software projects.</p>"
            "<p><b>Thorinux</b> is a registered trademark. Mathom is a project of the Thorinux brand, "
            "developed and maintained by Thorinux Systems.</p>"
            "<p>Mathom is based on BasKet Note Pads and preserves the attribution and licences of the original project.</p>"
            "<p>For questions or help: <a href=\"mailto:contact@thorinux.fr\">contact@thorinux.fr</a></p>");

    tabs->addTab(makePage(aboutHtml), i18n("About"));

    const QString reportHtml =
        i18n(
            "<h3>Bug reports or wishes</h3>"
            "<p>Mathom includes its own diagnostic system for technical problems. "
            "When a problem occurs, the diagnostic report can be found from Help → Open Diagnostics Folder.</p>"
            "<p>For a bug report, a feature request or any other feedback, contact "
            "<a href=\"mailto:contact@thorinux.fr\">contact@thorinux.fr</a>.</p>");

    tabs->addTab(makePage(reportHtml), i18n("Bug reports or wishes"));

    const QString supportHtml =
        i18n(
            "<h3>Support Mathom</h3>"
            "<p>Mathom is intended to remain free and open-source software.</p>"
            "<p>Ways to financially support the project will be added later. "
            "For now, using Mathom, testing development versions and reporting useful feedback already helps the project.</p>");

    tabs->addTab(makePage(supportHtml), i18n("Support Mathom"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::showShortcutsSettingsDialog()
{
    KShortcutsDialog::showDialog(actionCollection(), KShortcutsEditor::LetterShortcutsAllowed, this);
}

bool MainWindow::event(QEvent *event)
{
    bool shouldSave = false;

    if (event->type() == QEvent::Polish) {
        // If position and size has never been set, set nice ones:
        //  - Set size to sizeHint()
        //  - Keep the window manager placing the window where it want and save this
        if (Settings::mainWindowSize().isEmpty()) {
            //      qCDebug(BASKET_LOG) << "Main Window Position: Initial Set in show()";
            int defaultWidth = qApp->primaryScreen()->geometry().width() * 5 / 6;
            int defaultHeight = qApp->primaryScreen()->geometry().height() * 5 / 6;
            resize(defaultWidth, defaultHeight); // sizeHint() is bad (too small) and we want the user to have a good default area size
            shouldSave = true;
        } else {
            //      qCDebug(BASKET_LOG) << "Main Window Position: Recall in show(x="
            //                << Settings::mainWindowPosition().x() << ", y=" << Settings::mainWindowPosition().y()
            //                << ", width=" << Settings::mainWindowSize().width() << ", height=" << Settings::mainWindowSize().height()
            //                << ")";
            // move(Settings::mainWindowPosition());
            // resize(Settings::mainWindowSize());
        }
    }

    const bool result = KXmlGuiWindow::event(event);

    if (shouldSave) {
        //      qCDebug(BASKET_LOG) << "Main Window Position: Save size and position in show(x="
        //                << pos().x() << ", y=" << pos().y()
        //                << ", width=" << size().width() << ", height=" << size().height()
        //                << ")";
        Settings::setMainWindowPosition(pos());
        Settings::setMainWindowSize(size());
        Settings::saveConfig();
    }

    return result;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    //  qCDebug(BASKET_LOG) << "Main Window Position: Save size in resizeEvent(width=" << size().width() << ", height=" << size().height() << ") ; isMaximized="
    //            << (isMaximized() ? "true" : "false");
    Settings::setMainWindowSize(size());
    Settings::saveConfig();

    // Added to make it work (previous lines do not work):
    // saveMainWindowSettings( KSharedConfig::openConfig(), autoSaveGroup() );
    KXmlGuiWindow::resizeEvent(event);
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    //  qCDebug(BASKET_LOG) << "Main Window Position: Save position in moveEvent(x=" << pos().x() << ", y=" << pos().y() << ")";
    Settings::setMainWindowPosition(pos());
    Settings::saveConfig();

    // Added to make it work (previous lines do not work):
    // saveMainWindowSettings( KSharedConfig::openConfig(), autoSaveGroup() );
    KXmlGuiWindow::moveEvent(event);
}

bool MainWindow::queryExit()
{
    hide();
    return true;
}

void MainWindow::quit()
{
    m_quit = true;
    close();
}

bool MainWindow::queryClose()
{
    /*  if (m_shuttingDown) // Set in askForQuit(): we don't have to ask again
        return true;*/

    if (qApp->isSavingSession()) {
        Settings::saveConfig();
        return true;
    }

    return askForQuit();
}

bool MainWindow::askForQuit()
{
    QString message = i18n("<p>Do you really want to quit %1?</p>", QGuiApplication::applicationDisplayName());

    int really = KMessageBox::warningContinueCancel(this,
                                                    message,
                                                    i18n("Quit Confirm"),
                                                    KStandardGuiItem::quit(),
                                                    KStandardGuiItem::cancel(),
                                                    QStringLiteral("confirmQuitAsking"));

    if (really == KMessageBox::Cancel) {
        m_quit = false;
        return false;
    }

    return true;
}

void MainWindow::minimizeRestore()
{
    if (this->windowState() != Qt::WindowMinimized) {
        this->setWindowState(Qt::WindowMinimized);
    } else {
        this->setWindowState(Qt::WindowActive);
    }
}

#include "moc_mainwindow.cpp"
