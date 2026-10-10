#pragma once

#include <KCMultiDialog>

class KActionCollection;

class AdvancedSettingsDialog final : public KCMultiDialog
{
public:
    explicit AdvancedSettingsDialog(
        KActionCollection *actions,
        QWidget *parent = nullptr);
};
