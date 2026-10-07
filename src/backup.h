/**
 * SPDX-FileCopyrightText: (C) 2003 by Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BACKUP_H
#define BACKUP_H

#include <QDialog>
#include <QThread>

class QApplication;
class QLabel;

#include "basket_export.h"

/**
 * @author Sébastien Laoût
 */
class BackupDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BackupDialog(QWidget *parent = nullptr);
    ~BackupDialog() override;

private Q_SLOTS:
    void moveToAnotherFolder();
    void useAnotherExistingFolder();
    void backup();
    void restore();
    void populateLastBackup();

private:
    QLabel *m_lastBackup = nullptr;
};

/**
 * @author Sébastien Laoût <slaout@linux62.org>
 */
class BASKET_EXPORT Backup
{
public:
    static void figureOutBinaryPath(const char *argv0, QApplication &app);
    static void setFolderAndRestart(const QString &folder, const QString &message);
    static QString newSafetyBackupPath();

private:
    static QString binaryPath;
};

class BackupThread : public QThread
{
public:
    BackupThread(const QString &backupFile, const QString &folderToBackup);

    bool success() const
    {
        return m_success;
    }

    QString errorString() const
    {
        return m_errorString;
    }

protected:
    void run() override;

private:
    QString m_backupFile;
    QString m_folderToBackup;
    QString m_errorString;
    bool m_success = false;
};

class RestoreThread : public QThread
{
public:
    RestoreThread(const QString &backupFile, const QString &stagingFolder);

    bool success() const
    {
        return m_success;
    }

    bool isLegacyBackup() const
    {
        return m_legacyBackup;
    }

    QString errorString() const
    {
        return m_errorString;
    }

    QString createdAt() const
    {
        return m_createdAt;
    }

    QString applicationVersion() const
    {
        return m_applicationVersion;
    }

    int fileCount() const
    {
        return m_fileCount;
    }

    qint64 totalBytes() const
    {
        return m_totalBytes;
    }

protected:
    void run() override;

private:
    QString m_backupFile;
    QString m_stagingFolder;
    QString m_errorString;
    QString m_createdAt;
    QString m_applicationVersion;
    int m_fileCount = 0;
    qint64 m_totalBytes = 0;
    bool m_success = false;
    bool m_legacyBackup = false;
};

#endif // BACKUP_H
