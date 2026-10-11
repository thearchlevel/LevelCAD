#include <QJsonArray>
#include <QRegularExpression>
#include <QtEndian>
#include <cmath>
#include <levelcad/app/BoardIO.hpp>
#include <set>
#include <stdexcept>

namespace levelcad::app {
namespace {
QString text(const std::string& value) {
    return QString::fromStdString(value);
}

QJsonObject pointJson(core::Point point) {
    return {{"x", QString::number(point.x)}, {"y", QString::number(point.y)}};
}

core::Point readPoint(const QJsonObject& point) {
    bool first = false, second = false;
    auto x = point["x"].toString().toLongLong(&first),
         y = point["y"].toString().toLongLong(&second);
    if (!first || !second)
        throw std::runtime_error("Invalid PCB point");
    return {x, y};
}
} // namespace

QJsonObject boardToJson(const pcb::Board& board) {
    QJsonArray footprints, tracks, vias;
    for (const auto& footprint : board.footprints) {
        auto object = pointJson(footprint.position);
        object["symbolId"] = QString::number(footprint.symbolId);
        object["reference"] = text(footprint.reference);
        object["value"] = text(footprint.value);
        object["source"] = text(footprint.source);
        object["rotation"] = footprint.rotation;
        object["width"] = QString::number(footprint.width);
        object["length"] = QString::number(footprint.length);
        object["height"] = QString::number(footprint.height);
        QJsonArray pads;
        for (const auto& pad : footprint.pads) {
            auto value = pointJson(pad.offset);
            value["number"] = pad.number;
            value["diameter"] = QString::number(pad.diameter);
            value["drill"] = QString::number(pad.drill);
            value["net"] = text(pad.net);
            value["shape"] = text(pad.shape);
            value["sizeX"] = QString::number(pad.sizeX);
            value["sizeY"] = QString::number(pad.sizeY);
            value["rotation"] = pad.rotation;
            value["cornerRatio"] = pad.cornerRatio;
            value["layerMask"] = QString::number(pad.layerMask);
            pads.append(value);
        }
        object["pads"] = pads;
        object["modelName"] = text(footprint.modelName);
        object["modelOffsetX"] = footprint.modelOffsetX;
        object["modelOffsetY"] = footprint.modelOffsetY;
        object["modelOffsetZ"] = footprint.modelOffsetZ;
        object["modelRotationX"] = footprint.modelRotationX;
        object["modelRotationY"] = footprint.modelRotationY;
        object["modelRotationZ"] = footprint.modelRotationZ;
        object["modelScale"] = footprint.modelScale;
        object["bodyColor"] = text(footprint.bodyColor);
        object["texturePng"] = text(footprint.texturePng);
        QJsonArray mesh;
        for (const auto& triangle : footprint.mesh) {
            QJsonArray vertices;
            for (const auto& vertex : triangle)
                vertices.append(
                    QJsonArray{vertex.x, vertex.y, vertex.z, static_cast<double>(vertex.color)});
            mesh.append(vertices);
        }
        object["mesh"] = mesh;
        footprints.append(object);
    }
    for (const auto& track : board.tracks)
        tracks.append(QJsonObject{{"id", QString::number(track.id)},
                                  {"from", pointJson(track.from)},
                                  {"to", pointJson(track.to)},
                                  {"layer", track.layer},
                                  {"width", QString::number(track.width)},
                                  {"net", text(track.net)}});
    for (const auto& via : board.vias) {
        auto object = pointJson(via.position);
        object["id"] = QString::number(via.id);
        object["net"] = text(via.net);
        object["diameter"] = QString::number(via.diameter);
        object["drill"] = QString::number(via.drill);
        vias.append(object);
    }
    QJsonArray layers, outline;
    for (const auto& layer : board.layers)
        layers.append(QJsonObject{{"index", layer.index},
                                  {"name", text(layer.name)},
                                  {"color", text(layer.color)},
                                  {"visible", layer.visible}});
    for (const auto& stroke : board.outline) {
        QJsonArray points;
        for (auto point : stroke)
            points.append(pointJson(point));
        outline.append(points);
    }
    return QJsonObject{{"projectName", text(board.projectName)},
                       {"revision", text(board.revision)},
                       {"creator", text(board.creator)},
                       {"solderMask", text(board.solderMask)},
                       {"silkColor", text(board.silkColor)},
                       {"finishColor", text(board.finishColor)},
                       {"showAirwires", board.showAirwires},
                       {"showReferences", board.showReferences},
                       {"showPadNumbers", board.showPadNumbers},
                       {"layers", layers},
                       {"outline", outline},
                       {"format", "levelcad-pcb"},
                       {"version", 1},
                       {"width", QString::number(board.width)},
                       {"height", QString::number(board.height)},
                       {"thickness", QString::number(board.thickness)},
                       {"clearance", QString::number(board.clearance)},
                       {"footprints", footprints},
                       {"tracks", tracks},
                       {"vias", vias}};
}

bool boardFromJson(pcb::Board& destination, const QJsonObject& root, QString* error) {
    try {
        if (root["format"] != "levelcad-pcb" || root["version"].toInt() != 1 ||
            !root["footprints"].isArray() || !root["tracks"].isArray() || !root["vias"].isArray())
            throw std::runtime_error("Invalid PCB format");
        auto dimension = [](const QJsonValue& value) {
            bool ok = false;
            auto size = value.toString().toLongLong(&ok);
            if (!ok || size <= 0 || size > 500000000)
                throw std::runtime_error("Invalid PCB dimension");
            return size;
        };
        pcb::Board board;
        auto color = [](const QJsonValue& value, const std::string& fallback) {
            if (value.isUndefined())
                return fallback;
            auto string = value.toString();
            if (!QRegularExpression("^#[0-9a-fA-F]{6}$").match(string).hasMatch())
                throw std::runtime_error("Invalid material color");
            return string.toStdString();
        };
        board.projectName = root["projectName"].toString("Untitled").toStdString();
        board.revision = root["revision"].toString("1.0").toStdString();
        board.creator = root["creator"].toString().toStdString();
        board.solderMask = color(root["solderMask"], board.solderMask);
        board.silkColor = color(root["silkColor"], board.silkColor);
        board.finishColor = color(root["finishColor"], board.finishColor);
        board.showAirwires = root["showAirwires"].toBool(true);
        board.showReferences = root["showReferences"].toBool(true);
        board.showPadNumbers = root["showPadNumbers"].toBool(true);
        board.width = dimension(root["width"]);
        board.height = dimension(root["height"]);
        board.thickness = dimension(root["thickness"]);
        board.clearance = dimension(root["clearance"]);
        if (root.contains("layers")) {
            board.layers.clear();
            std::set<int> indices;
            for (auto value : root["layers"].toArray()) {
                auto layer = value.toObject();
                auto index = layer["index"].toInt(-1);
                if (index < 0 || index > 31 || !indices.insert(index).second)
                    throw std::runtime_error("Invalid copper layer");
                board.layers.push_back({index, layer["name"].toString().toStdString(),
                                        layer["color"].toString().toStdString(),
                                        layer["visible"].toBool(true)});
            }
            if (board.layers.empty())
                throw std::runtime_error("Board has no copper layers");
        }
        for (auto value : root["outline"].toArray()) {
            std::vector<core::Point> stroke;
            for (auto point : value.toArray())
                stroke.push_back(readPoint(point.toObject()));
            board.outline.push_back(stroke);
        }
        for (const auto& value : root["footprints"].toArray()) {
            const auto item = value.toObject();
            pcb::Footprint footprint;
            bool ok = false;
            footprint.symbolId = item["symbolId"].toString().toULongLong(&ok);
            if (!ok || !footprint.symbolId)
                throw std::runtime_error("Invalid footprint ID");
            footprint.position = readPoint(item);
            footprint.reference = item["reference"].toString().toStdString();
            footprint.value = item["value"].toString().toStdString();
            footprint.source = item["source"].toString().toStdString();
            footprint.rotation = item["rotation"].toDouble();
            if (!std::isfinite(footprint.rotation))
                throw std::runtime_error("Invalid footprint rotation");
            footprint.width = dimension(item["width"]);
            footprint.length = dimension(item["length"]);
            footprint.height = dimension(item["height"]);
            for (const auto& pin : item["pads"].toArray()) {
                const auto pad = pin.toObject();
                footprint.pads.push_back(
                    {pad["number"].toInt(), readPoint(pad), dimension(pad["diameter"]),
                     pad["drill"].toString() == "0" ? 0 : dimension(pad["drill"]),
                     pad["net"].toString().toStdString()});
            }
            for (std::size_t index = 0; index < footprint.pads.size(); ++index) {
                auto data = item["pads"].toArray()[static_cast<qsizetype>(index)].toObject();
                auto& pad = footprint.pads[index];
                pad.shape = data["shape"].toString("circle").toStdString();
                pad.sizeX = data["sizeX"].toString().toLongLong();
                pad.sizeY = data["sizeY"].toString().toLongLong();
                pad.rotation = data["rotation"].toDouble();
                pad.cornerRatio = data["cornerRatio"].toDouble(.25);
                if (!std::isfinite(pad.cornerRatio) || pad.cornerRatio < 0 || pad.cornerRatio > .5)
                    throw std::runtime_error("Invalid pad corner ratio");
                if (data.contains("layerMask"))
                    pad.layerMask = data["layerMask"].toString().toUInt();
            }
            footprint.modelName = item["modelName"].toString().toStdString();
            footprint.modelOffsetX = item["modelOffsetX"].toDouble();
            footprint.modelOffsetY = item["modelOffsetY"].toDouble();
            footprint.modelOffsetZ = item["modelOffsetZ"].toDouble();
            footprint.modelRotationX = item["modelRotationX"].toDouble();
            footprint.modelRotationY = item["modelRotationY"].toDouble();
            footprint.modelRotationZ = item["modelRotationZ"].toDouble();
            footprint.modelScale = item["modelScale"].toDouble(1);
            footprint.bodyColor = color(item["bodyColor"], footprint.bodyColor);
            footprint.texturePng = item["texturePng"].toString().toStdString();
            for (auto value :
                 {footprint.modelOffsetX, footprint.modelOffsetY, footprint.modelOffsetZ,
                  footprint.modelRotationX, footprint.modelRotationY, footprint.modelRotationZ})
                if (!std::isfinite(value) || std::abs(value) > 1000)
                    throw std::runtime_error("Invalid model transform");
            if (!std::isfinite(footprint.modelScale) || footprint.modelScale < .01 ||
                footprint.modelScale > 100)
                throw std::runtime_error("Invalid model scale");
            if (footprint.texturePng.size() > 3 * 1024 * 1024)
                throw std::runtime_error("Texture is too large");
            if (!footprint.texturePng.empty()) {
                auto png = QByteArray::fromBase64(QByteArray::fromStdString(footprint.texturePng));
                if (png.size() < 33 || !png.startsWith(QByteArray::fromHex("89504e470d0a1a0a")) ||
                    png.mid(12, 4) != "IHDR")
                    throw std::runtime_error("Invalid texture PNG");
                auto x =
                    qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(png.constData() + 16));
                auto y =
                    qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(png.constData() + 20));
                if (!x || !y || x > 2048 || y > 2048)
                    throw std::runtime_error("Texture dimensions exceed limits");
            }
            if (item["mesh"].toArray().size() > 200000)
                throw std::runtime_error("Mesh is too large");
            for (auto face : item["mesh"].toArray()) {
                auto vertices = face.toArray();
                if (vertices.size() != 3)
                    throw std::runtime_error("Invalid mesh triangle");
                pcb::MeshTriangle triangle;
                for (int index = 0; index < 3; ++index) {
                    auto point = vertices[index].toArray();
                    if (point.size() != 3 && point.size() != 4)
                        throw std::runtime_error("Invalid mesh vertex");
                    triangle[index] = {point[0].toDouble(NAN), point[1].toDouble(NAN),
                                       point[2].toDouble(NAN)};
                    if (point.size() == 4) {
                        double color = point[3].toDouble(-1);
                        if (color < 0 || color > 0xffffff || color != std::floor(color))
                            throw std::runtime_error("Invalid mesh material");
                        triangle[index].color = static_cast<std::uint32_t>(color);
                    }
                    auto v = triangle[index];
                    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
                        std::abs(v.x) > 1000 || std::abs(v.y) > 1000 || std::abs(v.z) > 1000)
                        throw std::runtime_error("Invalid mesh coordinates");
                }
                footprint.mesh.push_back(triangle);
            }
            board.footprints.push_back(footprint);
        }
        for (const auto& value : root["tracks"].toArray()) {
            const auto item = value.toObject();
            board.addTrack(readPoint(item["from"].toObject()), readPoint(item["to"].toObject()),
                           item["layer"].toInt(-1), item["net"].toString().toStdString(),
                           dimension(item["width"]));
            board.tracks.back().id = item["id"].toString().toULongLong();
        }
        for (const auto& value : root["vias"].toArray()) {
            const auto item = value.toObject();
            const auto id = board.addVia(readPoint(item), item["net"].toString().toStdString());
            auto& via = board.vias.back();
            via.diameter = dimension(item["diameter"]);
            via.drill = dimension(item["drill"]);
            via.id = item["id"].toString().toULongLong();
            (void)id;
        }
        std::set<std::uint64_t> ids, symbols;
        for (const auto& footprint : board.footprints) {
            if (!symbols.insert(footprint.symbolId).second)
                throw std::runtime_error("Duplicate footprint ID");
            std::set<int> pins;
            for (const auto& pad : footprint.pads)
                if (pad.number <= 0 || !pins.insert(pad.number).second || pad.drill >= pad.diameter)
                    throw std::runtime_error("Invalid pad");
        }
        for (const auto& track : board.tracks)
            if (!track.id || !ids.insert(track.id).second)
                throw std::runtime_error("Invalid track ID");
        for (const auto& via : board.vias)
            if (!via.id || !ids.insert(via.id).second || via.drill >= via.diameter)
                throw std::runtime_error("Invalid via");
        board.normalizeIds();
        destination = std::move(board);
        return true;
    } catch (const std::exception& exception) {
        if (error)
            *error = QString::fromUtf8(exception.what());
        return false;
    }
}
} // namespace levelcad::app
