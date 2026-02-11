#include "datahub.h"
#include "jira_client.h"

DataHub::DataHub(JiraClient* client, QObject* parent)
    : QObject(parent), m_client(client)
{
    Q_ASSERT(m_client);

    connect(m_client, &JiraClient::myTicketsReady, this, [this](const QList<JiraTicket>& tickets) {
        m_currentTickets = tickets;
        emit ticketsUpdated(m_currentTickets);
    });
}

void DataHub::refreshMyTickets()
{
    if (m_client)
        m_client->getMyTickets();
}
