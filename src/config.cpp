#include "config.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

static AppConfig fromJson(const QJsonObject& root)
{
    AppConfig cfg;

    const auto jiraObj = root.value("Jira").toObject();
    cfg.jira.instanceUrl = jiraObj.value("InstanceUrl").toString();
    cfg.jira.username = jiraObj.value("Username").toString();
    cfg.jira.apiToken = jiraObj.value("ApiToken").toString();

    return cfg;
}

static QJsonObject toJson(const AppConfig& cfg)
{
    QJsonObject jira;
    jira.insert("InstanceUrl", cfg.jira.instanceUrl);
    jira.insert("Username", cfg.jira.username);
    jira.insert("ApiToken", cfg.jira.apiToken);

    QJsonObject root;
    root.insert("Jira", jira);
    return root;
}

AppConfig ConfigService::load(const QString& path)
{
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
    {
        return AppConfig{};
    }

    const auto bytes = f.readAll();
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
    {
        return AppConfig{};
    }

    return fromJson(doc.object());
}

bool ConfigService::save(const AppConfig& cfg, const QString& path)
{
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
    {
        return false;
    }

    const QJsonDocument doc(toJson(cfg));
    out.write(doc.toJson(QJsonDocument::Indented));
    return out.commit();
}
