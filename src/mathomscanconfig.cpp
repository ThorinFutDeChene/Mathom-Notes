// SPDX-License-Identifier: GPL-2.0-or-later
#include "mathomscanconfig.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QDir>
#include <QStandardPaths>

QString MathomScanConfig::defaultDirectory()
{
    const QString downloads =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return downloads.isEmpty() ? QDir::homePath() : downloads;
}

MathomScanPreferences MathomScanConfig::load()
{
    KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("Mathom Scan"));
    MathomScanPreferences preferences;
    const int mode = group.readEntry(QStringLiteral("scannerMode"), 0);
    if (mode >= 0 && mode <= 2)
        preferences.mode = static_cast<MathomScanPreferences::Mode>(mode);
    preferences.deviceId = group.readEntry(QStringLiteral("deviceId"), QString());
    preferences.deviceLabel = group.readEntry(QStringLiteral("deviceLabel"), QString());
    preferences.outputDirectory =
        group.readEntry(QStringLiteral("outputDirectory"), defaultDirectory());
    return preferences;
}

void MathomScanConfig::save(const MathomScanPreferences &preferences)
{
    auto config = KSharedConfig::openConfig();
    KConfigGroup group(config, QStringLiteral("Mathom Scan"));
    group.writeEntry(QStringLiteral("scannerMode"), static_cast<int>(preferences.mode));
    group.writeEntry(QStringLiteral("deviceId"), preferences.deviceId);
    group.writeEntry(QStringLiteral("deviceLabel"), preferences.deviceLabel);
    group.writeEntry(QStringLiteral("outputDirectory"), preferences.outputDirectory);
    config->sync();
}
