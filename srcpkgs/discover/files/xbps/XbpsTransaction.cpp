/*
 *   SPDX-FileCopyrightText: 2026 Void Linux
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "XbpsTransaction.h"
#include "XbpsResource.h"

#include <Transaction/TransactionModel.h>
#include <resources/AbstractResourcesBackend.h>

#include <KLocalizedString>
#include <QDebug>
#include <QIcon>
#include <QTimer>

XbpsTransaction::XbpsTransaction(QObject *parent, XbpsResource *resource, Role role, const QString &program, const QStringList &arguments)
    : Transaction(parent, resource, role, AddonList())
    , m_program(program)
    , m_arguments(arguments)
{
    // The privileged helper is not started from the constructor but from the
    // event loop: the transaction has to be registered with the transaction
    // model before the helper can finish. A helper that fails right away
    // would otherwise try to remove the transaction from the model before it
    // was ever added to it, which leaves a dangling pointer behind.
    QTimer::singleShot(0, this, &XbpsTransaction::start);
}

Transaction *XbpsTransaction::createSystemUpdate(QObject *parent)
{
    // "-Syu" syncs the repositories and upgrades every package. The first
    // argument is not a bare "-Sy", so the polkit action that asks for the
    // administrator password is used.
    return new XbpsTransaction(parent, nullptr, Transaction::InstallRole, QStringLiteral("pkexec"),
                               {QStringLiteral("xbps-install"), QStringLiteral("-Syu")});
}

XbpsTransaction::~XbpsTransaction()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

void XbpsTransaction::start()
{
    if (status() >= DoneStatus) {
        // The transaction was cancelled before the helper could start.
        return;
    }

    if (m_program.isEmpty()) {
        setStatus(DoneWithErrorStatus);
        return;
    }

    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, &XbpsTransaction::processFinished);
    connect(m_process, &QProcess::errorOccurred, this, &XbpsTransaction::processError);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &XbpsTransaction::processOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &XbpsTransaction::processOutput);

    // Registering the transaction before the helper is started makes sure
    // that a helper which fails immediately is removed from the model again.
    TransactionModel::global()->addTransaction(this);

    setStatus(CommittingStatus);
    m_process->setProgram(m_program);
    m_process->setArguments(m_arguments);
    m_process->start();
}

void XbpsTransaction::cancel()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
    setStatus(CancelledStatus);
}

QString XbpsTransaction::name() const
{
    // The system update is not bound to a single resource.
    if (resource()) {
        return resource()->name();
    }
    return i18n("System update");
}

QVariant XbpsTransaction::icon() const
{
    if (resource()) {
        return resource()->icon();
    }
    return QIcon::fromTheme(QStringLiteral("system-upgrade"));
}

void XbpsTransaction::processOutput()
{
    if (!m_process) {
        return;
    }

    const QByteArray out = m_process->readAllStandardOutput();
    if (!out.isEmpty()) {
        const QString message = QString::fromUtf8(out).trimmed();
        if (!message.isEmpty()) {
            Q_EMIT passiveMessage(message);
        }
    }

    const QByteArray err = m_process->readAllStandardError();
    if (!err.isEmpty()) {
        const QString message = QString::fromUtf8(err).trimmed();
        if (!message.isEmpty()) {
            Q_EMIT passiveMessage(message);
        }
    }
}

void XbpsTransaction::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        setStatus(DoneWithErrorStatus);
        return;
    }

    if (auto *res = qobject_cast<XbpsResource *>(resource())) {
        if (role() == Transaction::InstallRole) {
            res->setState(AbstractResource::Installed);
        } else if (role() == Transaction::RemoveRole) {
            res->setState(AbstractResource::None);
        }
    }

    setStatus(DoneStatus);
}

void XbpsTransaction::processError(QProcess::ProcessError error)
{
    qWarning() << "Xbps transaction failed to run:" << error;
    setStatus(DoneWithErrorStatus);
}

#include "moc_XbpsTransaction.cpp"
