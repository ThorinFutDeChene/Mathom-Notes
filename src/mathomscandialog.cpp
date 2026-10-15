// SPDX-License-Identifier: GPL-2.0-or-later
#include "mathomscandialog.h"

#include <KLocalizedString>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTransform>
#include <QFont>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QToolButton>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>

namespace {
QString localizedSource(const QString &raw)
{
    const QString name = raw.toLower();
    if (name.contains(QStringLiteral("flatbed")))
        return i18n("Glass");
    if (name.contains(QStringLiteral("duplex"))
        && (name.contains(QStringLiteral("adf"))
            || name.contains(QStringLiteral("feeder"))))
        return i18n("Document feeder (duplex)");
    if (name.contains(QStringLiteral("adf"))
        || name.contains(QStringLiteral("feeder")))
        return i18n("Document feeder");
    return raw;
}
QString localizedMode(const QString &raw)
{
    const QString value = raw.toLower();
    if (value.contains(QStringLiteral("color")))
        return i18n("Color");
    if (value.contains(QStringLiteral("gray")))
        return i18n("Grayscale");
    if (value.contains(QStringLiteral("lineart"))
        || value.contains(QStringLiteral("black")))
        return i18n("Black and white");
    return raw;
}
bool feederSource(const QString &value)
{
    return value.contains(QStringLiteral("adf"), Qt::CaseInsensitive)
        || value.contains(QStringLiteral("feeder"), Qt::CaseInsensitive);
}
QIcon addPageIcon()
{
    QPixmap pixmap(150, 195);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QPen pen(Qt::gray, 2, Qt::DashLine);
    painter.setPen(pen);
    painter.drawRoundedRect(QRect(4, 4, 142, 187), 8, 8);
    QFont font = painter.font();
    font.setPointSize(45);
    painter.setFont(font);
    painter.setPen(Qt::darkGray);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("+"));
    return QIcon(pixmap);
}
} // namespace

MathomScanDialog::MathomScanDialog(QWidget *parent)
    : QDialog(parent)
    , m_preferences(MathomScanConfig::load())
{
    setWindowTitle(i18n("Mathom Scan"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1040, 560);
    setMinimumSize(680, 440);

    auto *layout = new QVBoxLayout(this);
    auto *hardware = new QHBoxLayout;
    m_scanners = new QComboBox(this);
    m_scanners->setMinimumWidth(200);
    m_refreshButton = new QPushButton(i18n("Search again"), this);
    hardware->addWidget(new QLabel(i18n("Scanner:"), this));
    hardware->addWidget(m_scanners, 1);
    hardware->addWidget(m_refreshButton);
    layout->addLayout(hardware);

    // One native Qt toolbar with an overflow menu on narrow windows.
    auto *toolbar = new QToolBar(i18n("Scanner options"), this);
    toolbar->setMovable(false);
    m_sources = new QComboBox(toolbar);
    m_duplex = new QCheckBox(i18n("Duplex"), toolbar);
    m_color = new QComboBox(toolbar);
    m_dpi = new QComboBox(toolbar);
    m_paper = new QComboBox(toolbar);
    m_output = new QComboBox(toolbar);
    m_paper->addItems({QStringLiteral("A5"), QStringLiteral("A4"), QStringLiteral("A3")});
    m_paper->setCurrentText(QStringLiteral("A4"));
    m_output->addItems({QStringLiteral("PDF"), QStringLiteral("JPEG")});
    m_scanButton = new QPushButton(i18n("Scan"), toolbar);
    m_scanButton->setIcon(QIcon::fromTheme(QStringLiteral("scanner")));
    toolbar->addWidget(m_scanButton);
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(i18n("Source:"), toolbar));
    toolbar->addWidget(m_sources);
    toolbar->addWidget(m_duplex);
    toolbar->addWidget(new QLabel(i18n("Color:"), toolbar));
    toolbar->addWidget(m_color);
    toolbar->addWidget(new QLabel(i18n("Resolution:"), toolbar));
    toolbar->addWidget(m_dpi);
    toolbar->addWidget(new QLabel(i18n("Page size:"), toolbar));
    toolbar->addWidget(m_paper);
    toolbar->addWidget(new QLabel(i18n("File type:"), toolbar));
    toolbar->addWidget(m_output);
    layout->addWidget(toolbar);

    m_status = new QLabel(i18n("Connect your scanner, then choose it."), this);
    m_status->setWordWrap(true);
    m_status->setAccessibleName(i18n("Scanner status"));
    layout->addWidget(m_status);

    m_preview = new QListWidget(this);
    m_preview->setViewMode(QListView::IconMode);
    m_preview->setFlow(QListView::LeftToRight);
    m_preview->setWrapping(false);
    m_preview->setMovement(QListView::Static);
    m_preview->setResizeMode(QListView::Adjust);
    m_preview->setIconSize(QSize(150, 195));
    m_preview->setSpacing(15);
    m_preview->setSelectionMode(QAbstractItemView::SingleSelection);
    m_preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_preview->setAccessibleName(i18n("Scanned pages and add-page control"));
    layout->addWidget(m_preview, 1);

    auto *pageActions = new QHBoxLayout;
    m_rotateLeft = new QPushButton(i18n("Rotate left 90°"), this);
    m_rotateRight = new QPushButton(i18n("Rotate right 90°"), this);
    m_moveLeft = new QPushButton(i18n("Move left"), this);
    m_moveRight = new QPushButton(i18n("Move right"), this);
    m_delete = new QPushButton(i18n("Delete page"), this);
    m_replace = new QPushButton(i18n("Replace page"), this);
    m_undo = new QPushButton(i18n("Undo rotation"), this);
    m_redo = new QPushButton(i18n("Redo rotation"), this);
    m_saveButton = new QPushButton(i18n("Finish and save"), this);
    pageActions->addWidget(m_rotateLeft);
    pageActions->addWidget(m_rotateRight);
    pageActions->addWidget(m_undo);
    pageActions->addWidget(m_redo);
    pageActions->addWidget(m_moveLeft);
    pageActions->addWidget(m_moveRight);
    pageActions->addWidget(m_delete);
    pageActions->addWidget(m_replace);
    pageActions->addStretch(1);
    pageActions->addWidget(m_saveButton);
    layout->addLayout(pageActions);

    connect(m_refreshButton, &QPushButton::clicked,
            this, &MathomScanDialog::searchDevices);
    connect(m_scanners, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MathomScanDialog::selectScanner);
    connect(m_scanButton, &QPushButton::clicked,
            this, &MathomScanDialog::startAcquisition);
    connect(m_preview, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const int row = m_preview->row(item);
        if (row == m_pages.size()) {
            startAcquisition();
        } else {
            selectPage(row);
        }
    });
    connect(m_rotateLeft, &QPushButton::clicked,
            this, [this] { rotatePage(-1); });
    connect(m_rotateRight, &QPushButton::clicked,
            this, [this] { rotatePage(1); });
    connect(m_undo, &QPushButton::clicked, this, &MathomScanDialog::undoRotation);
    connect(m_redo, &QPushButton::clicked, this, &MathomScanDialog::redoRotation);
    connect(m_replace, &QPushButton::clicked, this, [this] {
        if (m_selected < 0 || m_selected >= m_pages.size() || m_busy)
            return;
        m_replacing = m_selected;
        startAcquisition();
    });
    connect(m_moveLeft, &QPushButton::clicked,
            this, [this] { movePage(-1); });
    connect(m_moveRight, &QPushButton::clicked,
            this, [this] { movePage(1); });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        if (m_busy || m_selected < 0 || m_selected >= m_pages.size())
            return;
        if (QMessageBox::question(this, i18n("Remove page"),
                                  i18n("Remove the selected page from this document?"))
            != QMessageBox::Yes)
            return;
        const auto removed = m_pages.takeAt(m_selected);
        m_ignored.insert(QFileInfo(removed.path).fileName());
        m_selected = std::min(m_selected, int(m_pages.size()) - 1);
        saveManifest();
        refreshPages();
    });
    connect(m_saveButton, &QPushButton::clicked,
            this, &MathomScanDialog::saveDocument);
    connect(m_sources, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] {
        m_duplex->setEnabled(feederSource(m_sources->currentData().toString())
                             && m_capabilities.duplex);
    });

    connect(&m_discovery, &QFutureWatcher<QList<MathomScan::Device>>::finished,
            this, [this] {
        const auto devices = m_discovery.result();
        QSignalBlocker blocker(m_scanners);
        m_scanners->clear();
        const bool hasPreference = !m_preferences.deviceId.isEmpty();
        bool matched = false;
        for (const auto &device : devices) {
            if (hasPreference && device.id == m_preferences.deviceId)
                matched = true;
        }
        for (const auto &device : devices) {
            const bool mustMatch = m_preferences.mode == MathomScanPreferences::Required
                || (m_preferences.mode == MathomScanPreferences::Preferred && matched);
            if (!mustMatch || device.id == m_preferences.deviceId)
                m_scanners->addItem(device.label, device.id);
        }
        if (m_scanners->count()) {
            int index = m_scanners->findData(m_preferences.deviceId);
            m_scanners->setCurrentIndex(index >= 0 ? index : 0);
        }
        m_refreshButton->setEnabled(true);
        m_status->setText(m_scanners->count()
            ? i18np("%1 scanner available", "%1 scanners available", m_scanners->count())
            : i18n("No matching scanner found. Turn it on, connect it, then search again."));
        selectScanner();
    });
    connect(&m_inspection, &QFutureWatcher<MathomScan::Capabilities>::finished,
            this, [this] {
        updateCapabilities(m_inspection.result());
    });
    connect(&m_acquisition, &QFutureWatcher<MathomScan::Result>::finished,
            this, [this] {
        const MathomScan::Result result = m_acquisition.result();
        int first = 0;
        if (m_replacing >= 0 && !result.paths.isEmpty()
            && m_replacing < m_pages.size()) {
            m_ignored.insert(QFileInfo(m_pages.at(m_replacing).path).fileName());
            m_pages[m_replacing] = {result.paths.first(), 0, m_paper->currentText()};
            first = 1;
        }
        m_replacing = -1;
        for (int i = first; i < result.paths.size(); ++i)
            m_pages.append({result.paths.at(i), 0, m_paper->currentText()});
        m_nextPageNumber += result.paths.size();
        saveManifest();
        m_selected = m_pages.isEmpty() ? -1 : int(m_pages.size()) - 1;
        refreshPages();
        setBusy(false, result.error.isEmpty()
            ? i18np("%1 new page scanned", "%1 new pages scanned", result.paths.size())
            : i18n("Scanner error: %1. Previously scanned pages are preserved.",
                   result.error));
    });
    connect(&m_export, &QFutureWatcher<MathomScanExport::Output>::finished,
            this, [this] {
        const auto result = m_export.result();
        if (!result.error.isEmpty()) {
            setBusy(false, i18n("Could not save the document: %1. Pages were kept.",
                                result.error));
            m_closeAfterSave = false;
            return;
        }
        if (!m_sessionDir.isEmpty())
            QDir(m_sessionDir).removeRecursively();
        m_sessionDir.clear();
        m_pages.clear();
        m_allowClose = true;
        setBusy(false, i18np("Saved %1 file in your configured folder.",
                            "Saved %1 files in your configured folder.",
                            result.files.size()));
        if (m_closeAfterSave)
            close();
    });

    refreshPages();
    loadRecoveredSession();
    searchDevices();
}

QString MathomScanDialog::sessionsRoot() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/scan-sessions");
}

void MathomScanDialog::setBusy(bool busy, const QString &message)
{
    m_busy = busy;
    m_status->setText(message);
    m_scanButton->setEnabled(!busy && !m_scanners->currentData().toString().isEmpty());
    m_refreshButton->setEnabled(!busy && !m_discovery.isRunning());
    m_saveButton->setEnabled(!busy && !m_pages.isEmpty());
    m_scanners->setEnabled(!busy);
    m_sources->setEnabled(!busy);
    m_color->setEnabled(!busy);
    m_dpi->setEnabled(!busy);
    m_paper->setEnabled(!busy);
    m_output->setEnabled(!busy);
    m_preview->setEnabled(!busy);
    const bool selected = m_selected >= 0 && m_selected < m_pages.size();
    m_rotateLeft->setEnabled(!busy && selected);
    m_rotateRight->setEnabled(!busy && selected);
    m_delete->setEnabled(!busy && selected);
    m_replace->setEnabled(!busy && selected);
    m_undo->setEnabled(!busy && !m_rotationUndo.isEmpty());
    m_redo->setEnabled(!busy && !m_rotationRedo.isEmpty());
    m_moveLeft->setEnabled(!busy && selected && m_selected > 0);
    m_moveRight->setEnabled(!busy && selected && m_selected + 1 < m_pages.size());
}

void MathomScanDialog::searchDevices()
{
    if (m_busy || m_discovery.isRunning() || m_inspection.isRunning())
        return;
    m_refreshButton->setEnabled(false);
    m_scanButton->setEnabled(false);
    m_status->setText(i18n("Searching for scanners..."));
    m_discovery.setFuture(QtConcurrent::run([] {
        return MathomScan::discover();
    }));
}

void MathomScanDialog::selectScanner()
{
    if (m_discovery.isRunning() || m_busy || m_inspection.isRunning())
        return;
    const QString id = m_scanners->currentData().toString();
    if (id.isEmpty()) {
        setBusy(false, i18n("No scanner selected."));
        return;
    }
    m_scanButton->setEnabled(false);
    m_status->setText(i18n("Reading scanner capabilities..."));
    m_inspection.setFuture(QtConcurrent::run([id] {
        return MathomScan::inspect(id);
    }));
}

void MathomScanDialog::updateCapabilities(const MathomScan::Capabilities &capabilities)
{
    m_capabilities = capabilities;
    if (!capabilities.error.isEmpty()) {
        m_scanButton->setEnabled(false);
        m_status->setText(i18n("Cannot open scanner: %1", capabilities.error));
        return;
    }
    {
        QSignalBlocker blocker(m_sources);
        m_sources->clear();
        if (capabilities.sources.isEmpty())
            m_sources->addItem(i18n("Automatic"), QString());
        else
            for (const QString &value : capabilities.sources)
                m_sources->addItem(localizedSource(value), value);
        int defaultSource = 0;
        for (int i = 0; i < m_sources->count(); ++i)
            if (m_sources->itemData(i).toString().contains(
                    QStringLiteral("flatbed"), Qt::CaseInsensitive))
                defaultSource = i;
        m_sources->setCurrentIndex(defaultSource);
    }
    m_color->clear();
    if (capabilities.modes.isEmpty())
        m_color->addItem(i18n("Automatic"), QString());
    else
        for (const QString &value : capabilities.modes)
            m_color->addItem(localizedMode(value), value);
    m_dpi->clear();
    for (int dpi : capabilities.resolutions)
        m_dpi->addItem(i18n("%1 dpi", dpi), dpi);
    const int preferredDpi = m_dpi->findData(300);
    m_dpi->setCurrentIndex(preferredDpi >= 0 ? preferredDpi : 0);

    if (auto *model = qobject_cast<QStandardItemModel *>(m_paper->model())) {
        for (int i = 0; i < m_paper->count(); ++i) {
            const QString page = m_paper->itemText(i);
            const bool supported = page == QStringLiteral("A3")
                ? capabilities.canScanA3
                : page == QStringLiteral("A5")
                    ? capabilities.canScanA5 : capabilities.canScanA4;
            model->item(i)->setEnabled(supported);
        }
    }
    if (!capabilities.canScanA4)
        m_paper->setCurrentText(capabilities.canScanA5
            ? QStringLiteral("A5") : QStringLiteral("A3"));
    m_duplex->setChecked(false);
    m_duplex->setEnabled(feederSource(m_sources->currentData().toString())
                         && capabilities.duplex);
    setBusy(false, i18n("Scanner ready. Choose options and scan."));
}

void MathomScanDialog::startAcquisition()
{
    if (m_busy || m_scanners->currentData().toString().isEmpty())
        return;
    if (m_sessionDir.isEmpty()) {
        const QString id = QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMdd-HHmmss-"))
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_sessionDir = QDir(sessionsRoot()).filePath(id);
        m_scanTime = QDateTime::currentDateTime();
    }
    MathomScan::Options options;
    options.deviceId = m_scanners->currentData().toString();
    options.source = m_sources->currentData().toString();
    options.duplex = m_duplex->isChecked();
    options.mode = m_color->currentData().toString();
    options.dpi = m_dpi->currentData().toInt();
    options.paper = m_paper->currentText();
    options.sessionDirectory = m_sessionDir;
    options.nextImageNumber = m_nextPageNumber;
    m_cancel->store(false);
    setBusy(true, i18n("Scanning... You can continue working in Mathom Notes."));
    m_acquisition.setFuture(QtConcurrent::run([options, cancel = m_cancel] {
        return MathomScan::acquire(options, cancel, {});
    }));
}

void MathomScanDialog::refreshPages()
{
    QSignalBlocker blocker(m_preview);
    m_preview->clear();
    for (int i = 0; i < m_pages.size(); ++i) {
        QImage image(m_pages.at(i).path);
        if (m_pages.at(i).quarterTurns) {
            QTransform rotation;
            rotation.rotate(m_pages.at(i).quarterTurns * 90);
            image = image.transformed(rotation, Qt::FastTransformation);
        }
        const QPixmap thumb = QPixmap::fromImage(
            image.scaled(145, 185, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        auto *item = new QListWidgetItem(QIcon(thumb),
                                         i18n("Page %1", i + 1), m_preview);
        item->setToolTip(i18n("Select page %1 to rotate, delete or move it.", i + 1));
    }
    auto *ghost = new QListWidgetItem(addPageIcon(), i18n("Scan more pages"), m_preview);
    ghost->setToolTip(i18n("Scan the pages currently loaded in your scanner."));
    ghost->setData(Qt::UserRole, QStringLiteral("mathom-scan-add-page"));
    ghost->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_preview->setCurrentRow(m_selected >= 0 ? m_selected : m_pages.size());
    setBusy(m_busy, m_status->text());
}

void MathomScanDialog::selectPage(int index)
{
    m_selected = index >= 0 && index < m_pages.size() ? index : -1;
    setBusy(m_busy, m_status->text());
}

void MathomScanDialog::rotatePage(int delta)
{
    if (m_busy || m_selected < 0 || m_selected >= m_pages.size())
        return;
    auto &page = m_pages[m_selected];
    m_rotationUndo.append({page.path, page.quarterTurns});
    m_rotationRedo.clear();
    page.quarterTurns = (page.quarterTurns + delta + 4) % 4;
    saveManifest();
    refreshPages();
}

void MathomScanDialog::undoRotation()
{
    while (!m_rotationUndo.isEmpty()) {
        const auto step = m_rotationUndo.takeLast();
        for (auto &page : m_pages) {
            if (page.path == step.first) {
                m_rotationRedo.append({page.path, page.quarterTurns});
                page.quarterTurns = step.second;
                saveManifest();
                refreshPages();
                return;
            }
        }
    }
    refreshPages();
}

void MathomScanDialog::redoRotation()
{
    while (!m_rotationRedo.isEmpty()) {
        const auto step = m_rotationRedo.takeLast();
        for (auto &page : m_pages) {
            if (page.path == step.first) {
                m_rotationUndo.append({page.path, page.quarterTurns});
                page.quarterTurns = step.second;
                saveManifest();
                refreshPages();
                return;
            }
        }
    }
    refreshPages();
}

void MathomScanDialog::movePage(int delta)
{
    const int next = m_selected + delta;
    if (m_busy || m_selected < 0 || next < 0 || next >= m_pages.size())
        return;
    m_pages.swapItemsAt(m_selected, next);
    m_selected = next;
    saveManifest();
    refreshPages();
}

void MathomScanDialog::saveManifest()
{
    if (m_sessionDir.isEmpty() || !QDir().mkpath(m_sessionDir))
        return;
    QJsonObject object;
    object.insert(QStringLiteral("started"), m_scanTime.toString(Qt::ISODateWithMs));
    QJsonArray pages;
    for (const auto &page : m_pages) {
        QJsonObject p;
        p.insert(QStringLiteral("file"), QFileInfo(page.path).fileName());
        p.insert(QStringLiteral("turns"), page.quarterTurns);
        p.insert(QStringLiteral("paper"), page.paper);
        pages.append(p);
    }
    object.insert(QStringLiteral("pages"), pages);
    QJsonArray ignored;
    for (const auto &name : m_ignored)
        ignored.append(name);
    object.insert(QStringLiteral("ignored"), ignored);
    QSaveFile file(QDir(m_sessionDir).filePath(QStringLiteral("session.json")));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(object).toJson());
        file.commit();
    }
}

void MathomScanDialog::loadRecoveredSession()
{
    const QDir root(sessionsRoot());
    const QStringList directories = root.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QString &name : directories) {
        const QString directory = root.filePath(name);
        const QDir dir(directory);
        if (dir.entryList({QStringLiteral("page_*.png")}, QDir::Files).isEmpty())
            continue;
        const auto choice = QMessageBox::question(
            this, i18n("Recover unfinished scan"),
            i18n("Unfinished scanned pages were found. Restore this document?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (choice != QMessageBox::Yes)
            return; // Keep files: user may recover them later.
        m_sessionDir = directory;
        QFile file(dir.filePath(QStringLiteral("session.json")));
        if (file.open(QIODevice::ReadOnly)) {
            const auto object = QJsonDocument::fromJson(file.readAll()).object();
            m_scanTime = QDateTime::fromString(
                object.value(QStringLiteral("started")).toString(), Qt::ISODateWithMs);
            const auto ignored = object.value(QStringLiteral("ignored")).toArray();
            for (const auto &entry : ignored)
                m_ignored.insert(entry.toString());
            for (const auto &entry : object.value(QStringLiteral("pages")).toArray()) {
                const auto p = entry.toObject();
                const QString path = dir.filePath(p.value(QStringLiteral("file")).toString());
                if (QFile::exists(path))
                    m_pages.append({path, p.value(QStringLiteral("turns")).toInt(),
                                    p.value(QStringLiteral("paper")).toString(QStringLiteral("A4"))});
            }
        }
        for (const QString &entry : dir.entryList(
                 {QStringLiteral("page_*.png")}, QDir::Files, QDir::Name)) {
            bool alreadyKnown = m_ignored.contains(entry);
            for (const auto &page : m_pages)
                alreadyKnown |= QFileInfo(page.path).fileName() == entry;
            if (!alreadyKnown)
                m_pages.append({dir.filePath(entry), 0, QStringLiteral("A4")});
        }
        if (!m_scanTime.isValid())
            m_scanTime = QDateTime::currentDateTime();
        m_nextPageNumber = dir.entryList(
            {QStringLiteral("page_*.png")}, QDir::Files).size() + 1;
        m_selected = m_pages.isEmpty() ? -1 : 0;
        refreshPages();
        m_status->setText(i18n("Unfinished scan restored. Your pages are safe."));
        break;
    }
}

void MathomScanDialog::saveDocument()
{
    if (m_busy || m_pages.isEmpty())
        return;
    const auto pages = m_pages;
    const QString format = m_output->currentText();
    const QString paper = m_paper->currentText();
    const QString destination = m_preferences.outputDirectory;
    const QDateTime scanTime = m_scanTime;
    m_closeAfterSave = true;
    setBusy(true, i18n("Saving your scanned document..."));
    m_export.setFuture(QtConcurrent::run([pages, format, paper, destination, scanTime] {
        return MathomScanExport::write(pages, format, paper, destination, scanTime);
    }));
}

void MathomScanDialog::closeEvent(QCloseEvent *event)
{
    if (m_busy) {
        m_status->setText(i18n("An operation is in progress. Wait for it to finish."));
        event->ignore();
        return;
    }
    if (m_allowClose || m_pages.isEmpty()) {
        event->accept();
        return;
    }
    QMessageBox prompt(this);
    prompt.setWindowTitle(i18n("Unfinished scan"));
    prompt.setText(i18n("Your scanned pages have not been saved."));
    auto *continueButton = prompt.addButton(i18n("Continue"), QMessageBox::RejectRole);
    auto *saveButton = prompt.addButton(i18n("Save"), QMessageBox::AcceptRole);
    auto *abandonButton = prompt.addButton(i18n("Abandon"), QMessageBox::DestructiveRole);
    prompt.exec();
    if (prompt.clickedButton() == continueButton) {
        event->ignore();
    } else if (prompt.clickedButton() == saveButton) {
        event->ignore();
        saveDocument();
    } else if (prompt.clickedButton() == abandonButton) {
        if (QMessageBox::question(this, i18n("Discard scanned pages"),
                                  i18n("Permanently discard all pages of this scan?"))
            == QMessageBox::Yes) {
            if (!m_sessionDir.isEmpty())
                QDir(m_sessionDir).removeRecursively();
            m_allowClose = true;
            event->accept();
        } else {
            event->ignore();
        }
    } else {
        event->ignore();
    }
}
