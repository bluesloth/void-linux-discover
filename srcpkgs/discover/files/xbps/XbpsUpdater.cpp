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
    auto *transaction = qobject_cast<Transaction *>(sender());
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
        setProgressing(false);
        break;
    case Transaction::DoneWithErrorStatus:
        setErrorMessage(i18n("The system update failed."));
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
