// SPDX-License-Identifier: GPL-2.0-or-later
#include "../mathomscanexport.h"

#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

class MathomScanExportTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void pdfSequenceAndNoOverwrite()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString imagePath = temporary.path() + QStringLiteral("/source.png");
        QImage image(240, 320, QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(imagePath));

        const auto when = QDateTime::fromString(
            QStringLiteral("2026-10-15T09:15:42"), Qt::ISODate);
        const QList<MathomScanExport::Page> pages{
            {imagePath, 0, QStringLiteral("A4")},
            {imagePath, 1, QStringLiteral("A4")}
        };
        const auto first = MathomScanExport::write(
            pages, QStringLiteral("PDF"), QStringLiteral("A4"), temporary.path(), when);
        QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
        QCOMPARE(first.files.size(), 1);
        QVERIFY(first.files.first().endsWith(QStringLiteral("09-15_001.pdf")));

        const auto second = MathomScanExport::write(
            pages, QStringLiteral("PDF"), QStringLiteral("A4"), temporary.path(), when);
        QVERIFY2(second.error.isEmpty(), qPrintable(second.error));
        QCOMPARE(second.files.size(), 1);
        QVERIFY(second.files.first().endsWith(QStringLiteral("09-15_002.pdf")));
        QVERIFY(first.files != second.files);
        for (const QString &path : first.files + second.files) {
            QFile pdf(path);
            QVERIFY(pdf.open(QIODevice::ReadOnly));
            QCOMPARE(pdf.read(4), QByteArray("%PDF"));
        }
    }

    void jpegMultipageAndHourReset()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString imagePath = temporary.path() + QStringLiteral("/source.png");
        QImage image(180, 200, QImage::Format_RGB32);
        image.fill(Qt::black);
        QVERIFY(image.save(imagePath));

        const QList<MathomScanExport::Page> pages{
            {imagePath, 0, QStringLiteral("A5")},
            {imagePath, 1, QStringLiteral("A5")}
        };
        const QDateTime when = QDateTime::fromString(
            QStringLiteral("2026-10-15T10:05:32"), Qt::ISODate);
        const auto first = MathomScanExport::write(
            pages, QStringLiteral("JPEG"), QStringLiteral("A5"), temporary.path(), when);
        QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
        QCOMPARE(first.files.size(), 2);
        QVERIFY(first.files.at(0).endsWith(QStringLiteral("10-05_001_01.jpg")));
        QVERIFY(first.files.at(1).endsWith(QStringLiteral("10-05_001_02.jpg")));
        for (const QString &path : first.files)
            QVERIFY(!QImage(path).isNull());

        const auto second = MathomScanExport::write(
            pages, QStringLiteral("PDF"), QStringLiteral("A5"), temporary.path(), when);
        QVERIFY2(second.error.isEmpty(), qPrintable(second.error));
        QVERIFY(second.files.first().endsWith(QStringLiteral("10-05_002.pdf")));

        const QDateTime nextHour = QDateTime::fromString(
            QStringLiteral("2026-10-15T11:00:00"), Qt::ISODate);
        const auto third = MathomScanExport::write(
            pages, QStringLiteral("PDF"), QStringLiteral("A5"), temporary.path(), nextHour);
        QVERIFY2(third.error.isEmpty(), qPrintable(third.error));
        QVERIFY(third.files.first().endsWith(QStringLiteral("11-00_001.pdf")));
    }
};

QTEST_MAIN(MathomScanExportTest)
#include "mathomscanexporttest.moc"
