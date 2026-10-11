#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QMatrix4x4>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QVector3D>
#include <cmath>
#include <functional>
#include <levelcad/app/OfficialLibrary.hpp>
#include <levelcad/app/SExpression.hpp>
#include <levelcad/pcb/Footprints.hpp>
#include <set>
#include <stdexcept>

namespace levelcad::app {
QString officialLibraryRoot() {
    const auto configured = qEnvironmentVariable("LEVELCAD_LIBRARY_ROOT");
    if (!configured.isEmpty() && QDir(configured).exists("kicad-footprints"))
        return configured;
    for (auto base : {QCoreApplication::applicationDirPath(), QDir::currentPath()}) {
        QDir directory(base);
        for (int depth = 0; depth < 8; ++depth) {
            if (directory.exists("libraries/official/kicad-footprints"))
                return directory.filePath("libraries/official");
            if (directory.exists("official/kicad-footprints"))
                return directory.filePath("official");
            if (!directory.cdUp())
                break;
        }
    }
    return {};
}

QString officialFootprintPath(const QString& identifier) {
    const auto root = officialLibraryRoot();
    auto parts = identifier.split(':');
    if (root.isEmpty() || parts.size() != 2)
        return {};
    static const QRegularExpression valid("^[A-Za-z0-9_.,()+ -]+$");
    if (!valid.match(parts[0]).hasMatch() || !valid.match(parts[1]).hasMatch() ||
        parts[0].contains("..") || parts[1].contains(".."))
        return {};
    auto path =
        QDir(root).filePath("kicad-footprints/" + parts[0] + ".pretty/" + parts[1] + ".kicad_mod");
    return QFileInfo::exists(path) ? path : QString();
}

namespace {
double number(const sexpr::Expression* expression, int index, double fallback = 0) {
    if (!expression || expression->value(index).isEmpty())
        return fallback;
    bool ok = false;
    double value = expression->value(index).toDouble(&ok);
    if (!ok || !std::isfinite(value) || std::abs(value) > 10000)
        throw std::runtime_error("Invalid library geometry");
    return value;
}

QString packageFor(const pcb::Footprint& footprint) {
    auto source = QString::fromStdString(footprint.source);
    if (!source.startsWith("LevelCAD:"))
        return source;
    static const QMap<QString, QString> aliases{
        {"LevelCAD:R_Axial_P7.62mm",
         "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal"},
        {"LevelCAD:D_Axial_P7.62mm", "Diode_THT:D_DO-35_SOD27_P7.62mm_Horizontal"},
        {"LevelCAD:C_Radial_P2.50mm", "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm"},
        {"LevelCAD:LED_D5.0mm", "LED_THT:LED_D5.0mm"},
        {"LevelCAD:Header_1x02_P2.54mm",
         "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"},
        {"LevelCAD:Header_1x03_P2.54mm",
         "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical"},
        {"LevelCAD:Header_1x04_P2.54mm",
         "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"},
        {"LevelCAD:Header_1x08_P2.54mm",
         "Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical"},
        {"LevelCAD:R_0603", "Resistor_SMD:R_0603_1608Metric"},
        {"LevelCAD:R_0402", "Resistor_SMD:R_0402_1005Metric"},
        {"LevelCAD:R_0805", "Resistor_SMD:R_0805_2012Metric"},
        {"LevelCAD:R_1206", "Resistor_SMD:R_1206_3216Metric"},
        {"LevelCAD:C_0603", "Capacitor_SMD:C_0603_1608Metric"},
        {"LevelCAD:C_0402", "Capacitor_SMD:C_0402_1005Metric"},
        {"LevelCAD:C_0805", "Capacitor_SMD:C_0805_2012Metric"},
        {"LevelCAD:C_1206", "Capacitor_SMD:C_1206_3216Metric"},
        {"LevelCAD:DIP-8", "Package_DIP:DIP-8_W7.62mm"},
        {"LevelCAD:SOIC-8", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm"}};
    return aliases.value(source);
}

QString textFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 100 * 1024 * 1024)
        throw std::runtime_error("Cannot read local library asset");
    return QString::fromUtf8(file.readAll());
}

// Converted STEP models use VRML coordinates in 0.1 inches (2.54 mm).
// Preserve their local origin instead of centering asymmetric connectors.
std::vector<pcb::MeshTriangle> readVrml(const QString& path) {
    QString text = textFile(path);
    QMap<QString, QVector<QVector3D>> coordinates;
    std::vector<pcb::MeshTriangle> mesh;
    QMap<QString, std::uint32_t> materials;
    auto matching = [&](int start) {
        int depth = 0;
        for (int i = start; i < text.size(); ++i) {
            if (text[i] == '{')
                ++depth;
            if (text[i] == '}' && --depth == 0)
                return i;
        }
        throw std::runtime_error("Incomplete VRML model");
    };
    auto values = [](const QString& input) {
        QVector<double> result;
        static QRegularExpression numeric(R"([-+]?(?:[0-9]*\.[0-9]+|[0-9]+)(?:[eE][-+]?[0-9]+)?)");
        auto matches = numeric.globalMatch(input);
        while (matches.hasNext())
            result.append(matches.next().captured().toDouble());
        return result;
    };
    // Use normal regex syntax; numeric expressions are kept in one place for validation.
    QRegularExpression point("point\\s*\\[([^\\]]*)\\]");
    QRegularExpression indices("coordIndex\\s*\\[([^\\]]*)\\]");

    std::function<void(int, int, QMatrix4x4)> walk;
    walk = [&](int begin, int end, QMatrix4x4 transform) {
        QRegularExpression node(R"((Transform|Shape)[[:space:]]*[{])");
        int cursor = begin;
        while (cursor < end) {
            auto found = node.match(text, cursor);
            if (!found.hasMatch() || found.capturedStart() >= end)
                break;
            int brace = found.capturedEnd() - 1, close = matching(brace);
            auto content = text.mid(brace + 1, close - brace - 1);
            if (found.captured(1) == "Transform") {
                int children = content.indexOf("children");
                auto header = children < 0 ? content : content.left(children);
                QMatrix4x4 local;
                auto field = [&](const QString& name) {
                    auto match =
                        QRegularExpression(
                            name +
                            R"([[:space:]]+([-+0-9.eE]+[[:space:]]+[-+0-9.eE]+[[:space:]]+[-+0-9.eE]+(?:[[:space:]]+[-+0-9.eE]+)?))")
                            .match(header);
                    return values(match.captured(1));
                };
                auto translation = field("translation"), rotation = field("rotation"),
                     scale = field("scale"), center = field("center"),
                     orientation = field("scaleOrientation");
                if (translation.size() >= 3)
                    local.translate(translation[0], translation[1], translation[2]);
                if (center.size() >= 3)
                    local.translate(center[0], center[1], center[2]);
                if (rotation.size() >= 4)
                    local.rotate(rotation[3] * 180 / std::acos(-1.0), rotation[0], rotation[1],
                                 rotation[2]);
                if (orientation.size() >= 4)
                    local.rotate(orientation[3] * 180 / std::acos(-1.0), orientation[0],
                                 orientation[1], orientation[2]);
                if (scale.size() >= 3)
                    local.scale(scale[0], scale[1], scale[2]);
                if (orientation.size() >= 4)
                    local.rotate(-orientation[3] * 180 / std::acos(-1.0), orientation[0],
                                 orientation[1], orientation[2]);
                if (center.size() >= 3)
                    local.translate(-center[0], -center[1], -center[2]);
                walk(brace + 1, close, transform * local);
            } else {
                std::uint32_t color = 0x343a46;
                auto diffuse =
                    QRegularExpression(
                        R"(diffuseColor[[:space:]]+([-+0-9.eE]+[[:space:]]+[-+0-9.eE]+[[:space:]]+[-+0-9.eE]+))")
                        .match(content);
                auto channels = values(diffuse.captured(1));
                if (channels.size() == 3) {
                    color = 0;
                    for (auto channel : channels)
                        color = (color << 8) |
                                static_cast<std::uint32_t>(std::clamp(channel, 0.0, 1.0) * 255);
                    if (!color)
                        color = 1;
                    auto definition = QRegularExpression(
                                          R"(appearance[[:space:]]+DEF[[:space:]]+([a-zA-Z0-9_]+))")
                                          .match(content);
                    if (definition.hasMatch())
                        materials[definition.captured(1)] = color;
                } else {
                    auto use = QRegularExpression(
                                   R"(appearance[[:space:]]+USE[[:space:]]+([a-zA-Z0-9_]+))")
                                   .match(content);
                    color = materials.value(use.captured(1), color);
                }
                QVector<QVector3D> vertices;
                auto points = point.match(content);
                if (points.hasMatch()) {
                    auto numbers = values(points.captured(1));
                    if (numbers.size() % 3)
                        throw std::runtime_error("Invalid VRML coordinates");
                    for (int i = 0; i < numbers.size(); i += 3)
                        vertices.append(QVector3D(numbers[i], numbers[i + 1], numbers[i + 2]));
                    auto definition =
                        QRegularExpression(R"(coord[[:space:]]+DEF[[:space:]]+([a-zA-Z0-9_]+))")
                            .match(content);
                    if (definition.hasMatch())
                        coordinates[definition.captured(1)] = vertices;
                } else {
                    auto use =
                        QRegularExpression(R"(coord[[:space:]]+USE[[:space:]]+([a-zA-Z0-9_]+))")
                            .match(content);
                    vertices = coordinates.value(use.captured(1));
                }
                auto indexMatch = indices.match(content);
                if (!indexMatch.hasMatch()) {
                    cursor = close + 1;
                    continue;
                }
                auto indexValues = values(indexMatch.captured(1));
                QVector<int> polygon;
                auto vertex = [&](int index) {
                    if (index < 0 || index >= vertices.size())
                        throw std::runtime_error("Invalid VRML index");
                    auto v = transform.map(vertices[index]) * 2.54f;
                    if (!std::isfinite(v.x()) || !std::isfinite(v.y()) || !std::isfinite(v.z()) ||
                        v.length() > 10000)
                        throw std::runtime_error("Invalid VRML vertex");
                    return pcb::MeshVertex{v.x(), -v.y(), v.z(), color};
                };
                for (double index : indexValues) {
                    if (index != std::floor(index))
                        throw std::runtime_error("Invalid VRML face");
                    if (index == -1) {
                        for (int i = 1; i + 1 < polygon.size(); ++i)
                            mesh.push_back(
                                {vertex(polygon[0]), vertex(polygon[i]), vertex(polygon[i + 1])});
                        polygon.clear();
                    } else
                        polygon.append(static_cast<int>(index));
                    if (mesh.size() > 200000)
                        throw std::runtime_error("Model exceeds rendering budget");
                }
                if (!polygon.isEmpty())
                    throw std::runtime_error("Incomplete VRML face");
            }
            cursor = close + 1;
        }
    };
    walk(0, text.size(), QMatrix4x4());
    if (mesh.empty())
        throw std::runtime_error("No triangulated geometry in model");
    return mesh;
}
} // namespace

std::vector<pcb::MeshTriangle> loadOfficialVrml(const QString& path) {
    return readVrml(path);
}

std::optional<pcb::Footprint> officialFootprint(const core::Symbol& symbol) {
    auto identifier = QString::fromStdString(symbol.footprint);
    if (identifier.isEmpty()) {
        auto native = pcb::makeFootprint(symbol);
        if (!native)
            return {};
        identifier = packageFor(*native);
        if (identifier.isEmpty())
            return native;
    }
    if (identifier.startsWith("LevelCAD:")) {
        pcb::Footprint selected;
        selected.source = identifier.toStdString();
        auto alias = packageFor(selected);
        if (!alias.isEmpty())
            identifier = alias;
    }
    auto path = officialFootprintPath(identifier);
    if (path.isEmpty())
        return pcb::makeFootprint(symbol);
    auto root = sexpr::Parser(textFile(path)).read();
    pcb::Footprint result;
    result.width = result.length = 0;
    result.symbolId = symbol.id;
    result.reference = symbol.reference;
    result.value = symbol.value;
    result.source = identifier.toStdString();
    std::set<int> expected, actual;
    for (const auto& pin : core::symbolPins(symbol))
        expected.insert(pin.number);
    for (const auto& item : root.children)
        if (item.tag() == "pad") {
            bool ok = false;
            auto pin = item.value().toInt(&ok);
            if (!ok || pin <= 0 || actual.contains(pin))
                return {}; // Never silently discard stacked or named pads.
            actual.insert(pin);
            pcb::Pad pad;
            pad.number = pin;
            auto* at = item.child("at");
            auto* size = item.child("size");
            auto* drill = item.child("drill");
            pad.offset = {static_cast<core::Coord>(number(at, 1) * core::nmPerMm),
                          static_cast<core::Coord>(number(at, 2) * core::nmPerMm)};
            pad.sizeX = static_cast<core::Coord>(number(size, 1) * core::nmPerMm);
            pad.sizeY = static_cast<core::Coord>(number(size, 2) * core::nmPerMm);
            pad.diameter = std::max(pad.sizeX, pad.sizeY);
            if (pad.sizeX <= 0 || pad.sizeY <= 0)
                throw std::runtime_error("Invalid footprint pad size");
            if (drill && drill->value() == "oval")
                return {};
            pad.drill = static_cast<core::Coord>(number(drill, 1) * core::nmPerMm);
            pad.rotation =
                -number(at, 3); // O arquivo usa angulos anti-horarios; a tela usa Y para baixo.
            pad.cornerRatio = number(item.child("roundrect_rratio"), 1, .25);
            pad.shape = item.value(3).toStdString();
            pad.layerMask = 0;
            if (auto* layers = item.child("layers"))
                for (const auto& layer : layers->children) {
                    if (layer.atom == "F.Cu")
                        pad.layerMask |= 1u;
                    if (layer.atom == "B.Cu")
                        pad.layerMask |= 2u;
                    if (layer.atom == "*.Cu")
                        pad.layerMask = 0xffffffffu;
                }
            if (!pad.layerMask)
                return {};
            result.width = std::max(result.width, 2 * std::abs(pad.offset.x) + pad.sizeX);
            result.length = std::max(result.length, 2 * std::abs(pad.offset.y) + pad.sizeY);
            result.pads.push_back(pad);
        }
    if (actual != expected || actual.empty())
        return {};
    return result;
}

bool assignOfficialModel(pcb::Footprint& footprint) {
    auto path = officialFootprintPath(packageFor(footprint));
    if (path.isEmpty())
        return false;
    auto root = sexpr::Parser(textFile(path)).read();
    const auto* model = root.child("model");
    if (!model)
        return false;
    auto relative = model->value();
    const auto end = relative.indexOf('}');
    if (end >= 0)
        relative = relative.mid(end + 1);
    while (relative.startsWith('/'))
        relative.remove(0, 1);
    if (relative.contains("..") || relative.startsWith('/') || relative.contains(':'))
        return false;
    auto info = QFileInfo(relative);
    auto cached =
        QDir(officialLibraryRoot())
            .filePath("mesh-cache/" + info.path() + "/" + info.completeBaseName() + ".wrl");
    if (!QFileInfo::exists(cached))
        return false;
    static QMutex mutex;
    static QMap<QString, pcb::SharedMesh> cache;
    pcb::SharedMesh geometry;
    {
        QMutexLocker lock(&mutex);
        if (!cache.contains(cached)) {
            if (cache.size() >= 64)
                cache.clear();
            pcb::SharedMesh imported;
            imported = readVrml(cached);
            cache[cached] = std::move(imported);
        }
        geometry = cache.value(cached);
    }
    auto xyz = [&](const char* name) {
        auto* expression = model->child(name);
        return expression ? expression->child("xyz") : nullptr;
    };
    const auto* scale = xyz("scale");
    double sx = number(scale, 1, 1), sy = number(scale, 2, 1), sz = number(scale, 3, 1);
    footprint.mesh = geometry;
    if (sx != 1 || sy != 1 || sz != 1) {
        std::vector<pcb::MeshTriangle> adjusted(geometry.begin(), geometry.end());
        for (auto& triangle : adjusted)
            for (auto& vertex : triangle) {
                vertex.x *= sx;
                vertex.y *= sy;
                vertex.z *= sz;
            }
        footprint.mesh = std::move(adjusted);
    }
    footprint.modelName = ("official:" + relative).toStdString();
    const auto* offset = xyz("offset");
    const auto* rotation = xyz("rotate");
    footprint.modelOffsetX = number(offset, 1);
    footprint.modelOffsetY = -number(offset, 2);
    footprint.modelOffsetZ = number(offset, 3);
    footprint.modelRotationX = -number(rotation, 1);
    footprint.modelRotationY = number(rotation, 2);
    footprint.modelRotationZ = -number(rotation, 3);
    footprint.modelScale = 1;
    footprint.texturePng.clear();
    return true;
}
} // namespace levelcad::app