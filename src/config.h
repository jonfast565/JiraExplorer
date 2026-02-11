#pragma once

#include <QString>

struct JiraConfig
{
    QString instanceUrl;
    QString username;
    QString apiToken;
};

struct AppConfig
{
    JiraConfig jira;
};

class ConfigService
{
public:
    static AppConfig load(const QString& path = QStringLiteral("appsettings.json"));
    static bool save(const AppConfig& cfg, const QString& path = QStringLiteral("appsettings.json"));
};
