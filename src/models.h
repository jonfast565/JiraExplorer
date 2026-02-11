#pragma once

#include <QString>
#include <QDateTime>
#include <QDate>
#include <QList>
#include <optional>

struct JiraTicket
{
    QString key;
    QString summary;
    QString status;
    QString sprint;
};

struct JiraComment
{
    QString id;
    QString author;
    QDateTime created;
    QString editableBody; // plain text (one paragraph per line)
};

struct JiraHistoryEntry
{
    QString author;
    QDateTime when;
    QString field;
    QString fromValue;
    QString toValue;
};

struct JiraIssueFieldSnapshot
{
    QString description;
    std::optional<double> storyPoints;
    QString assigneeDisplayName;
    QString assigneeAccountId;
    QString sprintName;
    std::optional<int> sprintId;
    std::optional<QDate> dueDate;
};

struct JiraTransition
{
    QString id;
    QString name;
};
