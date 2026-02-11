#pragma once

#include <QMainWindow>
#include <QSystemTrayIcon>

#include "config.h"
#include "models.h"

namespace Ui {
class MainWindow;
}

class DataHub;
class JiraClient;
class TicketsModel;
class QTreeView;
class QTextEdit;
class QLabel;
class QComboBox;
class QListWidget;
class QLineEdit;
class QDateEdit;
class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void setupUi();
    void setupTray();
    void loadConfig();
    void applyConfig(const AppConfig& cfg);
    bool isConfigComplete() const;
    bool ensureConfigured(const QString& reason);
    bool openSettingsDialog(const QString& reason);

    void refreshTickets();
    void onTicketSelected(const QModelIndex& idx);

    AppConfig m_cfg;
    bool m_authRequired{false};

    Ui::MainWindow* ui;

    JiraClient* m_client;
    DataHub* m_hub;

    TicketsModel* m_ticketsModel;

    QTreeView* m_tree;
    QComboBox* m_statusFilter;

    QLabel* m_selectedKey;
    QLabel* m_selectedStatus;
    QLabel* m_selectedSummary;
    QPushButton* m_openInJira;

    QComboBox* m_transitions;
    QPushButton* m_applyTransition;

    QTextEdit* m_description;
    QLineEdit* m_storyPoints;
    QLineEdit* m_assignee;
    QLineEdit* m_sprintId;
    QLabel* m_currentSprintName;
    QDateEdit* m_dueDate;
    QPushButton* m_updateStoryPoints;
    QPushButton* m_updateAssignee;
    QPushButton* m_updateSprint;
    QPushButton* m_updateDueDate;
    QTextEdit* m_newComment;
    QListWidget* m_comments;
    QListWidget* m_history;

    QSystemTrayIcon* m_tray;
};
