/*
 *   SPDX-FileCopyrightText: 2026 Void Linux
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef XBPSNOTIFIER_H
#define XBPSNOTIFIER_H

#include <BackendNotifierModule.h>

#include <QFileSystemWatcher>
#include <QTimer>

/**
 * Update notifier for the xbps backend.
 *
 * Discover's tray icon and "Updates available" notification do NOT come from
 * the backend: they come from the separate DiscoverNotifier process, which
 * loads BackendNotifierModule plugins out of
 * <plugindir>/discover-notifier/. Upstream only ships FlatpakNotifier, so a
 * stock Void box silently ignores pending xbps updates.
 *
 * This module closes that gap with the same shape as FlatpakNotifier:
 *
 *  - recheckSystemUpdateNeeded() runs `xbps-install -nSu` off the GUI thread
 *    and records whether anything is pending;
 *  - foundUpdates() is emitted only when that answer *changes*, which is what
 *    DiscoverNotifier needs to update the tray icon and post one notification;
 *  - a daily timer and a QFileSystemWatcher on the cached repodata files keep
 *    the answer reasonably fresh.
 *
 * The dry run is deliberately UNPRIVILEGED: it answers from the repodata that
 * is already cached, and it never syncs. Syncing needs root, and a desktop
 * login must not silently perform privileged network work; the Discover GUI
 * backend does sync on demand through the allow_active polkit helper
 * (/usr/libexec/discover-xbps-refresh), which also refreshes the cache this
 * notifier reads.
 */
class XbpsNotifier : public BackendNotifierModule
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DISCOVER_NOTIFIER_IID)
    Q_INTERFACES(BackendNotifierModule)
public:
    explicit XbpsNotifier(QObject *parent = nullptr);
    ~XbpsNotifier() override = default;

    void recheckSystemUpdateNeeded() override;
    bool hasUpdates() override;
    bool hasSecurityUpdates() override
    {
        // xbps carries no security metadata, so no update can be classified
        // as a security update. Reporting updates as upgrades-only is honest;
        // guessing would make the tray show the "security" icon for routine
        // package bumps.
        return false;
    }
    bool needsReboot() const override
    {
        return false;
    }

private:
    void refreshWatchedRepodata();

    QFileSystemWatcher *const m_watcher;
    QTimer m_dailyCheck;
    bool m_hasUpdates = false;
    bool m_checking = false;
};

#endif // XBPSNOTIFIER_H
