// SPDX-License-Identifier: GPL-2.0-or-later
#include "mathomscanbackend.h"

extern "C" {
#include <sane/sane.h>
#include <sane/saneopts.h>
}

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <cstring>

namespace {
QMutex &saneMutex()
{
    static QMutex mutex;
    return mutex;
}

QString statusMessage(SANE_Status status)
{
    return QString::fromLocal8Bit(sane_strstatus(status));
}

struct SaneSession {
    SANE_Handle handle = nullptr;
    bool initialized = false;
    ~SaneSession()
    {
        if (handle) {
            sane_cancel(handle);
            sane_close(handle);
        }
        if (initialized)
            sane_exit();
    }
    SANE_Status start()
    {
        const SANE_Status status = sane_init(nullptr, nullptr);
        initialized = status == SANE_STATUS_GOOD;
        return status;
    }
};

int optionCount(SANE_Handle handle)
{
    SANE_Word count = 0;
    if (sane_control_option(handle, 0, SANE_ACTION_GET_VALUE, &count, nullptr) != SANE_STATUS_GOOD)
        return 0;
    return count;
}

int findOption(SANE_Handle handle, const QByteArray &name)
{
    const int count = optionCount(handle);
    for (int i = 1; i < count; ++i) {
        const auto *descriptor = sane_get_option_descriptor(handle, i);
        if (descriptor && descriptor->name && name == descriptor->name)
            return i;
    }
    return -1;
}

bool writable(SANE_Handle handle, int index)
{
    if (index < 0)
        return false;
    const auto *descriptor = sane_get_option_descriptor(handle, index);
    return descriptor && SANE_OPTION_IS_ACTIVE(descriptor->cap)
        && SANE_OPTION_IS_SETTABLE(descriptor->cap);
}

QStringList stringsFor(SANE_Handle handle, int index)
{
    QStringList result;
    if (index < 0)
        return result;
    const auto *descriptor = sane_get_option_descriptor(handle, index);
    if (!descriptor || descriptor->type != SANE_TYPE_STRING
        || descriptor->constraint_type != SANE_CONSTRAINT_STRING_LIST)
        return result;
    for (const char *const *entry = descriptor->constraint.string_list;
         entry && *entry; ++entry)
        result.append(QString::fromLocal8Bit(*entry));
    return result;
}

bool setString(SANE_Handle handle, const char *name, const QString &value)
{
    const int index = findOption(handle, name);
    if (!writable(handle, index) || value.isEmpty())
        return false;
    const auto *descriptor = sane_get_option_descriptor(handle, index);
    if (!descriptor || descriptor->type != SANE_TYPE_STRING)
        return false;
    QByteArray bytes = value.toLocal8Bit();
    bytes.resize(descriptor->size);
    SANE_Int info = 0;
    return sane_control_option(handle, index, SANE_ACTION_SET_VALUE,
                               bytes.data(), &info) == SANE_STATUS_GOOD;
}

bool setNumber(SANE_Handle handle, const char *name, double number)
{
    const int index = findOption(handle, name);
    if (!writable(handle, index))
        return false;
    const auto *descriptor = sane_get_option_descriptor(handle, index);
    if (!descriptor)
        return false;
    SANE_Word value = descriptor->type == SANE_TYPE_FIXED
        ? SANE_FIX(number) : static_cast<SANE_Word>(number);
    return sane_control_option(handle, index, SANE_ACTION_SET_VALUE,
                               &value, nullptr) == SANE_STATUS_GOOD;
}

QString chooseSource(const QStringList &sources, QString requested, bool duplex)
{
    if (requested.isEmpty())
        return {};
    // The SANE option is a device-specific string, not a portable enum.
    const QString lower = requested.toLower();
    const bool wantsFeeder = lower.contains(QStringLiteral("adf"))
        || lower.contains(QStringLiteral("feeder"))
        || lower.contains(QStringLiteral("chargeur"));
    const QString expected = wantsFeeder ? QStringLiteral("adf") : QStringLiteral("flatbed");
    if (wantsFeeder) {
        for (const QString &value : sources)
            if ((value.contains(expected, Qt::CaseInsensitive)
                 || value.contains(QStringLiteral("feeder"), Qt::CaseInsensitive))
                && value.contains(QStringLiteral("duplex"), Qt::CaseInsensitive) == duplex)
                return value;
    }
    if (sources.contains(requested))
        return requested;
    return sources.isEmpty() ? QString() : sources.first();
}

void configureScan(SANE_Handle handle, const MathomScan::Options &options)
{
    const auto sources = stringsFor(handle, findOption(handle, SANE_NAME_SCAN_SOURCE));
    setString(handle, SANE_NAME_SCAN_SOURCE,
              chooseSource(sources, options.source, options.duplex));
    if (options.duplex) {
        const int duplexIndex = findOption(handle, QByteArrayLiteral("duplex"));
        if (writable(handle, duplexIndex)) {
            SANE_Word enabled = SANE_TRUE;
            sane_control_option(handle, duplexIndex, SANE_ACTION_SET_VALUE,
                                &enabled, nullptr);
        }
    }
    if (!options.mode.isEmpty())
        setString(handle, SANE_NAME_SCAN_MODE, options.mode);
    if (options.dpi > 0)
        setNumber(handle, SANE_NAME_SCAN_RESOLUTION, options.dpi);

    const bool landscape = options.paper == QStringLiteral("A3");
    const double width = landscape ? 297.0 : options.paper == QStringLiteral("A5") ? 148.0 : 210.0;
    const double height = landscape ? 420.0 : options.paper == QStringLiteral("A5") ? 210.0 : 297.0;
    setNumber(handle, SANE_NAME_SCAN_TL_X, 0.0);
    setNumber(handle, SANE_NAME_SCAN_TL_Y, 0.0);
    setNumber(handle, SANE_NAME_SCAN_BR_X, width);
    setNumber(handle, SANE_NAME_SCAN_BR_Y, height);
}

QImage readFrame(SANE_Handle handle, const std::shared_ptr<std::atomic_bool> &cancel,
                 QString *error)
{
    SANE_Parameters params = {};
    const SANE_Status paramStatus = sane_get_parameters(handle, &params);
    if (paramStatus != SANE_STATUS_GOOD) {
        *error = statusMessage(paramStatus);
        return {};
    }
    if (params.pixels_per_line <= 0 || params.bytes_per_line <= 0
        || (params.format != SANE_FRAME_GRAY && params.format != SANE_FRAME_RGB)
        || (params.depth != 1 && params.depth != 8)
        || (params.format == SANE_FRAME_RGB && params.depth != 8)
        || !params.last_frame) {
        *error = QStringLiteral("Unsupported SANE image format or multi-frame format");
        return {};
    }
    // Bound memory per page so an erroneous backend cannot exhaust memory.
    constexpr qsizetype maxBytes = 256 * 1024 * 1024;
    QByteArray raw;
    if (params.lines > 0) {
        const qint64 expected = qint64(params.lines) * params.bytes_per_line;
        if (expected > maxBytes) {
            *error = QStringLiteral("Page exceeds safe memory limit");
            return {};
        }
        raw.reserve(static_cast<qsizetype>(expected));
    }
    QByteArray chunk(64 * 1024, Qt::Uninitialized);
    for (;;) {
        if (cancel && cancel->load()) {
            sane_cancel(handle);
            *error = QStringLiteral("Acquisition canceled");
            return {};
        }
        SANE_Int size = 0;
        const SANE_Status status = sane_read(
            handle, reinterpret_cast<SANE_Byte *>(chunk.data()),
            static_cast<SANE_Int>(chunk.size()), &size);
        if (size > 0) {
            if (raw.size() > maxBytes - size) {
                *error = QStringLiteral("Page exceeds safe memory limit");
                return {};
            }
            raw.append(chunk.constData(), size);
        }
        if (status == SANE_STATUS_EOF)
            break;
        if (status != SANE_STATUS_GOOD) {
            *error = statusMessage(status);
            return {};
        }
    }
    const int lines = params.lines > 0 ? params.lines
        : static_cast<int>(raw.size() / params.bytes_per_line);
    if (lines <= 0 || qint64(lines) * params.bytes_per_line > raw.size()) {
        *error = QStringLiteral("Incomplete image data from scanner");
        return {};
    }
    QImage::Format format = params.depth == 1 ? QImage::Format_Mono
        : params.format == SANE_FRAME_RGB ? QImage::Format_RGB888
                                           : QImage::Format_Grayscale8;
    QImage image(params.pixels_per_line, lines, format);
    if (image.isNull()) {
        *error = QStringLiteral("Unable to allocate scanned image");
        return {};
    }
    if (params.depth == 1) {
        image.setColor(0, qRgb(255, 255, 255));
        image.setColor(1, qRgb(0, 0, 0));
    }
    const int copyLength = std::min(params.bytes_per_line,
                                    static_cast<int>(image.bytesPerLine()));
    for (int row = 0; row < lines; ++row)
        std::memcpy(image.scanLine(row),
                    raw.constData() + qint64(row) * params.bytes_per_line,
                    static_cast<size_t>(copyLength));
    return image;
}
} // namespace

QList<MathomScan::Device> MathomScan::discover(QString *error)
{
    QMutexLocker lock(&saneMutex());
    QList<Device> result;
    SaneSession session;
    const SANE_Status start = session.start();
    if (start != SANE_STATUS_GOOD) {
        if (error)
            *error = statusMessage(start);
        return result;
    }
    const SANE_Device **devices = nullptr;
    const SANE_Status status = sane_get_devices(&devices, SANE_FALSE);
    if (status != SANE_STATUS_GOOD) {
        if (error)
            *error = statusMessage(status);
        return result;
    }
    for (int index = 0; devices && devices[index]; ++index) {
        const auto *device = devices[index];
        result.append({QString::fromLocal8Bit(device->name),
                       QString::fromLocal8Bit(device->model)
                           + QStringLiteral(" (")
                           + QString::fromLocal8Bit(device->vendor)
                           + QStringLiteral(")")});
    }
    return result;
}

MathomScan::Capabilities MathomScan::inspect(const QString &deviceId)
{
    QMutexLocker lock(&saneMutex());
    Capabilities result;
    SaneSession session;
    SANE_Status status = session.start();
    if (status == SANE_STATUS_GOOD) {
        const QByteArray name = deviceId.toLocal8Bit();
        status = sane_open(name.constData(), &session.handle);
    }
    if (status != SANE_STATUS_GOOD) {
        result.error = statusMessage(status);
        return result;
    }
    result.sources = stringsFor(session.handle,
                                findOption(session.handle, SANE_NAME_SCAN_SOURCE));
    result.modes = stringsFor(session.handle,
                              findOption(session.handle, SANE_NAME_SCAN_MODE));
    const int resolutionIndex =
        findOption(session.handle, SANE_NAME_SCAN_RESOLUTION);
    const auto *descriptor = resolutionIndex >= 0
        ? sane_get_option_descriptor(session.handle, resolutionIndex) : nullptr;
    if (descriptor && descriptor->constraint_type == SANE_CONSTRAINT_WORD_LIST) {
        const SANE_Word *list = descriptor->constraint.word_list;
        for (SANE_Word i = 1; i <= list[0]; ++i) {
            const int dpi = descriptor->type == SANE_TYPE_FIXED
                ? int(SANE_UNFIX(list[i])) : int(list[i]);
            if (dpi > 0 && !result.resolutions.contains(dpi))
                result.resolutions.append(dpi);
        }
    } else if (descriptor && descriptor->constraint_type == SANE_CONSTRAINT_RANGE) {
        const double min = descriptor->type == SANE_TYPE_FIXED
            ? SANE_UNFIX(descriptor->constraint.range->min)
            : descriptor->constraint.range->min;
        const double max = descriptor->type == SANE_TYPE_FIXED
            ? SANE_UNFIX(descriptor->constraint.range->max)
            : descriptor->constraint.range->max;
        for (int dpi : {150, 300, 600})
            if (dpi >= min && dpi <= max)
                result.resolutions.append(dpi);
    }
    if (result.resolutions.isEmpty())
        result.resolutions.append(300);
    std::sort(result.resolutions.begin(), result.resolutions.end());
    result.hasSourceOption = !result.sources.isEmpty();
    for (const QString &source : result.sources)
        if (source.contains(QStringLiteral("duplex"), Qt::CaseInsensitive))
            result.duplex = true;
    result.duplex |= writable(session.handle,
                               findOption(session.handle, QByteArrayLiteral("duplex")));
    const int brX = findOption(session.handle, SANE_NAME_SCAN_BR_X);
    const int brY = findOption(session.handle, SANE_NAME_SCAN_BR_Y);
    const auto *x = brX >= 0 ? sane_get_option_descriptor(session.handle, brX) : nullptr;
    const auto *y = brY >= 0 ? sane_get_option_descriptor(session.handle, brY) : nullptr;
    auto maxMm = [](const SANE_Option_Descriptor *option) -> double {
        if (!option || option->constraint_type != SANE_CONSTRAINT_RANGE
            || !option->constraint.range || option->unit != SANE_UNIT_MM)
            return -1.0;
        const SANE_Word max = option->constraint.range->max;
        return option->type == SANE_TYPE_FIXED ? SANE_UNFIX(max) : max;
    };
    const double width = maxMm(x), height = maxMm(y);
    if (width > 0 && height > 0) {
        result.canScanA3 = width >= 296 && height >= 419;
        result.canScanA4 = width >= 209 && height >= 296;
        result.canScanA5 = width >= 147 && height >= 209;
    }
    return result;
}

MathomScan::Result MathomScan::acquire(
    const Options &options, const std::shared_ptr<std::atomic_bool> &cancel,
    const std::function<void(int)> &pageAvailable)
{
    QMutexLocker lock(&saneMutex());
    Result result;
    SaneSession session;
    SANE_Status status = session.start();
    if (status == SANE_STATUS_GOOD) {
        const QByteArray name = options.deviceId.toLocal8Bit();
        status = sane_open(name.constData(), &session.handle);
    }
    if (status != SANE_STATUS_GOOD) {
        result.error = statusMessage(status);
        return result;
    }
    configureScan(session.handle, options);
    QDir sessionDir(options.sessionDirectory);
    if (!sessionDir.exists() && !QDir().mkpath(options.sessionDirectory)) {
        result.error = QStringLiteral("Cannot create recovery directory");
        return result;
    }
    const bool feeder = options.source.contains(QStringLiteral("adf"), Qt::CaseInsensitive)
        || options.source.contains(QStringLiteral("feeder"), Qt::CaseInsensitive);
    constexpr int maxPagesPerBatch = 500;
    for (int page = 0; page < maxPagesPerBatch; ++page) {
        if (cancel && cancel->load()) {
            result.canceled = true;
            break;
        }
        status = sane_start(session.handle);
        if ((status == SANE_STATUS_NO_DOCS || status == SANE_STATUS_EOF)
            && feeder && page > 0)
            break;
        if (status != SANE_STATUS_GOOD) {
            result.error = statusMessage(status);
            break;
        }
        QString readError;
        const QImage image = readFrame(session.handle, cancel, &readError);
        if (image.isNull()) {
            result.error = readError;
            result.canceled = cancel && cancel->load();
            break;
        }
        const QString filename = QStringLiteral("page_%1.png")
            .arg(options.nextImageNumber + page, 5, 10, QLatin1Char('0'));
        const QString path = sessionDir.filePath(filename);
        if (QFileInfo::exists(path) || !image.save(path, "PNG")) {
            result.error = QStringLiteral("Could not safely store scanned page");
            break;
        }
        result.paths.append(path);
        if (pageAvailable)
            pageAvailable(result.paths.size());
        if (!feeder)
            break;
    }
    return result;
}
