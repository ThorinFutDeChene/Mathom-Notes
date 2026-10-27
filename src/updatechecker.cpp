/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "updatechecker.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUrl>
#include <QWidget>

#include <KLocalizedString>
#include <KMessageBox>

#include "settings.h"
#include "bnpview.h"
#include "global.h"
#include "pendingsave.h"

namespace
{
const QString stableUpdateApiUrl =
    QStringLiteral(
        "https://api.github.com/repos/"
        "ThorinFutDeChene/Mathom-Notes/releases/latest");

const QString allUpdatesApiUrl =
    QStringLiteral(
        "https://api.github.com/repos/"
        "ThorinFutDeChene/Mathom-Notes/releases?per_page=20");
}

UpdateChecker::UpdateChecker(QWidget *parent)
    : QObject(parent)
    , m_parentWidget(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void UpdateChecker::check(QWidget *parent)
{
    auto *checker = new UpdateChecker(parent);
    checker->start();
}

QString UpdateChecker::installedVersion() const
{
    QProcess process;

    process.start(
        QStringLiteral("dpkg-query"),
        {
            QStringLiteral("-W"),
            QStringLiteral("-f=${Version}"),
            QStringLiteral("mathom"),
        });

    if (!process.waitForFinished(3000))
        return {};

    if (process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        return {};
    }

    return QString::fromUtf8(
               process.readAllStandardOutput())
        .trimmed();
}

bool UpdateChecker::isVersionGreater(
    const QString &candidate,
    const QString &reference) const
{
    QProcess process;

    process.start(
        QStringLiteral("dpkg"),
        {
            QStringLiteral("--compare-versions"),
            candidate,
            QStringLiteral("gt"),
            reference,
        });

    if (!process.waitForFinished(3000))
        return false;

    return process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0;
}

bool UpdateChecker::verifySha256(
    const QString &path,
    const QString &expectedSha256) const
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    QCryptographicHash hash(
        QCryptographicHash::Sha256);

    if (!hash.addData(&file))
        return false;

    const QString actual =
        QString::fromLatin1(
            hash.result().toHex());

    return actual.compare(
               expectedSha256,
               Qt::CaseInsensitive)
        == 0;
}

bool UpdateChecker::validateDebPackage(
    const QString &path,
    const QString &expectedVersion) const
{
    QProcess process;

    process.start(
        QStringLiteral("dpkg-deb"),
        {
            QStringLiteral("-f"),
            path,
            QStringLiteral("Package"),
            QStringLiteral("Version"),
            QStringLiteral("Architecture"),
        });

    if (!process.waitForFinished(5000))
        return false;

    if (process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        return false;
    }

    const QString metadata =
        QString::fromUtf8(
            process.readAllStandardOutput());

    const QRegularExpression packageRx(
        QStringLiteral(
            "(?:^|\\n)Package:\\s*mathom\\s*(?:\\n|$)"));

    const QRegularExpression versionRx(
        QStringLiteral(
            "(?:^|\\n)Version:\\s*%1\\s*(?:\\n|$)")
            .arg(
                QRegularExpression::escape(
                    expectedVersion)));

    const QRegularExpression architectureRx(
        QStringLiteral(
            "(?:^|\\n)Architecture:\\s*amd64\\s*(?:\\n|$)"));

    return packageRx.match(metadata).hasMatch()
        && versionRx.match(metadata).hasMatch()
        && architectureRx.match(metadata).hasMatch();
}

void UpdateChecker::downloadUpdate(
    const QUrl &url,
    const QString &fileName,
    const QString &version,
    const QString &expectedSha256)
{
    if (!url.isValid()
        || url.scheme() != QStringLiteral("https")
        || url.host() != QStringLiteral("github.com")) {
        KMessageBox::error(
            m_parentWidget,
            i18n("The update download address is invalid."),
            i18n("Mathom Update"));

        deleteLater();
        return;
    }

    if (expectedSha256.size() != 64) {
        KMessageBox::error(
            m_parentWidget,
            i18n(
                "The update has no valid SHA-256 "
                "integrity information."),
            i18n("Mathom Update"));

        deleteLater();
        return;
    }

    m_tempDir =
        new QTemporaryDir(
            QStringLiteral(
                "%1/mathom-update-XXXXXX")
                .arg(
                    QDir::tempPath()));

    if (!m_tempDir->isValid()) {
        KMessageBox::error(
            m_parentWidget,
            i18n(
                "Unable to create the temporary "
                "update directory."),
            i18n("Mathom Update"));

        deleteLater();
        return;
    }

    const QString path =
        m_tempDir->filePath(fileName);

    m_downloadFile =
        new QFile(path, this);

    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        KMessageBox::error(
            m_parentWidget,
            i18n(
                "Unable to create the temporary "
                "update file."),
            i18n("Mathom Update"));

        deleteLater();
        return;
    }

    m_progress =
        new QProgressDialog(
            i18n("Downloading Mathom update..."),
            i18n("Cancel"),
            0,
            100,
            m_parentWidget);

    m_progress->setWindowTitle(
        i18n("Mathom Update"));

    m_progress->setWindowModality(
        Qt::WindowModal);

    m_progress->setMinimumDuration(0);
    m_progress->setValue(0);

    QNetworkRequest request(url);

    request.setRawHeader(
        QByteArrayLiteral("User-Agent"),
        QByteArrayLiteral("Mathom-Updater"));

    QNetworkReply *reply =
        m_network->get(request);

    connect(
        reply,
        &QNetworkReply::readyRead,
        this,
        [this, reply]() {
            if (m_downloadFile)
                m_downloadFile->write(
                    reply->readAll());
        });

    connect(
        reply,
        &QNetworkReply::downloadProgress,
        this,
        [this](
            qint64 received,
            qint64 total) {
            if (!m_progress || total <= 0)
                return;

            const int percent =
                static_cast<int>(
                    (received * 100) / total);

            m_progress->setValue(percent);
        });

    connect(
        m_progress,
        &QProgressDialog::canceled,
        reply,
        &QNetworkReply::abort);

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this,
         reply,
         path,
         version,
         expectedSha256]() {
            if (m_downloadFile) {
                m_downloadFile->write(
                    reply->readAll());

                m_downloadFile->close();
            }

            if (m_progress) {
                m_progress->setValue(100);
                m_progress->deleteLater();
                m_progress = nullptr;
            }

            if (reply->error()
                != QNetworkReply::NoError) {
                const bool canceled =
                    reply->error()
                    == QNetworkReply::
                        OperationCanceledError;

                if (!canceled) {
                    KMessageBox::error(
                        m_parentWidget,
                        i18n(
                            "Unable to download "
                            "the Mathom update: %1",
                            reply->errorString()),
                        i18n("Mathom Update"));
                }

                reply->deleteLater();
                deleteLater();
                return;
            }

            reply->deleteLater();

            if (!verifySha256(
                    path,
                    expectedSha256)) {
                KMessageBox::error(
                    m_parentWidget,
                    i18n(
                        "The downloaded update failed "
                        "the SHA-256 integrity check. "
                        "It will not be installed."),
                    i18n("Mathom Update"));

                deleteLater();
                return;
            }

            if (!validateDebPackage(
                    path,
                    version)) {
                KMessageBox::error(
                    m_parentWidget,
                    i18n(
                        "The downloaded file is not "
                        "the expected Mathom Debian package. "
                        "It will not be installed."),
                    i18n("Mathom Update"));

                deleteLater();
                return;
            }

            installUpdate(
                path,
                version);
        });
}

void UpdateChecker::installUpdate(
    const QString &path,
    const QString &version)
{
    auto *process =
        new QProcess(this);

    process->setProgram(
        QStringLiteral("pkexec"));

    process->setArguments(
        {
            QStringLiteral("/usr/bin/apt-get"),
            QStringLiteral("install"),
            QStringLiteral("-y"),
            QStringLiteral("--no-remove"),
            path,
        });

    m_progress =
        new QProgressDialog(
            i18n(
                "Installing Mathom %1...",
                version),
            QString(),
            0,
            0,
            m_parentWidget);

    m_progress->setWindowTitle(
        i18n("Mathom Update"));

    m_progress->setCancelButton(nullptr);
    m_progress->setWindowModality(
        Qt::WindowModal);

    m_progress->setMinimumDuration(0);
    m_progress->show();

    connect(
        process,
        &QProcess::finished,
        this,
        [this, process, version](
            int exitCode,
            QProcess::ExitStatus exitStatus) {
            if (m_progress) {
                m_progress->close();
                m_progress->deleteLater();
                m_progress = nullptr;
            }

            if (exitStatus
                    != QProcess::NormalExit
                || exitCode != 0) {
                KMessageBox::error(
                    m_parentWidget,
                    i18n(
                        "Mathom %1 could not be installed. "
                        "The existing installation has "
                        "not been replaced.",
                        version),
                    i18n("Mathom Update"));

                process->deleteLater();
                deleteLater();
                return;
            }

            process->deleteLater();

            const auto answer =
                QMessageBox::question(
                    m_parentWidget,
                    i18n("Mathom Update"),
                    i18n(
                        "Mathom %1 has been installed "
                        "successfully.\n\n"
                        "Restart Mathom now?",
                        version),
                    QMessageBox::Yes
                        | QMessageBox::No,
                    QMessageBox::Yes);

            if (answer == QMessageBox::Yes) {
                if (Global::bnpView)
                    Global::bnpView->closeAllEditors();

                if (!PendingSave::retryPendingSaves()) {
                    KMessageBox::error(
                        m_parentWidget,
                        i18n(
                            "Some changes could not be saved. "
                            "Mathom Notes will stay open and retry automatically. "
                            "Restart after the changes have been saved."),
                        i18n("Changes Not Saved"));

                    deleteLater();
                    return;
                }

                // Wait until this Mathom process has fully exited before
                // starting the new one.  Mathom is a KDBusService::Unique
                // application: starting too early would activate this old
                // instance, make the duplicate process exit, and leave no
                // process to restart after shutdown.
                const qint64 currentPid =
                    QCoreApplication::applicationPid();

                const QString restartCommand =
                    QStringLiteral(
                        "while kill -0 %1 2>/dev/null; "
                        "do sleep 0.1; done; "
                        "exec /usr/bin/mathom")
                        .arg(currentPid);

                QProcess::startDetached(
                    QStringLiteral("/bin/sh"),
                    {
                        QStringLiteral("-c"),
                        restartCommand,
                    });

                QCoreApplication::quit();
                return;
            }

            deleteLater();
        });

    process->start();

    if (!process->waitForStarted(3000)) {
        if (m_progress) {
            m_progress->close();
            m_progress->deleteLater();
            m_progress = nullptr;
        }

        KMessageBox::error(
            m_parentWidget,
            i18n(
                "Unable to start the privileged "
                "Mathom installer."),
            i18n("Mathom Update"));

        process->deleteLater();
        deleteLater();
    }
}

void UpdateChecker::start()
{
    const QString currentVersion =
        installedVersion();

    if (currentVersion.isEmpty()) {
        KMessageBox::error(
            m_parentWidget,
            i18n(
                "Unable to determine the installed "
                "Mathom Debian package version."),
            i18n("Mathom Update"));

        deleteLater();
        return;
    }

    const bool allowDevelopment =
        Settings::allowDevelopmentUpdates();

    QNetworkRequest request{
        QUrl(
            allowDevelopment
                ? allUpdatesApiUrl
                : stableUpdateApiUrl)
    };

    request.setRawHeader(
        QByteArrayLiteral("Accept"),
        QByteArrayLiteral(
            "application/vnd.github+json"));

    request.setRawHeader(
        QByteArrayLiteral("User-Agent"),
        QByteArrayLiteral("Mathom-Updater"));

    request.setRawHeader(
        QByteArrayLiteral(
            "X-GitHub-Api-Version"),
        QByteArrayLiteral("2022-11-28"));

    QNetworkReply *reply =
        m_network->get(request);

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this,
         reply,
         currentVersion,
         allowDevelopment]() {
            if (reply->error()
                != QNetworkReply::NoError) {
                KMessageBox::error(
                    m_parentWidget,
                    i18n(
                        "Unable to contact the "
                        "Mathom update server: %1",
                        reply->errorString()),
                    i18n("Mathom Update"));

                reply->deleteLater();
                deleteLater();
                return;
            }

            const QByteArray data =
                reply->readAll();

            reply->deleteLater();

            QJsonParseError parseError;

            const QJsonDocument document =
                QJsonDocument::fromJson(
                    data,
                    &parseError);

            if (parseError.error
                != QJsonParseError::NoError) {
                KMessageBox::error(
                    m_parentWidget,
                    i18n(
                        "The update server returned "
                        "invalid data."),
                    i18n("Mathom Update"));

                deleteLater();
                return;
            }

            QList<QJsonObject> releases;

            if (document.isObject()) {
                releases.append(document.object());
            } else if (document.isArray()) {
                const QJsonArray array = document.array();
                for (const QJsonValue &value : array) {
                    if (value.isObject())
                        releases.append(value.toObject());
                }
            } else {
                KMessageBox::error(
                    m_parentWidget,
                    i18n(
                        "The update server returned "
                        "invalid data."),
                    i18n("Mathom Update"));

                deleteLater();
                return;
            }

            const QRegularExpression pattern(
                QStringLiteral(
                    "^mathom_(.+)_amd64\\.deb$"));

            const QRegularExpression developmentVersionPattern(
                QStringLiteral(
                    "^\\d+(?:\\.\\d+)*-0dev\\d+$"));

            QString newestStableVersion;
            QString newestStableFileName;
            QString newestStableSha256;
            QUrl newestStableUrl;

            QString newestDevelopmentVersion;
            QString newestDevelopmentFileName;
            QString newestDevelopmentSha256;
            QUrl newestDevelopmentUrl;

            for (const QJsonObject &release : releases) {
                if (release.value(QStringLiteral("draft")).toBool())
                    continue;

                const bool prerelease =
                    release.value(QStringLiteral("prerelease")).toBool();

                const QJsonArray assets =
                    release.value(
                        QStringLiteral("assets"))
                        .toArray();

                for (const QJsonValue &value : assets) {
                    const QJsonObject asset =
                        value.toObject();

                    const QString fileName =
                        asset.value(
                            QStringLiteral("name"))
                            .toString();

                    const auto match =
                        pattern.match(fileName);

                    if (!match.hasMatch())
                        continue;

                    const QString version =
                        match.captured(1);

                    const QString digest =
                        asset.value(
                            QStringLiteral("digest"))
                            .toString();

                    const QString prefix =
                        QStringLiteral("sha256:");

                    if (!digest.startsWith(
                            prefix,
                            Qt::CaseInsensitive)) {
                        continue;
                    }

                    const QUrl url(
                        asset.value(
                            QStringLiteral(
                                "browser_download_url"))
                            .toString());

                    if (prerelease) {
                        // Development packages use a GitHub-safe Debian
                        // version such as 0.1.8-0dev2.  Reject malformed
                        // prerelease asset names (for example a '~' silently
                        // rewritten to '.') so they cannot outrank the future
                        // stable release.
                        if (!developmentVersionPattern.match(version).hasMatch())
                            continue;

                        if (!newestDevelopmentVersion.isEmpty()
                            && !isVersionGreater(
                                version,
                                newestDevelopmentVersion)) {
                            continue;
                        }

                        newestDevelopmentVersion = version;
                        newestDevelopmentFileName = fileName;
                        newestDevelopmentSha256 =
                            digest.mid(prefix.size());
                        newestDevelopmentUrl = url;
                    } else {
                        if (!newestStableVersion.isEmpty()
                            && !isVersionGreater(
                                version,
                                newestStableVersion)) {
                            continue;
                        }

                        newestStableVersion = version;
                        newestStableFileName = fileName;
                        newestStableSha256 =
                            digest.mid(prefix.size());
                        newestStableUrl = url;
                    }
                }
            }

            QString newestVersion = newestStableVersion;
            QString newestFileName = newestStableFileName;
            QString newestSha256 = newestStableSha256;
            QUrl newestUrl = newestStableUrl;
            bool selectedDevelopment = false;

            if (allowDevelopment
                && !newestDevelopmentVersion.isEmpty()
                && (newestStableVersion.isEmpty()
                    || isVersionGreater(
                        newestDevelopmentVersion,
                        newestStableVersion))) {
                newestVersion = newestDevelopmentVersion;
                newestFileName = newestDevelopmentFileName;
                newestSha256 = newestDevelopmentSha256;
                newestUrl = newestDevelopmentUrl;
                selectedDevelopment = true;
            }

            if (newestVersion.isEmpty()) {
                KMessageBox::error(
                    m_parentWidget,
                    i18n(
                        "No valid Mathom Debian package "
                        "was found in the available releases."),
                    i18n("Mathom Update"));

                deleteLater();
                return;
            }

            QString versions =
                i18n(
                    "Installed version: %1",
                    currentVersion)
                + QStringLiteral("\n")
                + i18n(
                    "Available version: %1",
                    newestVersion);

            if (selectedDevelopment) {
                versions += QStringLiteral("\n")
                    + i18n("Update channel: development");
            } else {
                versions += QStringLiteral("\n")
                    + i18n("Update channel: stable");
            }

            if (!isVersionGreater(
                    newestVersion,
                    currentVersion)) {
                KMessageBox::information(
                    m_parentWidget,
                    i18n(
                        "Mathom is up to date.")
                        + QStringLiteral("\n\n")
                        + versions,
                    i18n("Mathom Update"));

                deleteLater();
                return;
            }

            const QString updateMessage =
                selectedDevelopment
                    ? i18n("A Mathom development update is available.")
                    : i18n("A Mathom update is available.");

            const auto answer =
                QMessageBox::question(
                    m_parentWidget,
                    i18n("Mathom Update"),
                    updateMessage
                        + QStringLiteral("\n\n")
                        + versions
                        + QStringLiteral("\n\n")
                        + i18n(
                            "Download and install it now?"),
                    QMessageBox::Yes
                        | QMessageBox::No,
                    QMessageBox::Yes);

            if (answer != QMessageBox::Yes) {
                deleteLater();
                return;
            }

            downloadUpdate(
                newestUrl,
                newestFileName,
                newestVersion,
                newestSha256);
        });
}
