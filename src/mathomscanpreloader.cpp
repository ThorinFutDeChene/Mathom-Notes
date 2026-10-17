// SPDX-License-Identifier: GPL-2.0-or-later
#include "mathomscanpreloader.h"

#include "diagnosticmanager.h"
#include "mathomscanconfig.h"

#include <QCoreApplication>
#include <QSet>
#include <QtConcurrent>

MathomScanPreloader &MathomScanPreloader::instance()
{
    // Qt owns this object; it survives all Mathom Scan windows. Its worker
    // futures are settled before it is destroyed on application shutdown.
    static auto *preloader = new MathomScanPreloader(QCoreApplication::instance());
    return *preloader;
}

MathomScanPreloader::MathomScanPreloader(QObject *parent)
    : QObject(parent)
{
    connect(&m_discovery, &QFutureWatcher<DiscoveryResult>::finished,
            this, [this] {
        const DiscoveryResult result = m_discovery.result();
        DiagnosticManager::instance().logEvent(
            QStringLiteral("SCAN_PRELOAD_DISCOVERY_END"),
            {{QStringLiteral("device_count"), int(result.devices.size())},
             {QStringLiteral("failed"), !result.error.isEmpty()}});
        if (m_refreshPending) {
            m_refreshPending = false;
            beginDiscovery();
            return;
        }

        m_devices = result.devices;
        m_hasDeviceList = true;
        m_capabilities.clear();
        m_priority.clear();
        m_background.clear();

        const QString configured = MathomScanConfig::load().deviceId;
        QString preferred;
        if (containsDevice(configured))
            preferred = configured;
        else if (!m_devices.isEmpty())
            preferred = m_devices.constFirst().id;
        if (!preferred.isEmpty())
            m_background.append(preferred);
        for (const auto &device : m_devices)
            if (device.id != preferred)
                m_background.append(device.id);

        // When Mathom Scan is not open, inspect devices in the background.
        // When open, wait for the selected scanner to be requested first.
        beginNextInspection();
        Q_EMIT devicesReady();
    });

    connect(&m_inspection, &QFutureWatcher<MathomScan::Capabilities>::finished,
            this, [this] {
        const QString completedId = m_activeId;
        const unsigned completedGeneration = m_activeGeneration;
        const MathomScan::Capabilities result = m_inspection.result();
        m_activeId.clear();

        // A manual refresh invalidates any result from the old device list.
        if (m_refreshPending) {
            m_refreshPending = false;
            beginDiscovery();
            return;
        }
        if (completedGeneration == m_generation && containsDevice(completedId)) {
            m_capabilities.insert(completedId, result);
            DiagnosticManager::instance().logEvent(
                QStringLiteral("SCAN_PRELOAD_INSPECTION_END"),
                {{QStringLiteral("failed"), !result.error.isEmpty()},
                 {QStringLiteral("source_count"), int(result.sources.size())}});
            Q_EMIT capabilitiesReady(completedId);
        }
        beginNextInspection();
    });
}

MathomScanPreloader::~MathomScanPreloader()
{
    // All workers capture only value types and shared diagnostic phase
    // counters, never this QObject. Wait before SANE/Qt tear down.
    m_discovery.waitForFinished();
    m_inspection.waitForFinished();
}

bool MathomScanPreloader::discoveryRunning() const
{
    return m_discovery.isRunning() || m_refreshPending;
}

MathomScan::DiagnosticStage MathomScanPreloader::discoveryStage() const
{
    return m_discoveryPhase->load(std::memory_order_relaxed);
}

MathomScan::DiagnosticStage MathomScanPreloader::inspectionStage() const
{
    return m_inspectionPhase->load(std::memory_order_relaxed);
}

bool MathomScanPreloader::containsDevice(const QString &deviceId) const
{
    if (deviceId.isEmpty())
        return false;
    for (const auto &device : m_devices)
        if (device.id == deviceId)
            return true;
    return false;
}

bool MathomScanPreloader::capabilityFor(
    const QString &deviceId, MathomScan::Capabilities *result) const
{
    const auto it = m_capabilities.constFind(deviceId);
    if (it == m_capabilities.cend())
        return false;
    if (result)
        *result = it.value();
    return true;
}

void MathomScanPreloader::start()
{
    if (m_started)
        return;
    m_started = true;
    beginDiscovery();
}

void MathomScanPreloader::refresh()
{
    if (!m_started) {
        start();
        return;
    }
    // Do not overlap discovery and inspection jobs, which share the SANE
    // backend and its process-wide mutex. Finish a running job first.
    m_hasDeviceList = false;
    m_priority.clear();
    m_background.clear();
    m_capabilities.clear();
    if (m_discovery.isRunning() || m_inspection.isRunning()) {
        m_refreshPending = true;
        Q_EMIT discoveryStarted();
        return;
    }
    beginDiscovery();
}

void MathomScanPreloader::beginDiscovery()
{
    m_refreshPending = false;
    m_hasDeviceList = false;
    m_devices.clear();
    m_capabilities.clear();
    m_priority.clear();
    m_background.clear();
    ++m_generation;
    m_discoveryPhase->store(Phase::Idle, std::memory_order_relaxed);
    auto phase = m_discoveryPhase;
    m_discovery.setFuture(QtConcurrent::run([phase] {
        DiscoveryResult result;
        result.devices = MathomScan::discover(
            &result.error, [phase](Phase stage) {
                phase->store(stage, std::memory_order_relaxed);
            });
        return result;
    }));
    DiagnosticManager::instance().logEvent(
        QStringLiteral("SCAN_PRELOAD_DISCOVERY_BEGIN"));
    Q_EMIT discoveryStarted();
}

void MathomScanPreloader::requestCapabilities(const QString &deviceId)
{
    if (!m_hasDeviceList || !containsDevice(deviceId)
        || m_capabilities.contains(deviceId))
        return;
    if (m_activeId == deviceId && m_activeGeneration == m_generation)
        return;
    m_priority.removeAll(deviceId);
    m_background.removeAll(deviceId);
    m_priority.prepend(deviceId);
    beginNextInspection();
}

void MathomScanPreloader::registerDialog()
{
    ++m_openDialogs;
}

void MathomScanPreloader::unregisterDialog()
{
    if (m_openDialogs > 0)
        --m_openDialogs;
    // Continue preloading all remaining devices after the window closes.
    beginNextInspection();
}

void MathomScanPreloader::beginNextInspection()
{
    if (!m_hasDeviceList || m_refreshPending || m_discovery.isRunning()
        || m_inspection.isRunning() || !m_activeId.isEmpty())
        return;

    QString nextId;
    while (nextId.isEmpty()) {
        QString candidate;
        if (!m_priority.isEmpty())
            candidate = m_priority.takeFirst();
        else if (m_openDialogs == 0 && !m_background.isEmpty())
            candidate = m_background.takeFirst();
        else
            return;

        if (containsDevice(candidate) && !m_capabilities.contains(candidate))
            nextId = candidate;
    }
    m_activeId = nextId;
    m_activeGeneration = m_generation;
    m_inspectionPhase->store(Phase::Idle, std::memory_order_relaxed);
    auto phase = m_inspectionPhase;
    m_inspection.setFuture(QtConcurrent::run([nextId, phase] {
        return MathomScan::inspect(nextId, [phase](Phase stage) {
            phase->store(stage, std::memory_order_relaxed);
        });
    }));
    DiagnosticManager::instance().logEvent(
        QStringLiteral("SCAN_PRELOAD_INSPECTION_BEGIN"));
    Q_EMIT inspectionStarted(nextId);
}
