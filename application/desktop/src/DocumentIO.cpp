#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <cmath>
#include <levelcad/app/BoardIO.hpp>
#include <levelcad/app/DocumentIO.hpp>
#include <stdexcept>

namespace levelcad::app {
namespace {
bool coordinate(const QJsonValue& value, core::Coord& result) {
    bool ok = false;
    if (!value.isString())
        return false;
    result = value.toString().toLongLong(&ok);
    return ok;
}

QJsonObject encodePoint(core::Point point) {
    return {{"x", QString::number(point.x)}, {"y", QString::number(point.y)}};
}

core::Point decodePoint(const QJsonObject& object) {
    core::Point point;
    if (!coordinate(object["x"], point.x) || !coordinate(object["y"], point.y))
        throw std::runtime_error("Invalid point coordinates");
    return point;
}
} // namespace

QJsonObject documentToJson(const core::Document& document) {
    QJsonArray symbols;
    for (const auto& symbol : document.symbols()) {
        auto object = encodePoint(symbol.origin);
        object["id"] = QString::number(symbol.id);
        object["kind"] = static_cast<int>(symbol.kind);
        object["rotation"] = symbol.rotation;
        object["reference"] = QString::fromStdString(symbol.reference);
        object["value"] = QString::fromStdString(symbol.value);
        if (symbol.kind == core::SymbolKind::Library) {
            object["libraryId"] = QString::fromStdString(symbol.libraryId);
            object["footprint"] = QString::fromStdString(symbol.footprint);
            object["datasheet"] = QString::fromStdString(symbol.datasheet);
            object["spiceType"] = QString::fromStdString(symbol.spiceType);
            QJsonArray pins, strokes, order;
            for (const auto& pin : symbol.pinDefinitions) {
                auto definition = encodePoint(pin.position);
                definition["number"] = pin.number;
                definition["name"] = QString::fromStdString(pin.name);
                definition["electricalType"] = QString::fromStdString(pin.electricalType);
                pins.append(definition);
            }
            for (const auto& stroke : symbol.strokes) {
                QJsonArray points;
                for (const auto& point : stroke)
                    points.append(encodePoint(point));
                strokes.append(points);
            }
            for (const auto number : symbol.spicePinOrder)
                order.append(number);
            object["pins"] = pins;
            object["strokes"] = strokes;
            object["spicePinOrder"] = order;
        }
        symbols.append(object);
    }
    QJsonArray wires;
    for (const auto& wire : document.wires()) {
        wires.append(QJsonObject{{"id", QString::number(wire.id)},
                                 {"x1", QString::number(wire.from.x)},
                                 {"y1", QString::number(wire.from.y)},
                                 {"x2", QString::number(wire.to.x)},
                                 {"y2", QString::number(wire.to.y)}});
    }
    QJsonArray notes;
    for (const auto& note : document.notes()) {
        auto object = encodePoint(note.position);
        object["id"] = QString::number(note.id);
        object["text"] = QString::fromStdString(note.text);
        object["author"] = QString::fromStdString(note.author);
        object["electrical"] = note.electrical;
        object["background"] = note.background;
        object["widthMm"] = note.widthMm;
        object["heightMm"] = note.heightMm;
        object["color"] = QString::fromStdString(note.color);
        notes.append(object);
    }
    return {{"format", "levelcad-schematic"},
            {"version", 4},
            {"author", "Wenderson Dias"},
            {"publisher", "The Archlevel Foundation"},
            {"symbols", symbols},
            {"wires", wires},
            {"notes", notes}};
}

bool saveDocument(const core::Document& document, const QString& fileName, QString* error,
                  pcb::Board* board) {
    auto root = documentToJson(document);
    if (board)
        root["pcb"] = boardToJson(*board);
    const auto bytes = QJsonDocument(root).toJson();
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool loadDocument(core::Document& document, const QString& fileName, QString* error,
                  pcb::Board* board) {
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    try {
        if (file.size() > 64 * 1024 * 1024)
            throw std::runtime_error("Schematic is larger than 64 MB");
        QJsonParseError parseError;
        const auto json = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !json.isObject())
            throw std::runtime_error("Invalid JSON");
        auto candidate = document;
        pcb::Board candidateBoard;
        if (board && json.object().contains("pcb") &&
            !boardFromJson(candidateBoard, json.object()["pcb"].toObject(), error))
            return false;
        if (!documentFromJson(candidate, json.object(), error))
            return false;
        document = std::move(candidate);
        if (board)
            *board = std::move(candidateBoard);
        return true;
    } catch (const std::exception& exception) {
        if (error)
            *error = QString::fromUtf8(exception.what());
        return false;
    }
}

bool documentFromJson(core::Document& document, const QJsonObject& root, QString* error) {
    try {
        const int version = root["version"].toInt(-1);
        if (root["format"].toString() != "levelcad-schematic" || version < 1 || version > 4 ||
            !root["symbols"].isArray() || !root["wires"].isArray())
            throw std::runtime_error("Unsupported document format");
        std::vector<core::Symbol> symbols;
        std::vector<core::Wire> wires;
        for (const auto& value : root["symbols"].toArray()) {
            if (!value.isObject())
                throw std::runtime_error("Invalid symbol");
            const auto object = value.toObject();
            core::Symbol symbol;
            bool ok = false;
            symbol.id = object["id"].toString().toULongLong(&ok);
            const int kind = object["kind"].toInt(-1);
            if (!ok || kind < 0 || kind > (version >= 3 ? 10 : version == 2 ? 9 : 2))
                throw std::runtime_error("Invalid symbol data");
            symbol.kind = static_cast<core::SymbolKind>(kind);
            symbol.origin = decodePoint(object);
            symbol.rotation = object["rotation"].toInt();
            symbol.reference = object["reference"].toString().toStdString();
            symbol.value = object["value"].toString().toStdString();
            if (symbol.kind == core::SymbolKind::Library) {
                if (!object["pins"].isArray() || !object["strokes"].isArray())
                    throw std::runtime_error("Library symbol has no embedded definition");
                symbol.libraryId = object["libraryId"].toString().toStdString();
                symbol.footprint = object["footprint"].toString().toStdString();
                symbol.datasheet = object["datasheet"].toString().toStdString();
                symbol.spiceType = object["spiceType"].toString().toStdString();
                for (const auto& pin : object["pins"].toArray()) {
                    if (!pin.isObject())
                        throw std::runtime_error("Invalid pin definition");
                    const auto definition = pin.toObject();
                    symbol.pinDefinitions.push_back(
                        {definition["number"].toInt(-1),
                         definition["name"].toString().toStdString(),
                         definition["electricalType"].toString().toStdString(),
                         decodePoint(definition)});
                }
                for (const auto& stroke : object["strokes"].toArray()) {
                    if (!stroke.isArray())
                        throw std::runtime_error("Invalid symbol stroke");
                    std::vector<core::Point> points;
                    for (const auto& point : stroke.toArray()) {
                        if (!point.isObject())
                            throw std::runtime_error("Invalid stroke point");
                        points.push_back(decodePoint(point.toObject()));
                    }
                    symbol.strokes.push_back(std::move(points));
                }
                for (const auto& number : object["spicePinOrder"].toArray())
                    symbol.spicePinOrder.push_back(number.toInt(-1));
            }
            symbols.push_back(std::move(symbol));
        }
        for (const auto& value : root["wires"].toArray()) {
            if (!value.isObject())
                throw std::runtime_error("Invalid wire");
            const auto object = value.toObject();
            core::Wire wire;
            bool ok = false;
            wire.id = object["id"].toString().toULongLong(&ok);
            if (!ok || !coordinate(object["x1"], wire.from.x) ||
                !coordinate(object["y1"], wire.from.y) || !coordinate(object["x2"], wire.to.x) ||
                !coordinate(object["y2"], wire.to.y))
                throw std::runtime_error("Invalid wire data");
            wires.push_back(wire);
        }
        // Valide tudo antes de trocar o documento: um arquivo ruim não apaga o trabalho aberto.
        std::vector<core::Note> notes;
        for (const auto& value : root["notes"].toArray()) {
            const auto object = value.toObject();
            bool ok = false;
            auto id = object["id"].toString().toULongLong(&ok);
            if (!ok)
                throw std::runtime_error("Invalid comment ID");
            notes.push_back({id, decodePoint(object), object["text"].toString().toStdString(),
                             object["author"].toString().toStdString(),
                             object["electrical"].toBool()});
            auto& note = notes.back();
            note.background = object["background"].toBool();
            note.widthMm = object["widthMm"].toDouble(40.0);
            note.heightMm = object["heightMm"].toDouble(30.0);
            note.color = object["color"].toString("#4b8096").toStdString();
            if (!std::isfinite(note.widthMm) || !std::isfinite(note.heightMm) || note.widthMm < 1 ||
                note.heightMm < 1 || note.widthMm > 1000 || note.heightMm > 1000 ||
                !QRegularExpression("^#[0-9a-fA-F]{6}$")
                     .match(QString::fromStdString(note.color))
                     .hasMatch() ||
                (note.background && note.electrical))
                throw std::runtime_error("Invalid schematic area: " + std::to_string(note.widthMm) + "/" + std::to_string(note.heightMm) + "/" + note.color + "/" + std::to_string(note.electrical));
        }
        document.load(std::move(symbols), std::move(wires), std::move(notes));
        return true;
    } catch (const std::exception& exception) {
        if (error)
            *error = QString::fromUtf8(exception.what());
        return false;
    }
}
} // namespace levelcad::app
