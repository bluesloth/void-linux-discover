/*
 *   SPDX-FileCopyrightText: 2026 Void Linux
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "XbpsResource.h"

XbpsResource::XbpsResource(AbstractResourcesBackend *parent)
    : AbstractResource(parent)
{
}

AbstractResource::State XbpsResource::state()
{
    return m_state;
}

void XbpsResource::setState(AbstractResource::State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged();
}

void XbpsResource::setUpgradeable(bool upgradeable)
{
    if (upgradeable) {
        setState(AbstractResource::Upgradeable);
    } else if (m_state == AbstractResource::Upgradeable) {
        setState(m_installedVersion.isEmpty() ? AbstractResource::None : AbstractResource::Installed);
    }
}

QString XbpsResource::sizeDescription()
{
    if (m_size == 0) {
        // Never report "0 B": Discover would interpret a non-empty
        // description with a zero size as "still resolving" and animate its
        // spinner forever.
        return QString();
    }
    return AbstractResource::sizeDescription();
}

QVariant XbpsResource::icon() const
{
    return QIcon::fromTheme(QStringLiteral("package-x-generic"));
}

#include "moc_XbpsResource.cpp"
