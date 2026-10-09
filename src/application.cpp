/**
 * SPDX-FileCopyrightText: (C) 2003 by Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "application.h"

#include <QCommandLineParser>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <KLocalizedString>

#include "aboutdata.h"
#include "bnpview.h"
#include "config.h"
#include "diagnosticmanager.h"
#include "global.h"
#include "mainwindow.h"
#include "mathomicons.h"
#include "secondarylanguageengine.h"
#include "secondarylanguagesettings.h"
#include "secondarylanguagevalidator.h"

#if HAVE_LIBGIT2
extern "C" {
#include <git2.h>
}
#endif

using namespace std::chrono_literals;

namespace
{

constexpr char SecondaryLanguageDisabledProperty[] =
    "mathomSecondaryLanguageDisabled";

bool containsPrintableCharacter(const QKeyEvent *event)
{
    if (!event)
        return false;

    for (const QChar character : event->text()) {
        if (character.isPrint())
            return true;
    }

    return false;
}

bool secondaryLanguageDisabledFor(QWidget *widget)
{
    /*
     * Une zone technique peut désactiver les transformations
     * pour elle-même et pour tous ses widgets enfants.
     */
    for (QWidget *current = widget;
         current;
         current = current->parentWidget()) {

        if (current
                ->property(
                    SecondaryLanguageDisabledProperty)
                .toBool()) {
            return true;
        }
    }

    return false;
}

void applySecondaryLanguageTransformation(
    QLineEdit *lineEdit)
{
    if (!lineEdit
        || lineEdit->isReadOnly()
        || lineEdit->echoMode() != QLineEdit::Normal
        || secondaryLanguageDisabledFor(lineEdit)) {
        return;
    }

    const QVector<SecondaryLanguageSelection> selections =
        SecondaryLanguageSettings::load();

    if (selections.isEmpty()
        || !SecondaryLanguageValidator::isValid(selections)) {
        return;
    }

    const int cursorPosition =
        lineEdit->cursorPosition();

    if (cursorPosition <= 0)
        return;

    const SecondaryLanguageTransformation transformation =
        SecondaryLanguageEngine::transform(
            lineEdit->text().left(cursorPosition),
            selections);

    if (!transformation.matched
        || transformation.ambiguous
        || transformation.replaceLength <= 0
        || transformation.replaceLength > cursorPosition) {
        return;
    }

    const int replacementStart =
        cursorPosition
        - transformation.replaceLength;

    lineEdit->setSelection(
        replacementStart,
        transformation.replaceLength);

    lineEdit->insert(
        transformation.replacement);

    lineEdit->setCursorPosition(
        replacementStart
        + transformation.replacement.size());
}

}

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
    , m_mainWindow(nullptr)
{
    // TODO(i18n-niveau-1): terminer, relire et tester les 26 catalogues restants lors d une prochaine mise a jour (12/38 deja testes).
    KLocalizedString::setApplicationDomain("basket");
    // Use the bundled Mathom logo directly. This avoids picking up an
    // older system-installed Basket/Mathom icon from the host icon theme.
    setWindowIcon(MathomIcons::application());

    KAboutData::setApplicationData(AboutData());
    DiagnosticManager::instance().startSession();
    // BasketPart::createAboutData();

#if HAVE_LIBGIT2
    git_libgit2_init();
#endif
}

Application::~Application()
{
    DiagnosticManager::instance().closeSession();
#if HAVE_LIBGIT2
    git_libgit2_shutdown();
#endif
}

bool Application::notify(
    QObject *receiver,
    QEvent *event)
{
    /*
     * Conserver une référence sûre : certains événements peuvent
     * provoquer la destruction du widget pendant leur traitement.
     */
    QPointer<QWidget> targetWidget =
        qobject_cast<QWidget *>(receiver);

    QKeyEvent *keyEvent =
        event->type() == QEvent::KeyPress
        ? static_cast<QKeyEvent *>(event)
        : nullptr;

    const bool printableInput =
        containsPrintableCharacter(keyEvent);

    /*
     * Qt insère d'abord normalement le caractère.
     * On analyse ensuite le texte réellement présent dans le champ.
     *
     * Cela conserve notamment le fonctionnement d'AltGr.
     */
    const bool result =
        QApplication::notify(receiver, event);

    if (printableInput && targetWidget) {
        if (auto *lineEdit =
                qobject_cast<QLineEdit *>(
                    targetWidget.data())) {

            applySecondaryLanguageTransformation(
                lineEdit);
        }
    }

    return result;
}

void Application::tryLoadFile(const QStringList &args, const QString &workingDir)
{
    // Open the basket archive or template file supplied as argument:
    if (args.count() >= 1) {
        QString fileName = QDir(workingDir).filePath(args.last());

        if (QFile::exists(fileName)) {
            QFileInfo fileInfo(fileName);
            if (fileInfo.absoluteFilePath().contains(Global::basketsFolder())) {
                QString folder = fileInfo.absolutePath().split(QLatin1Char('/')).last();
                folder.append(QStringLiteral("/"));
                BNPView::s_basketToOpen = folder;
                QTimer::singleShot(100ms, Global::bnpView, &BNPView::delayedOpenBasket);
            } else if (!fileInfo.isDir()) { // Do not mis-interpret data-folder param!
                // Tags are not loaded until Global::bnpView::lateInit() is called.
                // It is called 0ms after the application start.
                BNPView::s_fileToOpen = fileName;
                QTimer::singleShot(100ms, Global::bnpView, &BNPView::delayedOpenArchive);
                //              Global::bnpView->openArchive(fileName);
            }
        }
    }
}

void Application::setMainWindow(MainWindow *mainWindown)
{
    m_mainWindow = mainWindown;
}

void Application::onActivateRequested(const QStringList &args, const QString &workingDir)
{
    if (m_mainWindow) {
        // Restore window:
        m_mainWindow->show(); // from tray
        m_mainWindow->setWindowState(Qt::WindowActive); // from minimized
        // Raise to the top
        m_mainWindow->raise();
    }

    // KDBusService::activateRequested() forwards QCoreApplication::arguments(),
    // including argv[0].  Do not mistake the Mathom executable itself for a
    // Mathom-House archive when a second instance activates the running one.
    QStringList forwardedArgs = args;
    if (!forwardedArgs.isEmpty())
        forwardedArgs.removeFirst();

    tryLoadFile(forwardedArgs, workingDir);
}
