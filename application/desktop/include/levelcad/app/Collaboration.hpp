#pragma once
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <functional>

namespace levelcad::app {
// O servidor é a autoridade; cada campo só muda se a versão anterior ainda coincidir.
bool mergeEdits(const QJsonObject& base, const QJsonObject& edited, QJsonObject& current,
                QJsonArray& audit, QString& error);

class CollaborationServer : public QObject {
  public:
    explicit CollaborationServer(const QString& directory, QObject* parent = nullptr,
                                 const QString& adminToken = {});
    ~CollaborationServer() override;
    bool listen(quint16 port);

    quint16 port() const {
        return listener_.serverPort();
    }

  private:
    struct Peer {
        QByteArray buffer;
        QString name;
        quint64 actor{};
        bool ready{};
        QJsonObject presence;
        bool admin{};
    };

    QTcpServer listener_;
    QHash<QTcpSocket*, Peer> peers_;
    QJsonObject state_;
    quint64 revision_{}, nextActor_{1};
    QString directory_;
    void receive(QTcpSocket* socket, const QJsonObject& message);
    void persist(const QJsonArray& audit);
    void sendRoster();
    QString adminToken_;
    QSet<QString> banned_;
};

class CollaborationClient : public QObject {
  public:
    explicit CollaborationClient(QObject* parent = nullptr);
    ~CollaborationClient() override;
    void join(const QString& host, quint16 port, const QString& name, const QJsonObject& initial);
    void disconnect();
    void publishSelection(const QString& workspace, const QJsonArray& ids);
    void adminAction(const QString& action, const QString& target = {},
                     const QString& address = {});
    QString adminToken;
    void submit(const QJsonObject& edited);

    bool active() const {
        return ready_;
    }

    quint64 idFloor() const {
        return actor_ * 1000000000ULL;
    }

    std::function<void(const QJsonObject&, bool)> onState;
    std::function<void(const QJsonObject&)> onAudit;
    std::function<void(const QJsonObject&)> onPresence;
    std::function<void(const QJsonObject&)> onRoster;
    std::function<void(const QString&)> onStatus;
    std::function<void(bool)> onBusy;

  private:
    QTcpSocket socket_;
    QByteArray buffer_;
    QJsonObject baseline_, initial_, submitted_;
    bool ready_{}, pending_{};
    quint64 actor_{};
    QString name_;
};
} // namespace levelcad::app
