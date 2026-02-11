#include "settingsdialog.h"

#include "ui_settingsdialog.h"

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent),
      ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::setConfig(const AppConfig& cfg)
{
    ui->lineInstanceUrl->setText(cfg.jira.instanceUrl);
    ui->lineUsername->setText(cfg.jira.username);
    ui->lineApiToken->setText(cfg.jira.apiToken);
}

AppConfig SettingsDialog::config() const
{
    AppConfig cfg;
    cfg.jira.instanceUrl = ui->lineInstanceUrl->text().trimmed();
    cfg.jira.username = ui->lineUsername->text().trimmed();
    cfg.jira.apiToken = ui->lineApiToken->text();
    return cfg;
}
