#include "advancedsettingsdialog.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KPageWidgetItem>
#include <KPluginMetaData>
#include <KShortcutsDialog>
#include <KShortcutsEditor>

#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

AdvancedSettingsDialog::AdvancedSettingsDialog(
    KActionCollection *actions,
    KActionCollection *globalActions,
    QWidget *parent)
    : KCMultiDialog(parent)
{
    setWindowTitle(
        i18n("Advanced Settings - Mathom Notes"));

    setMinimumSize(680, 460);
    setFaceType(KPageDialog::List);

    /*
     * Raccourcis clavier :
     * conserver l'outil KDE complet et sa gestion
     * native de l'enregistrement et de l'annulation.
     */
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    auto *description = new QLabel(
        i18n(
            "Customize the keyboard shortcuts used "
            "to execute Mathom Notes commands.\n\n"
            "Secondary language input shortcuts are "
            "configured separately in the standard settings."),
        page);

    description->setWordWrap(true);
    layout->addWidget(description);

    auto *button = new QPushButton(
        i18n("Configure Keyboard Shortcuts..."),
        page);

    button->setIcon(
        QIcon::fromTheme(
            QStringLiteral("configure-shortcuts")));

    layout->addWidget(button, 0, Qt::AlignLeft);
    layout->addStretch();

    connect(
        button,
        &QPushButton::clicked,
        this,
        [this, actions, globalActions]() {
            KShortcutsDialog dialog(
                KShortcutsEditor::AllActions,
                KShortcutsEditor::LetterShortcutsAllowed,
                this);

            dialog.addCollection(
                actions,
                i18n("Mathom Notes commands"));

            if (globalActions) {
                dialog.addCollection(
                    globalActions,
                    i18n("Mathom Notes global shortcuts"));
            }

            dialog.configure(true);
        });

    auto *item = addPage(
        page,
        i18n("Keyboard Shortcuts"));

    item->setIcon(
        QIcon::fromTheme(
            QStringLiteral("preferences-desktop-keyboard")));

    /*
     * Les autres modules avances seront migres
     * progressivement apres validation.
     */
    const auto plugins = KPluginMetaData::findPlugins(
        QStringLiteral("pim/kcms/mathom"));

    for (const KPluginMetaData &metadata : plugins) {
        const QString section = metadata.rawData()
            .value(QStringLiteral("X-Mathom-SettingsSection"))
            .toString();

        if (section == QStringLiteral("advanced"))
            addModule(metadata);
    }
}
