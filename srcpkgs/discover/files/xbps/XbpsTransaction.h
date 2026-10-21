/*
 *   SPDX-FileCopyrightText: 2026 Void Linux
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef XBPSTRANSACTION_H
#define XBPSTRANSACTION_H

#include <Transaction/Transaction.h>

#include <QProcess>
#include <QStringList>

class XbpsResource;
class AbstractResourcesBackend;

/**
 * Runs a privileged xbps operation through pkexec.
 *
 * The polkit actions used for the privileged helper are described by
 * org.kde.discover.xbps.policy, which requires admin authentication.
 *
 * Note that the first argument of the privileged program is deliberately
 * never a bare "-Sy": a policy shipped by other tools grants passwordless
 * execution for that argument, which would silently perform a privileged
 * repository sync without asking the user.
 */
class XbpsTransaction : public Transaction
{
    Q_OBJECT

public:
    XbpsTransaction(QObject *parent, XbpsResource *resource, Role role, const QString &program, const QStringList &arguments);
    ~XbpsTransaction() override;

    void cancel() override;

    /**
     * @returns a name that identifies the transaction. The system update is
     * not bound to a single resource and therefore needs its own name.
     */
    QString name() const override;
    QVariant icon() const override;

    /**
     * Creates a transaction that updates every package of the system.
     * The transaction is not bound to a single resource.
     */
    static Transaction *createSystemUpdate(QObject *parent);

private:
    void start();

private Q_SLOTS:
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void processError(QProcess::ProcessError error);
    void processOutput();

private:
    QString m_program;
    QStringList m_arguments;
    QProcess *m_process = nullptr;
};

#endif // XBPSTRANSACTION_H
