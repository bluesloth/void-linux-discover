/*
 *   SPDX-FileCopyrightText: 2026 Void Linux
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "XbpsNotifier.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QProcess>
#include <QtConcurrentRun>

using namespace std::chrono_literals;

Q_LOGGING_CATEGORY(XBPS_NOTIFIER, "org.kde.discover.xbps.notifier")

/**
 * Asks xbps whether any update is pending.
 *
 * This is the same dry run the backend uses to build its update list, so the
 * notifier and the Updates page cannot disagree. It runs on a worker thread
 * (QtConcurrent), which is why it is a free function that touches no member
 * state: the notifier object lives on the GUI thread.
 *
 * Returns false when xbps cannot be run to completion, which is the safe
 * answer — a broken check must not keep a stale "updates available" badge on
 * screen.
 */
static bool hasPendingUpdates()
{
    QProcess process;
    process.setProgram(QStringLiteral("xbps-install"));
    process.setArguments({QStringLiteral("-nSu")});
    process.start();
    if (!process.waitForStarted(5000)) {
        qCWarning(XBPS_NOTIFIER) << "could not start xbps-install";
        return false;
    }
    if (!process.waitForFinished(60000)) {
        qCWarning(XBPS_NOTIFIER) << "xbps-install -nSu did not finish in time";
        process.kill();
        process.waitForFinished(2000);
        return false;
    }

    const QByteArray output = process.readAllStandardOutput();
    for (const QByteArray &line : output.split('\n')) {
        // "<pkgver> update <arch> <repository> <installed size> <download size>"
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() >= 2 && fields.at(1) == "update") {
            return true;
        }
    }
    return false;
}

XbpsNotifier::XbpsNotifier(QObject *parent)
    : BackendNotifierModule(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    // Refresh at least once a day, like FlatpakNotifier: the cached repodata
    // can change without anything on this process's radar.
    m_dailyCheck.setInterval(24h);
    connect(&m_dailyCheck, &QTimer::timeout, this, &XbpsNotifier::recheckSystemUpdateNeeded);
    m_dailyCheck.start();

    // The cached repodata is what the dry run answers from, so watching it is
    // the cheapest way to notice that a sync happened (Discover's helper, a
    // manual `xbps-install -S`, or the tray's own daily check).
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &XbpsNotifier::recheckSystemUpdateNeeded);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &XbpsNotifier::recheckSystemUpdateNeeded);
    refreshWatchedRepodata();
}

/**
 * Re-scans the per-repository cached repodata files and keeps the watcher in
 * sync with what is on disk.
 *
 * This has to run before AND after a check: a repository can be added or
 * removed at any time, and QFileSystemWatcher silently stops watching a file
 * that is replaced (which is exactly what a repodata sync does).
 */
void XbpsNotifier::refreshWatchedRepodata()
{
    const QDir dir(QStringLiteral("/var/db/xbps"));
    const QStringList repositories = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    QStringList wanted;
    for (const QString &repository : repositories) {
        const QString path = dir.filePath(repository + QStringLiteral("/x86_64-repodata"));
        if (QFileInfo::exists(path)) {
            wanted.append(path);
        }
    }

    const QStringList watched = m_watcher->files();
    for (const QString &path : watched) {
        if (!wanted.contains(path)) {
            m_watcher->removePath(path);
        }
    }
    for (const QString &path : wanted) {
        if (!watched.contains(path)) {
            m_watcher->addPath(path);
        }
    }
}

bool XbpsNotifier::hasUpdates()
{
    return m_hasUpdates;
}

void XbpsNotifier::recheckSystemUpdateNeeded()
{
    if (m_checking) {
        // DiscoverNotifier pokes us on start and on every DBus broadcast; a
        // queued second run would only repeat the same answer a minute later.
        return;
    }
    m_checking = true;
    refreshWatchedRepodata();

    auto *future = new QFutureWatcher<bool>(this);
    connect(future, &QFutureWatcher<bool>::finished, this, [this, future]() {
        const bool hasUpdates = future->result();
        future->deleteLater();
        m_checking = false;

        // A sync may have replaced a watched file while we were running; the
        // watcher would otherwise be left pointing at the deleted inode.
        refreshWatchedRepodata();

        if (hasUpdates == m_hasUpdates) {
            return;
        }
        m_hasUpdates = hasUpdates;
        Q_EMIT foundUpdates();
    });
    future->setFuture(QtConcurrent::run(&hasPendingUpdates));
}

#include "moc_XbpsNotifier.cpp"
