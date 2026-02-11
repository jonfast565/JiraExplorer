#include "ticketsmodel.h"

#include <QMap>

TicketsModel::TicketsModel(QObject* parent)
    : QStandardItemModel(parent)
{
    setHorizontalHeaderLabels({"Tickets"});
}

void TicketsModel::setTickets(const QList<JiraTicket>& tickets)
{
    clear();
    setHorizontalHeaderLabels({"Tickets"});

    // Group by sprint name
    QMap<QString, QList<JiraTicket>> groups;
    for (const auto& t : tickets)
        groups[t.sprint.isEmpty() ? QStringLiteral("No Sprint") : t.sprint].append(t);

    for (auto it = groups.begin(); it != groups.end(); ++it)
    {
        auto* groupItem = new QStandardItem(QStringLiteral("📁 %1").arg(it.key()));
        groupItem->setData("group", RoleType);
        groupItem->setData(it.key(), RoleSprint);
        groupItem->setEditable(false);

        for (const auto& t : it.value())
        {
            auto* ticket = new QStandardItem(QStringLiteral("%1  —  %2").arg(t.key, t.summary));
            ticket->setData("ticket", RoleType);
            ticket->setData(t.key, RoleKey);
            ticket->setData(t.status, RoleStatus);
            ticket->setData(t.summary, RoleSummary);
            ticket->setData(t.sprint, RoleSprint);
            ticket->setEditable(false);
            groupItem->appendRow(ticket);
        }

        appendRow(groupItem);
    }
}

QString TicketsModel::ticketKeyForIndex(const QModelIndex& index) const
{
    if (!index.isValid())
        return {};
    const auto type = data(index, RoleType).toString();
    if (type != "ticket")
        return {};
    return data(index, RoleKey).toString();
}
