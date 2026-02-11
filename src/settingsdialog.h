#pragma once

#include <QDialog>

#include "config.h"

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() override;

    void setConfig(const AppConfig& cfg);
    AppConfig config() const;

private:
    Ui::SettingsDialog* ui;
};
