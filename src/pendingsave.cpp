/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pendingsave.h"

#include <QPointer>
#include <QSet>
#include <algorithm>
#include <utility>

#include "bnpview.h"
#include "global.h"

namespace
{
QSet<PendingSave *> &pendingSaves()
{
    static QSet<PendingSave *> saves;
    return saves;
}

void refreshSavedStatus()
{
    if (Global::bnpView)
        Global::bnpView->setUnsavedStatus(false);
}
}

PendingSave::PendingSave(QObject *parent, std::function<void()> retry)
    : QObject(parent)
    , m_retry(std::move(retry))
    , m_timer(this)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] { m_retry(); });
}

PendingSave::~PendingSave()
{
    cancel();
}

bool PendingSave::recordResult(bool success)
{
    if (success) {
        cancel();
        return true;
    }

    const bool wasPending = m_pending;
    m_pending = true;
    pendingSaves().insert(this);
    if (!m_timer.isActive()) {
        m_timer.start(m_retryDelay);
        m_retryDelay = std::min(60000, m_retryDelay * 2);
    }
    if (!wasPending)
        refreshSavedStatus();
    return false;
}

void PendingSave::cancel()
{
    m_timer.stop();
    m_retryDelay = 1000;
    if (!m_pending)
        return;
    m_pending = false;
    pendingSaves().remove(this);
    refreshSavedStatus();
}

bool PendingSave::belongsTo(const QObject *scope) const
{
    if (!scope)
        return true;
    for (const QObject *object = this; object; object = object->parent()) {
        if (object == scope)
            return true;
    }
    return false;
}

bool PendingSave::hasPendingSaves(const QObject *scope)
{
    for (const PendingSave *save : pendingSaves()) {
        if (save->belongsTo(scope))
            return true;
    }
    return false;
}

bool PendingSave::retryPendingSaves(const QObject *scope)
{
    QList<QPointer<PendingSave>> saves;
    for (PendingSave *save : pendingSaves()) {
        if (save->belongsTo(scope))
            saves.append(save);
    }
    for (const QPointer<PendingSave> &save : std::as_const(saves)) {
        if (save && save->m_pending)
            save->m_retry();
    }
    return !hasPendingSaves(scope);
}

void PendingSave::cancelPendingSaves(const QObject *scope)
{
    const auto saves = pendingSaves().values();
    for (PendingSave *save : saves) {
        if (save->belongsTo(scope))
            save->cancel();
    }
}
