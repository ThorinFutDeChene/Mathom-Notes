// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "mathomscanbackend.h"
#include "mathomscanconfig.h"
#include "mathomscanexport.h"

#include <QDateTime>
#include <QDialog>
#include <QFutureWatcher>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <memory>
#include <atomic>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

class MathomScanDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit MathomScanDialog(QWidget *parent = nullptr);
    ~MathomScanDialog() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QVariantMap diagnosticState() const;
    void traceScanEvent(const QString &event, const QVariantMap &details = {}) const;
    void searchDevices();
    void displayDevices(const QList<MathomScan::Device> &devices);
    void selectScanner();
    void updateCapabilities(const MathomScan::Capabilities &capabilities);
    void updateScanProfile();
    int selectedDpi() const;
    void startAcquisition();
    void refreshPages();
    void selectPage(int index);
    void rotatePage(int delta);
    void saveManifest();
    void loadRecoveredSession();
    void saveDocument();
    void movePage(int delta);
    void undoRotation();
    void redoRotation();
    QString sessionsRoot() const;
    void setBusy(bool busy, const QString &message);

    MathomScanPreferences m_preferences;
    QComboBox *m_scanners = nullptr;
    QComboBox *m_sources = nullptr;
    QComboBox *m_color = nullptr;
    QComboBox *m_profile = nullptr;
    QComboBox *m_paper = nullptr;
    QCheckBox *m_duplex = nullptr;
    QListWidget *m_preview = nullptr;
    QPushButton *m_scanButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_rotateLeft = nullptr;
    QPushButton *m_rotateRight = nullptr;
    QPushButton *m_moveLeft = nullptr;
    QPushButton *m_moveRight = nullptr;
    QPushButton *m_delete = nullptr;
    QPushButton *m_replace = nullptr;
    QPushButton *m_undo = nullptr;
    QPushButton *m_redo = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_profileInfo = nullptr;

    QFutureWatcher<MathomScan::Result> m_acquisition;
    QFutureWatcher<MathomScanExport::Output> m_export;
    std::shared_ptr<std::atomic_bool> m_cancel =
        std::make_shared<std::atomic_bool>(false);

    using DiagnosticPhase = MathomScan::DiagnosticStage;
    using PhaseAtomic = std::atomic<DiagnosticPhase>;
    std::shared_ptr<PhaseAtomic> m_acquisitionPhase =
        std::make_shared<PhaseAtomic>(DiagnosticPhase::Idle);

    QList<MathomScanExport::Page> m_pages;
    QString m_sessionDir;
    QDateTime m_scanTime;
    int m_selected = -1;
    int m_nextPageNumber = 1;
    bool m_busy = false;
    MathomScan::Capabilities m_capabilities;
    QSet<QString> m_ignored;
    QList<QPair<QString, int>> m_rotationUndo;
    QList<QPair<QString, int>> m_rotationRedo;
    int m_replacing = -1;
    bool m_exporting = false;
    bool m_closeAfterSave = false;
    bool m_allowClose = false;
};
