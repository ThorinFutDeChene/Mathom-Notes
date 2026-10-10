#include "advancedsettingsdialog.h"

#include <KLocalizedString>
#include <KPageWidgetItem>
#include <KPluginMetaData>

#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

AdvancedSettingsDialog::AdvancedSettingsDialog(QWidget *parent)
    : KCMultiDialog(parent)
{
    setWindowTitle(i18n("Advanced Settings - Mathom Notes"));
    setMinimumSize(680, 460);
    setFaceType(KPageDialog::List);

    /*
     * Seuls les modules explicitement classes comme
     * "advanced" apparaissent dans cette fenetre.
     */
    int advancedModules = 0;

    const auto plugins = KPluginMetaData::findPlugins(
        QStringLiteral("pim/kcms/mathom"));

    for (const KPluginMetaData &metadata : plugins) {
        const QString section = metadata.rawData()
            .value(QStringLiteral("X-Mathom-SettingsSection"))
            .toString();

        if (section != QStringLiteral("advanced"))
            continue;

        addModule(metadata);
        ++advancedModules;
    }

    /*
     * Premiere etape : aucun reglage n'a encore ete migre.
     */
    if (advancedModules != 0)
        return;

    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    auto *title = new QLabel(
        i18n("Advanced Settings"),
        page);

    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);

    layout->addWidget(title);

    auto *description = new QLabel(
        i18n(
            "This window will progressively receive the "
            "advanced configuration modules of Mathom Notes.\n\n"
            "No settings have been moved yet."),
        page);

    description->setWordWrap(true);
    layout->addWidget(description);
    layout->addStretch();

    auto *item = addPage(page, i18n("Introduction"));
    item->setIcon(
        QIcon::fromTheme(
            QStringLiteral("preferences-system")));
}
