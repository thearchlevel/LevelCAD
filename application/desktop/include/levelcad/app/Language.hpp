#pragma once
#include <QString>
class QWidget;

namespace levelcad::app {
void setUiLanguage(const QString& language, QWidget* root);
QString uiLanguage();
QString translateUi(const QString& text);
} // namespace levelcad::app
