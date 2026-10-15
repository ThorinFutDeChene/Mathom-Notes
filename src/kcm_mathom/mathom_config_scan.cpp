// SPDX-License-Identifier: GPL-2.0-or-later
#include "mathomscanbackend.h"
#include "mathomscanconfig.h"

#include <KCModule>
#include <KLocalizedString>
#include <KPluginFactory>

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtConcurrent>

class ScanSettingsPage final : public KCModule
{
    Q_OBJECT
public:
    explicit ScanSettingsPage(QObject *parent,
                              const KPluginMetaData &metadata = KPluginMetaData())
        : KCModule(parent, metadata)
    {
        auto *layout = new QVBoxLayout(widget());
        auto *description = new QLabel(
            i18n("Configure how Mathom detects scanners and where documents are saved. "
                 "Scanning never imports documents automatically into Mathoms."),
            widget());
        description->setWordWrap(true);
        layout->addWidget(description);

        auto *form = new QFormLayout;
        m_mode = new QComboBox(widget());
        m_mode->addItem(i18n("Choose a scanner each time"), 0);
        m_mode->addItem(i18n("Use only the configured scanner"), 1);
        m_mode->addItem(i18n("Prefer the configured scanner; otherwise offer alternatives"), 2);
        form->addRow(i18n("Selection behavior:"), m_mode);
        layout->addLayout(form);

        auto *deviceRow = new QHBoxLayout;
        m_device = new QComboBox(widget());
        m_device->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
        m_refresh = new QPushButton(i18n("Search for scanners"), widget());
        deviceRow->addWidget(m_device, 1);
        deviceRow->addWidget(m_refresh);
        auto *deviceLabel = new QLabel(i18n("Configured scanner:"), widget());
        layout->addWidget(deviceLabel);
        layout->addLayout(deviceRow);

        auto *destination = new QHBoxLayout;
        m_directory = new QLineEdit(widget());
        m_browse = new QPushButton(i18n("Browse..."), widget());
        destination->addWidget(m_directory, 1);
        destination->addWidget(m_browse);
        layout->addWidget(new QLabel(i18n("Save scanned documents in:"), widget()));
        layout->addLayout(destination);

        m_status = new QLabel(widget());
        m_status->setWordWrap(true);
        layout->addWidget(m_status);
        layout->addStretch(1);

        connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this]() {
            m_device->setEnabled(m_mode->currentData().toInt() != 0);
            if (!m_loading)
                markAsChanged();
        });
        connect(m_device, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this]() { if (!m_loading) markAsChanged(); });
        connect(m_directory, &QLineEdit::textChanged, this,
                [this]() { if (!m_loading) markAsChanged(); });
        connect(m_browse, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getExistingDirectory(
                widget(), i18n("Choose scan destination"), m_directory->text());
            if (!path.isEmpty())
                m_directory->setText(path);
        });
        connect(m_refresh, &QPushButton::clicked,
                this, &ScanSettingsPage::refreshDevices);

        connect(&m_search, &QFutureWatcher<QList<MathomScan::Device>>::finished,
                this, [this]() {
            m_loading = true;
            const QString selected = m_device->currentData().toString();
            const auto devices = m_search.result();
            m_device->clear();
            for (const auto &device : devices)
                m_device->addItem(device.label, device.id);
            const int index = m_device->findData(selected);
            if (index >= 0)
                m_device->setCurrentIndex(index);
            else if (!selected.isEmpty())
                m_device->insertItem(0, i18n("Previously configured scanner (offline)"), selected);
            m_status->setText(devices.isEmpty()
                ? i18n("No scanner detected. Connect or turn on your scanner, then search again.")
                : i18np("%1 scanner detected", "%1 scanners detected", devices.size()));
            m_refresh->setEnabled(true);
            m_loading = false;
        });

        load();
        refreshDevices();
    }

    void load() override
    {
        m_loading = true;
        const auto settings = MathomScanConfig::load();
        m_mode->setCurrentIndex(static_cast<int>(settings.mode));
        m_directory->setText(settings.outputDirectory);
        m_device->clear();
        if (!settings.deviceId.isEmpty())
            m_device->addItem(settings.deviceLabel.isEmpty()
                                  ? i18n("Saved scanner") : settings.deviceLabel,
                              settings.deviceId);
        m_device->setEnabled(settings.mode != MathomScanPreferences::FreeChoice);
        m_loading = false;
    }

    void save() override
    {
        const QString path = m_directory->text().trimmed();
        if (path.isEmpty()) {
            m_status->setText(i18n("Choose an output folder before saving."));
            return;
        }
        MathomScanPreferences settings;
        settings.mode =
            static_cast<MathomScanPreferences::Mode>(m_mode->currentData().toInt());
        settings.deviceId = m_device->currentData().toString();
        settings.deviceLabel = m_device->currentText();
        settings.outputDirectory = path;
        if (settings.mode != MathomScanPreferences::FreeChoice
            && settings.deviceId.isEmpty()) {
            m_status->setText(i18n("Choose a scanner for the selected behavior."));
            return;
        }
        MathomScanConfig::save(settings);
        setNeedsSave(false);
    }

    void defaults() override
    {
        m_mode->setCurrentIndex(0);
        m_directory->setText(MathomScanConfig::defaultDirectory());
        markAsChanged();
    }

private:
    void refreshDevices()
    {
        if (m_search.isRunning())
            return;
        m_refresh->setEnabled(false);
        m_status->setText(i18n("Searching for scanners..."));
        m_search.setFuture(QtConcurrent::run([] {
            return MathomScan::discover();
        }));
    }

    QComboBox *m_mode = nullptr;
    QComboBox *m_device = nullptr;
    QLineEdit *m_directory = nullptr;
    QPushButton *m_browse = nullptr;
    QPushButton *m_refresh = nullptr;
    QLabel *m_status = nullptr;
    QFutureWatcher<QList<MathomScan::Device>> m_search;
    bool m_loading = false;
};

K_PLUGIN_CLASS_WITH_JSON(ScanSettingsPage, "mathom_config_scan.json")

#include "mathom_config_scan.moc"
