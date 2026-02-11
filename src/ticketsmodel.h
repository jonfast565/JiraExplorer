#pragma once

#include <QStandardItemModel>
#include <QHash>

#include "models.h"

class TicketsModel : public QStandardItemModel
{
    Q_OBJECT
public:
    enum Roles {
        RoleType = Qt::UserRole + 1, // "group" or "ticket"
        RoleKey,
        RoleStatus,
        RoleSummary,
        RoleSprint
    };

    explicit TicketsModel(QObject* parent = nullptr);

    void setTickets(const QList<JiraTicket>& tickets);

    // Returns issueKey if index corresponds to a ticket.
    QString ticketKeyForIndex(const QModelIndex& index) const;
};
