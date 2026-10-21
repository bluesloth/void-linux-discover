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

QVariant XbpsResource::icon() const
{
    return QIcon::fromTheme(QStringLiteral("package-x-generic"));
}

#include "moc_XbpsResource.cpp"
