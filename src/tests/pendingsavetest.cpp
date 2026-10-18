/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QTemporaryDir>

#include "../common.h"
#include "../pendingsave.h"

namespace
{
QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

class PendingSaveTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void failedWriteReturnsWithoutDispatchingEvents();
    void retryUsesCurrentDataAndPath();
    void failedRetriesAreSpaced();
    void successfulWriteCancelsOldRetry();
    void ownerDestructionCancelsRetry();
    void retryIsScopedToItsOwner();
    void cleanup() { QVERIFY(!PendingSave::hasPendingSaves()); }
};

void PendingSaveTest::failedWriteReturnsWithoutDispatchingEvents()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = directory.filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(target));
    QVERIFY(FileStorage::safelySaveToFile(target + QStringLiteral("/existing"), QByteArray("original")));

    bool eventDispatched = false;
    QObject eventContext;
    QTimer::singleShot(0, &eventContext, [&eventDispatched] { eventDispatched = true; });
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(!FileStorage::safelySaveToFile(target, QByteArray("replacement")));
    QVERIFY(elapsed.elapsed() < 1000);
    QVERIFY(!eventDispatched);
    QCOMPARE(readFile(target + QStringLiteral("/existing")), QByteArray("original"));
    QTRY_VERIFY(eventDispatched);
}

void PendingSaveTest::retryUsesCurrentDataAndPath()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString originalPath = directory.filePath(QStringLiteral("missing/note.txt"));
    QString path = originalPath;
    QByteArray data("first edit");
    int attempts = 0;
    QObject owner;
    PendingSave pending(&owner, [&] {
        ++attempts;
        pending.recordResult(FileStorage::safelySaveToFile(path, data));
    });

    QVERIFY(!pending.recordResult(FileStorage::safelySaveToFile(path, data)));
    QVERIFY(PendingSave::hasPendingSaves(&owner));
    QTest::qWait(150);
    QCOMPARE(attempts, 0);

    // A rename/move or a newer edit must supersede the failed write.
    path = directory.filePath(QStringLiteral("current.txt"));
    data = QByteArray("latest edit\0with binary bytes", 28);
    QTRY_VERIFY_WITH_TIMEOUT(!pending.isPending(), 2500);
    QCOMPARE(attempts, 1);
    QCOMPARE(readFile(path), data);
    QVERIFY(!QFile::exists(originalPath));
}

void PendingSaveTest::failedRetriesAreSpaced()
{
    QObject owner;
    int attempts = 0;
    PendingSave pending(&owner, [&] {
        ++attempts;
        pending.recordResult(false);
    });
    QVERIFY(!pending.recordResult(false));
    QTRY_COMPARE_WITH_TIMEOUT(attempts, 1, 2500);
    QTest::qWait(300);
    QCOMPARE(attempts, 1);
    QTRY_COMPARE_WITH_TIMEOUT(attempts, 2, 3000);
    QVERIFY(pending.isPending());
    pending.cancel();
}

void PendingSaveTest::successfulWriteCancelsOldRetry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString parent = directory.filePath(QStringLiteral("restored"));
    const QString path = parent + QStringLiteral("/note.txt");
    int retries = 0;
    QObject owner;
    PendingSave pending(&owner, [&] { ++retries; });
    QVERIFY(!pending.recordResult(FileStorage::safelySaveToFile(path, QByteArray("old"))));
    QVERIFY(QDir().mkpath(parent));
    QVERIFY(pending.recordResult(FileStorage::safelySaveToFile(path, QByteArray("new"))));
    QTest::qWait(1150);
    QCOMPARE(retries, 0);
    QCOMPARE(readFile(path), QByteArray("new"));
}

void PendingSaveTest::ownerDestructionCancelsRetry()
{
    auto *owner = new QObject;
    int retries = 0;
    QPointer<PendingSave> pending = new PendingSave(owner, [&] { ++retries; });
    QVERIFY(!pending->recordResult(false));
    delete owner;
    QVERIFY(pending.isNull());
    QVERIFY(!PendingSave::hasPendingSaves());
    QTest::qWait(1150);
    QCOMPARE(retries, 0);
}

void PendingSaveTest::retryIsScopedToItsOwner()
{
    QObject firstOwner;
    QObject secondOwner;
    int firstAttempts = 0;
    int secondAttempts = 0;
    PendingSave first(&firstOwner, [&] { ++firstAttempts; first.recordResult(true); });
    PendingSave second(&secondOwner, [&] { ++secondAttempts; second.recordResult(false); });
    first.recordResult(false);
    second.recordResult(false);
    QVERIFY(PendingSave::retryPendingSaves(&firstOwner));
    QCOMPARE(firstAttempts, 1);
    QCOMPARE(secondAttempts, 0);
    QVERIFY(!PendingSave::hasPendingSaves(&firstOwner));
    QVERIFY(PendingSave::hasPendingSaves(&secondOwner));
    QVERIFY(!PendingSave::retryPendingSaves());
    QCOMPARE(secondAttempts, 1);
    second.cancel();
}

QTEST_GUILESS_MAIN(PendingSaveTest)
#include "pendingsavetest.moc"
