/*
 *   SPDX-FileCopyrightText: 2026 Void Linux
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef XBPSUPDATER_H
#define XBPSUPDATER_H

#include <resources/AbstractBackendUpdater.h>

#include <QDateTime>
#include <QSet>

class XbpsBackend;
class XbpsResource;

/**
 * Handles the pending system updates of the xbps backend.
 *
 * xbps upgrades the whole system at once ("xbps-install -Syu"), therefore the
 * updater runs a single transaction for all the packages that have a pending
 * update instead of one transaction per package.
 */
class XbpsUpdater : public AbstractBackendUpdater
{
    Q_OBJECT
    Q_PROPERTY(int updatesCount READ updatesCount NOTIFY updatesCountChanged)

public:
    explicit XbpsUpdater(XbpsBackend *backend);

    bool hasUpdates() const override;
    qreal progress() const override;
    void prepare() override;
    QList<AbstractResource *> toUpdate() const override;
    void addResources(const QList<AbstractResource *> &apps) override;
    void removeResources(const QList<AbstractResource *> &apps) override;
    QDateTime lastUpdate() const override;
    bool isCancelable() const override;
    bool isProgressing() const override;
    bool isMarked(AbstractResource *res) const override;
    double updateSize() const override;
    quint64 downloadSpeed() const override;
    bool isFetchingUpdates() const override;
    int updatesCount() const
    {
        return m_updatesCount;
    }

    /**
     * Replaces the list of packages that have a pending update.
     */
    void setUpgradeableResources(const QList<AbstractResource *> &resources);
    void setUpdatesCount(int count);

public Q_SLOTS:
    void start() override;
    void cancel() override;

Q_SIGNALS:
    void updatesCountChanged(int updatesCount);

private:
    void transactionStatusChanged();
    void setProgress(qreal progress);
    void setProgressing(bool progressing);

private:
    QSet<AbstractResource *> m_upgradeable;
    QSet<AbstractResource *> m_toUpdate;
    QDateTime m_lastUpdate;
    int m_updatesCount = 0;
    qreal m_progress = 0;
    bool m_progressing = false;
};

#endif // XBPSUPDATER_H
