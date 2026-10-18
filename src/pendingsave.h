/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MATHOM_PENDING_SAVE_H
#define MATHOM_PENDING_SAVE_H

#include <QObject>
#include <QTimer>
#include <functional>

#include "basket_export.h"

// Retry a failed save while its owner and current in-memory data still exist.
// The callback serializes the current model; no stale file bytes are queued.
class BASKET_EXPORT PendingSave : public QObject
{
public:
    PendingSave(QObject *parent, std::function<void()> retry);
    ~PendingSave() override;

    bool recordResult(bool success);
    bool isPending() const { return m_pending; }
    void cancel();

    static bool hasPendingSaves(const QObject *scope = nullptr);
    // Make one immediate attempt per pending owner, without processing events.
    static bool retryPendingSaves(const QObject *scope = nullptr);
    static void cancelPendingSaves(const QObject *scope);

private:
    bool belongsTo(const QObject *scope) const;

    std::function<void()> m_retry;
    QTimer m_timer;
    bool m_pending = false;
    int m_retryDelay = 1000;
};

#endif
