/*
 *   SPDX-FileCopyrightText: 2026 Void Linux
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "XbpsUpdater.h"
#include "XbpsBackend.h"
#include "XbpsResource.h"
#include "XbpsTransaction.h"

#include <Transaction/Transaction.h>

#include <KLocalizedString>
#include <QPointer>

XbpsUpdater::XbpsUpdater(XbpsBackend *backend)
    : AbstractBackendUpdater(backend)
{
}

bool XbpsUpdater::hasUpdates() const
{
    return m_updatesCount > 0;
}

qreal XbpsUpdater::progress() const
{
    return m_progress;
}

void XbpsUpdater::prepare()
{
    m_toUpdate = m_upgradeable;
}

QList<AbstractResource *> XbpsUpdater::toUpdate() const
{
    return m_toUpdate.values();
}

void XbpsUpdater::addResources(const QList<AbstractResource *> &apps)
{
    for (AbstractResource *res : apps) {
        if (m_upgradeable.contains(res)) {
            m_toUpdate.insert(res);
        }
    }
}

void XbpsUpdater::removeResources(const QList<AbstractResource *> &apps)
{
    for (AbstractResource *res : apps) {
        m_toUpdate.remove(res);
    }
}

QDateTime XbpsUpdater::lastUpdate() const
{
    return m_lastUpdate;
}

bool XbpsUpdater::isCancelable() const
{
    return false;
}

bool XbpsUpdater::isProgressing() const
{
    return m_progressing;
}

bool XbpsUpdater::isMarked(AbstractResource *res) const
{
    return m_toUpdate.contains(res);
}

double XbpsUpdater::updateSize() const
{
    double size = 0;
    for (AbstractResource *res : std::as_const(m_toUpdate)) {
        size += res->size();
    }
    return size;
}

quint64 XbpsUpdater::downloadSpeed() const
{
    return 0;
}

bool XbpsUpdater::isFetchingUpdates() const
{
    return false;
}

void XbpsUpdater::setUpgradeableResources(const QList<AbstractResource *> &resources)
{
    m_upgradeable = QSet<AbstractResource *>(resources.begin(), resources.end());
    m_toUpdate = m_upgradeable;
    m_updatesCount = resources.count();
    Q_EMIT updatesCountChanged(m_updatesCount);
}

void XbpsUpdater::setUpdatesCount(int count)
{
    if (m_updatesCount == count) {
        return;
    }
    m_updatesCount = count;
    Q_EMIT updatesCountChanged(m_updatesCount);
}

void XbpsUpdater::start()
{
    if (m_progressing) {
        return;
    }

    // A failed attempt must not poison the next one. AbstractBackendUpdater
    // only clears its error message when it is set to a different value, so a
    // stale "the update failed" left over from a dismissed polkit prompt or a
    // transient repository lock would otherwise stick to the updater for the
    // rest of the window's lifetime - Discover keeps showing its "there was an
    // issue" dialogue even after a retry has succeeded underneath (seen live
    // on the box: first attempt errored, retry installed the package, the
    // dialogue still claimed a failure).
    setErrorMessage(QString());

    setProgress(0);
    setProgressing(true);

    auto *transaction = XbpsTransaction::createSystemUpdate(this);
    if (!transaction) {
        setProgressing(false);
        return;
    }

    connect(transaction, &Transaction::statusChanged, this, &XbpsUpdater::transactionStatusChanged);
    connect(transaction, &Transaction::passiveMessage, this, &AbstractBackendUpdater::passiveMessage);
}

void XbpsUpdater::cancel()
{
    setProgressing(false);
}

void XbpsUpdater::transactionStatusChanged()
{
    auto *transaction = qobject_cast<XbpsTransaction *>(sender());
    if (!transaction) {
        return;
    }

    switch (transaction->status()) {
    case Transaction::SetupStatus:
    case Transaction::QueuedStatus:
    case Transaction::DownloadingStatus:
    case Transaction::CommittingStatus:
        break;
    case Transaction::DoneStatus:
        setProgress(100);
        m_lastUpdate = QDateTime::currentDateTime();
        // Belt and braces with the reset in start(): a transaction that has
        // succeeded cannot leave an error behind for the next one to inherit.
        setErrorMessage(QString());
        setProgressing(false);
        break;
    case Transaction::DoneWithErrorStatus:
        // pkexec distinguishes its own failures from the helper's: 126 means
        // the authorization dialogue was dismissed or the request was denied,
        // 127 means the command could not be run at all. Neither of those says
        // anything about the packages, so reporting them as a failed update
        // sends the user looking in the wrong place - name the actual
        // condition instead. Any other non-zero code is the package manager's
        // own verdict and does mean the transaction failed.
        switch (transaction->errorCode()) {
        case 126:
            setErrorMessage(i18n("Authentication was cancelled."));
            break;
        case 127:
            setErrorMessage(i18n("The xbps installer could not be started."));
            break;
        default:
            setErrorMessage(i18n("The system update failed."));
            break;
        }
        setProgressing(false);
        break;
    case Transaction::CancelledStatus:
        setProgressing(false);
        break;
    }
}

void XbpsUpdater::setProgress(qreal progress)
{
    if (qFuzzyCompare(m_progress, progress)) {
        return;
    }
    m_progress = progress;
    Q_EMIT progressChanged(m_progress);
}

void XbpsUpdater::setProgressing(bool progressing)
{
    if (m_progressing == progressing) {
        return;
    }
    m_progressing = progressing;
    Q_EMIT progressingChanged(m_progressing);
}

#include "moc_XbpsUpdater.cpp"
