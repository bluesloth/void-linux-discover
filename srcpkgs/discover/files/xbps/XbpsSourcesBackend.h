/*
 *   SPDX-FileCopyrightText: 2026 Void Linux
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef XBPPSSOURCESBACKEND_H
#define XBPPSSOURCESBACKEND_H

#include <resources/AbstractSourcesBackend.h>

#include <QStandardItemModel>

/**
 * Lists the xbps repositories that are configured on the system.
 *
 * Adding or removing a repository means writing to /usr/share/xbps.d, which
 * is not supported by this backend for the time being.
 */
class XbpsSourcesBackend : public AbstractSourcesBackend
{
    Q_OBJECT

public:
    explicit XbpsSourcesBackend(AbstractResourcesBackend *parent);

    QAbstractItemModel *sources() override;
    bool addSource(const QString &id) override;
    bool removeSource(const QString &id) override;
    QString idDescription() override;
    QVariantList actions() const override;
    bool supportsAdding() const override;

private:
    void loadSources();

    QStandardItemModel *m_sourcesModel = nullptr;
};

#endif // XBPPSSOURCESBACKEND_H
