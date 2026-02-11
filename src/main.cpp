#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Enable high DPI scaling (Qt 6 handles most of this automatically)
    QApplication::setApplicationName("JiraExplorerQt");
    QApplication::setOrganizationName("JiraExplorer");

    MainWindow w;
    w.show();

    return app.exec();
}
