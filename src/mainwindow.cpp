#include "mainwindow.h"

#include "config.h"
#include "datahub.h"
#include "error.h"
#include "jira_client.h"
#include "settingsdialog.h"
#include "ticketsmodel.h"
#include "ui_mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTextEdit>
#include <QLineEdit>
#include <QTreeView>
#include <QUrl>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      m_client(new JiraClient(this)),
      m_hub(new DataHub(m_client, this)),
      m_ticketsModel(new TicketsModel(this)),
      m_tray(nullptr)
{
    ui->setupUi(this);
    setupUi();
    setupTray();

    // Wire-up hub -> model
    connect(m_hub, &DataHub::ticketsUpdated, this, [this](const QList<JiraTicket>& tickets) {
        m_ticketsModel->setTickets(tickets);
        // Populate status filter
        QSet<QString> statuses;
        for (const auto& t : tickets) statuses.insert(t.status);
        auto current = m_statusFilter->currentText();
        m_statusFilter->blockSignals(true);
        m_statusFilter->clear();
        m_statusFilter->addItem("All");
        for (const auto& s : statuses.values()) m_statusFilter->addItem(s);
        m_statusFilter->setCurrentText(current.isEmpty() ? "All" : current);
        m_statusFilter->blockSignals(false);
    });

    connect(m_client, &JiraClient::operationFailed, this, [this](const QString& ctx, const QString& err) {
        ErrorService::showError(ctx, err, this);
    });

    connect(m_client, &JiraClient::authenticationRequired, this, [this](const QString& msg) {
        m_authRequired = true;
        statusBar()->showMessage("Jira authentication required. Please update Settings.", 5000);
        openSettingsDialog(msg);
    });

    connect(m_client, &JiraClient::operationSucceeded, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 3000);

        // After writes, refresh the currently selected ticket details.
        const auto key = m_selectedKey ? m_selectedKey->text() : QString();
        if (!key.isEmpty() && !key.startsWith('('))
        {
            m_client->getIssueFieldSnapshot(key);
            m_client->getIssueComments(key);
            m_client->getIssueHistory(key);
            m_client->getTransitions(key);
        }
    });

    connect(m_client, &JiraClient::issueFieldSnapshotReady, this, [this](const JiraIssueFieldSnapshot& s) {
        m_description->setPlainText(s.description);
        if (s.storyPoints.has_value())
            m_storyPoints->setText(QString::number(*s.storyPoints));
        else
            m_storyPoints->clear();

        m_assignee->setText(s.assigneeDisplayName.isEmpty() ? s.assigneeAccountId : s.assigneeDisplayName);
        if (s.sprintId.has_value())
            m_sprintId->setText(QString::number(*s.sprintId));
        else
            m_sprintId->clear();
        m_currentSprintName->setText(s.sprintName.isEmpty() ? QString() : ("Current: " + s.sprintName));

        if (s.dueDate.has_value())
            m_dueDate->setDate(*s.dueDate);
    });

    connect(m_client, &JiraClient::issueCommentsReady, this, [this](const QList<JiraComment>& comments) {
        m_comments->clear();
        if (comments.isEmpty())
        {
            m_comments->addItem("(no comments)");
            return;
        }
        for (const auto& c : comments)
        {
            const auto header = QString("%1 (%2)").arg(c.author, c.created.isValid() ? c.created.toString("yyyy-MM-dd HH:mm") : "");
            auto* item = new QListWidgetItem(header + "\n" + c.editableBody, m_comments);
            item->setData(Qt::UserRole, c.id);
            item->setData(Qt::UserRole + 1, c.editableBody);
        }
    });

    connect(m_client, &JiraClient::issueHistoryReady, this, [this](const QList<JiraHistoryEntry>& entries) {
        m_history->clear();
        if (entries.isEmpty())
        {
            m_history->addItem("(no history)" );
            return;
        }
        for (const auto& e : entries)
        {
            const auto line = QString("%1 (%2): Changed %3 from '%4' to '%5'")
                .arg(e.author,
                     e.when.isValid() ? e.when.toString("yyyy-MM-dd HH:mm") : "",
                     e.field,
                     e.fromValue.isEmpty() ? "(empty)" : e.fromValue,
                     e.toValue.isEmpty() ? "(empty)" : e.toValue);
            m_history->addItem(line);
        }
    });

    connect(m_client, &JiraClient::transitionsReady, this, [this](const QList<JiraTransition>& transitions) {
        m_transitions->clear();
        m_transitions->addItem("(select)", QString());
        for (const auto& t : transitions)
            m_transitions->addItem(t.name, t.id);
    });

    loadConfig();
    if (ensureConfigured("Jira setup is required before loading tickets."))
        refreshTickets();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupUi()
{
    m_tree = ui->treeTickets;
    m_selectedKey = ui->labelSelectedKey;
    m_selectedStatus = ui->labelSelectedStatus;
    m_selectedSummary = ui->labelSelectedSummary;
    m_openInJira = ui->buttonOpenInJira;
    m_transitions = ui->comboTransitions;
    m_applyTransition = ui->buttonApplyTransition;
    m_description = ui->textDescription;
    m_storyPoints = ui->lineStoryPoints;
    m_assignee = ui->lineAssignee;
    m_sprintId = ui->lineSprintId;
    m_currentSprintName = ui->labelCurrentSprintName;
    m_dueDate = ui->dateDueDate;
    m_updateStoryPoints = ui->buttonUpdateStoryPoints;
    m_updateAssignee = ui->buttonUpdateAssignee;
    m_updateSprint = ui->buttonUpdateSprint;
    m_updateDueDate = ui->buttonUpdateDueDate;
    m_newComment = ui->textNewComment;
    m_comments = ui->listComments;
    m_history = ui->listHistory;

    auto statusFilterWidget = new QWidget(ui->toolBar);
    auto statusLayout = new QHBoxLayout(statusFilterWidget);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(6);
    auto statusLabel = new QLabel("Filter by Status:", statusFilterWidget);
    m_statusFilter = new QComboBox(statusFilterWidget);
    m_statusFilter->addItem("All");
    statusLayout->addWidget(statusLabel);
    statusLayout->addWidget(m_statusFilter);
    ui->toolBar->addWidget(statusFilterWidget);
    ui->toolBar->setStyleSheet(QString());

    m_description->setPlaceholderText("Select a ticket to load description...");

    ui->splitterDescDetails->setStretchFactor(0, 3);
    ui->splitterDescDetails->setStretchFactor(1, 1);
    ui->splitterMain->setStretchFactor(0, 1);
    ui->splitterMain->setStretchFactor(1, 3);

    connect(ui->actionSettings, &QAction::triggered, this, [this] {
        if (openSettingsDialog(QString()))
            refreshTickets();
    });
    connect(ui->actionQuit, &QAction::triggered, qApp, &QApplication::quit);
    connect(ui->actionRefresh, &QAction::triggered, this, &MainWindow::refreshTickets);

    connect(m_statusFilter, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        // Very simple filter: refetch and rebuild model, removing tickets not matching.
        // If you want a richer experience, use QSortFilterProxyModel.
        auto tickets = m_hub->currentTickets();
        if (text != "All")
        {
            QList<JiraTicket> filtered;
            for (const auto& t : tickets)
                if (t.status == text)
                    filtered.append(t);
            m_ticketsModel->setTickets(filtered);
        }
        else
        {
            m_ticketsModel->setTickets(tickets);
        }
    });

    m_tree->setModel(m_ticketsModel);
    connect(m_tree, &QTreeView::clicked, this, &MainWindow::onTicketSelected);

    connect(m_openInJira, &QPushButton::clicked, this, [this] {
        if (!ensureConfigured("Jira setup is required before opening issues in Jira."))
            return;
        const auto key = m_selectedKey->text();
        if (key.startsWith('(')) return;
        const auto base = m_cfg.jira.instanceUrl;
        if (base.isEmpty()) return;
        QString b = base.trimmed();
        while (b.endsWith('/')) b.chop(1);
        QDesktopServices::openUrl(QUrl(b + "/browse/" + key));
    });

    connect(m_applyTransition, &QPushButton::clicked, this, [this] {
        const auto key = m_selectedKey->text();
        if (key.startsWith('(')) return;
        const auto id = m_transitions->currentData().toString();
        if (id.isEmpty()) return;
        m_client->transitionIssue(key, id);
    });

    connect(ui->buttonSaveDescription, &QPushButton::clicked, this, [this] {
        const auto key = m_selectedKey->text();
        if (key.startsWith('(')) return;
        m_client->updateIssueDescription(key, m_description->toPlainText());
    });

    connect(m_updateStoryPoints, &QPushButton::clicked, this, [this] {
        const auto key = m_selectedKey->text();
        if (key.startsWith('(')) return;
        const auto t = m_storyPoints->text().trimmed();
        if (t.isEmpty()) { m_client->updateStoryPoints(key, std::nullopt); return; }
        bool ok = false;
        const double v = t.toDouble(&ok);
        if (!ok) { ErrorService::showError("Story Points", "Enter a number or leave blank to clear", this); return; }
        m_client->updateStoryPoints(key, v);
    });

    connect(m_updateAssignee, &QPushButton::clicked, this, [this] {
        const auto key = m_selectedKey->text();
        if (key.startsWith('(')) return;
        m_client->updateAssignee(key, m_assignee->text());
    });

    connect(m_updateSprint, &QPushButton::clicked, this, [this] {
        const auto key = m_selectedKey->text();
        if (key.startsWith('(')) return;
        const auto t = m_sprintId->text().trimmed();
        if (t.isEmpty()) { m_client->updateSprint(key, std::nullopt); return; }
        bool ok = false;
        const int v = t.toInt(&ok);
        if (!ok) { ErrorService::showError("Sprint", "Enter a numeric sprint id or leave blank to clear", this); return; }
        m_client->updateSprint(key, v);
    });

    connect(m_updateDueDate, &QPushButton::clicked, this, [this] {
        const auto key = m_selectedKey->text();
        if (key.startsWith('(')) return;
        if (!m_dueDate->date().isValid()) { m_client->updateDueDate(key, std::nullopt); return; }
        m_client->updateDueDate(key, m_dueDate->date());
    });

    connect(ui->buttonPostComment, &QPushButton::clicked, this, [this] {
        const auto key = m_selectedKey->text();
        if (key.startsWith('(')) return;
        m_client->addComment(key, m_newComment->toPlainText());
        m_newComment->clear();
    });

    connect(ui->buttonEditComment, &QPushButton::clicked, this, [this] {
        const auto key = m_selectedKey->text();
        if (key.startsWith('(')) return;
        auto* item = m_comments->currentItem();
        if (!item) return;
        const auto commentId = item->data(Qt::UserRole).toString();
        const auto currentBody = item->data(Qt::UserRole + 1).toString();
        bool ok = false;
        const auto updated = QInputDialog::getMultiLineText(this, "Edit Comment", "Comment:", currentBody, &ok);
        if (!ok) return;
        m_client->updateComment(key, commentId, updated);
    });
}

void MainWindow::setupTray()
{
    m_tray = new QSystemTrayIcon(QIcon(":/icon.ico"), this);
    auto* menu = new QMenu(this);

    auto* showHide = menu->addAction("Show/Hide");
    connect(showHide, &QAction::triggered, this, [this] {
        if (isVisible())
            hide();
        else
        {
            show();
            raise();
            activateWindow();
        }
    });

    auto* refresh = menu->addAction("Refresh");
    connect(refresh, &QAction::triggered, this, &MainWindow::refreshTickets);

    auto* settings = menu->addAction("Settings...");
    connect(settings, &QAction::triggered, this, [this] {
        if (openSettingsDialog(QString()))
            refreshTickets();
    });

    menu->addSeparator();
    auto* quit = menu->addAction("Quit");
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    m_tray->setContextMenu(menu);
    m_tray->setToolTip("JiraExplorerQt");
    m_tray->show();
}

void MainWindow::loadConfig()
{
    m_cfg = ConfigService::load();
    applyConfig(m_cfg);
}

void MainWindow::applyConfig(const AppConfig& cfg)
{
    m_client->configure(cfg.jira.instanceUrl, cfg.jira.username, cfg.jira.apiToken);
}

bool MainWindow::isConfigComplete() const
{
    return !m_cfg.jira.instanceUrl.trimmed().isEmpty()
        && !m_cfg.jira.username.trimmed().isEmpty()
        && !m_cfg.jira.apiToken.isEmpty();
}

bool MainWindow::ensureConfigured(const QString& reason)
{
    if (!m_authRequired && isConfigComplete())
        return true;

    return openSettingsDialog(reason);
}

bool MainWindow::openSettingsDialog(const QString& reason)
{
    if (!reason.isEmpty())
        QMessageBox::warning(this, "Setup required", reason);

    while (true)
    {
        SettingsDialog dlg(this);
        dlg.setConfig(m_cfg);
        if (dlg.exec() != QDialog::Accepted)
        {
            statusBar()->showMessage("Jira setup is required to continue.", 5000);
            return false;
        }

        m_cfg = dlg.config();
        if (!isConfigComplete())
        {
            QMessageBox::warning(this,
                                 "Setup required",
                                 "Jira Instance URL, Username, and API Token are required.");
            continue;
        }

        if (!ConfigService::save(m_cfg))
            ErrorService::showError("Settings", "Failed to save appsettings.json", this);
        applyConfig(m_cfg);
        m_authRequired = false;
        return true;
    }
}

void MainWindow::refreshTickets()
{
    if (!ensureConfigured("Jira setup is required before refreshing tickets."))
        return;

    statusBar()->showMessage("Refreshing tickets...");
    m_hub->refreshMyTickets();
}

void MainWindow::onTicketSelected(const QModelIndex& idx)
{
    const auto key = m_ticketsModel->ticketKeyForIndex(idx);
    if (key.isEmpty())
    {
        // Expand/collapse group nodes.
        if (m_ticketsModel->data(idx, TicketsModel::RoleType).toString() == "group")
        {
            m_tree->setExpanded(idx, !m_tree->isExpanded(idx));
        }
        return;
    }

    if (!ensureConfigured("Jira setup is required before loading ticket details."))
        return;

    m_selectedKey->setText(key);
    m_selectedStatus->setText(m_ticketsModel->data(idx, TicketsModel::RoleStatus).toString());

    m_selectedSummary->setText(m_ticketsModel->data(idx, TicketsModel::RoleSummary).toString());
    m_description->setPlainText("Loading...");

    m_transitions->clear();
    m_transitions->addItem("(loading)", QString());

    m_comments->clear();
    m_comments->addItem("Loading...");

    m_history->clear();
    m_history->addItem("Loading...");

    // Load details
    m_client->getIssueFieldSnapshot(key);
    m_client->getIssueComments(key);
    m_client->getIssueHistory(key);
    m_client->getTransitions(key);
}
