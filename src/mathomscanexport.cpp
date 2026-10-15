// SPDX-License-Identifier: GPL-2.0-or-later
#include "mathomscanexport.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLockFile>
#include <QMarginsF>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTransform>
#include <algorithm>

namespace {
QImage renderedPage(const MathomScanExport::Page &page)
{
    QImage image(page.path);
    if (image.isNull())
        return {};
    const int turns = ((page.quarterTurns % 4) + 4) % 4;
    if (turns) {
        QTransform transform;
        transform.rotate(turns * 90);
        image = image.transformed(transform, Qt::SmoothTransformation);
    }
    return image;
}
QString documentPrefix(const QDateTime &when, int number)
{
    return QStringLiteral("scan_%1_%2")
        .arg(when.toString(QStringLiteral("yyyy-MM-dd_HH-mm")))
        .arg(number, 3, 10, QLatin1Char('0'));
}
int nextNumber(const QDir &directory, const QDateTime &when)
{
    const QString hour = when.toString(QStringLiteral("yyyy-MM-dd_HH"));
    const QRegularExpression pattern(
        QStringLiteral("^scan_%1-\\d{2}_(\\d{3,})(?:_\\d+)?\\.(?:pdf|jpg)$")
            .arg(QRegularExpression::escape(hour)),
        QRegularExpression::CaseInsensitiveOption);
    int highest = 0;
    for (const QString &filename : directory.entryList(QDir::Files)) {
        const auto match = pattern.match(filename);
        if (match.hasMatch())
            highest = std::max(highest, match.captured(1).toInt());
    }
    return highest + 1;
}
bool writePdf(const QList<MathomScanExport::Page> &pages,
              const QString &paper, const QString &path, QString *error)
{
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        *error = output.errorString();
        return false;
    }
    QPdfWriter writer(&output);
    writer.setResolution(300);
    const QPageSize::PageSizeId pageId =
        paper == QStringLiteral("A3") ? QPageSize::A3
        : paper == QStringLiteral("A5") ? QPageSize::A5 : QPageSize::A4;
    writer.setPageSize(QPageSize(pageId));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter painter(&writer);
    if (!painter.isActive()) {
        *error = QStringLiteral("Could not initialize PDF writer");
        output.cancelWriting();
        return false;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (qsizetype i = 0; i < pages.size(); ++i) {
        if (i && !writer.newPage()) {
            *error = QStringLiteral("Could not add PDF page");
            painter.end();
            output.cancelWriting();
            return false;
        }
        const QImage image = renderedPage(pages.at(i));
        if (image.isNull()) {
            *error = QStringLiteral("Could not read scanned page");
            painter.end();
            output.cancelWriting();
            return false;
        }
        const QRectF sheet(0, 0, writer.width(), writer.height());
        QSizeF scaled = image.size();
        scaled.scale(sheet.size(), Qt::KeepAspectRatio);
        const QRectF area(
            (sheet.width() - scaled.width()) / 2.0,
            (sheet.height() - scaled.height()) / 2.0,
            scaled.width(), scaled.height());
        painter.drawImage(area, image);
    }
    if (!painter.end() || !output.commit()) {
        *error = output.errorString().isEmpty()
            ? QStringLiteral("Could not finalize PDF")
            : output.errorString();
        return false;
    }
    return true;
}
bool writeJpeg(const MathomScanExport::Page &page,
               const QString &path, QString *error)
{
    const QImage image = renderedPage(page);
    if (image.isNull()) {
        *error = QStringLiteral("Could not read scanned page");
        return false;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)
        || !image.save(&output, "JPEG", 90) || !output.commit()) {
        *error = output.errorString().isEmpty()
            ? QStringLiteral("Could not save JPEG") : output.errorString();
        return false;
    }
    return true;
}
}

MathomScanExport::Output MathomScanExport::write(
    const QList<Page> &pages, const QString &format,
    const QString &paper, const QString &directory, const QDateTime &scanTime)
{
    Output result;
    if (pages.isEmpty()) {
        result.error = QStringLiteral("No scanned pages");
        return result;
    }
    QDir destination(directory);
    if (!destination.exists()) {
        result.error = QStringLiteral("The configured output directory does not exist");
        return result;
    }
    QLockFile lock(destination.filePath(QStringLiteral(".mathom-scan.lock")));
    if (!lock.tryLock(5000)) {
        result.error = QStringLiteral("Could not reserve unique document name");
        return result;
    }
    const QDateTime timestamp = scanTime.isValid()
        ? scanTime : QDateTime::currentDateTime();
    int number = nextNumber(destination, timestamp);
    QStringList targetPaths;
    for (;;) {
        targetPaths.clear();
        const QString prefix = documentPrefix(timestamp, number);
        if (format == QStringLiteral("JPEG")) {
            for (qsizetype i = 0; i < pages.size(); ++i) {
                const QString suffix = pages.size() == 1
                    ? QString()
                    : QStringLiteral("_%1").arg(i + 1, 2, 10, QLatin1Char('0'));
                targetPaths.append(destination.filePath(prefix + suffix
                                                        + QStringLiteral(".jpg")));
            }
        } else {
            targetPaths.append(destination.filePath(prefix + QStringLiteral(".pdf")));
        }
        bool occupied = false;
        for (const QString &path : targetPaths)
            occupied |= QFileInfo::exists(path);
        if (!occupied)
            break;
        ++number;
    }
    if (format == QStringLiteral("JPEG")) {
        for (qsizetype index = 0; index < pages.size(); ++index) {
            const QString path = targetPaths.at(index);
            if (!writeJpeg(pages.at(index), path, &result.error)) {
                // Keep the session sources intact for a retry. Remove outputs
                // produced by this attempt, never files belonging to others.
                for (const QString &created : result.files)
                    QFile::remove(created);
                result.files.clear();
                return result;
            }
            result.files.append(path);
        }
    } else if (writePdf(pages, paper, targetPaths.first(), &result.error)) {
        result.files = targetPaths;
    }
    return result;
}
