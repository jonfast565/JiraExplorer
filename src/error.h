#pragma once

#include <QString>

class QWidget;

class ErrorService
{
public:
    static void showError(const QString& title, const QString& details, QWidget* parent = nullptr);
};
