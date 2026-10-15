// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>
#include "basket_export.h"

struct BASKET_EXPORT MathomScanPreferences
{
    enum Mode { FreeChoice = 0, Required = 1, Preferred = 2 };
    Mode mode = FreeChoice;
    QString deviceId;
    QString deviceLabel;
    QString outputDirectory;
};

class BASKET_EXPORT MathomScanConfig
{
public:
    static MathomScanPreferences load();
    static void save(const MathomScanPreferences &preferences);
    static QString defaultDirectory();
};
