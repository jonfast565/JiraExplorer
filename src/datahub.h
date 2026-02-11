#pragma once

#include <QObject>
#include <QList>

#include "models.h"

class JiraClient;

class DataHub : public QObject
{
    Q_OBJECT
public:
    explicit DataHub(JiraClient* client, QObject* parent = nullptr);

    const QList<JiraTicket>& currentTickets() const { return m_currentTickets; }

    void refreshMyTickets();

signals:
    void ticketsUpdated(const QList<JiraTicket>& tickets);

private:
    JiraClient* m_client;
    QList<JiraTicket> m_currentTickets;
};
