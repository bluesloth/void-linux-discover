/*
 *   SPDX-FileCopyrightText: 2026 Void Linux
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef XBPSRESOURCE_H
#define XBPSRESOURCE_H

#include <resources/AbstractResource.h>

#include <QIcon>
#include <QString>

/**
 * A single xbps package exposed to Discover.
 *
 * The resource is intentionally AppStream-free: Void does not ship a
 * per-package AppStream catalog for repository packages, so the fields below
 * are populated straight from the xbps query output.
 */
class XbpsResource : public AbstractResource
{
    Q_OBJECT

public:
    explicit XbpsResource(AbstractResourcesBackend *parent);

    QList<PackageState> addonsInformation() override
    {
        return {};
    }
    QString section() override
    {
        return QStringLiteral("System");
    }
    QString origin() const override
    {
        return QStringLiteral("xbps");
    }
    QString longDescription() override
    {
        return m_summary;
    }
    QString availableVersion() const override
    {
        return m_availableVersion;
    }
    QString installedVersion() const override
    {
        return m_installedVersion;
    }
    QJsonArray licenses() override
    {
        return {};
    }
    quint64 size() override
    {
        return m_size;
    }
    // Discover's application page renders a BusyIndicator while
    // "sizeDescription" is non-empty but "appSize" is not visible, and
    // "appSize" only becomes visible once "size" is non-zero (see
    // ApplicationPageFullComponent.qml). The inherited implementation formats
    // an unknown size as "0 B", which is never empty, so a package whose size
    // we have not resolved would spin forever. An empty string means "we do
    // not know yet, and no progress is pending".
    QString sizeDescription() override;
    QUrl homepage() override
    {
        return {};
    }
    QUrl helpURL() override
    {
        return {};
    }
    QUrl bugURL() override
    {
        return {};
    }
    QUrl donationURL() override
    {
        return {};
    }
    bool hasCategory(const QString &) const override
    {
        return false;
    }
    AbstractResource::State state() override;
    QVariant icon() const override;
    QString comment() override
    {
        return m_summary;
    }
    QString name() const override
    {
        return m_packageName;
    }
    QString packageName() const override
    {
        return m_packageName;
    }
    AbstractResource::Type type() const override
    {
        return System;
    }
    bool canExecute() const override
    {
        return false;
    }
    void invokeApplication() const override
    {
    }
    void fetchChangelog() override
    {
    }
    void fetchScreenshots() override
    {
    }
    QString appstreamId() const override
    {
        return m_packageName;
    }
    QUrl url() const override
    {
        return {};
    }
    QString author() const override
    {
        return {};
    }
    QString sourceIcon() const override
    {
        return QStringLiteral("application-x-executable");
    }
    QDate releaseDate() const override
    {
        return m_releaseDate;
    }

    void setPackageName(const QString &name)
    {
        m_packageName = name;
    }
    void setSummary(const QString &summary)
    {
        m_summary = summary;
    }
    void setAvailableVersion(const QString &version)
    {
        m_availableVersion = version;
    }
    void setInstalledVersion(const QString &version)
    {
        m_installedVersion = version;
    }
    void setSize(quint64 size)
    {
        if (m_size == size) {
            return;
        }
        m_size = size;
        Q_EMIT sizeChanged();
    }
    void setReleaseDate(const QDate &date)
    {
        m_releaseDate = date;
    }
    void setState(AbstractResource::State state);
    void setUpgradeable(bool upgradeable);

    QString summary() const
    {
        return m_summary;
    }

private:
    QString m_packageName;
    QString m_summary;
    QString m_availableVersion;
    QString m_installedVersion;
    quint64 m_size = 0;
    QDate m_releaseDate;
    AbstractResource::State m_state = AbstractResource::None;
};

#endif // XBPSRESOURCE_H
