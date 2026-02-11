#include "jira_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QUrlQuery>

#include <algorithm>

static QString toIsoDate(const std::optional<QDate>& d)
{
    if (!d.has_value()) return QString();
    return d->toString("yyyy-MM-dd");
}

static QString trimTrailingSlash(QString s)
{
    while (s.endsWith('/')) s.chop(1);
    return s;
}

static QString enc(const QString& s)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(s));
}

JiraClient::JiraClient(QObject* parent)
    : QObject(parent)
{
}

void JiraClient::configure(const QString& instanceUrl, const QString& username, const QString& apiToken)
{
    m_instanceUrl = trimTrailingSlash(instanceUrl);
    m_username = username;
    m_apiToken = apiToken;
    m_basePlatform = m_instanceUrl + "/rest/api/3";
    m_baseAgile = m_instanceUrl + "/rest/agile/1.0";
    m_fieldMetadataLoaded = false;
    m_sprintFieldId.clear();
    m_storyPointsFieldId.clear();
}

QByteArray JiraClient::authHeader() const
{
    const QByteArray userPass = (m_username + ":" + m_apiToken).toUtf8();
    return "Basic " + userPass.toBase64();
}

QNetworkRequest JiraClient::makeRequest(const QUrl& url) const
{
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", authHeader());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    return req;
}

bool JiraClient::isAuthError(const QNetworkReply* reply, QNetworkReply::NetworkError err) const
{
    if (!reply) return false;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return err == QNetworkReply::AuthenticationRequiredError
        || err == QNetworkReply::ContentAccessDenied
        || status == 401
        || status == 403;
}

void JiraClient::ensureFieldMetadata(std::function<void()> cont)
{
    if (m_fieldMetadataLoaded)
    {
        cont();
        return;
    }

    const QUrl url(m_basePlatform + "/field");
    QNetworkReply* reply = m_net.get(makeRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, cont]() {
        const auto guard = QPointer<QNetworkReply>(reply);
        const auto data = reply->readAll();
        const auto err = reply->error();
        const auto errStr = reply->errorString();
        reply->deleteLater();

        if (err != QNetworkReply::NoError)
        {
            if (isAuthError(reply, err))
            {
                emit authenticationRequired("Jira authentication failed while loading field metadata. Please configure your API token.");
                return;
            }
            emit operationFailed("Load field metadata", errStr);
            cont();
            return;
        }

        const auto doc = QJsonDocument::fromJson(data);
        if (!doc.isArray())
        {
            emit operationFailed("Load field metadata", "Unexpected JSON (expected array)");
            cont();
            return;
        }

        const auto arr = doc.array();
        for (const auto& v : arr)
        {
            const auto o = v.toObject();
            const auto name = o.value("name").toString();
            const auto id = o.value("id").toString();
            if (name.compare("Sprint", Qt::CaseInsensitive) == 0)
                m_sprintFieldId = id;
            if (name.compare("Story Points", Qt::CaseInsensitive) == 0)
                m_storyPointsFieldId = id;
        }

        m_fieldMetadataLoaded = true;
        cont();
    });
}

void JiraClient::getMyTickets()
{
    // Mirrors C# MyTicketJql and pagination via nextPageToken.
    const QString jql = "assignee = currentUser() and status NOT IN (Closed, Done) ORDER BY updated DESC";
    const int maxResults = 1000;

    ensureFieldMetadata([this, jql, maxResults]() {
        QList<JiraTicket> all;

        // We implement pagination by recursively requesting with nextPageToken.
        std::function<void(const QString&)> fetchPage;
        fetchPage = [this, &all, jql, maxResults, &fetchPage](const QString& nextPageToken) {
            QUrl url(m_basePlatform + "/search/jql");

            QJsonObject body;
            body.insert("jql", jql);
            body.insert("maxResults", maxResults);

            QJsonArray fields;
            fields.append("key");
            fields.append("summary");
            fields.append("status");
            fields.append("updated");
            if (!m_sprintFieldId.isEmpty())
                fields.append(m_sprintFieldId);
            body.insert("fields", fields);

            if (!nextPageToken.isEmpty())
                body.insert("nextPageToken", nextPageToken);

            const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
            QNetworkReply* reply = m_net.post(makeRequest(url), payload);

            QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, &all, fetchPage]() mutable {
                const auto data = reply->readAll();
                const auto err = reply->error();
                const auto errStr = reply->errorString();
                reply->deleteLater();

                if (err != QNetworkReply::NoError)
                {
                    if (isAuthError(reply, err))
                    {
                        emit authenticationRequired("Jira authentication failed while loading tickets. Please configure your API token.");
                        emit myTicketsReady({});
                        return;
                    }
                    emit operationFailed("GetMyTickets", errStr);
                    emit myTicketsReady(all);
                    return;
                }

                const auto doc = QJsonDocument::fromJson(data);
                if (!doc.isObject())
                {
                    emit operationFailed("GetMyTickets", "Unexpected JSON (expected object)");
                    emit myTicketsReady(all);
                    return;
                }

                const auto root = doc.object();
                const auto issues = root.value("issues").toArray();
                for (const auto& v : issues)
                {
                    const auto issue = v.toObject();
                    const auto key = issue.value("key").toString();
                    const auto fields = issue.value("fields").toObject();
                    const auto summary = fields.value("summary").toString();

                    QString status;
                    const auto statusObj = fields.value("status").toObject();
                    status = statusObj.value("name").toString();

                    QString sprintName = "No Sprint";
                    if (!m_sprintFieldId.isEmpty() && fields.contains(m_sprintFieldId))
                    {
                        const auto sprintVal = fields.value(m_sprintFieldId);
                        if (sprintVal.isArray() && !sprintVal.toArray().isEmpty())
                        {
                            const auto first = sprintVal.toArray().first();
                            if (first.isObject())
                                sprintName = first.toObject().value("name").toString("Sprint");
                            else if (first.isString())
                                sprintName = first.toString();
                        }
                        else if (sprintVal.isObject())
                        {
                            sprintName = sprintVal.toObject().value("name").toString("Sprint");
                        }
                        else if (sprintVal.isString())
                        {
                            sprintName = sprintVal.toString();
                        }
                    }

                    all.append(JiraTicket{key, summary, status, sprintName});
                }

                const auto token = root.value("nextPageToken").toString();
                if (!token.isEmpty())
                {
                    fetchPage(token);
                    return;
                }

                emit myTicketsReady(all);
            });
        };

        fetchPage(QString());
    });
}

void JiraClient::getIssueFieldSnapshot(const QString& issueKey)
{
    if (issueKey.trimmed().isEmpty())
    {
        emit issueFieldSnapshotReady(JiraIssueFieldSnapshot{});
        return;
    }

    ensureFieldMetadata([this, issueKey]() {
        // fields: description, assignee, duedate, story points (custom), sprint (custom)
        QStringList fields;
        fields << "description" << "assignee" << "duedate";
        if (!m_storyPointsFieldId.isEmpty()) fields << m_storyPointsFieldId;
        if (!m_sprintFieldId.isEmpty()) fields << m_sprintFieldId;

        const QString fieldsParam = fields.join(',');
        QUrl url(m_basePlatform + "/issue/" + enc(issueKey));
        QUrlQuery q;
        q.addQueryItem("fields", fieldsParam);
        url.setQuery(q);

        QNetworkReply* reply = m_net.get(makeRequest(url));
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const auto data = reply->readAll();
            const auto err = reply->error();
            const auto errStr = reply->errorString();
            reply->deleteLater();

            if (err != QNetworkReply::NoError)
            {
                if (isAuthError(reply, err))
                {
                    emit authenticationRequired("Jira authentication failed while loading issue details. Please configure your API token.");
                    emit issueFieldSnapshotReady(JiraIssueFieldSnapshot{});
                    return;
                }
                emit operationFailed("GetIssueFieldSnapshot", errStr);
                emit issueFieldSnapshotReady(JiraIssueFieldSnapshot{});
                return;
            }

            const auto doc = QJsonDocument::fromJson(data);
            if (!doc.isObject())
            {
                emit operationFailed("GetIssueFieldSnapshot", "Unexpected JSON (expected object)");
                emit issueFieldSnapshotReady(JiraIssueFieldSnapshot{});
                return;
            }

            const auto root = doc.object();
            const auto fieldsObj = root.value("fields").toObject();

            JiraIssueFieldSnapshot snap;

            // Description (ADF)
            const auto desc = fieldsObj.value("description");
            if (!desc.isNull() && !desc.isUndefined())
                snap.description = adfToPlainText(desc);

            // Story points
            if (!m_storyPointsFieldId.isEmpty())
            {
                const auto sp = fieldsObj.value(m_storyPointsFieldId);
                if (sp.isDouble()) snap.storyPoints = sp.toDouble();
            }

            // Assignee
            const auto assignee = fieldsObj.value("assignee");
            if (assignee.isObject())
            {
                const auto ao = assignee.toObject();
                snap.assigneeDisplayName = ao.value("displayName").toString();
                snap.assigneeAccountId = ao.value("accountId").toString();
            }

            // Due date
            const auto due = fieldsObj.value("duedate").toString();
            if (!due.isEmpty())
            {
                const auto d = QDate::fromString(due, Qt::ISODate);
                if (d.isValid()) snap.dueDate = d;
            }

            // Sprint
            if (!m_sprintFieldId.isEmpty())
            {
                const auto sprintVal = fieldsObj.value(m_sprintFieldId);
                if (sprintVal.isArray() && !sprintVal.toArray().isEmpty())
                {
                    const auto first = sprintVal.toArray().first();
                    extractSprint(first, snap.sprintId, snap.sprintName);
                    if (snap.sprintName.isEmpty() && first.isString())
                        snap.sprintName = parseSprintNameFromLegacyString(first.toString());
                }
                else if (sprintVal.isObject())
                {
                    extractSprint(sprintVal, snap.sprintId, snap.sprintName);
                }
                else if (sprintVal.isString())
                {
                    snap.sprintName = parseSprintNameFromLegacyString(sprintVal.toString());
                }
            }

            emit issueFieldSnapshotReady(snap);
        });
    });
}

void JiraClient::getIssueComments(const QString& issueKey)
{
    if (issueKey.trimmed().isEmpty())
    {
        emit issueCommentsReady({});
        return;
    }

    QList<JiraComment> all;
    const int maxResults = 50;

    std::function<void(int)> fetch;
    fetch = [this, issueKey, maxResults, &all, &fetch](int startAt) {
        QUrl url(m_basePlatform + "/issue/" + enc(issueKey) + "/comment");
        QUrlQuery q;
        q.addQueryItem("startAt", QString::number(startAt));
        q.addQueryItem("maxResults", QString::number(maxResults));
        url.setQuery(q);

        QNetworkReply* reply = m_net.get(makeRequest(url));
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, startAt, maxResults, &all, fetch]() mutable {
            const auto data = reply->readAll();
            const auto err = reply->error();
            const auto errStr = reply->errorString();
            reply->deleteLater();

            if (err != QNetworkReply::NoError)
            {
                if (isAuthError(reply, err))
                {
                    emit authenticationRequired("Jira authentication failed while loading comments. Please configure your API token.");
                    emit issueCommentsReady({});
                    return;
                }
                emit operationFailed("GetIssueComments", errStr);
                emit issueCommentsReady(all);
                return;
            }

            const auto doc = QJsonDocument::fromJson(data);
            if (!doc.isObject())
            {
                emit operationFailed("GetIssueComments", "Unexpected JSON (expected object)");
                emit issueCommentsReady(all);
                return;
            }

            const auto root = doc.object();
            const auto comments = root.value("comments").toArray();
            if (comments.isEmpty())
            {
                emit issueCommentsReady(all);
                return;
            }

            for (const auto& v : comments)
            {
                const auto c = v.toObject();
                JiraComment jc;
                jc.id = c.value("id").toString();
                const auto authorObj = c.value("author").toObject();
                jc.author = authorObj.value("displayName").toString();
                const auto createdStr = c.value("created").toString();
                jc.created = QDateTime::fromString(createdStr, Qt::ISODateWithMs);
                if (!jc.created.isValid())
                    jc.created = QDateTime::fromString(createdStr, Qt::ISODate);

                const auto body = c.value("body");
                QString bodyText;
                if (body.isString()) bodyText = body.toString();
                else bodyText = adfToPlainText(body);
                jc.editableBody = bodyText;
                all.append(jc);
            }

            const int total = root.value("total").toInt(startAt + comments.size());
            const int nextStart = startAt + comments.size();
            if (nextStart >= total)
            {
                emit issueCommentsReady(all);
                return;
            }

            fetch(nextStart);
        });
    };

    fetch(0);
}

void JiraClient::getIssueHistory(const QString& issueKey)
{
    if (issueKey.trimmed().isEmpty())
    {
        emit issueHistoryReady({});
        return;
    }

    QUrl url(m_basePlatform + "/issue/" + enc(issueKey));
    QUrlQuery q;
    q.addQueryItem("expand", "changelog");
    q.addQueryItem("fields", "summary");
    url.setQuery(q);

    QNetworkReply* reply = m_net.get(makeRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto data = reply->readAll();
        const auto err = reply->error();
        const auto errStr = reply->errorString();
        reply->deleteLater();

        if (err != QNetworkReply::NoError)
        {
            if (isAuthError(reply, err))
            {
                emit authenticationRequired("Jira authentication failed while loading history. Please configure your API token.");
                emit issueHistoryReady({});
                return;
            }
            emit operationFailed("GetIssueHistory", errStr);
            emit issueHistoryReady({});
            return;
        }

        const auto doc = QJsonDocument::fromJson(data);
        if (!doc.isObject())
        {
            emit operationFailed("GetIssueHistory", "Unexpected JSON (expected object)");
            emit issueHistoryReady({});
            return;
        }

        QList<JiraHistoryEntry> history;
        const auto root = doc.object();
        const auto changelog = root.value("changelog").toObject();
        const auto histories = changelog.value("histories").toArray();
        for (const auto& hv : histories)
        {
            const auto entry = hv.toObject();
            const auto createdStr = entry.value("created").toString();
            auto when = QDateTime::fromString(createdStr, Qt::ISODateWithMs);
            if (!when.isValid()) when = QDateTime::fromString(createdStr, Qt::ISODate);

            QString author;
            const auto authorObj = entry.value("author").toObject();
            author = authorObj.value("displayName").toString();

            const auto items = entry.value("items").toArray();
            for (const auto& iv : items)
            {
                const auto item = iv.toObject();
                JiraHistoryEntry h;
                h.when = when;
                h.author = author;
                h.field = item.value("field").toString();
                h.fromValue = item.value("fromString").toString();
                h.toValue = item.value("toString").toString();
                history.append(h);
            }
        }

        std::sort(history.begin(), history.end(), [](const JiraHistoryEntry& a, const JiraHistoryEntry& b) {
            if (a.when != b.when) return a.when > b.when;
            return a.author.toLower() > b.author.toLower();
        });

        emit issueHistoryReady(history);
    });
}

void JiraClient::getTransitions(const QString& issueKey)
{
    if (issueKey.trimmed().isEmpty())
    {
        emit transitionsReady({});
        return;
    }

    QUrl url(m_basePlatform + "/issue/" + enc(issueKey) + "/transitions");
    QNetworkReply* reply = m_net.get(makeRequest(url));
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto data = reply->readAll();
        const auto err = reply->error();
        const auto errStr = reply->errorString();
        reply->deleteLater();

        if (err != QNetworkReply::NoError)
        {
            if (isAuthError(reply, err))
            {
                emit authenticationRequired("Jira authentication failed while loading transitions. Please configure your API token.");
                emit transitionsReady({});
                return;
            }
            emit operationFailed("GetTransitions", errStr);
            emit transitionsReady({});
            return;
        }

        const auto doc = QJsonDocument::fromJson(data);
        if (!doc.isObject())
        {
            emit operationFailed("GetTransitions", "Unexpected JSON (expected object)");
            emit transitionsReady({});
            return;
        }

        QList<JiraTransition> list;
        const auto root = doc.object();
        const auto arr = root.value("transitions").toArray();
        for (const auto& v : arr)
        {
            const auto o = v.toObject();
            const auto id = o.value("id").toString();
            if (id.isEmpty()) continue;
            list.append(JiraTransition{id, o.value("name").toString()});
        }
        emit transitionsReady(list);
    });
}

void JiraClient::updateIssueDescription(const QString& issueKey, const QString& plainText)
{
    if (issueKey.trimmed().isEmpty()) return;

    QUrl url(m_basePlatform + "/issue/" + enc(issueKey));
    QJsonObject payload;
    QJsonObject fields;
    fields.insert("description", buildAdfDocument(plainText));
    payload.insert("fields", fields);

    QNetworkReply* reply = m_net.put(makeRequest(url), QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto err = reply->error();
        const auto errStr = reply->errorString();
        reply->deleteLater();
        if (err != QNetworkReply::NoError)
        {
            if (isAuthError(reply, err))
            {
                emit authenticationRequired("Jira authentication failed while updating the description. Please configure your API token.");
                return;
            }
            emit operationFailed("UpdateIssueDescription", errStr);
            return;
        }
        emit operationSucceeded("Description updated");
    });
}

void JiraClient::addComment(const QString& issueKey, const QString& plainText)
{
    if (issueKey.trimmed().isEmpty() || plainText.trimmed().isEmpty()) return;

    QUrl url(m_basePlatform + "/issue/" + enc(issueKey) + "/comment");
    QJsonObject payload;
    payload.insert("body", buildAdfDocument(plainText));

    QNetworkReply* reply = m_net.post(makeRequest(url), QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto err = reply->error();
        const auto errStr = reply->errorString();
        reply->deleteLater();
        if (err != QNetworkReply::NoError)
        {
            if (isAuthError(reply, err))
            {
                emit authenticationRequired("Jira authentication failed while adding a comment. Please configure your API token.");
                return;
            }
            emit operationFailed("AddComment", errStr);
            return;
        }
        emit operationSucceeded("Comment posted");
    });
}

void JiraClient::updateComment(const QString& issueKey, const QString& commentId, const QString& plainText)
{
    if (issueKey.trimmed().isEmpty() || commentId.trimmed().isEmpty()) return;

    QUrl url(m_basePlatform + "/issue/" + enc(issueKey) + "/comment/" + enc(commentId));
    QJsonObject payload;
    payload.insert("body", buildAdfDocument(plainText));

    QNetworkReply* reply = m_net.put(makeRequest(url), QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto err = reply->error();
        const auto errStr = reply->errorString();
        reply->deleteLater();
        if (err != QNetworkReply::NoError)
        {
            if (isAuthError(reply, err))
            {
                emit authenticationRequired("Jira authentication failed while updating a comment. Please configure your API token.");
                return;
            }
            emit operationFailed("UpdateComment", errStr);
            return;
        }
        emit operationSucceeded("Comment updated");
    });
}

void JiraClient::updateStoryPoints(const QString& issueKey, const std::optional<double>& storyPoints)
{
    if (issueKey.trimmed().isEmpty()) return;

    ensureFieldMetadata([this, issueKey, storyPoints]() {
        if (m_storyPointsFieldId.isEmpty()) return;

        QUrl url(m_basePlatform + "/issue/" + enc(issueKey));
        QJsonObject payload;
        QJsonObject fields;
        if (storyPoints.has_value())
            fields.insert(m_storyPointsFieldId, *storyPoints);
        else
            fields.insert(m_storyPointsFieldId, QJsonValue(QJsonValue::Null));
        payload.insert("fields", fields);

        QNetworkReply* reply = m_net.put(makeRequest(url), QJsonDocument(payload).toJson(QJsonDocument::Compact));
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const auto err = reply->error();
            const auto errStr = reply->errorString();
            reply->deleteLater();
            if (err != QNetworkReply::NoError)
            {
                if (isAuthError(reply, err))
                {
                    emit authenticationRequired("Jira authentication failed while updating story points. Please configure your API token.");
                    return;
                }
                emit operationFailed("UpdateStoryPoints", errStr);
                return;
            }
            emit operationSucceeded("Story points updated");
        });
    });
}

void JiraClient::updateAssignee(const QString& issueKey, const QString& assigneeInput)
{
    if (issueKey.trimmed().isEmpty()) return;

    const QString trimmed = assigneeInput.trimmed();
    resolveUserAccountId(trimmed, [this, issueKey, trimmed](const QString& resolved) {
        QString accountId;
        if (!trimmed.isEmpty())
            accountId = resolved.isEmpty() ? trimmed : resolved;

        QUrl url(m_basePlatform + "/issue/" + enc(issueKey) + "/assignee");
        QJsonObject payload;
        if (!accountId.isEmpty())
            payload.insert("accountId", accountId);
        else
            payload.insert("accountId", QJsonValue(QJsonValue::Null));

        QNetworkReply* reply = m_net.put(makeRequest(url), QJsonDocument(payload).toJson(QJsonDocument::Compact));
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const auto err = reply->error();
            const auto errStr = reply->errorString();
            reply->deleteLater();
        if (err != QNetworkReply::NoError)
        {
            if (isAuthError(reply, err))
            {
                emit authenticationRequired("Jira authentication failed while updating the assignee. Please configure your API token.");
                return;
            }
            emit operationFailed("UpdateAssignee", errStr);
            return;
        }
            emit operationSucceeded("Assignee updated");
        });
    });
}

void JiraClient::updateDueDate(const QString& issueKey, const std::optional<QDate>& dueDate)
{
    if (issueKey.trimmed().isEmpty()) return;

    QUrl url(m_basePlatform + "/issue/" + enc(issueKey));
    QJsonObject payload;
    QJsonObject fields;
    if (dueDate.has_value())
        fields.insert("duedate", dueDate->toString("yyyy-MM-dd"));
    else
        fields.insert("duedate", QJsonValue(QJsonValue::Null));
    payload.insert("fields", fields);

    QNetworkReply* reply = m_net.put(makeRequest(url), QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto err = reply->error();
        const auto errStr = reply->errorString();
        reply->deleteLater();
        if (err != QNetworkReply::NoError)
        {
            if (isAuthError(reply, err))
            {
                emit authenticationRequired("Jira authentication failed while updating the due date. Please configure your API token.");
                return;
            }
            emit operationFailed("UpdateDueDate", errStr);
            return;
        }
        emit operationSucceeded("Due date updated");
    });
}

void JiraClient::updateSprint(const QString& issueKey, const std::optional<int>& sprintId)
{
    if (issueKey.trimmed().isEmpty()) return;

    ensureFieldMetadata([this, issueKey, sprintId]() {
        if (m_sprintFieldId.isEmpty()) return;

        QUrl url(m_basePlatform + "/issue/" + enc(issueKey));
        QJsonObject payload;
        QJsonObject fields;

        if (sprintId.has_value())
        {
            QJsonArray arr;
            arr.append(*sprintId);
            fields.insert(m_sprintFieldId, arr);
        }
        else
        {
            fields.insert(m_sprintFieldId, QJsonValue(QJsonValue::Null));
        }
        payload.insert("fields", fields);

        QNetworkReply* reply = m_net.put(makeRequest(url), QJsonDocument(payload).toJson(QJsonDocument::Compact));
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const auto err = reply->error();
            const auto errStr = reply->errorString();
            reply->deleteLater();
        if (err != QNetworkReply::NoError)
        {
            if (isAuthError(reply, err))
            {
                emit authenticationRequired("Jira authentication failed while updating the sprint. Please configure your API token.");
                return;
            }
            emit operationFailed("UpdateSprint", errStr);
            return;
        }
            emit operationSucceeded("Sprint updated");
        });
    });
}

void JiraClient::transitionIssue(const QString& issueKey, const QString& transitionId)
{
    if (issueKey.trimmed().isEmpty() || transitionId.trimmed().isEmpty()) return;

    QUrl url(m_basePlatform + "/issue/" + enc(issueKey) + "/transitions");
    QJsonObject payload;
    QJsonObject transition;
    transition.insert("id", transitionId);
    payload.insert("transition", transition);

    QNetworkReply* reply = m_net.post(makeRequest(url), QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto err = reply->error();
        const auto errStr = reply->errorString();
        reply->deleteLater();
        if (err != QNetworkReply::NoError)
        {
            if (isAuthError(reply, err))
            {
                emit authenticationRequired("Jira authentication failed while transitioning the issue. Please configure your API token.");
                return;
            }
            emit operationFailed("TransitionIssue", errStr);
            return;
        }
        emit operationSucceeded("Transition applied");
    });
}

// ---- Tray helpers (Agile endpoints) ----

void JiraClient::getMostRecentActiveSprint()
{
    // Mirror C# logic: iterate scrum boards, check active sprints, pick most recent by startDate.
    getAllBoards("scrum", [this](const QList<QJsonObject>& boards) {
        if (boards.isEmpty())
        {
            emit mostRecentActiveSprintReady(std::nullopt, QString(), std::nullopt);
            return;
        }

        struct Best { bool has{false}; int id{0}; QString name; QDateTime start; bool hasStart{false}; };
        auto* best = new Best();
        auto* remaining = new int(boards.size());

        for (const auto& b : boards)
        {
            const int boardId = b.value("id").toInt();
            getBoardSprints(boardId, "active", [this, best, remaining](const QList<QJsonObject>& sprints) {
                for (const auto& s : sprints)
                {
                    const int id = s.value("id").toInt();
                    const QString name = s.value("name").toString();
                    const QString startStr = s.value("startDate").toString();
                    QDateTime start = QDateTime::fromString(startStr, Qt::ISODateWithMs);
                    if (!start.isValid()) start = QDateTime::fromString(startStr, Qt::ISODate);

                    const bool hasStart = start.isValid();
                    if (!best->has)
                    {
                        best->has = true;
                        best->id = id;
                        best->name = name;
                        best->start = start;
                        best->hasStart = hasStart;
                        continue;
                    }

                    // Prefer larger (more recent) startDate.
                    if (hasStart && (!best->hasStart || start > best->start))
                    {
                        best->id = id;
                        best->name = name;
                        best->start = start;
                        best->hasStart = true;
                    }
                }

                (*remaining)--;
                if (*remaining == 0)
                {
                    if (!best->has)
                        emit mostRecentActiveSprintReady(std::nullopt, QString(), std::nullopt);
                    else
                        emit mostRecentActiveSprintReady(best->id, best->name, best->hasStart ? std::optional<QDateTime>(best->start) : std::nullopt);
                    delete best;
                    delete remaining;
                }
            });
        }
    });
}

void JiraClient::getIssuesForSprint(int sprintId)
{
    if (sprintId <= 0)
    {
        emit sprintIssuesReady({});
        return;
    }

    QList<JiraTicket> all;
    const int maxResults = 50;

    std::function<void(int)> fetch;
    fetch = [this, sprintId, maxResults, &all, &fetch](int startAt) {
        QUrl url(m_baseAgile + "/sprint/" + QString::number(sprintId) + "/issue");
        QUrlQuery q;
        q.addQueryItem("startAt", QString::number(startAt));
        q.addQueryItem("maxResults", QString::number(maxResults));
        url.setQuery(q);

        QNetworkReply* reply = m_net.get(makeRequest(url));
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, startAt, &all, fetch]() mutable {
            const auto data = reply->readAll();
            const auto err = reply->error();
            const auto errStr = reply->errorString();
            reply->deleteLater();

            if (err != QNetworkReply::NoError)
            {
                if (isAuthError(reply, err))
                {
                    emit authenticationRequired("Jira authentication failed while loading sprint issues. Please configure your API token.");
                    emit sprintIssuesReady({});
                    return;
                }
                emit operationFailed("GetIssuesForSprint", errStr);
                emit sprintIssuesReady(all);
                return;
            }

            const auto doc = QJsonDocument::fromJson(data);
            if (!doc.isObject())
            {
                emit operationFailed("GetIssuesForSprint", "Unexpected JSON (expected object)");
                emit sprintIssuesReady(all);
                return;
            }

            const auto root = doc.object();
            const auto issues = root.value("issues").toArray();
            if (issues.isEmpty())
            {
                emit sprintIssuesReady(all);
                return;
            }

            for (const auto& v : issues)
            {
                const auto issue = v.toObject();
                const auto fields = issue.value("fields").toObject();

                QString sprintName = "This Sprint";
                const auto sprintVal = fields.value("sprint");
                if (sprintVal.isObject())
                    sprintName = sprintVal.toObject().value("name").toString(sprintName);

                JiraTicket t;
                t.key = issue.value("key").toString();
                t.summary = fields.value("summary").toString();
                t.status = fields.value("status").toObject().value("name").toString();
                t.sprint = sprintName;
                all.append(t);
            }

            const int total = root.value("total").toInt(startAt + issues.size());
            const int nextStart = startAt + root.value("maxResults").toInt(issues.size());
            if (nextStart >= total)
            {
                emit sprintIssuesReady(all);
                return;
            }
            fetch(nextStart);
        });
    };

    fetch(0);
}

QJsonObject JiraClient::buildAdfDocument(const QString& plainText)
{
    const QString safe = plainText;
    const auto parts = safe.split('\n');

    QJsonArray content;
    for (const auto& p : parts)
    {
        QJsonObject textNode;
        textNode.insert("type", "text");
        textNode.insert("text", p);

        QJsonArray paraContent;
        paraContent.append(textNode);

        QJsonObject paragraph;
        paragraph.insert("type", "paragraph");
        paragraph.insert("content", paraContent);

        content.append(paragraph);
    }

    QJsonObject doc;
    doc.insert("version", 1);
    doc.insert("type", "doc");
    doc.insert("content", content);
    return doc;
}

QString JiraClient::adfToPlainText(const QJsonValue& adf)
{
    // Port of C# AdfChildToPlainText: walks doc/paragraph/text/hardBreak.
    QStringList out;

    std::function<void(const QJsonValue&, QStringList&)> walk;
    walk = [&walk](const QJsonValue& node, QStringList& lines) {
        if (node.isNull() || node.isUndefined()) return;

        if (node.isArray())
        {
            bool first = true;
            for (const auto& v : node.toArray())
            {
                if (!first) lines.append("");
                first = false;
                walk(v, lines);
            }
            return;
        }

        if (!node.isObject()) return;

        const auto o = node.toObject();
        const auto type = o.value("type").toString();

        if (type == "doc")
        {
            const auto content = o.value("content").toArray();
            bool firstPara = true;
            for (const auto& v : content)
            {
                if (!firstPara) lines.append("");
                firstPara = false;
                walk(v, lines);
            }
            return;
        }

        if (type == "paragraph")
        {
            const auto content = o.value("content").toArray();
            if (lines.isEmpty()) lines.append(QString());
            for (const auto& v : content)
            {
                walk(v, lines);
            }
            return;
        }

        if (type == "hardBreak")
        {
            lines.append(QString());
            return;
        }

        if (type == "text")
        {
            if (lines.isEmpty()) lines.append(QString());
            auto current = lines.takeLast();
            current += o.value("text").toString();
            lines.append(current);
            return;
        }

        // Fallback: walk children if present
        const auto content = o.value("content");
        if (content.isArray())
        {
            for (const auto& v : content.toArray())
                walk(v, lines);
        }
    };

    walk(adf, out);
    while (!out.isEmpty() && out.last().trimmed().isEmpty()) out.removeLast();
    return out.join("\n").trimmed();
}

// ---- Helper implementations ----

QString JiraClient::parseSprintNameFromLegacyString(const QString& raw)
{
    const QString sprintString = "Sprint";
    if (raw.trimmed().isEmpty()) return sprintString;
    const int idx = raw.indexOf("name=", 0, Qt::CaseInsensitive);
    if (idx < 0) return sprintString;
    const QString after = raw.mid(idx + 5);
    const int end = after.indexOf(',');
    return (end > 0 ? after.left(end) : after).trimmed();
}

void JiraClient::extractSprint(const QJsonValue& element, std::optional<int>& id, QString& name)
{
    id.reset();
    name.clear();
    if (!element.isObject()) return;
    const auto o = element.toObject();
    if (o.value("id").isDouble()) id = o.value("id").toInt();
    name = o.value("name").toString();
}

void JiraClient::resolveUserAccountId(const QString& query, std::function<void(const QString&)> cont)
{
    if (query.trimmed().isEmpty())
    {
        cont(QString());
        return;
    }

    QUrl url(m_basePlatform + "/user/search/query");
    QJsonObject payload;
    payload.insert("query", query);
    payload.insert("maxResults", 1);
    QNetworkReply* reply = m_net.post(makeRequest(url), QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, cont]() {
        const auto data = reply->readAll();
        const auto err = reply->error();
        const auto errStr = reply->errorString();
        reply->deleteLater();

        if (err != QNetworkReply::NoError)
        {
            if (isAuthError(reply, err))
            {
                emit authenticationRequired("Jira authentication failed while resolving an account id. Please configure your API token.");
                cont(QString());
                return;
            }
            emit operationFailed("ResolveUserAccountId", errStr);
            cont(QString());
            return;
        }

        const auto doc = QJsonDocument::fromJson(data);
        if (!doc.isArray() || doc.array().isEmpty())
        {
            cont(QString());
            return;
        }

        const auto first = doc.array().first().toObject();
        cont(first.value("accountId").toString());
    });
}

void JiraClient::getAllBoards(const QString& type, std::function<void(const QList<QJsonObject>&)> cont)
{
    // GET /board?startAt=..&maxResults=..&type=scrum
    auto* all = new QList<QJsonObject>();
    const int max = 50;

    std::function<void(int)> fetch;
    fetch = [this, type, max, all, cont, &fetch](int startAt) {
        QUrl url(m_baseAgile + "/board");
        QUrlQuery q;
        q.addQueryItem("startAt", QString::number(startAt));
        q.addQueryItem("maxResults", QString::number(max));
        if (!type.isEmpty()) q.addQueryItem("type", type);
        url.setQuery(q);

        QNetworkReply* reply = m_net.get(makeRequest(url));
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, startAt, max, all, cont, fetch]() mutable {
            const auto data = reply->readAll();
            const auto err = reply->error();
            const auto errStr = reply->errorString();
            reply->deleteLater();

            if (err != QNetworkReply::NoError)
            {
                if (isAuthError(reply, err))
                {
                    emit authenticationRequired("Jira authentication failed while loading boards. Please configure your API token.");
                    cont({});
                    delete all;
                    return;
                }
                emit operationFailed("GetAllBoards", errStr);
                cont(*all);
                delete all;
                return;
            }

            const auto doc = QJsonDocument::fromJson(data);
            if (!doc.isObject())
            {
                emit operationFailed("GetAllBoards", "Unexpected JSON (expected object)");
                cont(*all);
                delete all;
                return;
            }

            const auto root = doc.object();
            const auto values = root.value("values").toArray();
            for (const auto& v : values) all->append(v.toObject());
            const bool isLast = root.value("isLast").toBool(false);
            if (isLast || values.isEmpty())
            {
                cont(*all);
                delete all;
                return;
            }

            fetch(startAt + values.size());
        });
    };

    fetch(0);
}

void JiraClient::getBoardSprints(int boardId, const QString& state, std::function<void(const QList<QJsonObject>&)> cont)
{
    auto* all = new QList<QJsonObject>();
    const int max = 50;

    std::function<void(int)> fetch;
    fetch = [this, boardId, state, max, all, cont, &fetch](int startAt) {
        QUrl url(m_baseAgile + "/board/" + QString::number(boardId) + "/sprint");
        QUrlQuery q;
        q.addQueryItem("startAt", QString::number(startAt));
        q.addQueryItem("maxResults", QString::number(max));
        if (!state.isEmpty()) q.addQueryItem("state", state);
        url.setQuery(q);

        QNetworkReply* reply = m_net.get(makeRequest(url));
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, startAt, all, cont, fetch]() mutable {
            const auto data = reply->readAll();
            const auto err = reply->error();
            const auto errStr = reply->errorString();
            reply->deleteLater();

            if (err != QNetworkReply::NoError)
            {
                if (isAuthError(reply, err))
                {
                    emit authenticationRequired("Jira authentication failed while loading sprints. Please configure your API token.");
                    cont({});
                    delete all;
                    return;
                }
                emit operationFailed("GetBoardSprints", errStr);
                cont(*all);
                delete all;
                return;
            }

            const auto doc = QJsonDocument::fromJson(data);
            if (!doc.isObject())
            {
                emit operationFailed("GetBoardSprints", "Unexpected JSON (expected object)");
                cont(*all);
                delete all;
                return;
            }

            const auto root = doc.object();
            const auto values = root.value("values").toArray();
            for (const auto& v : values) all->append(v.toObject());

            const bool isLast = root.value("isLast").toBool(false);
            if (isLast || values.isEmpty())
            {
                cont(*all);
                delete all;
                return;
            }

            fetch(startAt + values.size());
        });
    };

    fetch(0);
}
