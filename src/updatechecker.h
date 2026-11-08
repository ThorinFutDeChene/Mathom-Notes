/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QWidget;
class QFile;
class QNetworkAccessManager;
class QProgressDialog;
class QTemporaryDir;

class UpdateChecker final : public QObject
{
public:
    static void check(QWidget *parent);
    static void checkAutomatically(QWidget *parent);

private:
    explicit UpdateChecker(QWidget *parent, bool automatic);

    void start();

    QString installedVersion() const;

    bool isVersionGreater(
        const QString &candidate,
        const QString &reference) const;

    void downloadUpdate(
        const QUrl &url,
        const QString &fileName,
        const QString &version,
        const QString &expectedSha256);

    bool verifySha256(
        const QString &path,
        const QString &expectedSha256) const;

    bool validateDebPackage(
        const QString &path,
        const QString &expectedVersion) const;

    void installUpdate(
        const QString &path,
        const QString &version);

    QWidget *m_parentWidget = nullptr;
    bool m_automatic = false;
    QNetworkAccessManager *m_network = nullptr;

    QTemporaryDir *m_tempDir = nullptr;
    QFile *m_downloadFile = nullptr;
    QProgressDialog *m_progress = nullptr;
};
