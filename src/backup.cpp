/**
 * SPDX-FileCopyrightText: (C) 2003 by Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "backup.h"

#include "bnpview.h"
#include "diagnosticmanager.h"
#include "formatimporter.h"
#include "global.h"
#include "settings.h"
#include "tools.h"
#include "variouswidgets.h"

#include <algorithm>

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

#include <KAboutData>
#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>
#include <KTar>

#include <KIO/CommandLauncherJob>

#include <unistd.h>

namespace
{

const QString fullBackupMagicFolder =
    QStringLiteral("MathomBackup");

const QString legacyBackupMagicFolder =
    QStringLiteral("BasKet-Note-Pads_Backup");

constexpr int fullBackupFormatVersion = 1;

const QStringList excludedTransientDataFolders = {
    QStringLiteral("diagnostics"),
    QStringLiteral("temp-cut")
};

struct ManifestFile
{
    QString path;
    qint64 size = 0;
    QString sha256;
};

struct ConfigPaths
{
    QString mathomRc;
    QString appConfig;
    QString kxmlgui5;
    QString kxmlgui6;
    QString globalShortcuts;
};

ConfigPaths currentConfigPaths()
{
    const QString configRoot =
        QStandardPaths::writableLocation(
            QStandardPaths::ConfigLocation);

    const QString genericDataRoot =
        QStandardPaths::writableLocation(
            QStandardPaths::GenericDataLocation);

    return {
        QDir(configRoot).filePath(
            QStringLiteral("mathomrc")),
        QStandardPaths::writableLocation(
            QStandardPaths::AppConfigLocation),
        QDir(genericDataRoot).filePath(
            QStringLiteral("kxmlgui5/mathom")),
        QDir(genericDataRoot).filePath(
            QStringLiteral("kxmlgui6/mathom")),
        QDir(configRoot).filePath(
            QStringLiteral("kglobalshortcutsrc"))
    };
}

bool pathExists(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

bool removePath(const QString &path)
{
    const QFileInfo info(path);

    if (!info.exists() && !info.isSymLink())
        return true;

    if (info.isSymLink() || info.isFile())
        return QFile::remove(path);

    if (info.isDir())
        return QDir(path).removeRecursively();

    return false;
}

bool isExcludedDataPath(const QString &relativePath)
{
    const QString clean =
        QDir::cleanPath(relativePath);

    for (const QString &excluded :
         excludedTransientDataFolders) {
        if (clean == excluded
            || clean.startsWith(
                excluded + QLatin1Char('/'))) {
            return true;
        }
    }

    return false;
}

bool copyFileReplacing(
    const QString &source,
    const QString &destination,
    QString *errorString)
{
    if (!QFileInfo::exists(source)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Missing source file: %1")
                    .arg(source);
        }
        return false;
    }

    if (!QDir().mkpath(
            QFileInfo(destination).absolutePath())) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot create destination folder for %1")
                    .arg(destination);
        }
        return false;
    }

    if (pathExists(destination)
        && !removePath(destination)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot replace %1")
                    .arg(destination);
        }
        return false;
    }

    if (!QFile::copy(source, destination)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot copy %1 to %2")
                    .arg(source, destination);
        }
        return false;
    }

    return true;
}

bool copyDirectoryRecursively(
    const QString &sourcePath,
    const QString &destinationPath,
    const bool excludeTransientData,
    QString *errorString)
{
    QDir sourceDir(sourcePath);

    if (!sourceDir.exists()) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Missing source folder: %1")
                    .arg(sourcePath);
        }
        return false;
    }

    if (!QDir().mkpath(destinationPath)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot create destination folder: %1")
                    .arg(destinationPath);
        }
        return false;
    }

    QDirIterator iterator(
        sourcePath,
        QDir::NoDotAndDotDot
            | QDir::AllEntries
            | QDir::Hidden
            | QDir::System,
        QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        const QString sourceItem =
            iterator.next();

        const QFileInfo info =
            iterator.fileInfo();

        const QString relativePath =
            sourceDir.relativeFilePath(sourceItem);

        if (excludeTransientData
            && isExcludedDataPath(relativePath)) {
            continue;
        }

        const QString destinationItem =
            QDir(destinationPath)
                .filePath(relativePath);

        if (info.isDir() && !info.isSymLink()) {
            if (!QDir().mkpath(destinationItem)) {
                if (errorString) {
                    *errorString =
                        QStringLiteral(
                            "Cannot create folder: %1")
                            .arg(destinationItem);
                }
                return false;
            }
            continue;
        }

        if (info.isFile() || info.isSymLink()) {
            if (!QDir().mkpath(
                    QFileInfo(destinationItem)
                        .absolutePath())) {
                if (errorString) {
                    *errorString =
                        QStringLiteral(
                            "Cannot create folder for: %1")
                            .arg(destinationItem);
                }
                return false;
            }

            if (!QFile::copy(
                    sourceItem,
                    destinationItem)) {
                if (errorString) {
                    *errorString =
                        QStringLiteral(
                            "Cannot copy %1")
                            .arg(sourceItem);
                }
                return false;
            }
        }
    }

    return true;
}

QString sha256File(
    const QString &path,
    QString *errorString)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot read %1")
                    .arg(path);
        }
        return {};
    }

    QCryptographicHash hash(
        QCryptographicHash::Sha256);

    while (!file.atEnd()) {
        const QByteArray data =
            file.read(1024 * 1024);

        if (data.isEmpty()
            && file.error()
                != QFileDevice::NoError) {
            if (errorString) {
                *errorString =
                    QStringLiteral(
                        "Read error on %1")
                        .arg(path);
            }
            return {};
        }

        hash.addData(data);
    }

    return QString::fromLatin1(
        hash.result().toHex());
}

bool safeManifestPath(const QString &path)
{
    if (path.isEmpty()
        || QDir::isAbsolutePath(path)) {
        return false;
    }

    const QString clean =
        QDir::cleanPath(path);

    if (clean == QStringLiteral("..")
        || clean.startsWith(
            QStringLiteral("../"))
        || clean.contains(
            QStringLiteral("/../"))) {
        return false;
    }

    return clean == path
        || QDir::fromNativeSeparators(clean)
            == QDir::fromNativeSeparators(path);
}

QVector<ManifestFile> collectManifestFiles(
    const QString &snapshotRoot,
    QString *errorString)
{
    QVector<ManifestFile> files;
    QDir root(snapshotRoot);

    QDirIterator iterator(
        snapshotRoot,
        QDir::NoDotAndDotDot
            | QDir::Files
            | QDir::Hidden
            | QDir::System,
        QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        const QString absolutePath =
            iterator.next();

        const QString relativePath =
            QDir::fromNativeSeparators(
                root.relativeFilePath(
                    absolutePath));

        if (relativePath
            == QStringLiteral(
                "manifest.json")) {
            continue;
        }

        QString hashError;
        const QString hash =
            sha256File(
                absolutePath,
                &hashError);

        if (hash.isEmpty()) {
            if (errorString)
                *errorString = hashError;
            return {};
        }

        files.append({
            relativePath,
            QFileInfo(absolutePath).size(),
            hash
        });
    }

    std::sort(
        files.begin(),
        files.end(),
        [](const ManifestFile &left,
           const ManifestFile &right) {
            return left.path < right.path;
        });

    return files;
}

bool exportMathomGlobalShortcuts(
    const QString &destination,
    QString *errorString)
{
    const ConfigPaths paths =
        currentConfigPaths();

    if (!QFileInfo::exists(
            paths.globalShortcuts)) {
        return true;
    }

    KConfig source(
        paths.globalShortcuts,
        KConfig::SimpleConfig);

    const QStringList groups = {
        QStringLiteral("mathom"),
        QStringLiteral("fr.thorinux.mathom")
    };

    bool copiedSomething = false;

    if (!QDir().mkpath(
            QFileInfo(destination)
                .absolutePath())) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot create shortcut backup folder");
        }
        return false;
    }

    KConfig target(
        destination,
        KConfig::SimpleConfig);

    for (const QString &groupName :
         groups) {
        if (!source.hasGroup(groupName))
            continue;

        const KConfigGroup sourceGroup(
            &source,
            groupName);

        KConfigGroup targetGroup(
            &target,
            groupName);

        const QMap<QString, QString> entries =
            sourceGroup.entryMap();

        for (auto iterator =
                 entries.constBegin();
             iterator
             != entries.constEnd();
             ++iterator) {
            targetGroup.writeEntry(
                iterator.key(),
                iterator.value());
        }

        copiedSomething = true;
    }

    target.sync();

    if (!copiedSomething)
        QFile::remove(destination);

    return true;
}

bool createSnapshot(
    const QString &snapshotRoot,
    const QString &dataFolder,
    QString *errorString)
{
    const QString dataDestination =
        QDir(snapshotRoot)
            .filePath(
                QStringLiteral("data"));

    const QString configDestination =
        QDir(snapshotRoot)
            .filePath(
                QStringLiteral("config"));

    if (!copyDirectoryRecursively(
            dataFolder,
            dataDestination,
            true,
            errorString)) {
        return false;
    }

    if (!QDir().mkpath(
            configDestination)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot create snapshot configuration folder");
        }
        return false;
    }

    const ConfigPaths paths =
        currentConfigPaths();

    if (QFileInfo::exists(paths.mathomRc)
        && !copyFileReplacing(
            paths.mathomRc,
            QDir(configDestination)
                .filePath(
                    QStringLiteral("mathomrc")),
            errorString)) {
        return false;
    }

    if (QDir(paths.appConfig).exists()) {
        if (!copyDirectoryRecursively(
                paths.appConfig,
                QDir(configDestination)
                    .filePath(
                        QStringLiteral(
                            "app-config")),
                false,
                errorString)) {
            return false;
        }
    }

    if (QDir(paths.kxmlgui5).exists()) {
        if (!copyDirectoryRecursively(
                paths.kxmlgui5,
                QDir(configDestination)
                    .filePath(
                        QStringLiteral(
                            "kxmlgui5")),
                false,
                errorString)) {
            return false;
        }
    }

    if (QDir(paths.kxmlgui6).exists()) {
        if (!copyDirectoryRecursively(
                paths.kxmlgui6,
                QDir(configDestination)
                    .filePath(
                        QStringLiteral(
                            "kxmlgui6")),
                false,
                errorString)) {
            return false;
        }
    }

    if (!exportMathomGlobalShortcuts(
            QDir(configDestination)
                .filePath(
                    QStringLiteral(
                        "kglobalshortcutsrc.mathom")),
            errorString)) {
        return false;
    }

    return true;
}

bool writeManifest(
    const QString &snapshotRoot,
    QString *errorString)
{
    const QVector<ManifestFile> files =
        collectManifestFiles(
            snapshotRoot,
            errorString);

    if (errorString
        && !errorString->isEmpty()) {
        return false;
    }

    QJsonArray jsonFiles;
    qint64 totalBytes = 0;
    int dataFiles = 0;
    int configFiles = 0;

    for (const ManifestFile &file :
         files) {
        QJsonObject item;
        item.insert(
            QStringLiteral("path"),
            file.path);
        item.insert(
            QStringLiteral("size"),
            static_cast<double>(
                file.size));
        item.insert(
            QStringLiteral("sha256"),
            file.sha256);

        jsonFiles.append(item);

        totalBytes += file.size;

        if (file.path.startsWith(
                QStringLiteral("data/"))) {
            ++dataFiles;
        } else if (file.path.startsWith(
                       QStringLiteral(
                           "config/"))) {
            ++configFiles;
        }
    }

    QJsonObject inventory;
    inventory.insert(
        QStringLiteral("file_count"),
        files.size());
    inventory.insert(
        QStringLiteral("data_files"),
        dataFiles);
    inventory.insert(
        QStringLiteral("config_files"),
        configFiles);
    inventory.insert(
        QStringLiteral("total_bytes"),
        static_cast<double>(
            totalBytes));

    QJsonObject manifest;
    manifest.insert(
        QStringLiteral("format"),
        QStringLiteral("MathomBackup"));
    manifest.insert(
        QStringLiteral("format_version"),
        fullBackupFormatVersion);
    manifest.insert(
        QStringLiteral("type"),
        QStringLiteral("full"));
    manifest.insert(
        QStringLiteral("created_at"),
        QDateTime::currentDateTimeUtc()
            .toString(Qt::ISODate));
    manifest.insert(
        QStringLiteral(
            "application_version"),
        KAboutData::applicationData()
            .version());
    manifest.insert(
        QStringLiteral("inventory"),
        inventory);
    manifest.insert(
        QStringLiteral("files"),
        jsonFiles);

    QSaveFile manifestFile(
        QDir(snapshotRoot)
            .filePath(
                QStringLiteral(
                    "manifest.json")));

    if (!manifestFile.open(
            QIODevice::WriteOnly)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot create backup manifest");
        }
        return false;
    }

    const QByteArray data =
        QJsonDocument(manifest)
            .toJson(
                QJsonDocument::Indented);

    if (manifestFile.write(data)
        != data.size()
        || !manifestFile.commit()) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot write backup manifest");
        }
        return false;
    }

    return true;
}

bool createTarFromSnapshot(
    const QString &snapshotRoot,
    const QString &destination,
    QString *errorString)
{
    KTar tar(
        destination,
        QStringLiteral(
            "application/x-gzip"));

    if (!tar.open(
            QIODevice::WriteOnly)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot create backup archive");
        }
        return false;
    }

    QDir root(snapshotRoot);

    QDirIterator iterator(
        snapshotRoot,
        QDir::NoDotAndDotDot
            | QDir::Files
            | QDir::Hidden
            | QDir::System,
        QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        const QString localFile =
            iterator.next();

        const QString relativePath =
            QDir::fromNativeSeparators(
                root.relativeFilePath(
                    localFile));

        const QString archivePath =
            fullBackupMagicFolder
            + QLatin1Char('/')
            + relativePath;

        if (!tar.addLocalFile(
                localFile,
                archivePath)) {
            tar.close();

            if (errorString) {
                *errorString =
                    QStringLiteral(
                        "Cannot add %1 to backup archive")
                        .arg(relativePath);
            }
            return false;
        }
    }

    if (!tar.close()) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot finalize backup archive");
        }
        return false;
    }

    return true;
}

bool parseAndValidateSnapshot(
    const QString &snapshotRoot,
    QString *createdAt,
    QString *applicationVersion,
    int *fileCount,
    qint64 *totalBytes,
    QString *errorString)
{
    const QString manifestPath =
        QDir(snapshotRoot)
            .filePath(
                QStringLiteral(
                    "manifest.json"));

    QFile manifestFile(manifestPath);

    if (!manifestFile.open(
            QIODevice::ReadOnly)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Backup manifest is missing");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            manifestFile.readAll(),
            &parseError);

    if (parseError.error
            != QJsonParseError::NoError
        || !document.isObject()) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Backup manifest is invalid");
        }
        return false;
    }

    const QJsonObject manifest =
        document.object();

    if (manifest.value(
            QStringLiteral("format"))
            .toString()
            != QStringLiteral(
                "MathomBackup")
        || manifest.value(
               QStringLiteral(
                   "format_version"))
               .toInt(-1)
            != fullBackupFormatVersion
        || manifest.value(
               QStringLiteral("type"))
               .toString()
            != QStringLiteral("full")) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Unsupported Mathom backup format");
        }
        return false;
    }

    const QJsonArray jsonFiles =
        manifest.value(
            QStringLiteral("files"))
            .toArray();

    QSet<QString> expectedPaths;
    qint64 computedBytes = 0;

    for (const QJsonValue &value :
         jsonFiles) {
        if (!value.isObject()) {
            if (errorString) {
                *errorString =
                    QStringLiteral(
                        "Invalid file entry in backup manifest");
            }
            return false;
        }

        const QJsonObject item =
            value.toObject();

        const QString relativePath =
            item.value(
                QStringLiteral("path"))
                .toString();

        if (!safeManifestPath(
                relativePath)
            || expectedPaths.contains(
                relativePath)) {
            if (errorString) {
                *errorString =
                    QStringLiteral(
                        "Unsafe or duplicate path in backup manifest");
            }
            return false;
        }

        const QString absolutePath =
            QDir(snapshotRoot)
                .filePath(
                    relativePath);

        const QFileInfo info(
            absolutePath);

        if (!info.exists()
            || !info.isFile()) {
            if (errorString) {
                *errorString =
                    QStringLiteral(
                        "Backup file is missing: %1")
                        .arg(
                            relativePath);
            }
            return false;
        }

        const qint64 expectedSize =
            static_cast<qint64>(
                item.value(
                    QStringLiteral("size"))
                    .toDouble(-1));

        if (expectedSize < 0
            || info.size()
                != expectedSize) {
            if (errorString) {
                *errorString =
                    QStringLiteral(
                        "Backup file size mismatch: %1")
                        .arg(
                            relativePath);
            }
            return false;
        }

        QString hashError;
        const QString actualHash =
            sha256File(
                absolutePath,
                &hashError);

        if (actualHash.isEmpty()
            || actualHash.compare(
                   item.value(
                       QStringLiteral(
                           "sha256"))
                       .toString(),
                   Qt::CaseInsensitive)
                != 0) {
            if (errorString) {
                *errorString =
                    hashError.isEmpty()
                    ? QStringLiteral(
                          "Backup checksum mismatch: %1")
                          .arg(
                              relativePath)
                    : hashError;
            }
            return false;
        }

        expectedPaths.insert(
            relativePath);

        computedBytes +=
            expectedSize;
    }

    QSet<QString> actualPaths;
    QDir root(snapshotRoot);

    QDirIterator iterator(
        snapshotRoot,
        QDir::NoDotAndDotDot
            | QDir::Files
            | QDir::Hidden
            | QDir::System,
        QDirIterator::Subdirectories);

    while (iterator.hasNext()) {
        const QString localFile =
            iterator.next();

        const QString relativePath =
            QDir::fromNativeSeparators(
                root.relativeFilePath(
                    localFile));

        if (relativePath
            == QStringLiteral(
                "manifest.json")) {
            continue;
        }

        actualPaths.insert(
            relativePath);
    }

    if (actualPaths != expectedPaths) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Backup content does not match its manifest");
        }
        return false;
    }

    const QJsonObject inventory =
        manifest.value(
            QStringLiteral(
                "inventory"))
            .toObject();

    const int manifestFileCount =
        inventory.value(
            QStringLiteral(
                "file_count"))
            .toInt(-1);

    const qint64 manifestBytes =
        static_cast<qint64>(
            inventory.value(
                QStringLiteral(
                    "total_bytes"))
                .toDouble(-1));

    if (manifestFileCount
            != expectedPaths.size()
        || manifestBytes
            != computedBytes) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Backup inventory does not match its content");
        }
        return false;
    }

    if (createdAt) {
        *createdAt =
            manifest.value(
                QStringLiteral(
                    "created_at"))
                .toString();
    }

    if (applicationVersion) {
        *applicationVersion =
            manifest.value(
                QStringLiteral(
                    "application_version"))
                .toString();
    }

    if (fileCount)
        *fileCount = manifestFileCount;

    if (totalBytes)
        *totalBytes = manifestBytes;

    return true;
}

bool extractFullBackup(
    const QString &archivePath,
    const QString &destination,
    QString *createdAt,
    QString *applicationVersion,
    int *fileCount,
    qint64 *totalBytes,
    QString *errorString)
{
    KTar tar(
        archivePath,
        QStringLiteral(
            "application/x-gzip"));

    if (!tar.open(
            QIODevice::ReadOnly)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot open backup archive");
        }
        return false;
    }

    const KArchiveDirectory *root =
        tar.directory();

    if (!root
        || !root->entries().contains(
            fullBackupMagicFolder)) {
        tar.close();

        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Full backup root is missing");
        }
        return false;
    }

    const KArchiveEntry *entry =
        root->entry(
            fullBackupMagicFolder);

    if (!entry || !entry->isDirectory()) {
        tar.close();

        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Full backup root is invalid");
        }
        return false;
    }

    if (!QDir().mkpath(destination)) {
        tar.close();

        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot create restore staging folder");
        }
        return false;
    }

    static_cast<
        const KArchiveDirectory *>(
            entry)
        ->copyTo(destination);

    tar.close();

    return parseAndValidateSnapshot(
        destination,
        createdAt,
        applicationVersion,
        fileCount,
        totalBytes,
        errorString);
}

bool extractLegacyBackup(
    const QString &archivePath,
    const QString &destination,
    QString *errorString)
{
    KTar tar(
        archivePath,
        QStringLiteral(
            "application/x-gzip"));

    if (!tar.open(
            QIODevice::ReadOnly)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot open legacy backup archive");
        }
        return false;
    }

    const KArchiveDirectory *root =
        tar.directory();

    if (!root
        || !root->entries().contains(
            legacyBackupMagicFolder)) {
        tar.close();

        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Legacy backup root is missing");
        }
        return false;
    }

    const KArchiveEntry *entry =
        root->entry(
            legacyBackupMagicFolder);

    if (!entry || !entry->isDirectory()) {
        tar.close();

        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Legacy backup root is invalid");
        }
        return false;
    }

    if (!QDir().mkpath(destination)) {
        tar.close();

        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot create restore staging folder");
        }
        return false;
    }

    static_cast<
        const KArchiveDirectory *>(
            entry)
        ->copyTo(destination);

    tar.close();

    const QString basketsTree =
        QDir(destination)
            .filePath(
                QStringLiteral(
                    "baskets/baskets.xml"));

    if (!QFileInfo::exists(
            basketsTree)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Legacy backup does not contain a Mathom tree");
        }
        return false;
    }

    return true;
}

bool archiveHasTopLevelEntry(
    const QString &archivePath,
    const QString &entryName)
{
    KTar tar(
        archivePath,
        QStringLiteral(
            "application/x-gzip"));

    if (!tar.open(
            QIODevice::ReadOnly)) {
        return false;
    }

    const KArchiveDirectory *root =
        tar.directory();

    const bool found =
        root
        && root->entries()
               .contains(entryName);

    tar.close();

    return found;
}

bool restoreGlobalShortcuts(
    const QString &subsetPath,
    QString *errorString)
{
    const ConfigPaths paths =
        currentConfigPaths();

    if (!QDir().mkpath(
            QFileInfo(
                paths.globalShortcuts)
                .absolutePath())) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot create configuration folder for global shortcuts");
        }
        return false;
    }

    KConfig target(
        paths.globalShortcuts,
        KConfig::SimpleConfig);

    const QStringList groups = {
        QStringLiteral("mathom"),
        QStringLiteral("fr.thorinux.mathom")
    };

    for (const QString &groupName :
         groups) {
        KConfigGroup targetGroup(
            &target,
            groupName);

        targetGroup.deleteGroup();
    }

    if (QFileInfo::exists(
            subsetPath)) {
        KConfig source(
            subsetPath,
            KConfig::SimpleConfig);

        for (const QString &groupName :
             groups) {
            if (!source.hasGroup(groupName))
                continue;

            const KConfigGroup sourceGroup(
                &source,
                groupName);

            KConfigGroup targetGroup(
                &target,
                groupName);

            const QMap<QString, QString> entries =
                sourceGroup.entryMap();

            for (auto iterator =
                     entries.constBegin();
                 iterator
                 != entries.constEnd();
                 ++iterator) {
                targetGroup.writeEntry(
                    iterator.key(),
                    iterator.value());
            }
        }
    }

    target.sync();
    return true;
}

struct ConfigRollbackState
{
    bool mathomRc = false;
    bool appConfig = false;
    bool kxmlgui5 = false;
    bool kxmlgui6 = false;
    bool globalShortcuts = false;
};

bool snapshotConfigForRollback(
    const QString &rollbackRoot,
    ConfigRollbackState *state,
    QString *errorString)
{
    if (!state)
        return false;

    const ConfigPaths paths =
        currentConfigPaths();

    if (!QDir().mkpath(rollbackRoot)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot create configuration rollback folder");
        }
        return false;
    }

    if (QFileInfo::exists(
            paths.mathomRc)) {
        state->mathomRc = true;

        if (!copyFileReplacing(
                paths.mathomRc,
                QDir(rollbackRoot)
                    .filePath(
                        QStringLiteral(
                            "mathomrc")),
                errorString)) {
            return false;
        }
    }

    if (QDir(paths.appConfig).exists()) {
        state->appConfig = true;

        if (!copyDirectoryRecursively(
                paths.appConfig,
                QDir(rollbackRoot)
                    .filePath(
                        QStringLiteral(
                            "app-config")),
                false,
                errorString)) {
            return false;
        }
    }

    if (QDir(paths.kxmlgui5).exists()) {
        state->kxmlgui5 = true;

        if (!copyDirectoryRecursively(
                paths.kxmlgui5,
                QDir(rollbackRoot)
                    .filePath(
                        QStringLiteral(
                            "kxmlgui5")),
                false,
                errorString)) {
            return false;
        }
    }

    if (QDir(paths.kxmlgui6).exists()) {
        state->kxmlgui6 = true;

        if (!copyDirectoryRecursively(
                paths.kxmlgui6,
                QDir(rollbackRoot)
                    .filePath(
                        QStringLiteral(
                            "kxmlgui6")),
                false,
                errorString)) {
            return false;
        }
    }

    if (QFileInfo::exists(
            paths.globalShortcuts)) {
        state->globalShortcuts = true;

        if (!copyFileReplacing(
                paths.globalShortcuts,
                QDir(rollbackRoot)
                    .filePath(
                        QStringLiteral(
                            "kglobalshortcutsrc")),
                errorString)) {
            return false;
        }
    }

    return true;
}

bool replaceOptionalFile(
    const QString &stagedPath,
    const QString &destination,
    QString *errorString)
{
    if (pathExists(destination)
        && !removePath(destination)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot remove current configuration file: %1")
                    .arg(destination);
        }
        return false;
    }

    if (!QFileInfo::exists(stagedPath))
        return true;

    return copyFileReplacing(
        stagedPath,
        destination,
        errorString);
}

bool replaceOptionalDirectory(
    const QString &stagedPath,
    const QString &destination,
    QString *errorString)
{
    if (pathExists(destination)
        && !removePath(destination)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot remove current configuration folder: %1")
                    .arg(destination);
        }
        return false;
    }

    if (!QDir(stagedPath).exists())
        return true;

    return copyDirectoryRecursively(
        stagedPath,
        destination,
        false,
        errorString);
}

bool applyStagedConfig(
    const QString &stagedConfigRoot,
    QString *errorString)
{
    const ConfigPaths paths =
        currentConfigPaths();

    if (!replaceOptionalFile(
            QDir(stagedConfigRoot)
                .filePath(
                    QStringLiteral(
                        "mathomrc")),
            paths.mathomRc,
            errorString)) {
        return false;
    }

    if (!replaceOptionalDirectory(
            QDir(stagedConfigRoot)
                .filePath(
                    QStringLiteral(
                        "app-config")),
            paths.appConfig,
            errorString)) {
        return false;
    }

    if (!replaceOptionalDirectory(
            QDir(stagedConfigRoot)
                .filePath(
                    QStringLiteral(
                        "kxmlgui5")),
            paths.kxmlgui5,
            errorString)) {
        return false;
    }

    if (!replaceOptionalDirectory(
            QDir(stagedConfigRoot)
                .filePath(
                    QStringLiteral(
                        "kxmlgui6")),
            paths.kxmlgui6,
            errorString)) {
        return false;
    }

    return restoreGlobalShortcuts(
        QDir(stagedConfigRoot)
            .filePath(
                QStringLiteral(
                    "kglobalshortcutsrc.mathom")),
        errorString);
}

void restoreConfigRollback(
    const QString &rollbackRoot,
    const ConfigRollbackState &state)
{
    const ConfigPaths paths =
        currentConfigPaths();

    removePath(paths.mathomRc);
    removePath(paths.appConfig);
    removePath(paths.kxmlgui5);
    removePath(paths.kxmlgui6);

    if (state.mathomRc) {
        copyFileReplacing(
            QDir(rollbackRoot)
                .filePath(
                    QStringLiteral(
                        "mathomrc")),
            paths.mathomRc,
            nullptr);
    }

    if (state.appConfig) {
        copyDirectoryRecursively(
            QDir(rollbackRoot)
                .filePath(
                    QStringLiteral(
                        "app-config")),
            paths.appConfig,
            false,
            nullptr);
    }

    if (state.kxmlgui5) {
        copyDirectoryRecursively(
            QDir(rollbackRoot)
                .filePath(
                    QStringLiteral(
                        "kxmlgui5")),
            paths.kxmlgui5,
            false,
            nullptr);
    }

    if (state.kxmlgui6) {
        copyDirectoryRecursively(
            QDir(rollbackRoot)
                .filePath(
                    QStringLiteral(
                        "kxmlgui6")),
            paths.kxmlgui6,
            false,
            nullptr);
    }

    if (pathExists(
            paths.globalShortcuts)) {
        removePath(
            paths.globalShortcuts);
    }

    if (state.globalShortcuts) {
        copyFileReplacing(
            QDir(rollbackRoot)
                .filePath(
                    QStringLiteral(
                        "kglobalshortcutsrc")),
            paths.globalShortcuts,
            nullptr);
    }
}

bool prepareDataReplacement(
    const QString &stagedData,
    const QString &currentData,
    QString *preparedPath,
    QString *oldPath,
    bool *hadOldData,
    QString *errorString)
{
    if (!preparedPath
        || !oldPath
        || !hadOldData) {
        return false;
    }

    const QString cleanCurrent =
        QDir::cleanPath(currentData);

    const QFileInfo currentInfo(
        cleanCurrent);

    const QString parentPath =
        currentInfo.absolutePath();

    const QString baseName =
        currentInfo.fileName();

    if (baseName.isEmpty()) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Invalid Mathom data folder");
        }
        return false;
    }

    if (!QDir().mkpath(parentPath)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot create Mathom data parent folder");
        }
        return false;
    }

    const QString token =
        QStringLiteral("%1-%2")
            .arg(
                QCoreApplication::
                    applicationPid())
            .arg(
                QDateTime::
                    currentMSecsSinceEpoch());

    *preparedPath =
        QDir(parentPath)
            .filePath(
                baseName
                + QStringLiteral(
                    ".restore-new-")
                + token);

    *oldPath =
        QDir(parentPath)
            .filePath(
                baseName
                + QStringLiteral(
                    ".restore-old-")
                + token);

    removePath(*preparedPath);
    removePath(*oldPath);

    if (!copyDirectoryRecursively(
            stagedData,
            *preparedPath,
            false,
            errorString)) {
        removePath(*preparedPath);
        return false;
    }

    *hadOldData =
        QDir(cleanCurrent).exists();

    return true;
}

bool swapPreparedData(
    const QString &currentData,
    const QString &preparedPath,
    const QString &oldPath,
    const bool hadOldData,
    QString *errorString)
{
    const QString cleanCurrent =
        QDir::cleanPath(currentData);

    const QFileInfo currentInfo(
        cleanCurrent);

    QDir parent(
        currentInfo.absolutePath());

    const QString currentName =
        currentInfo.fileName();

    const QString preparedName =
        QFileInfo(preparedPath)
            .fileName();

    const QString oldName =
        QFileInfo(oldPath)
            .fileName();

    if (hadOldData
        && !parent.rename(
            currentName,
            oldName)) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot preserve the current Mathom data folder");
        }
        return false;
    }

    if (!parent.rename(
            preparedName,
            currentName)) {
        if (hadOldData) {
            parent.rename(
                oldName,
                currentName);
        }

        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot activate the restored Mathom data folder");
        }
        return false;
    }

    return true;
}

void rollbackDataSwap(
    const QString &currentData,
    const QString &oldPath,
    const bool hadOldData)
{
    const QString cleanCurrent =
        QDir::cleanPath(currentData);

    const QFileInfo currentInfo(
        cleanCurrent);

    QDir parent(
        currentInfo.absolutePath());

    const QString currentName =
        currentInfo.fileName();

    const QString oldName =
        QFileInfo(oldPath)
            .fileName();

    removePath(cleanCurrent);

    if (hadOldData) {
        parent.rename(
            oldName,
            currentName);
    }
}

bool applyFullSnapshot(
    const QString &stagingRoot,
    const QString &currentData,
    QString *errorString)
{
    const QString stagedData =
        QDir(stagingRoot)
            .filePath(
                QStringLiteral("data"));

    const QString stagedConfig =
        QDir(stagingRoot)
            .filePath(
                QStringLiteral("config"));

    if (!QDir(stagedData).exists()) {
        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Backup data folder is missing");
        }
        return false;
    }

    QString preparedPath;
    QString oldPath;
    bool hadOldData = false;

    if (!prepareDataReplacement(
            stagedData,
            currentData,
            &preparedPath,
            &oldPath,
            &hadOldData,
            errorString)) {
        return false;
    }

    QTemporaryDir rollbackConfig;

    if (!rollbackConfig.isValid()) {
        removePath(preparedPath);

        if (errorString) {
            *errorString =
                QStringLiteral(
                    "Cannot create configuration rollback area");
        }
        return false;
    }

    ConfigRollbackState configState;

    if (!snapshotConfigForRollback(
            rollbackConfig.path(),
            &configState,
            errorString)) {
        removePath(preparedPath);
        return false;
    }

    if (!swapPreparedData(
            currentData,
            preparedPath,
            oldPath,
            hadOldData,
            errorString)) {
        removePath(preparedPath);
        return false;
    }

    if (!applyStagedConfig(
            stagedConfig,
            errorString)) {
        restoreConfigRollback(
            rollbackConfig.path(),
            configState);

        rollbackDataSwap(
            currentData,
            oldPath,
            hadOldData);

        return false;
    }

    if (hadOldData)
        removePath(oldPath);

    return true;
}

bool applyLegacyData(
    const QString &stagingRoot,
    const QString &currentData,
    QString *errorString)
{
    QString preparedPath;
    QString oldPath;
    bool hadOldData = false;

    if (!prepareDataReplacement(
            stagingRoot,
            currentData,
            &preparedPath,
            &oldPath,
            &hadOldData,
            errorString)) {
        return false;
    }

    if (!swapPreparedData(
            currentData,
            preparedPath,
            oldPath,
            hadOldData,
            errorString)) {
        removePath(preparedPath);
        return false;
    }

    if (hadOldData)
        removePath(oldPath);

    return true;
}

QString formattedBackupSize(
    const qint64 bytes)
{
    const double mebibytes =
        static_cast<double>(bytes)
        / (1024.0 * 1024.0);

    return QStringLiteral("%1 MiB")
        .arg(
            QString::number(
                mebibytes,
                'f',
                mebibytes < 10.0
                    ? 2
                    : 1));
}

void waitForThread(
    QThread &thread,
    QProgressDialog &dialog)
{
    thread.start();

    while (thread.isRunning()) {
        dialog.setValue(
            dialog.value() + 1);
        qApp->processEvents();
        usleep(300);
    }

    thread.wait();
}

} // namespace

BackupDialog::BackupDialog(QWidget *parent)
    : QDialog(parent)
{
    setModal(true);
    setWindowTitle(i18n("Backup & Restore"));

    auto *mainWidget =
        new QWidget(this);

    auto *mainLayout =
        new QVBoxLayout;

    setLayout(mainLayout);
    mainLayout->addWidget(mainWidget);

    auto *page =
        new QWidget(this);

    auto *pageVBoxLayout =
        new QVBoxLayout(page);

    pageVBoxLayout->setContentsMargins({});
    mainLayout->addWidget(page);

    QString savesFolder =
        Global::savesFolder();

    savesFolder =
        savesFolder.left(
            savesFolder.length() - 1);

    auto *folderGroup =
        new QGroupBox(
            i18n("Save Folder"),
            page);

    pageVBoxLayout->addWidget(
        folderGroup);

    auto *folderGroupLayout =
        new QVBoxLayout;

    folderGroup->setLayout(
        folderGroupLayout);

    folderGroupLayout->addWidget(
        new QLabel(
            QStringLiteral("<qt><nobr>")
                + i18n(
                    "Your Mathom data is currently stored in this folder:<br><b>%1</b>",
                    savesFolder),
            folderGroup));

    auto *folderWidget =
        new QWidget;

    folderGroupLayout->addWidget(
        folderWidget);

    auto *folderLayout =
        new QHBoxLayout(
            folderWidget);

    folderLayout->setContentsMargins(
        0, 0, 0, 0);

    auto *moveFolder =
        new QPushButton(
            i18n("&Move to Another Folder..."),
            folderWidget);

    auto *useFolder =
        new QPushButton(
            i18n("&Use Another Existing Folder..."),
            folderWidget);

    auto *helpLabel =
        new HelpLabel(
            i18n("Why to do that?"),
            i18n(
                "<p>You can move the folder where %1 stores its Mathom data to:</p><ul>"
                "<li>Store your Mathom data in a visible place in your home folder, such as ~/Mathom, so you can back it up manually whenever you want.</li>"
                "<li>Store your Mathom data on a server to share it between two computers.<br>"
                "In this case, mount the shared-folder to the local file system and ask %1 to use that mount point.<br>"
                "Warning: you should not run %1 at the same time on both computers, or you risk to loss data while the two applications are desynced.</li>"
                "</ul><p>Please remember that you should not change the content of that folder manually (eg. adding a file directly to the data folder will not add a mathom to Mathom).</p>",
                QGuiApplication::
                    applicationDisplayName()),
            folderWidget);

    folderLayout->addWidget(moveFolder);
    folderLayout->addWidget(useFolder);
    folderLayout->addWidget(helpLabel);
    folderLayout->addStretch();

    connect(
        moveFolder,
        &QPushButton::clicked,
        this,
        &BackupDialog::
            moveToAnotherFolder);

    connect(
        useFolder,
        &QPushButton::clicked,
        this,
        &BackupDialog::
            useAnotherExistingFolder);

    auto *backupGroup =
        new QGroupBox(
            i18n("Full Backups"),
            page);

    pageVBoxLayout->addWidget(
        backupGroup);

    auto *backupGroupLayout =
        new QVBoxLayout;

    backupGroup->setLayout(
        backupGroupLayout);

    backupGroupLayout->addWidget(
        new QLabel(
            i18n(
                "A full Mathom backup contains all Mathom-Houses, shelves, Pages, mathoms, attachments, tags, profiles and Mathom settings."),
            backupGroup));

    auto *backupWidget =
        new QWidget;

    backupGroupLayout->addWidget(
        backupWidget);

    auto *backupLayout =
        new QHBoxLayout(
            backupWidget);

    backupLayout->setContentsMargins(
        0, 0, 0, 0);

    auto *backupButton =
        new QPushButton(
            i18n("&Create Full Backup..."),
            backupWidget);

    auto *restoreButton =
        new QPushButton(
            i18n("&Restore Full Backup..."),
            backupWidget);

    m_lastBackup =
        new QLabel(
            QString(),
            backupWidget);

    backupLayout->addWidget(
        backupButton);

    backupLayout->addWidget(
        restoreButton);

    backupLayout->addWidget(
        m_lastBackup);

    backupLayout->addStretch();

    connect(
        backupButton,
        &QPushButton::clicked,
        this,
        &BackupDialog::backup);

    connect(
        restoreButton,
        &QPushButton::clicked,
        this,
        &BackupDialog::restore);

    populateLastBackup();

    (new QWidget(page))
        ->setSizePolicy(
            QSizePolicy::Expanding,
            QSizePolicy::Expanding);

    auto *buttonBox =
        new QDialogButtonBox(
            QDialogButtonBox::Close);

    connect(
        buttonBox,
        &QDialogButtonBox::rejected,
        this,
        &BackupDialog::reject);

    mainLayout->addWidget(
        buttonBox);

    buttonBox->button(
        QDialogButtonBox::Close)
        ->setDefault(true);
}

BackupDialog::~BackupDialog() = default;

void BackupDialog::populateLastBackup()
{
    KConfigGroup group(
        KSharedConfig::openConfig(),
        QStringLiteral("Backups"));

    const QDateTime lastBackup =
        group.readEntry(
            QStringLiteral(
                "lastSuccessfulFullBackup"),
            QDateTime());

    QString text =
        i18n("Last full backup: never");

    if (lastBackup.isValid()) {
        text =
            i18n(
                "Last full backup: %1",
                lastBackup.toLocalTime()
                    .toString(
                        QStringLiteral(
                            "dd.MM.yyyy hh:mm:ss")));
    }

    m_lastBackup->setText(text);
}

void BackupDialog::moveToAnotherFolder()
{
    const QString currentSavesFolder =
        Global::savesFolder();

    const QUrl selectedURL =
        QFileDialog::
            getExistingDirectoryUrl(
                this,
                i18n(
                    "Choose a Folder Where to Move Mathom Data"),
                QUrl::fromLocalFile(
                    currentSavesFolder));

    if (selectedURL.isEmpty())
        return;

    QString folder =
        selectedURL.path();

    QDir dir(folder);

    if (dir.exists()) {
        const QStringList content =
            dir.entryList();

        if (content.count() > 2) {
            const int result =
                KMessageBox::
                    warningContinueCancel(
                        nullptr,
                        QStringLiteral("<qt>")
                            + i18n(
                                "The folder <b>%1</b> is not empty. Do you want to overwrite it?",
                                folder),
                        i18n(
                            "Overwrite Folder?"),
                        KGuiItem(
                            i18n("&Overwrite"),
                            QStringLiteral(
                                "document-save")));

            if (result
                == KMessageBox::Cancel) {
                return;
            }
        }

        Tools::deleteRecursively(
            folder);
    }

    FormatImporter copier;
    copier.moveFolder(
        currentSavesFolder,
        folder);

    Backup::setFolderAndRestart(
        folder,
        i18n(
            "Your Mathom data has been successfully moved to <b>%1</b>. %2 is going to be restarted to take this change into account."));
}

void BackupDialog::useAnotherExistingFolder()
{
    const QString currentSavesFolder =
        Global::savesFolder();

    const QUrl selectedURL =
        QFileDialog::
            getExistingDirectoryUrl(
                this,
                i18n(
                    "Choose a Folder Where to Move Mathom Data"),
                QUrl::fromLocalFile(
                    currentSavesFolder));

    if (selectedURL.isEmpty())
        return;

    Backup::setFolderAndRestart(
        selectedURL.path(),
        i18n(
            "Your Mathom data folder has been successfully changed to <b>%1</b>. %2 is going to be restarted to take this change into account."));
}

void BackupDialog::backup()
{
    KConfig *config =
        KSharedConfig::
            openConfig()
            .data();

    KConfigGroup configGroup(
        config,
        QStringLiteral("Backups"));

    const QString folder =
        configGroup.readPathEntry(
            QStringLiteral("lastFolder"),
            QDir::homePath());

    const QString fileName =
        i18nc(
            "Backup filename (without extension), %1 is the date",
            "Mathom_Backup_%1",
            QDate::currentDate()
                .toString(
                    Qt::ISODate));

    QString destination =
        QFileDialog::getSaveFileName(
            this,
            i18n(
                "Create Full Mathom Backup"),
            QDir(folder)
                .filePath(fileName),
            i18n(
                "Mathom backup (*.mathom-backup);;All Files (*)"));

    if (destination.isEmpty())
        return;

    if (!destination.endsWith(
            QStringLiteral(
                ".mathom-backup"),
            Qt::CaseInsensitive)) {
        destination +=
            QStringLiteral(
                ".mathom-backup");
    }

    if (Global::bnpView) {
        Global::bnpView
            ->closeAllEditors();
        Global::bnpView
            ->save();
    }

    Settings::saveConfig();

    if (Global::config())
        Global::config()->sync();

    KSharedConfig::openConfig()->sync();

    DiagnosticManager::instance()
        .logEvent(
            QStringLiteral(
                "FULL_BACKUP_BEGIN"),
            {
                {
                    QStringLiteral(
                        "destination"),
                    QFileInfo(
                        destination)
                        .fileName()
                }
            });

    QProgressDialog dialog(this);
    dialog.setWindowTitle(
        i18n(
            "Create Full Mathom Backup"));
    dialog.setLabelText(
        i18n(
            "Creating and validating the full Mathom backup. Please wait..."));
    dialog.setModal(true);
    dialog.setCancelButton(nullptr);
    dialog.setAutoClose(true);
    dialog.setRange(0, 0);
    dialog.setValue(0);
    dialog.show();

    BackupThread thread(
        destination,
        Global::savesFolder());

    waitForThread(
        thread,
        dialog);

    if (!thread.success()) {
        QFile::remove(destination);

        DiagnosticManager::instance()
            .logEvent(
                QStringLiteral(
                    "FULL_BACKUP_FAIL"));

        KMessageBox::error(
            this,
            i18n(
                "The full Mathom backup could not be created or validated.<br><br>%1",
                thread.errorString()),
            i18n(
                "Backup Error"));

        return;
    }

    configGroup.writePathEntry(
        QStringLiteral("lastFolder"),
        QFileInfo(destination)
            .absolutePath());

    configGroup.writeEntry(
        QStringLiteral(
            "lastSuccessfulFullBackup"),
        QDateTime::
            currentDateTime());

    config->sync();

    populateLastBackup();

    DiagnosticManager::instance()
        .logEvent(
            QStringLiteral(
                "FULL_BACKUP_OK"));

    KMessageBox::information(
        this,
        i18n(
            "The full Mathom backup has been created and its integrity has been verified."),
        i18n(
            "Backup Complete"));
}

void BackupDialog::restore()
{
    KConfig *config =
        KSharedConfig::
            openConfig()
            .data();

    KConfigGroup configGroup(
        config,
        QStringLiteral("Backups"));

    const QString folder =
        configGroup.readPathEntry(
            QStringLiteral("lastFolder"),
            QDir::homePath());

    const QString path =
        QFileDialog::getOpenFileName(
            this,
            i18n(
                "Open Mathom Backup"),
            folder,
            i18n(
                "Mathom backup (*.mathom-backup);;Legacy Mathom/BasKet backup (*.tar.gz);;All Files (*)"));

    if (path.isEmpty())
        return;

    configGroup.writePathEntry(
        QStringLiteral("lastFolder"),
        QFileInfo(path)
            .absolutePath());

    config->sync();

    DiagnosticManager::instance()
        .logEvent(
            QStringLiteral(
                "FULL_RESTORE_BEGIN"),
            {
                {
                    QStringLiteral(
                        "source"),
                    QFileInfo(path)
                        .fileName()
                }
            });

    QTemporaryDir staging;

    if (!staging.isValid()) {
        KMessageBox::error(
            this,
            i18n(
                "Mathom could not create a temporary restore area."),
            i18n(
                "Restore Error"));
        return;
    }

    QProgressDialog validationDialog(this);
    validationDialog.setWindowTitle(
        i18n(
            "Validate Mathom Backup"));
    validationDialog.setLabelText(
        i18n(
            "Extracting and checking the backup before changing any current data..."));
    validationDialog.setModal(true);
    validationDialog.setCancelButton(nullptr);
    validationDialog.setAutoClose(true);
    validationDialog.setRange(0, 0);
    validationDialog.setValue(0);
    validationDialog.show();

    RestoreThread restoreThread(
        path,
        staging.path());

    waitForThread(
        restoreThread,
        validationDialog);

    if (!restoreThread.success()) {
        DiagnosticManager::instance()
            .logEvent(
                QStringLiteral(
                    "FULL_RESTORE_FAIL"),
                {
                    {
                        QStringLiteral(
                            "phase"),
                        QStringLiteral(
                            "validation")
                    }
                });

        KMessageBox::error(
            this,
            i18n(
                "This file is not a valid Mathom backup, or its integrity check failed. No current Mathom data has been changed.<br><br>%1",
                restoreThread.errorString()),
            i18n(
                "Restore Error"));

        return;
    }

    if (restoreThread.isLegacyBackup()) {
        const int answer =
            KMessageBox::
                warningContinueCancel(
                    this,
                    i18n(
                        "This is a legacy Mathom/BasKet backup. It contains Mathom data but not the complete Mathom configuration.<br><br>"
                        "Mathom will preserve your current configuration and restore only the legacy data."),
                    i18n(
                        "Restore Legacy Backup?"),
                    KGuiItem(
                        i18n("&Restore"),
                        QStringLiteral(
                            "document-revert")));

        if (answer
            != KMessageBox::Continue) {
            return;
        }
    } else {
        const QDateTime created =
            QDateTime::fromString(
                restoreThread.createdAt(),
                Qt::ISODate);

        const QString createdText =
            created.isValid()
            ? created.toLocalTime()
                  .toString(
                      QStringLiteral(
                          "dd.MM.yyyy hh:mm:ss"))
            : restoreThread.createdAt();

        const int answer =
            KMessageBox::
                warningContinueCancel(
                    this,
                    i18n(
                        "The backup has passed its integrity check.<br><br>"
                        "<b>Created:</b> %1<br>"
                        "<b>Mathom version:</b> %2<br>"
                        "<b>Files:</b> %3<br>"
                        "<b>Stored data:</b> %4<br>"
                        "<b>Integrity:</b> verified<br><br>"
                        "Restoring will replace the current Mathom data and Mathom configuration. "
                        "A separate full safety backup of the current state will be created first.",
                        createdText,
                        restoreThread.applicationVersion(),
                        restoreThread.fileCount(),
                        formattedBackupSize(
                            restoreThread.totalBytes())),
                    i18n(
                        "Restore Full Backup?"),
                    KGuiItem(
                        i18n("&Restore"),
                        QStringLiteral(
                            "document-revert")));

        if (answer
            != KMessageBox::Continue) {
            return;
        }
    }

    if (Global::bnpView) {
        Global::bnpView
            ->closeAllEditors();
        Global::bnpView
            ->save();
    }

    Settings::saveConfig();

    if (Global::config())
        Global::config()->sync();

    KSharedConfig::openConfig()->sync();

    const QString safetyBackup =
        Backup::newSafetyBackupPath();

    QProgressDialog safetyDialog(this);
    safetyDialog.setWindowTitle(
        i18n(
            "Create Safety Backup"));
    safetyDialog.setLabelText(
        i18n(
            "Creating and validating a safety backup of the current Mathom state before restoration..."));
    safetyDialog.setModal(true);
    safetyDialog.setCancelButton(nullptr);
    safetyDialog.setAutoClose(true);
    safetyDialog.setRange(0, 0);
    safetyDialog.setValue(0);
    safetyDialog.show();

    BackupThread safetyThread(
        safetyBackup,
        Global::savesFolder());

    waitForThread(
        safetyThread,
        safetyDialog);

    if (!safetyThread.success()) {
        DiagnosticManager::instance()
            .logEvent(
                QStringLiteral(
                    "FULL_RESTORE_FAIL"),
                {
                    {
                        QStringLiteral(
                            "phase"),
                        QStringLiteral(
                            "safety-backup")
                    }
                });

        KMessageBox::error(
            this,
            i18n(
                "Mathom refused to start the restoration because the safety backup of the current state could not be created and validated.<br><br>%1",
                safetyThread.errorString()),
            i18n(
                "Restore Aborted"));

        return;
    }

    QProgressDialog applyDialog(this);
    applyDialog.setWindowTitle(
        i18n(
            "Restore Mathom Backup"));
    applyDialog.setLabelText(
        i18n(
            "Applying the validated backup. Please wait..."));
    applyDialog.setModal(true);
    applyDialog.setCancelButton(nullptr);
    applyDialog.setAutoClose(true);
    applyDialog.setRange(0, 0);
    applyDialog.setValue(0);
    applyDialog.show();

    qApp->processEvents();

    QString applyError;

    const bool restored =
        restoreThread.isLegacyBackup()
        ? applyLegacyData(
              staging.path(),
              Global::savesFolder(),
              &applyError)
        : applyFullSnapshot(
              staging.path(),
              Global::savesFolder(),
              &applyError);

    applyDialog.hide();

    if (!restored) {
        DiagnosticManager::instance()
            .logEvent(
                QStringLiteral(
                    "FULL_RESTORE_FAIL"),
                {
                    {
                        QStringLiteral(
                            "phase"),
                        QStringLiteral(
                            "apply")
                    }
                });

        KMessageBox::error(
            this,
            i18n(
                "The restoration could not be completed. Mathom rolled back the active data when possible.<br><br>"
                "The full safety backup created before the operation is still available at:<br><b>%1</b><br><br>%2",
                safetyBackup,
                applyError),
            i18n(
                "Restore Error"));

        return;
    }

    if (!restoreThread.isLegacyBackup()) {
        if (Global::basketConfig) {
            Global::basketConfig
                ->reparseConfiguration();
        }

        KSharedConfig::openConfig()
            ->reparseConfiguration();

        Settings::loadConfig();
    }

    DiagnosticManager::instance()
        .logEvent(
            QStringLiteral(
                "FULL_RESTORE_OK"),
            {
                {
                    QStringLiteral(
                        "legacy"),
                    restoreThread
                        .isLegacyBackup()
                }
            });

    KMessageBox::information(
        this,
        i18n(
            "The backup has been restored successfully.<br><br>"
            "A full safety backup of the state that existed immediately before restoration has been kept at:<br><b>%1</b>",
            safetyBackup),
        i18n(
            "Restore Complete"));

    Backup::setFolderAndRestart(
        Global::savesFolder(),
        i18n(
            "Your Mathom backup has been successfully restored to <b>%1</b>. %2 is going to be restarted to load the restored state."));
}

QString Backup::binaryPath;

void Backup::figureOutBinaryPath(
    const char *argv0,
    QApplication &app)
{
    binaryPath =
        QDir(
            QString::fromUtf8(
                argv0))
            .canonicalPath();

    if (binaryPath.isEmpty())
        binaryPath =
            app.applicationFilePath();
}

void Backup::setFolderAndRestart(
    const QString &folder,
    const QString &message)
{
    Settings::setDataFolder(
        folder);

    Settings::saveConfig();

    KMessageBox::information(
        nullptr,
        QStringLiteral("<qt>")
            + message.arg(
                folder.endsWith(
                    QLatin1Char('/'))
                    ? folder.left(
                          folder.length()
                          - 1)
                    : folder,
                QGuiApplication::
                    applicationDisplayName()),
        i18n("Restart"));

    auto *job =
        new KIO::CommandLauncherJob(
            binaryPath);

    job->setExecutable(
        QCoreApplication::
            applicationName());

    job->setDesktopName(
        QStringLiteral(
            "fr.thorinux.mathom"));

    job->start();

    exit(0);
}

QString Backup::newSafetyBackupPath()
{
    const QString timestamp =
        QDateTime::currentDateTime()
            .toString(
                QStringLiteral(
                    "yyyy-MM-dd_HH-mm-ss"));

    const QString baseName =
        i18nc(
            "Safety backup filename before restoring Mathom data",
            "Mathom_Before_Restoration_%1",
            timestamp);

    QString candidate =
        QDir::home()
            .filePath(
                baseName
                + QStringLiteral(
                    ".mathom-backup"));

    if (!QFileInfo::exists(candidate))
        return candidate;

    for (int index = 2;; ++index) {
        candidate =
            QDir::home()
                .filePath(
                    QStringLiteral(
                        "%1_%2.mathom-backup")
                        .arg(
                            baseName)
                        .arg(
                            index));

        if (!QFileInfo::exists(
                candidate)) {
            return candidate;
        }
    }
}

BackupThread::BackupThread(
    const QString &backupFile,
    const QString &folderToBackup)
    : m_backupFile(backupFile)
    , m_folderToBackup(folderToBackup)
{
}

void BackupThread::run()
{
    m_success = false;
    m_errorString.clear();

    QTemporaryDir snapshot;

    if (!snapshot.isValid()) {
        m_errorString =
            QStringLiteral(
                "Cannot create temporary backup snapshot");
        return;
    }

    const QString snapshotRoot =
        QDir(snapshot.path())
            .filePath(
                fullBackupMagicFolder);

    if (!QDir().mkpath(
            snapshotRoot)) {
        m_errorString =
            QStringLiteral(
                "Cannot create backup snapshot root");
        return;
    }

    if (!createSnapshot(
            snapshotRoot,
            m_folderToBackup,
            &m_errorString)) {
        return;
    }

    if (!writeManifest(
            snapshotRoot,
            &m_errorString)) {
        return;
    }

    QFile::remove(
        m_backupFile);

    if (!createTarFromSnapshot(
            snapshotRoot,
            m_backupFile,
            &m_errorString)) {
        QFile::remove(
            m_backupFile);
        return;
    }

    QTemporaryDir validation;

    if (!validation.isValid()) {
        m_errorString =
            QStringLiteral(
                "Cannot create backup validation area");
        QFile::remove(
            m_backupFile);
        return;
    }

    if (!extractFullBackup(
            m_backupFile,
            validation.path(),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            &m_errorString)) {
        QFile::remove(
            m_backupFile);
        return;
    }

    m_success = true;
}

RestoreThread::RestoreThread(
    const QString &backupFile,
    const QString &stagingFolder)
    : m_backupFile(backupFile)
    , m_stagingFolder(stagingFolder)
{
}

void RestoreThread::run()
{
    m_success = false;
    m_legacyBackup = false;
    m_errorString.clear();
    m_createdAt.clear();
    m_applicationVersion.clear();
    m_fileCount = 0;
    m_totalBytes = 0;

    if (archiveHasTopLevelEntry(
            m_backupFile,
            fullBackupMagicFolder)) {
        m_success =
            extractFullBackup(
                m_backupFile,
                m_stagingFolder,
                &m_createdAt,
                &m_applicationVersion,
                &m_fileCount,
                &m_totalBytes,
                &m_errorString);
        return;
    }

    if (archiveHasTopLevelEntry(
            m_backupFile,
            legacyBackupMagicFolder)) {
        m_legacyBackup = true;
        m_success =
            extractLegacyBackup(
                m_backupFile,
                m_stagingFolder,
                &m_errorString);
        return;
    }

    m_errorString =
        QStringLiteral(
            "No supported Mathom backup root was found");
}

#include "moc_backup.cpp"
