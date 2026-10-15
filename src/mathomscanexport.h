// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace MathomScanExport {
struct Page {
    QString path;
    int quarterTurns = 0;
    QString paper = QStringLiteral("A4");
};
struct Output {
    QStringList files;
    QString error;
};
Output write(const QList<Page> &pages, const QString &format,
             const QString &paper, const QString &directory,
             const QDateTime &scanTime);
}
