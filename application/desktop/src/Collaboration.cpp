#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QTimer>
#include <levelcad/app/BoardIO.hpp>
#include <levelcad/app/Collaboration.hpp>
#include <levelcad/app/DocumentIO.hpp>

namespace levelcad::app {
namespace {
constexpr qsizetype maximumMessage = 64 * 1024 * 1024;

void send(QTcpSocket* socket, const QJsonObject& message) {
    if (socket->bytesToWrite() > maximumMessage) {
        socket->abort();
        return;
    }
    socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

using Objects = QMap<QString, QJsonObject>;

Objects objects(const QJsonArray& array) {
    Objects result;
    for (const auto& value : array) {
        auto item = value.toObject();
        result[item["id"].toString()] = item;
    }
    return result;
}

bool saveJson(const QString& path, const QJsonObject& object) {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
} // namespace

bool mergeEdits(const QJsonObject& base, const QJsonObject& edited, QJsonObject& current,
                QJsonArray& audit, QString& error) {
    auto candidate = current;
    QJsonArray changes;
    for (const auto& collection : {QString("symbols"), QString("wires"), QString("notes")}) {
        auto before = objects(base[collection].toArray()),
             after = objects(edited[collection].toArray());
        auto latest = objects(candidate[collection].toArray());
        auto conflict = [&](const QString& id, const QString& field) {
            error = QString::fromUtf8("Conflito no objeto %1, campo %2. Sua edicao foi preservada.")
                        .arg(id, field);
            return false;
        };
        for (auto it = before.begin(); it != before.end(); ++it) {
            const auto id = it.key();
            if (!after.contains(id)) {
                if (!latest.contains(id) || latest[id] != it.value())
                    return conflict(id, "excluir");
                changes.append(QJsonObject{{"collection", collection},
                                           {"id", id},
                                           {"field", "excluir"},
                                           {"before", it.value()}});
                latest.remove(id);
            }
        }
        for (auto it = after.begin(); it != after.end(); ++it) {
            const auto id = it.key();
            if (!before.contains(id)) {
                if (latest.contains(id))
                    return conflict(id, "criar");
                latest[id] = it.value();
                changes.append(QJsonObject{{"collection", collection},
                                           {"id", id},
                                           {"field", "criar"},
                                           {"after", it.value()}});
                continue;
            }
            const auto original = before[id], modified = it.value();
            if (original == modified)
                continue;
            if (!latest.contains(id))
                return conflict(id, "objeto excluido");
            auto fields = original.keys();
            for (const auto& key : modified.keys())
                if (!fields.contains(key))
                    fields.append(key);
            for (const auto& field : fields) {
                if (original[field] == modified[field])
                    continue;
                if (latest[id][field] != original[field])
                    return conflict(id, field);
                auto item = latest[id];
                if (modified.contains(field))
                    item[field] = modified[field];
                else
                    item.remove(field);
                latest[id] = item;
                changes.append(QJsonObject{{"collection", collection},
                                           {"id", id},
                                           {"field", field},
                                           {"before", original[field]},
                                           {"after", modified[field]}});
            }
        }
        QJsonArray array;
        for (const auto& item : latest)
            array.append(item);
        candidate[collection] = array;
    }
    if (edited["pcb"] != base["pcb"]) {
        if (current["pcb"] != base["pcb"]) {
            error = "Conflito no layout PCB; sua edicao foi preservada.";
            return false;
        }
        pcb::Board validation;
        if (!boardFromJson(validation, edited["pcb"].toObject(), &error))
            return false;
        candidate["pcb"] = edited["pcb"];
        changes.append(QJsonObject{{"collection", "pcb"},
                                   {"id", "board"},
                                   {"field", "layout"},
                                   {"before", base["pcb"]},
                                   {"after", edited["pcb"]}});
    }
    core::Document validation;
    if (!documentFromJson(validation, candidate, &error))
        return false;
    current = candidate;
    audit = changes;
    return true;
}

CollaborationServer::CollaborationServer(const QString& directory, QObject* parent,
                                         const QString& adminToken)
    : QObject(parent), directory_(directory), adminToken_(adminToken) {
    QDir().mkpath(directory_);
    QFile bans(directory_ + "/blocked.json");
    if (bans.open(QIODevice::ReadOnly))
        for (auto value : QJsonDocument::fromJson(bans.readAll()).object()["addresses"].toArray())
            banned_.insert(value.toString());
    core::Document document;
    QString error;
    loadDocument(document, directory_ + "/project.levelsch", &error);
    state_ = documentToJson(document);
    QFile project(directory_ + "/project.levelsch");
    if (project.open(QIODevice::ReadOnly)) {
        auto root = QJsonDocument::fromJson(project.readAll()).object();
        pcb::Board board;
        if (root.contains("pcb") && boardFromJson(board, root["pcb"].toObject(), &error))
            state_["pcb"] = root["pcb"];
    }
    for (const auto& collection : {"symbols", "wires", "notes"})
        for (const auto& item : state_[collection].toArray())
            nextActor_ = qMax(nextActor_,
                              item.toObject()["id"].toString().toULongLong() / 1000000000ULL + 1);
    connect(&listener_, &QTcpServer::newConnection, this, [this] {
        while (auto* socket = listener_.nextPendingConnection()) {
            peers_.insert(socket, {});
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                const auto departed = peers_.value(socket);
                for (auto it = peers_.begin(); it != peers_.end(); ++it)
                    if (it.key() != socket && it->ready)
                        send(it.key(), {{"type", "presence"},
                                        {"actor", QString::number(departed.actor)},
                                        {"name", departed.name},
                                        {"workspace", ""},
                                        {"ids", QJsonArray{}}});
                peers_.remove(socket);
                sendRoster();
                socket->deleteLater();
            });
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                auto& buffer = peers_[socket].buffer;
                buffer += socket->readAll();
                if (buffer.size() > maximumMessage) {
                    socket->abort();
                    return;
                }
                while (buffer.contains('\n')) {
                    auto line = buffer.left(buffer.indexOf('\n'));
                    buffer.remove(0, line.size() + 1);
                    QJsonParseError error;
                    auto json = QJsonDocument::fromJson(line, &error);
                    if (error.error != QJsonParseError::NoError || !json.isObject()) {
                        socket->abort();
                        return;
                    }
                    receive(socket, json.object());
                }
            });
            QTimer::singleShot(10000, socket, [this, socket] {
                if (!peers_.value(socket).ready)
                    socket->abort();
            });
        }
    });
}

CollaborationServer::~CollaborationServer() {
    QObject::disconnect(&listener_, nullptr, this, nullptr);
    for (auto* socket : peers_.keys()) {
        QObject::disconnect(socket, nullptr, this, nullptr);
        socket->abort();
    }
}

bool CollaborationServer::listen(quint16 port) {
    return listener_.listen(QHostAddress::AnyIPv4, port);
}

void CollaborationServer::persist(const QJsonArray& audit) {
    saveJson(directory_ + "/project.levelsch", state_);
    QFile file(directory_ + "/edits.jsonl");
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        for (const auto& entry : audit)
            file.write(QJsonDocument(entry.toObject()).toJson(QJsonDocument::Compact) + '\n');
}

void CollaborationServer::receive(QTcpSocket* socket, const QJsonObject& message) {
    auto& peer = peers_[socket];
    if (!peer.ready) {
        if (message["type"] != "hello" || message["protocol"].toInt() != 1) {
            socket->abort();
            return;
        }
        peer.admin = !adminToken_.isEmpty() && message["adminToken"].toString() == adminToken_ &&
                     socket->peerAddress().isLoopback();
        if (banned_.contains(socket->peerAddress().toString()) && !peer.admin) {
            send(socket, {{"type", "denied"}, {"message", "Acesso bloqueado pelo anfitriao."}});
            socket->disconnectFromHost();
            return;
        }
        peer.name = message["name"].toString().trimmed().left(80);
        if (peer.name.isEmpty()) {
            socket->abort();
            return;
        }
        peer.actor = nextActor_++;
        if (peer.actor > 1000000000ULL) {
            socket->abort();
            return;
        }
        peer.ready = true;
        // A primeira pessoa inicializa uma sala vazia. Depois disso, vale a cópia do servidor.
        if (revision_ == 0 && state_["symbols"].toArray().isEmpty() &&
            state_["wires"].toArray().isEmpty() && state_["notes"].toArray().isEmpty()) {
            core::Document validation;
            pcb::Board board;
            QString error;
            auto initial = message["document"].toObject();
            if (documentFromJson(validation, initial, &error) &&
                (!initial.contains("pcb") ||
                 boardFromJson(board, initial["pcb"].toObject(), &error))) {
                state_ = initial;
                ++revision_;
                persist({});
                for (const auto& collection : {"symbols", "wires", "notes"})
                    for (const auto& item : state_[collection].toArray())
                        nextActor_ = qMax(
                            nextActor_,
                            item.toObject()["id"].toString().toULongLong() / 1000000000ULL + 1);
                if (nextActor_ > peer.actor + 1)
                    peer.actor = nextActor_++;
            }
        }
        send(socket, {{"type", "welcome"},
                      {"actor", QString::number(peer.actor)},
                      {"revision", QString::number(revision_)},
                      {"document", state_}});
        sendRoster();
        for (auto it = peers_.begin(); it != peers_.end(); ++it)
            if (it.key() != socket && it->ready && !it->presence.isEmpty())
                send(socket, it->presence);
        return;
    }
    if (message["type"] == "admin") {
        if (!peer.admin) {
            send(socket, {{"type", "denied"},
                          {"message", "Somente o anfitriao pode administrar participantes."}});
            return;
        }
        auto action = message["action"].toString();
        if (action == "unban") {
            banned_.remove(message["address"].toString());
        } else if (action == "kick" || action == "ban") {
            auto target = message["target"].toString().toULongLong();
            for (auto* client : peers_.keys())
                if (peers_.value(client).actor == target && !peers_.value(client).admin) {
                    if (action == "ban")
                        banned_.insert(client->peerAddress().toString());
                    send(client,
                         {{"type", "denied"},
                          {"message", action == "ban" ? "Acesso bloqueado pelo anfitriao."
                                                      : "Voce foi removido pelo anfitriao."}});
                    client->disconnectFromHost();
                    break;
                }
        }
        QJsonArray addresses;
        for (auto address : banned_)
            addresses.append(address);
        saveJson(directory_ + "/blocked.json", {{"addresses", addresses}});
        sendRoster();
        return;
    }
    if (message["type"] == "presence") {
        auto ids = message["ids"].toArray();
        if (ids.size() > 128)
            return;
        auto workspace = message["workspace"].toString();
        if (workspace != "schematic" && workspace != "pcb")
            return;
        peer.presence = {{"type", "presence"},
                         {"actor", QString::number(peer.actor)},
                         {"name", peer.name},
                         {"workspace", workspace},
                         {"ids", ids}};
        for (auto it = peers_.begin(); it != peers_.end(); ++it)
            if (it->ready)
                send(it.key(), peer.presence);
        return;
    }
    if (message["type"] != "edit")
        return;
    QJsonArray audit;
    QString error;
    auto base = message["base"].toObject(), edited = message["document"].toObject();
    core::Document validation;
    bool valid =
        documentFromJson(validation, base, &error) && documentFromJson(validation, edited, &error);
    for (const auto& collection : {"symbols", "wires", "notes"}) {
        auto old = objects(base[collection].toArray());
        for (const auto& item : edited[collection].toArray()) {
            auto id = item.toObject()["id"].toString();
            if (!old.contains(id) && id.toULongLong() / 1000000000ULL != peer.actor) {
                valid = false;
                error = "ID fora da sessao";
            }
        }
    }
    if (!valid || !mergeEdits(base, edited, state_, audit, error)) {
        send(socket, {{"type", "conflict"}, {"message", error}, {"document", state_}});
        return;
    }
    ++revision_;
    QJsonArray entries;
    for (auto entry : audit) {
        auto object = entry.toObject();
        object["author"] = peer.name;
        object["session"] = QString::number(peer.actor);
        object["time"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        object["revision"] = QString::number(revision_);
        entries.append(object);
    }
    persist(entries);
    for (auto it = peers_.begin(); it != peers_.end(); ++it)
        if (it->ready)
            send(it.key(), {{"type", "state"},
                            {"actor", QString::number(peer.actor)},
                            {"document", state_},
                            {"audit", entries}});
}

CollaborationClient::CollaborationClient(QObject* parent) : QObject(parent) {
    connect(&socket_, &QTcpSocket::connected, this, [this] {
        send(&socket_, {{"type", "hello"},
                        {"protocol", 1},
                        {"name", name_},
                        {"document", initial_},
                        {"adminToken", adminToken}});
    });
    connect(&socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (onStatus)
            onStatus(socket_.errorString());
    });
    connect(&socket_, &QTcpSocket::disconnected, this, [this] {
        ready_ = pending_ = false;
        if (onBusy)
            onBusy(false);
        if (onStatus)
            onStatus("Desconectado. As edicoes locais continuam disponiveis.");
    });
    connect(&socket_, &QTcpSocket::readyRead, this, [this] {
        buffer_ += socket_.readAll();
        if (buffer_.size() > maximumMessage) {
            socket_.abort();
            return;
        }
        while (buffer_.contains('\n')) {
            auto line = buffer_.left(buffer_.indexOf('\n'));
            buffer_.remove(0, line.size() + 1);
            auto message = QJsonDocument::fromJson(line).object();
            const auto type = message["type"].toString();
            if (type == "roster") {
                if (onRoster)
                    onRoster(message);
            } else if (type == "denied") {
                if (onStatus)
                    onStatus(message["message"].toString());
            } else if (type == "presence") {
                if (message["actor"].toString().toULongLong() != actor_ && onPresence)
                    onPresence(message);
            } else if (type == "welcome") {
                actor_ = message["actor"].toString().toULongLong();
                ready_ = true;
                baseline_ = message["document"].toObject();
                if (onState)
                    onState(baseline_, true);
                if (onStatus)
                    onStatus("Colaboracao conectada: " + name_);
            } else if (type == "conflict") {
                auto path = QDir::tempPath() + "/LevelCAD-conflict-" + QString::number(actor_) +
                            ".levelsch";
                saveJson(path, submitted_);
                pending_ = false;
                baseline_ = message["document"].toObject();
                if (onState)
                    onState(baseline_, false);
                if (onBusy)
                    onBusy(false);
                if (onStatus)
                    onStatus(message["message"].toString() + " / " + path);
            } else if (type == "state") {
                baseline_ = message["document"].toObject();
                if (message["actor"].toString().toULongLong() == actor_) {
                    pending_ = false;
                    if (onBusy)
                        onBusy(false);
                }
                if (!pending_ && onState)
                    onState(baseline_, false);
                for (const auto& entry : message["audit"].toArray())
                    if (onAudit)
                        onAudit(entry.toObject());
            }
        }
    });
}

CollaborationClient::~CollaborationClient() {
    QObject::disconnect(&socket_, nullptr, this, nullptr);
    socket_.abort();
}

void CollaborationClient::join(const QString& host, quint16 port, const QString& name,
                               const QJsonObject& initial) {
    socket_.abort();
    buffer_.clear();
    ready_ = pending_ = false;
    initial_ = initial;
    name_ = name;
    socket_.connectToHost(host, port);
}

void CollaborationClient::disconnect() {
    socket_.disconnectFromHost();
}

void CollaborationClient::submit(const QJsonObject& edited) {
    if (!ready_ || pending_ || edited == baseline_)
        return;
    submitted_ = edited;
    pending_ = true;
    send(&socket_, {{"type", "edit"}, {"base", baseline_}, {"document", edited}});
    if (onBusy)
        onBusy(true);
}
} // namespace levelcad::app

namespace levelcad::app {
void CollaborationClient::publishSelection(const QString& workspace, const QJsonArray& ids) {
    if (ready_)
        send(&socket_, {{"type", "presence"}, {"workspace", workspace}, {"ids", ids}});
}
} // namespace levelcad::app

namespace levelcad::app {
void CollaborationServer::sendRoster() {
    for (auto recipient = peers_.begin(); recipient != peers_.end(); ++recipient)
        if (recipient->ready) {
            QJsonArray participants;
            for (auto entry = peers_.begin(); entry != peers_.end(); ++entry)
                if (entry->ready)
                    participants.append(
                        QJsonObject{{"actor", QString::number(entry->actor)},
                                    {"name", entry->name},
                                    {"host", entry->admin},
                                    {"ip", recipient->admin ? entry.key()->peerAddress().toString()
                                                            : QString()}});
            QJsonArray addresses;
            if (recipient->admin)
                for (auto address : banned_)
                    addresses.append(address);
            send(recipient.key(), {{"type", "roster"},
                                   {"participants", participants},
                                   {"banned", addresses},
                                   {"admin", recipient->admin}});
        }
}

void CollaborationClient::adminAction(const QString& action, const QString& target,
                                      const QString& address) {
    if (ready_)
        send(&socket_,
             {{"type", "admin"}, {"action", action}, {"target", target}, {"address", address}});
}
} // namespace levelcad::app
