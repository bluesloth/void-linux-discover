/*
 *   SPDX-FileCopyrightText: 2026 Void Linux
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "XbpsSourcesBackend.h"

#include <KLocalizedString>
#include <QProcess>
#include <QStandardItemModel>

namespace
{
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
}

XbpsSourcesBackend::XbpsSourcesBackend(AbstractResourcesBackend *parent)
    : AbstractSourcesBackend(parent)
    , m_sourcesModel(new QStandardItemModel(this))
{
    m_sourcesModel->setHorizontalHeaderLabels({i18n("Repository")});

    loadSources();
}

QAbstractItemModel *XbpsSourcesBackend::sources()
{
    return m_sourcesModel;
}

bool XbpsSourcesBackend::addSource(const QString &)
{
    // Adding a repository requires writing to /usr/share/xbps.d, which is
    // not supported.
    return false;
}

bool XbpsSourcesBackend::removeSource(const QString &)
{
    // Removing a repository requires writing to /usr/share/xbps.d, which is
    // not supported.
    return false;
}

QString XbpsSourcesBackend::idDescription()
{
    return i18n("Repository");
}

QVariantList XbpsSourcesBackend::actions() const
{
    return {};
}

bool XbpsSourcesBackend::supportsAdding() const
{
    return false;
}

void XbpsSourcesBackend::loadSources()
{
    m_sourcesModel->removeRows(0, m_sourcesModel->rowCount());

    QProcess process;
    process.start(QStringLiteral("xbps-query"), {QStringLiteral("-L")});
    if (!process.waitForFinished(10000)) {
        process.kill();
        return;
    }

    const QList<QByteArray> lines = process.readAllStandardOutput().split('\n');
    for (const QByteArray &line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }

        // "<number of packages> <url> (<signature description>)"
        const QList<QByteArray> fields = splitFields(line);
        if (fields.size() < 2) {
            continue;
        }

        const QString packages = QString::fromUtf8(fields.at(0));
        const QString url = QString::fromUtf8(fields.at(1));

        auto *item = new QStandardItem(url);
        item->setData(url, IdRole);
        item->setData(url, DisambiguatedIdRole);
        item->setData(i18n("%n package(s)", packages.toInt()), Qt::ToolTipRole);
        item->setEditable(false);
        m_sourcesModel->appendRow(item);
    }
}

#include "moc_XbpsSourcesBackend.cpp"
