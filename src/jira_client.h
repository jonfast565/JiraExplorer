#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QList>
#include <QJsonObject>
#include <QJsonValue>

#include <functional>

#include "models.h"

class JiraClient : public QObject
{
    Q_OBJECT
public:
    explicit JiraClient(QObject* parent = nullptr);

    void configure(const QString& instanceUrl, const QString& username, const QString& apiToken);

    void getMyTickets();
    void getIssueFieldSnapshot(const QString& issueKey);
    void getIssueComments(const QString& issueKey);
    void getIssueHistory(const QString& issueKey);
    void getTransitions(const QString& issueKey);

    // Tray helpers (mirrors TrayViewModel in the WPF app)
    void getMostRecentActiveSprint();
    void getIssuesForSprint(int sprintId);

    void updateIssueDescription(const QString& issueKey, const QString& plainText);
    void addComment(const QString& issueKey, const QString& plainText);
    void updateComment(const QString& issueKey, const QString& commentId, const QString& plainText);

    void updateStoryPoints(const QString& issueKey, const std::optional<double>& storyPoints);
    void updateAssignee(const QString& issueKey, const QString& assigneeInput);
    void updateDueDate(const QString& issueKey, const std::optional<QDate>& dueDate);
    void updateSprint(const QString& issueKey, const std::optional<int>& sprintId);

    void transitionIssue(const QString& issueKey, const QString& transitionId);

signals:
    void myTicketsReady(const QList<JiraTicket>& tickets);
    void issueFieldSnapshotReady(const JiraIssueFieldSnapshot& snapshot);
    void issueCommentsReady(const QList<JiraComment>& comments);
    void issueHistoryReady(const QList<JiraHistoryEntry>& entries);
    void transitionsReady(const QList<JiraTransition>& transitions);

    void mostRecentActiveSprintReady(const std::optional<int>& sprintId,
                                    const QString& sprintName,
                                    const std::optional<QDateTime>& startDate);
    void sprintIssuesReady(const QList<JiraTicket>& tickets);

    void operationSucceeded(const QString& message);
    void operationFailed(const QString& context, const QString& error);
    void authenticationRequired(const QString& message);

private:
    QNetworkRequest makeRequest(const QUrl& url) const;
    QByteArray authHeader() const;
    bool isAuthError(const QNetworkReply* reply, QNetworkReply::NetworkError err) const;

    // Jira wants Atlassian Document Format (ADF) for description/comments.
    static QJsonObject buildAdfDocument(const QString& plainText);
    static QString adfToPlainText(const QJsonValue& adf);

    QString m_instanceUrl;
    QString m_username;
    QString m_apiToken;

    QString m_basePlatform;
    QString m_baseAgile;

    QNetworkAccessManager m_net;

    // Lazy field metadata (story points + sprint custom field ids)
    bool m_fieldMetadataLoaded{false};
    QString m_sprintFieldId;
    QString m_storyPointsFieldId;

    void ensureFieldMetadata(std::function<void()> cont);

    // Helpers used by multiple calls
    static QString parseSprintNameFromLegacyString(const QString& raw);
    static void extractSprint(const QJsonValue& element, std::optional<int>& id, QString& name);

    void resolveUserAccountId(const QString& query, std::function<void(const QString&)> cont);
    void getAllBoards(const QString& type, std::function<void(const QList<QJsonObject>&)> cont);
    void getBoardSprints(int boardId, const QString& state, std::function<void(const QList<QJsonObject>&)> cont);
};
