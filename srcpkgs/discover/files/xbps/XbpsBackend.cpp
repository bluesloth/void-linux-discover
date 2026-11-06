/*
 *   SPDX-FileCopyrightText: 2026 Void Linux
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "XbpsBackend.h"
#include "XbpsResource.h"
#include "XbpsSourcesBackend.h"
#include "XbpsTransaction.h"
#include "XbpsUpdater.h"

#include <Transaction/Transaction.h>
#include <Transaction/TransactionModel.h>
#include <resources/SourcesModel.h>

#include <KLocalizedString>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTimer>
#include <QXmlStreamReader>

DISCOVER_BACKEND_PLUGIN(XbpsBackend)

namespace
{
/**
 * Splits a xbps pkgver ("name-version_revision") into its name and version.
 *
 * xbps package names may contain dashes and may even end with a digit
 * ("libui-devel-42"), so the only reliable separator is the *last* dash:
 * xbps version strings never contain a dash themselves.
 */
void splitPkgver(const QString &pkgver, QString *name, QString *version)
{
    const int dash = pkgver.lastIndexOf(QLatin1Char('-'));
    if (dash < 0) {
        *name = pkgver;
        version->clear();
        return;
    }
    *name = pkgver.left(dash);
    *version = pkgver.mid(dash + 1);
}

struct ParsedPackage {
    QString name;
    QString version;
    QString description;
    bool installed = false;
};

/**
 * Parses the output of "xbps-query -Rs <term>" and "xbps-query -s <term>".
 *
 * Every line looks like "[-] <pkgver>  <description>", where the marker
 * tells whether the package is installed on the system.
 */
QList<ParsedPackage> parsePackageList(const QByteArray &output)
{
    QList<ParsedPackage> packages;

    for (const QByteArray &line : output.split('\n')) {
        if (line.trimmed().isEmpty()) {
            continue;
        }

        const QByteArray marker = line.left(3);
        if (marker != "[-]" && marker != "[*]") {
            continue;
        }

        const QByteArray rest = line.mid(3).trimmed();
        if (rest.isEmpty()) {
            continue;
        }

        const int space = rest.indexOf(' ');
        const QString pkgver = QString::fromUtf8(space < 0 ? rest : rest.left(space));
        const QString description = space < 0 ? QString() : QString::fromUtf8(rest.mid(space + 1)).trimmed();

        ParsedPackage package;
        package.installed = marker == "[*]";
        package.description = description;
        splitPkgver(pkgver, &package.name, &package.version);
        if (package.name.isEmpty()) {
            continue;
        }
        packages.append(package);
    }

    return packages;
}

/**
 * Splits a line of xbps output at the spaces, dropping empty fields.
 */
QList<QByteArray> splitFields(const QByteArray &line)
{
    QList<QByteArray> fields;
    for (const QByteArray &field : line.split(' ')) {
        if (!field.isEmpty()) {
            fields.append(field);
        }
    }
    return fields;
}

/**
 * Runs a program and returns its standard output. Returns an empty
 * QByteArray when the program could not be started.
 */
QByteArray runAndCapture(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(5000)) {
        return {};
    }
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished(2000);
        return {};
    }
    return process.readAllStandardOutput();
}

/**
 * Reads the installed size (in bytes) of every installed package out of the
 * local xbps package database.
 *
 * The database is an XML plist at /var/db/xbps/pkgdb-<version>.plist whose
 * root <dict> maps each package name to a <dict> of its properties, with
 * "installed_size" an <integer> in raw bytes. Reading it once is far cheaper
 * than forking "xbps-query -p installed_size" for every installed package
 * (there are hundreds), and xbps-query only prints a human-readable size
 * ("48MB"), never the raw value.
 *
 * Returns an empty hash when the database is missing or unreadable: sizes are
 * cosmetic, and the UI degrades to "no size shown" rather than failing.
 */
QHash<QString, quint64> readInstalledSizes()
{
    QHash<QString, quint64> sizes;

    const QDir dir(QStringLiteral("/var/db/xbps"));
    const QStringList databases = dir.entryList({QStringLiteral("pkgdb-*.plist")}, QDir::Files, QDir::Name);
    if (databases.isEmpty()) {
        return sizes;
    }

    QFile file(dir.filePath(databases.last()));
    if (!file.open(QIODevice::ReadOnly)) {
        return sizes;
    }

    QXmlStreamReader xml(&file);

    // Depth of the <dict> elements: 1 is the top-level package map, 2 is a
    // package's own property dict (3+ would be a nested value we do not need).
    int dictDepth = 0;
    QString currentPackage;
    bool expectInstalledSize = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto element = xml.name();
            if (element == QLatin1String("dict")) {
                ++dictDepth;
            } else if (element == QLatin1String("key")) {
                const QString key = xml.readElementText();
                if (dictDepth == 1) {
                    currentPackage = key;
                } else if (dictDepth == 2 && key == QLatin1String("installed_size")) {
                    expectInstalledSize = true;
                }
            } else if (element == QLatin1String("integer") && expectInstalledSize && dictDepth == 2) {
                const quint64 value = xml.readElementText().toULongLong();
                if (!currentPackage.isEmpty() && value > 0) {
                    sizes.insert(currentPackage, value);
                }
                expectInstalledSize = false;
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("dict")) {
            if (dictDepth == 2) {
                currentPackage.clear();
                expectInstalledSize = false;
            }
            --dictDepth;
        }
    }

    return sizes;
}
}

XbpsBackend::XbpsBackend(QObject *parent)
    : AbstractResourcesBackend(parent)
    , m_updater(new XbpsUpdater(this))
    , m_updatesTimer(new QTimer(this))
{
    connect(m_updater, &XbpsUpdater::updatesCountChanged, this, [this](int) {
        Q_EMIT updatesCountChanged();
    });
    connect(TransactionModel::global(), &TransactionModel::lastTransactionFinished, this, [this]() {
        refreshInstalled();
        refreshUpdates();
    });

    m_updatesTimer->setSingleShot(true);
    connect(m_updatesTimer, &QTimer::timeout, this, &XbpsBackend::beginUpdateCheck);

    SourcesModel::global()->addSourcesBackend(new XbpsSourcesBackend(this));

    QTimer::singleShot(0, this, &XbpsBackend::refreshInstalled);
    QTimer::singleShot(0, this, &XbpsBackend::checkForUpdates);
}

XbpsBackend::~XbpsBackend() = default;

AbstractBackendUpdater *XbpsBackend::backendUpdater() const
{
    return m_updater;
}

XbpsResource *XbpsBackend::resourceByPackageName(const QString &name) const
{
    return m_resources.value(name, nullptr);
}

void XbpsBackend::addResource(XbpsResource *res)
{
    res->setParent(this);
    auto &slot = m_resources[res->packageName()];
    if (slot && slot != res) {
        Q_EMIT resourceRemoved(slot);
        delete slot;
    }
    slot = res;
}

void XbpsBackend::refreshInstalled()
{
    loadInstalled();
}

void XbpsBackend::refreshUpdates()
{
    loadUpdates();
}

void XbpsBackend::beginUpdateCheck()
{
    // Discover's update check is the only path that syncs the index.
    // refreshUpdates() deliberately does not: it also runs after every
    // finished transaction, and the one transaction that can change what is
    // available (`xbps-install -Syu`) has just synced the repositories
    // itself, so a second sync there would be pure noise.
    if (m_refreshProcess) {
        // A check is already in flight. Its completion drives the progress to
        // 100 and refreshes the list, so there is nothing to do here.
        return;
    }

    auto *process = new QProcess(this);
    m_refreshProcess = process;
    process->setProgram(QStringLiteral("pkexec"));
    process->setArguments({QStringLiteral("/usr/libexec/discover-xbps-refresh")});
    // The helper must never receive an answer to anything it asks: a
    // repository key import question has to read EOF and decline, exactly as
    // it does when the helper is run on its own.
    process->setStandardInputFile(QProcess::nullDevice());
    process->setProcessChannelMode(QProcess::SeparateChannels);

    // One completion path, whatever happened: the probe below must run
    // exactly once, because a second one would double the update list's
    // rebuild and re-emit contentsChanged() for no reason. The refresh is
    // best effort throughout: a refused authorisation, a missing helper or
    // an unreachable mirror all just leave the cached index in place, which
    // is a far better outcome than an update list that never loads.
    const auto finish = [this](const QString &logLine, bool isWarning) {
        if (!m_refreshProcess) {
            return;
        }
        m_refreshProcess->deleteLater();
        m_refreshProcess = nullptr;
        if (!logLine.isEmpty()) {
            if (isWarning) {
                qWarning().noquote() << logLine;
            } else {
                qInfo().noquote() << logLine;
            }
        }
        loadUpdates();
    };

    // QProcess does not emit finished() when the process never started.
    connect(process, &QProcess::errorOccurred, this, [process, finish](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        finish(QStringLiteral("xbps-backend: repodata not refreshed: %1").arg(process->errorString()), true);
    });

    connect(process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [process, finish](int exitCode, QProcess::ExitStatus exitStatus) {
                if (exitStatus != QProcess::NormalExit) {
                    finish(QStringLiteral("xbps-backend: repodata not refreshed, the helper was killed"), true);
                    return;
                }
                switch (exitCode) {
                case 0:
                    finish(QStringLiteral("xbps-backend: repodata refreshed"), false);
                    break;
                case 75:
                    // The helper's throttle (EX_TEMPFAIL): the index was
                    // synced recently, so there was nothing to do.
                    finish(QStringLiteral("xbps-backend: repodata refresh skipped, the index was synced recently"), false);
                    break;
                case 126:
                    finish(QStringLiteral("xbps-backend: repodata not refreshed, authentication was cancelled"), true);
                    break;
                case 127:
                    finish(QStringLiteral("xbps-backend: repodata not refreshed, pkexec could not run the helper"), true);
                    break;
                default: {
                    const QString stderr = QString::fromUtf8(process->readAllStandardError()).trimmed();
                    finish(QStringLiteral("xbps-backend: repodata not refreshed (helper exit %1)%2")
                               .arg(QString::number(exitCode), stderr.isEmpty() ? QString() : QStringLiteral(": ") + stderr),
                           true);
                    break;
                }
                }
            });

    // A polkit request that never resolves must not leave the update list
    // parked on "(Fetching...)" for the lifetime of the window.
    QTimer::singleShot(120000, process, [process]() {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
        }
    });

    process->start();
}

void XbpsBackend::loadInstalled()
{
    const QByteArray output = runAndCapture(QStringLiteral("xbps-query"), {QStringLiteral("-l")});
    if (output.isEmpty()) {
        return;
    }

    // "xbps-query -l" only prints human-readable sizes and offers no batch
    // property form, so one read of the package database is both cheaper and
    // more precise than 800+ forks. Without a size Discover animates its
    // "unknown size" spinner forever on the application page.
    const QHash<QString, quint64> sizes = readInstalledSizes();

    QHash<QString, XbpsResource *> found;

    for (const QByteArray &line : output.split('\n')) {
        if (line.trimmed().isEmpty()) {
            continue;
        }

        // "ii <pkgver>  <description>"; the first column is the install state.
        const QList<QByteArray> fields = splitFields(line);
        if (fields.size() < 2) {
            continue;
        }
        const QByteArray state = fields.at(0);
        if (state.size() != 2 || !state.startsWith('i')) {
            continue;
        }

        const QString pkgver = QString::fromUtf8(fields.at(1));
        if (pkgver.isEmpty()) {
            continue;
        }

        QString name;
        QString version;
        splitPkgver(pkgver, &name, &version);
        if (name.isEmpty()) {
            continue;
        }

        XbpsResource *res = m_resources.value(name, nullptr);
        if (!res) {
            res = new XbpsResource(this);
            res->setPackageName(name);
            addResource(res);
        }
        res->setInstalledVersion(version);
        const auto size = sizes.constFind(name);
        if (size != sizes.constEnd()) {
            res->setSize(*size);
        }
        if (res->summary().isEmpty()) {
            // The description is everything after the pkgver column.
            const int descStart = line.indexOf(fields.at(1)) + fields.at(1).size();
            res->setSummary(QString::fromUtf8(line.mid(descStart)).trimmed());
        }
        if (res->state() != AbstractResource::Upgradeable) {
            res->setState(AbstractResource::Installed);
        }
        found.insert(name, res);
    }

    // Packages that were removed from the system must lose their installed
    // version. Resources for packages that are only available in the
    // repositories (the result of a search) are not installed in the first
    // place and must be kept: dropping them would make the search results
    // disappear from Discover as soon as a transaction finished.
    for (const QString &name : std::as_const(m_installedPackages)) {
        if (found.contains(name)) {
            continue;
        }
        XbpsResource *res = m_resources.value(name, nullptr);
        if (!res) {
            continue;
        }
        res->setInstalledVersion(QString());
        res->setState(AbstractResource::None);
        res->setSize(0);
    }

    m_installedPackages.clear();
    for (auto it = found.constBegin(); it != found.constEnd(); ++it) {
        m_installedPackages.insert(it.key());
    }

    Q_EMIT contentsChanged();
}

void XbpsBackend::loadUpdates()
{
    // "xbps-install -nSu" is a dry run of a system update: it lists every
    // package that has a pending update without touching the system.
    //
    // A dry run answers from the repodata that is already on disk, and it
    // does NOT sync it. That is why checkForUpdates() syncs first, through
    // pkexec and /usr/libexec/discover-xbps-refresh: without that the update
    // list is built from whatever metadata happened to be cached, and nothing
    // else on a stock Void box refreshes it (there is no xbps cron job), so
    // Discover reported "up to date" indefinitely while the mirrors moved on.
    const QByteArray output = runAndCapture(QStringLiteral("xbps-install"), {QStringLiteral("-nSu")});

    QList<AbstractResource *> upgradeable;
    for (const QByteArray &line : output.split('\n')) {
        if (line.trimmed().isEmpty()) {
            continue;
        }

        // "<pkgver> update <arch> <repository> <installed size> <download size>"
        const QList<QByteArray> fields = splitFields(line);
        if (fields.size() < 2 || fields.at(1) != "update") {
            continue;
        }

        QString name;
        QString version;
        splitPkgver(QString::fromUtf8(fields.at(0)), &name, &version);
        if (name.isEmpty()) {
            continue;
        }

        XbpsResource *res = m_resources.value(name, nullptr);
        if (!res) {
            res = new XbpsResource(this);
            res->setPackageName(name);
            addResource(res);
        }
        res->setAvailableVersion(version);
        if (fields.size() > 5) {
            // The last column is the size of the package archive, which is
            // what has to be downloaded for the update.
            res->setSize(fields.at(5).toULongLong());
        } else if (fields.size() > 4) {
            res->setSize(fields.at(4).toULongLong());
        }
        res->setState(AbstractResource::Upgradeable);
        upgradeable.append(res);
    }

    // Packages that are not upgradeable anymore must lose their state.
    for (AbstractResource *resource : std::as_const(m_upgradeableResources)) {
        if (upgradeable.contains(resource)) {
            continue;
        }
        auto *res = qobject_cast<XbpsResource *>(resource);
        if (res) {
            res->setState(res->installedVersion().isEmpty() ? AbstractResource::None : AbstractResource::Installed);
        }
    }
    m_upgradeableResources = upgradeable;

    m_updater->setUpgradeableResources(upgradeable);

    // Discover's UI keeps the "Fetching..." state for as long as
    // fetchingUpdatesProgress() is below 100 (DiscoverDrawer.qml,
    // DiscoverWindow.qml). The probe above has now completed - whether it
    // found updates or not - so the progress must be driven to 100 and the
    // change signalled, or the Updates tab sticks on "(Fetching...)" for the
    // lifetime of the window. This is the whole of the "refreshing" state in
    // this backend: the metadata probe is a synchronous unprivileged dry run,
    // so there is no intermediate progress to report.
    m_fetchProgress = 100;
    Q_EMIT fetchingUpdatesProgressChanged();

    Q_EMIT contentsChanged();
}

ResultsStream *XbpsBackend::search(const AbstractResourcesBackend::Filters &filter)
{
    auto *stream = new ResultsStream(QStringLiteral("XbpsSearch"));

    if (filter.search.isEmpty()) {
        QTimer::singleShot(0, stream, &ResultsStream::finish);
        return stream;
    }

    // The repositories hold the packages that can be installed, the local
    // package database holds the ones that are already installed. Both are
    // queried, one after the other.
    auto *repoProcess = new QProcess(stream);
    repoProcess->setProgram(QStringLiteral("xbps-query"));
    repoProcess->setArguments({QStringLiteral("-Rs"), filter.search});

    auto *localProcess = new QProcess(stream);
    localProcess->setProgram(QStringLiteral("xbps-query"));
    localProcess->setArguments({QStringLiteral("-s"), filter.search});

    auto *packages = new QList<ParsedPackage>();

    connect(repoProcess, &QProcess::finished, this, [this, stream, filter, repoProcess, localProcess, packages]() {
        *packages += parsePackageList(repoProcess->readAllStandardOutput());
        localProcess->start();
    });

    connect(localProcess, &QProcess::finished, this, [this, stream, filter, localProcess, packages]() {
        *packages += parsePackageList(localProcess->readAllStandardOutput());

        QVector<StreamResult> results;
        for (const ParsedPackage &package : std::as_const(*packages)) {
            XbpsResource *res = m_resources.value(package.name, nullptr);
            if (!res) {
                res = new XbpsResource(this);
                res->setPackageName(package.name);
                res->setSummary(package.description);
                addResource(res);
            } else if (res->summary().isEmpty()) {
                res->setSummary(package.description);
            }
            res->setAvailableVersion(package.version);
            if (package.installed && res->state() == AbstractResource::None) {
                res->setState(AbstractResource::Installed);
            }
            results << StreamResult(res);
        }

        delete packages;

        filter.filterJustInCase(results);
        Q_EMIT stream->resourcesFound(results);
        stream->finish();
    });

    repoProcess->start();

    return stream;
}

Transaction *XbpsBackend::installApplication(AbstractResource *app)
{
    return installApplication(app, AddonList());
}

Transaction *XbpsBackend::installApplication(AbstractResource *app, const AddonList &)
{
    auto *res = qobject_cast<XbpsResource *>(app);
    if (!res) {
        return nullptr;
    }
    return new XbpsTransaction(this, res, Transaction::InstallRole, QStringLiteral("pkexec"),
                               {QStringLiteral("xbps-install"), QStringLiteral("-y"), QStringLiteral("--"), res->packageName()});
}

Transaction *XbpsBackend::removeApplication(AbstractResource *app)
{
    auto *res = qobject_cast<XbpsResource *>(app);
    if (!res) {
        return nullptr;
    }
    return new XbpsTransaction(this, res, Transaction::RemoveRole, QStringLiteral("pkexec"),
                               {QStringLiteral("xbps-remove"), QStringLiteral("-y"), QStringLiteral("--"), res->packageName()});
}

int XbpsBackend::fetchingUpdatesProgress() const
{
    return m_fetchProgress;
}

void XbpsBackend::checkForUpdates()
{
    // The whole check is: sync the index (beginUpdateCheck), then probe
    // (loadUpdates). Progress stays at 0 for the duration, which is what
    // Discover renders as "(Fetching...)", and loadUpdates drives it to 100
    // once the probe is done.
    m_fetchProgress = 0;
    Q_EMIT fetchingUpdatesProgressChanged();
    m_updatesTimer->start(0);
}

QString XbpsBackend::displayName() const
{
    return i18nc("Backend plugin display name", "XBPS");
}

int XbpsBackend::updatesCount() const
{
    return m_updater->updatesCount();
}

#include "XbpsBackend.moc"
#include "moc_XbpsBackend.cpp"
