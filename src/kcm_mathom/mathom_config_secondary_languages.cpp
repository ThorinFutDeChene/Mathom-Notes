#include "secondarylanguagespage.h"

#include <KPluginFactory>

K_PLUGIN_CLASS_WITH_JSON(
    SecondaryLanguagesPage,
    "mathom_config_secondary_languages.json")

#include "mathom_config_secondary_languages.moc"
