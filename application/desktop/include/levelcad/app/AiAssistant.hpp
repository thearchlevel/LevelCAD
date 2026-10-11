#pragma once
#include <QJsonObject>
#include <QPointer>
#include <QWidget>
#include <cstdint>
#include <functional>
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;

namespace levelcad::app {
QJsonObject aiRequest(const QString& provider, const QString& model, const QString& prompt,
                      const QJsonObject& context);
QJsonObject aiProposal(const QString& provider, const QJsonObject& response);

class AiAssistant final : public QWidget {
  public:
    explicit AiAssistant(QWidget* parent = nullptr);
    std::function<QJsonObject()> context;
    std::function<std::uint64_t()> revision;
    std::function<void(const QJsonObject&, std::uint64_t)> onProposal;

  private:
    void send();
    QComboBox* provider_;
    QLineEdit *model_, *key_;
    QPlainTextEdit *prompt_, *output_;
    QPushButton *send_, *cancel_, *review_;
    QLabel* status_;
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> reply_;
    QJsonObject proposal_;
    std::uint64_t proposedRevision_{};
};
} // namespace levelcad::app
