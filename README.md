# JiraExplorerQt (Qt 6 / C++ Widgets)

This is a **Qt C++ port scaffold** of the WPF application in `JiraExplorer.zip`.

What is already ported:
- App settings persisted in **appsettings.json** (same schema as the C# app)
- `QSystemTrayIcon` with Show/Hide, Refresh, Quit
- Main window layout: menu + toolbar + ticket tree + details pane
- A working `JiraClient::getMyTickets()` that calls Jira Cloud/Data Center REST API v3 search endpoint (same JQL as the C# app)
- Tree grouping by sprint (similar to WPF TreeView)
- Issue description load/save (ADF parsing + PUT /issue/{key})
- Comments load/add/update
- Issue field snapshot (story points, assignee, sprint, due date)
- Transitions list + apply transition
- Activity history (changelog)

## Build

Requires Qt 6 (Widgets + Network) and CMake.

```bash
cmake -S . -B build
cmake --build build -j
```

## Mapping notes (WPF → Qt)

- `MainWindow.xaml` → `src/mainwindow.ui` (Qt Widgets via `.ui`)
- `TaskbarIcon` (Hardcodet.Wpf.TaskbarNotification) → `QSystemTrayIcon`
- WPF MVVM (`MainViewModel`, `TrayViewModel`) → Qt signals/slots + `TicketsModel` (QStandardItemModel)
- RichTextBox binding (ADF ↔ plain text) → `QTextEdit` (currently plain text; can be extended to rich HTML if desired)

The C# implementation is the reference for request URLs, payloads, and edge cases.
