#pragma once

#include <KCMultiDialog>

class KActionCollection;

class AdvancedSettingsDialog final : public KCMultiDialog
{
public:
    explicit AdvancedSettingsDialog(
        KActionCollection *actions,
        KActionCollection *globalActions,
        QWidget *parent = nullptr);
};
