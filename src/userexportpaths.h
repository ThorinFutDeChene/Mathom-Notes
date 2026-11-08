// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

// Only user-requested exports and manual backups use Documents.
// Working notes, settings and automatic backups remain in XDG locations.
namespace MathomUserPaths
{
inline QString documentsDirectory()
{
    QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty())
        documents = QDir::home().filePath(QStringLiteral("Documents"));
    return QDir(documents).filePath(QStringLiteral("Mathom Notes"));
}

// Use only when an explicit backup/export action needs an existing folder.
inline QString readyDirectory()
{
    const QString preferred = documentsDirectory();
    if (QDir().mkpath(preferred))
        return preferred;

    // A missing/unwritable Documents location must not prevent a manual save.
    const QString fallback =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return fallback.isEmpty() ? QDir::homePath() : fallback;
}
} // namespace MathomUserPaths
