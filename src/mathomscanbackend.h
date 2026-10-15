// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "basket_export.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include <atomic>
#include <memory>

namespace MathomScan
{
struct Device {
    QString id;
    QString label;
};

struct Capabilities {
    QStringList sources;
    QStringList modes;
    QList<int> resolutions;
    bool hasSourceOption = false;
    bool duplex = false;
    bool canScanA3 = false;
    bool canScanA4 = true;
    bool canScanA5 = true;
    QString error;
};

struct Options {
    QString deviceId;
    QString source;
    bool duplex = false;
    QString mode;
    int dpi = 300;
    QString paper = QStringLiteral("A4");
    QString sessionDirectory;
    int nextImageNumber = 1;
};

struct Result {
    QStringList paths;
    QString error;
    bool canceled = false;
};

// SANE is used directly, without spawning scanimage or linking GTK/Vala.
// Calls are serialized because many SANE backends are not thread-safe.
BASKET_EXPORT QList<Device> discover(QString *error = nullptr);
BASKET_EXPORT Capabilities inspect(const QString &deviceId);
BASKET_EXPORT Result acquire(const Options &options,
               const std::shared_ptr<std::atomic_bool> &cancel,
               const std::function<void(int)> &pageAvailable);
}
