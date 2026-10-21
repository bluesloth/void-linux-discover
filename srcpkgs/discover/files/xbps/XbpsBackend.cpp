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
#include <QProcess>
#include <QTimer>

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
    connect(m_updatesTimer, &QTimer::timeout, this, &XbpsBackend::refreshUpdates);

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

void XbpsBackend::loadInstalled()
{
    const QByteArray output = runAndCapture(QStringLiteral("xbps-query"), {QStringLiteral("-l")});
    if (output.isEmpty()) {
        return;
    }

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
    // A regular user cannot refresh the repository metadata (that lives in
    // /var/db/xbps), so the list is based on the metadata that is already
    // there. The privileged transaction of the updater syncs the
    // repositories before it upgrades anything.
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
    m_fetchProgress = 0;
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
