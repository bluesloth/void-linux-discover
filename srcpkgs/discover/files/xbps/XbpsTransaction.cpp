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

XbpsTransaction *XbpsTransaction::createSystemUpdate(QObject *parent)
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

int XbpsTransaction::errorCode() const
{
    return m_errorCode;
}

void XbpsTransaction::processOutput()
{
    if (!m_process) {
        return;
    }

    // The output of the helper is collected, not forwarded. Discover treats
    // every passive message as something the user has to read: the QML shows
    // them in a sheet titled "Update Issue" that offers a generic apology
    // ("There was an issue during the update or installation process"), so
    // relaying the helper's output as it arrives makes a perfectly successful
    // installation look like a failure. xbps writes its progress - download
    // bars, unpacking, the summary line - to stderr, so forwarding it
    // verbatim produced that sheet on every single transaction, whether it
    // succeeded or not.
    //
    // The upstream contract (see AlpineApkTransaction) is the opposite: the
    // helper's output is only reported when the transaction fails, as a
    // single message.
    m_output += QString::fromUtf8(m_process->readAllStandardOutput());
    m_output += QString::fromUtf8(m_process->readAllStandardError());
}

void XbpsTransaction::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_errorCode = (exitStatus == QProcess::NormalExit) ? exitCode : -1;

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        reportFailure();
        return;
    }

    if (auto *res = qobject_cast<XbpsResource *>(resource())) {
        if (role() == Transaction::InstallRole) {
            res->setState(AbstractResource::Installed);
        } else if (role() == Transaction::RemoveRole) {
            res->setState(AbstractResource::None);
        }
    }

    // A successful transaction says nothing: no passive message, so no
    // "Update Issue" sheet.
    setStatus(DoneStatus);
}

void XbpsTransaction::reportFailure()
{
    // The tail of the output carries the reason - xbps prints the error
    // after the progress - while the beginning is the download log. Only the
    // last lines are worth showing.
    QStringList lines = m_output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    while (lines.size() > 12) {
        lines.removeFirst();
    }
    const QString detail = lines.join(QLatin1Char('\n')).trimmed();

    if (!detail.isEmpty()) {
        Q_EMIT passiveMessage(i18n("Error:\n%1", detail));
    } else if (m_errorCode == 126) {
        // pkexec: the authorization dialogue was dismissed or denied.
        Q_EMIT passiveMessage(i18n("Error:\nAuthentication was cancelled."));
    } else if (m_errorCode == 127) {
        // pkexec: the command could not be run at all.
        Q_EMIT passiveMessage(i18n("Error:\nThe xbps helper could not be started."));
    } else {
        Q_EMIT passiveMessage(i18n("Error:\nThe xbps helper did not complete (exit code %1).", m_errorCode));
    }

    setStatus(DoneWithErrorStatus);
}

void XbpsTransaction::processError(QProcess::ProcessError error)
{
    qWarning() << "Xbps transaction failed to run:" << error;
    // A helper that could not even be started is the same condition pkexec
    // reports as 127.
    m_errorCode = (error == QProcess::FailedToStart) ? 127 : -1;
    reportFailure();
}

#include "moc_XbpsTransaction.cpp"
