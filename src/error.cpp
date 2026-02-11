#include "error.h"

#include <QMessageBox>

void ErrorService::showError(const QString& title, const QString& details, QWidget* parent)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle(title);
    box.setText(title);
    box.setDetailedText(details);
    box.exec();
}
