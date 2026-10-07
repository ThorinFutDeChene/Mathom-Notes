/*
   SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settings.h"

#include <KPluginFactory>

K_PLUGIN_CLASS_WITH_JSON(
    BackupSettingsPage,
    "mathom_config_backups.json")

#include "mathom_config_backups.moc"
