#pragma once
#include <QString>
class QWidget;

namespace levelcad::app {
QString chooseExportFile(QWidget* parent, const QString& title, const QString& suggestedPath,
                         const QString& filter);
}
