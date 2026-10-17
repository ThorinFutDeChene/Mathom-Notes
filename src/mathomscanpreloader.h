// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "mathomscanbackend.h"

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>

// Process-wide, in-memory scanner discovery and capability cache.
// SANE operations run only in QtConcurrent workers, never on the GUI thread.
// The preloader outlives Mathom Scan windows so closing one cannot destroy
// an active SANE discovery or inspection worker.
class MathomScanPreloader final : public QObject
{
    Q_OBJECT
public:
    static MathomScanPreloader &instance();

    void start();
    void refresh();
    void requestCapabilities(const QString &deviceId);
    void registerDialog();
    void unregisterDialog();

    bool hasDeviceList() const { return m_hasDeviceList; }
    bool discoveryRunning() const;
    bool inspectionRunning() const { return m_inspection.isRunning(); }
    const QList<MathomScan::Device> &devices() const { return m_devices; }
    bool capabilityFor(const QString &deviceId, MathomScan::Capabilities *result) const;

    MathomScan::DiagnosticStage discoveryStage() const;
    MathomScan::DiagnosticStage inspectionStage() const;

Q_SIGNALS:
    void discoveryStarted();
    void devicesReady();
    void inspectionStarted(const QString &deviceId);
    void capabilitiesReady(const QString &deviceId);

private:
    struct DiscoveryResult {
        QList<MathomScan::Device> devices;
        QString error;
    };
    explicit MathomScanPreloader(QObject *parent);
    ~MathomScanPreloader() override;

    void beginDiscovery();
    void beginNextInspection();
    bool containsDevice(const QString &deviceId) const;

    QFutureWatcher<DiscoveryResult> m_discovery;
    QFutureWatcher<MathomScan::Capabilities> m_inspection;
    QList<MathomScan::Device> m_devices;
    QHash<QString, MathomScan::Capabilities> m_capabilities;
    QList<QString> m_priority;
    QList<QString> m_background;
    QString m_activeId;
    bool m_started = false;
    bool m_hasDeviceList = false;
    bool m_refreshPending = false;
    unsigned m_generation = 0;
    unsigned m_activeGeneration = 0;
    int m_openDialogs = 0;

    using Phase = MathomScan::DiagnosticStage;
    using AtomicPhase = std::atomic<Phase>;
    std::shared_ptr<AtomicPhase> m_discoveryPhase =
        std::make_shared<AtomicPhase>(Phase::Idle);
    std::shared_ptr<AtomicPhase> m_inspectionPhase =
        std::make_shared<AtomicPhase>(Phase::Idle);
};
