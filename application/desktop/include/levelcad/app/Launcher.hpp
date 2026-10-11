#pragma once
#include <QDialog>
#include <QSplashScreen>
#include <QString>
class QLineEdit;
class QListWidget;

namespace levelcad::app {
class Launcher final : public QDialog {
  public:
    explicit Launcher(QWidget* parent=nullptr);
    QString operation() const { return operation_; }
    QString selectedFile() const { return file_; }
  private:
    void choose(const QString& operation);
    QLineEdit* name_;
    QListWidget* recent_;
    QString operation_,file_;
};
class LoadingScreen final : public QSplashScreen {
  public:
    LoadingScreen();
    void progress(int value,const QString& stage);
  protected:
    void drawContents(QPainter*) override;
  private:
    int progress_{};
    QString stage_;
};
} // namespace levelcad::app
