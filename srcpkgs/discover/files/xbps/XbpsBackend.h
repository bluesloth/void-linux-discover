/*
 *   SPDX-FileCopyrightText: 2026 Void Linux
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef XBPSBACKEND_H
#define XBPSBACKEND_H

#include <resources/AbstractResourcesBackend.h>

#include <QHash>
#include <QSet>
#include <QTimer>

class XbpsResource;
class XbpsUpdater;

/**
 * Native xbps backend for Discover.
 *
 * Discover's PackageKit backend is not usable on Void (there is no
 * PackageKit daemon), so this backend talks to the xbps tools directly:
 *
 *  - search:              xbps-query -Rs <term>
 *  - installed packages:  xbps-query -l
 *  - repositories:        xbps-query -L
 *  - pending updates:     xbps-install -nSu
 *  - privileged work:     pkexec xbps-install / pkexec xbps-remove
 */
class XbpsBackend : public AbstractResourcesBackend
{
    Q_OBJECT

public:
    explicit XbpsBackend(QObject *parent = nullptr);
    ~XbpsBackend() override;

    QList<std::shared_ptr<Category>> category() const override
    {
        return {};
    }
    int updatesCount() const override;
    AbstractBackendUpdater *backendUpdater() const override;
    AbstractReviewsBackend *reviewsBackend() const override
    {
        return nullptr;
    }
    ResultsStream *search(const AbstractResourcesBackend::Filters &filter) override;
    bool isValid() const override
    {
        return true;
    }

    Transaction *installApplication(AbstractResource *app) override;
    Transaction *installApplication(AbstractResource *app, const AddonList &addons) override;
    Transaction *removeApplication(AbstractResource *app) override;

    int fetchingUpdatesProgress() const override;
    void checkForUpdates() override;
    QString displayName() const override;
    bool hasApplications() const override
    {
        return true;
    }

    XbpsResource *resourceByPackageName(const QString &name) const;
    void addResource(XbpsResource *res);

    /**
     * Re-reads the installed package list. Called after a transaction
     * finishes so that Discover reflects the new system state.
     */
    void refreshInstalled();

public Q_SLOTS:
    void refreshUpdates();

private:
    void loadInstalled();
    void loadUpdates();

    QHash<QString, XbpsResource *> m_resources;
    /**
     * The names of the packages that were installed at the time of the last
     * refresh. Only those may be dropped when they disappear from the
     * system; the resources of packages that are merely available in the
     * repositories belong to a search result.
     */
    QSet<QString> m_installedPackages;
    QList<AbstractResource *> m_upgradeableResources;
    XbpsUpdater *m_updater = nullptr;
    QTimer *m_updatesTimer = nullptr;
    int m_fetchProgress = 0;
};

#endif // XBPSBACKEND_H
