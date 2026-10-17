/**
 * SPDX-FileCopyrightText: (C) 2003 Sébastien Laoût <slaout@linux62.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KIconTheme>
#include <KLocalizedString>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <config.h>
#include <kconfig.h> // TMP IN ALPHA 1

#include "application.h"
#include "backup.h"
#include "bnpview.h"
#include "global.h"
#include "diagnosticmanager.h"
#include "mainwindow.h"
#include "mathomscanpreloader.h"
#ifdef DEBUG_PIPE
#include "debugwindow.h"
#endif
#include "settings.h"

static bool copyDirectoryRecursively(const QString &sourcePath, const QString &destinationPath)
{
    QDir sourceDir(sourcePath);
    if (!sourceDir.exists()) {
        return false;
    }

    if (!QDir().mkpath(destinationPath)) {
        return false;
    }

    QDirIterator it(sourcePath,
                    QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const QString sourceItem = it.next();
        const QFileInfo info = it.fileInfo();
        const QString relativePath = sourceDir.relativeFilePath(sourceItem);
        const QString destinationItem = QDir(destinationPath).filePath(relativePath);

        if (info.isDir()) {
            if (!QDir().mkpath(destinationItem)) {
                return false;
            }
        } else if (info.isFile()) {
            if (!QDir().mkpath(QFileInfo(destinationItem).path())) {
                return false;
            }
            if (!QFile::copy(sourceItem, destinationItem)) {
                return false;
            }
        }
    }

    return true;
}

int main(int argc, char *argv[])
{
    const char *argv0 = (argc >= 1 ? argv[0] : "");

    // KF6 icon handling must be initialized before QApplication.
    // This installs KIconEngine and lets Mathom use the KDE icon theme
    // (Breeze in our bundled runtime) even when running under MATE/GNOME.
    KIconTheme::initTheme();

    Application app(argc, argv);

    KCrash::initialize();

    QCommandLineParser opts;
    opts.addOption(QCommandLineOption(QStringList() << QStringLiteral("d") << QStringLiteral("debug"), i18n("Show the debug window")));
    opts.addOption(QCommandLineOption(QStringList() << QStringLiteral("f") << QStringLiteral("data-folder"),
                                      i18n("Custom folder to load and save Mathom data."),
                                      i18nc("Command line help: --data-folder <FOLDER>", "folder")));
    opts.addOption(QCommandLineOption(QStringLiteral("start-hidden"),
                                      i18n("Automatically hide the main window in the system tray on startup."))); //

    opts.addPositionalArgument(QStringLiteral("file"), i18n("Open a Mathom-House archive or template."));
    KAboutData::applicationData().setupCommandLine(&opts); //--author, --license
    opts.process(app);
    KAboutData::applicationData().processCommandLine(&opts); // show author, license information and exit
    KDBusService service(KDBusService::Unique);
    QObject::connect(&service, &KDBusService::activateRequested, &app, &Application::onActivateRequested);
    // Custom data folder;
    // the own block is to to not keep variables live for the whole application lifetime
    {
        const QString customDataFolder = opts.value(QStringLiteral("data-folder"));
        if (!customDataFolder.isEmpty()) {
            Global::setCustomSavesFolder(customDataFolder);
        }
    }
    // First-run migration from BasKet to Mathom.
    // Keep the original BasKet profile untouched.
    if (opts.value(QStringLiteral("data-folder")).isEmpty()) {
        const QString genericData =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const QString configDir =
            QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

        const QString newData =
            QDir(genericData).filePath(QStringLiteral("mathom"));
        const QString newConfig =
            QDir(configDir).filePath(QStringLiteral("mathomrc"));

        struct LegacyProfile {
            QString data;
            QString config;
            QDateTime modified;
        };

        QList<LegacyProfile> candidates;

        // BasKet installed as Flatpak.
        const QString flatpakRoot =
            QDir::home().filePath(QStringLiteral(".var/app/org.kde.basket"));

        const QString flatpakData =
            QDir(flatpakRoot).filePath(QStringLiteral("data/basket"));
        const QString flatpakConfig =
            QDir(flatpakRoot).filePath(QStringLiteral("config/basketrc"));
        const QString flatpakTree =
            QDir(flatpakData).filePath(QStringLiteral("baskets/baskets.xml"));

        if (QFileInfo::exists(flatpakTree)) {
            candidates.append({
                flatpakData,
                flatpakConfig,
                QFileInfo(flatpakTree).lastModified()
            });
        }

        // BasKet installed natively.
        const QString nativeData =
            QDir(genericData).filePath(QStringLiteral("basket"));
        const QString nativeConfig =
            QDir(configDir).filePath(QStringLiteral("basketrc"));
        const QString nativeTree =
            QDir(nativeData).filePath(QStringLiteral("baskets/baskets.xml"));

        if (QFileInfo::exists(nativeTree)) {
            candidates.append({
                nativeData,
                nativeConfig,
                QFileInfo(nativeTree).lastModified()
            });
        }

        if (!QDir(newData).exists() && !candidates.isEmpty()) {
            const LegacyProfile *source = &candidates.first();

            for (const LegacyProfile &candidate : candidates) {
                if (candidate.modified > source->modified) {
                    source = &candidate;
                }
            }

            if (!copyDirectoryRecursively(source->data, newData)) {
                QDir(newData).removeRecursively();
            } else if (QFileInfo::exists(source->config)
                       && !QFileInfo::exists(newConfig)) {
                QFile::copy(source->config, newConfig);
            }
        }
    }

    app.tryLoadFile(opts.positionalArguments(), QDir::currentPath());

    // Initialize the config file
    Global::basketConfig = KSharedConfig::openConfig(QStringLiteral("mathomrc"));

    Backup::figureOutBinaryPath(argv0, app);

    /* Main Window */
    auto *win = new MainWindow();
    app.setMainWindow(win);
    /* Debug mode */
    if (opts.isSet(QStringLiteral("debug")))
        Global::bnpView->enableDebugMode();

    win->show();

    // Start probing connected scanners as soon as the main window is shown,
    // without waiting for the user to open Mathom Scan.
    QTimer::singleShot(0, win, [] {
        MathomScanPreloader::instance().start();
    });

    QTimer::singleShot(0, win, [win]() {
        DiagnosticManager::instance().showPendingReportDialog(win);
    });

    // Self-test of the presence of mathomui.rc (the only required file after basket executable)
    if (Global::bnpView->popupMenu(QStringLiteral("basket")) == nullptr)
        // An error message will be show by BNPView::popupMenu()
        return 1;

#ifdef DEBUG_PIPE
    // Install the debug message handler for external usage
    qInstallMessageHandler(debugMessageHandler);
#endif

    /* Go */
    int result = app.exec();
    app.setMainWindow(nullptr);
    return result;
}
