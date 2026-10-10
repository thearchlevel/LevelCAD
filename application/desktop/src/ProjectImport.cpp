#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <cmath>
#include <levelcad/app/ProjectImport.hpp>
#include <levelcad/app/SExpression.hpp>
#include <levelcad/app/SymbolLibrary.hpp>
#include <set>
#include <stdexcept>

namespace levelcad::app {
namespace {
using sexpr::Expression;

Expression read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024)
        throw std::runtime_error("Cannot read project (limit: 64 MB)");
    return sexpr::Parser(QString::fromUtf8(file.readAll())).read();
}

double number(const Expression* item, int index, double fallback = 0) {
    if (!item || item->value(index).isEmpty())
        return fallback;
    bool ok = false;
    auto result = item->value(index).toDouble(&ok);
    if (!ok || !std::isfinite(result) || std::abs(result) > 100000)
        throw std::runtime_error(("Valor numerico invalido em " + item->tag() + "[" +
                                  QString::number(index) + "]: " + item->value(index))
                                     .toStdString());
    return result;
}

core::Point point(const Expression* item, int index = 1) {
    return {static_cast<core::Coord>(std::llround(number(item, index) * core::nmPerMm)),
            static_cast<core::Coord>(std::llround(number(item, index + 1) * core::nmPerMm))};
}

QString property(const Expression& item, const QString& key) {
    for (const auto& child : item.children)
        if (child.tag() == "property" && child.value() == key)
            return child.value(2);
    for (const auto& child : item.children)
        if (child.tag() == "fp_text" && child.value() == key.toLower())
            return child.value(2);
    return {};
}

QString serialize(const Expression& item) {
    if (item.children.empty()) {
        auto atom = item.atom;
        atom.replace("\\", "\\\\").replace("\"", "\\\"");
        return "\"" + atom + "\"";
    }
    QString result = "(";
    for (const auto& child : item.children)
        result += serialize(child) + " ";
    return result + ")";
}

void schematic(ProjectImport& project, const QString& path) {
    auto root = read(path);
    if (root.tag() != "kicad_sch")
        throw std::runtime_error("Only modern schematic files are supported");
    QMap<QString, core::Symbol> definitions;
    if (auto* library = root.child("lib_symbols")) {
        auto embedded = *library;
        embedded.children[0].atom = "kicad_symbol_lib";
        auto parsed = loadKiCadLibraryText(serialize(embedded), "Embedded");
        for (auto& entry : parsed.entries) {
            auto id = entry.name.mid(QString("Embedded:").size());
            entry.prototype.libraryId = id.toStdString();
            definitions[id] = entry.prototype;
        }
        for (auto warning : parsed.skipped)
            project.warnings.append("Simbolo: " + warning);
    }
    std::vector<core::Symbol> symbols;
    std::vector<core::Wire> wires;
    std::vector<core::Note> notes;
    std::uint64_t next = 1;
    for (const auto& item : root.children) {
        if (item.tag() == "symbol") {
            auto* lib = item.child("lib_id");
            auto key = lib ? lib->value() : QString();
            if (!definitions.contains(key)) {
                project.warnings.append("Simbolo sem definicao suportada: " + key);
                continue;
            }
            if (number(item.child("unit"), 1, 1) > 1) {
                project.warnings.append("Unidade adicional nao importada: " +
                                        property(item, "Reference"));
                continue;
            }
            auto symbol = definitions[key];
            symbol.id = next++;
            symbol.origin = point(item.child("at"));
            symbol.rotation = -static_cast<int>(number(item.child("at"), 3));
            if (symbol.rotation % 90) {
                project.warnings.append("Rotacao livre no esquematico nao suportada: " + key);
                continue;
            }
            symbol.reference = property(item, "Reference").toStdString();
            symbol.value = property(item, "Value").toStdString();
            symbol.footprint = property(item, "Footprint").toStdString();
            if (auto* mirror = item.child("mirror")) {
                auto reflect = [&](core::Point& p) {
                    if (mirror->value() == "x")
                        p.y = -p.y;
                    else if (mirror->value() == "y")
                        p.x = -p.x;
                };
                for (auto& pin : symbol.pinDefinitions)
                    reflect(pin.position);
                for (auto& stroke : symbol.strokes)
                    for (auto& p : stroke)
                        reflect(p);
            }
            if (key == "Device:R" || key == "Device:R_US")
                symbol.spiceType = "R";
            if (key == "Device:C" || key == "Device:C_Polarized")
                symbol.spiceType = "C";
            if (key == "Device:L")
                symbol.spiceType = "L";
            if (!symbol.spiceType.empty())
                symbol.spicePinOrder = {1, 2};
            if (key.startsWith("power:") && symbol.pinDefinitions.size() == 1) {
                auto pins = core::symbolPins(symbol);
                notes.push_back({next++, pins[0].position, symbol.value, "Importado", true});
            }
            symbols.push_back(symbol);
        } else if (item.tag() == "wire") {
            if (auto* pts = item.child("pts")) {
                std::vector<core::Point> points;
                for (auto& xy : pts->children)
                    if (xy.tag() == "xy")
                        points.push_back(point(&xy));
                for (std::size_t i = 1; i < points.size(); ++i)
                    if (points[i - 1] != points[i])
                        wires.push_back({next++, points[i - 1], points[i]});
            }
        } else if (item.tag() == "label" || item.tag() == "global_label" ||
                   item.tag() == "hierarchical_label") {
            notes.push_back(
                {next++, point(item.child("at")), item.value().toStdString(), "Importado", true});
            if (item.tag() == "hierarchical_label")
                project.warnings.append("Rotulo hierarquico importado apenas na folha atual: " +
                                        item.value());
        } else if (item.tag() == "text")
            notes.push_back(
                {next++, point(item.child("at")), item.value().toStdString(), "Importado", false});
        else if (item.tag() == "junction") {
            auto p = point(item.child("at"));
            std::vector<core::Wire> split;
            for (auto& wire : wires) {
                (void)wire;
            }
            // Junctions are handled below, after all segments have been read.
        } else if (item.tag() == "sheet" || item.tag() == "bus" || item.tag() == "bus_entry")
            project.warnings.append("Recurso sem equivalente completo: " + item.tag());
    }
    for (const auto& item : root.children)
        if (item.tag() == "junction") {
            auto p = point(item.child("at"));
            std::vector<core::Wire> split;
            for (const auto& wire : wires) {
                auto dx = static_cast<long double>(wire.to.x) - wire.from.x,
                     dy = static_cast<long double>(wire.to.y) - wire.from.y;
                auto px = static_cast<long double>(p.x) - wire.from.x,
                     py = static_cast<long double>(p.y) - wire.from.y;
                if (p != wire.from && p != wire.to && dx * py == dy * px &&
                    p.x >= std::min(wire.from.x, wire.to.x) &&
                    p.x <= std::max(wire.from.x, wire.to.x) &&
                    p.y >= std::min(wire.from.y, wire.to.y) &&
                    p.y <= std::max(wire.from.y, wire.to.y)) {
                    split.push_back({wire.id, wire.from, p});
                    split.push_back({next++, p, wire.to});
                } else
                    split.push_back(wire);
            }
            wires = std::move(split);
        }
    project.document.load(std::move(symbols), std::move(wires), std::move(notes));
    project.hasSchematic = true;
}

void board(ProjectImport& project, const QString& path) {
    auto root = read(path);
    if (root.tag() != "kicad_pcb")
        throw std::runtime_error("Only modern PCB files are supported");
    auto& board = project.board;
    QMap<QString, int> layerIndices;
    board.layers.clear();
    if (auto* layers = root.child("layers"))
        for (const auto& layer : layers->children) {
            if (layer.children.size() < 3 || !layer.value().endsWith(".Cu"))
                continue;
            auto name = layer.value();
            int index = name == "F.Cu" ? 0 : name == "B.Cu" ? 1 : board.layers.size() + 1;
            if (index > 31)
                throw std::runtime_error("More than 32 copper layers");
            layerIndices[name] = index;
            board.layers.push_back(
                {index, name.toStdString(),
                 QColor::fromHsv((index * 67) % 360, 160, 235).name().toStdString(), true});
        }
    if (board.layers.empty()) {
        board.layers = {{0, "Top copper", "#ff766e", true}, {1, "Bottom copper", "#72aaff", true}};
        layerIndices["F.Cu"] = 0;
        layerIndices["B.Cu"] = 1;
    }
    if (auto* general = root.child("general"))
        board.thickness =
            static_cast<core::Coord>(number(general->child("thickness"), 1, 1.6) * core::nmPerMm);
    QMap<int, std::string> nets;
    for (auto& item : root.children)
        if (item.tag() == "net")
            nets[static_cast<int>(number(&item, 1))] = item.value(2).toStdString();
    std::uint64_t next = 1;
    for (const auto& symbol : project.document.symbols())
        next = std::max(next, symbol.id + 1);
    for (const auto& item : root.children) {
        if (item.tag() == "footprint" || item.tag() == "module") {
            pcb::Footprint footprint;
            footprint.symbolId = next++;
            footprint.reference = property(item, "Reference").toStdString();
            footprint.value = property(item, "Value").toStdString();
            footprint.source = item.value().toStdString();
            footprint.position = point(item.child("at"));
            footprint.rotation = -number(item.child("at"), 3);
            for (const auto& symbol : project.document.symbols())
                if (symbol.reference == footprint.reference) {
                    footprint.symbolId = symbol.id;
                    break;
                }
            std::set<int> pins;
            for (const auto& pad : item.children)
                if (pad.tag() == "pad") {
                    bool ok = false;
                    int pin = pad.value().toInt(&ok);
                    if (!ok || pin <= 0) {
                        project.warnings.append("Pad mecanico/alfanumerico sem mapeamento: " +
                                                QString::fromStdString(footprint.reference) + "." +
                                                pad.value());
                        continue;
                    }
                    if (!pins.insert(pin).second) {
                        project.warnings.append("Pad repetido nao importado: " +
                                                QString::fromStdString(footprint.reference) + "." +
                                                pad.value());
                        continue;
                    }
                    auto size = point(pad.child("size"));
                    pcb::Pad definition;
                    definition.number = pin;
                    definition.offset = point(pad.child("at"));
                    definition.sizeX = size.x;
                    definition.sizeY = size.y;
                    definition.diameter = std::max(size.x, size.y);
                    // Furos oblongos possuem a palavra 'oval' antes das dimensoes.
                    // O modelo atual representa o menor diametro e informa a aproximacao.
                    auto* drill = pad.child("drill");
                    if (drill && drill->value() == "oval") {
                        definition.drill = static_cast<core::Coord>(
                            std::min(number(drill, 2), number(drill, 3)) * core::nmPerMm);
                        project.warnings.append("Furo oblongo aproximado por furo circular: " +
                                                QString::fromStdString(footprint.reference) + "." +
                                                pad.value());
                    } else {
                        definition.drill =
                            static_cast<core::Coord>(number(drill, 1) * core::nmPerMm);
                    }
                    definition.shape = pad.value(3).toStdString();
                    definition.rotation = -number(pad.child("at"), 3) - footprint.rotation;
                    definition.layerMask = 0;
                    if (auto* layers = pad.child("layers"))
                        for (const auto& atom : layers->children) {
                            if (atom.atom == "*.Cu")
                                definition.layerMask = 0xffffffffu;
                            else if (layerIndices.contains(atom.atom))
                                definition.layerMask |= 1u << layerIndices[atom.atom];
                        }
                    if (auto* net = pad.child("net"))
                        definition.net = net->value(2).isEmpty()
                                             ? nets[static_cast<int>(number(net, 1))]
                                             : net->value(2).toStdString();
                    if (size.x <= 0 || size.y <= 0)
                        throw std::runtime_error("Invalid pad size");
                    footprint.pads.push_back(definition);
                }
            double maxX = 2, maxY = 2;
            for (const auto& pad : footprint.pads) {
                maxX = std::max(maxX, (std::abs(double(pad.offset.x)) + pad.diameter / 2) /
                                          core::nmPerMm);
                maxY = std::max(maxY, (std::abs(double(pad.offset.y)) + pad.diameter / 2) /
                                          core::nmPerMm);
            }
            footprint.width = static_cast<core::Coord>(maxX * 2 * core::nmPerMm);
            footprint.length = static_cast<core::Coord>(maxY * 2 * core::nmPerMm);
            if (item.child("model"))
                project.warnings.append("Modelo externo nao incorporado: " +
                                        QString::fromStdString(footprint.reference) +
                                        ". Atribua um STL na biblioteca 3D.");
            board.footprints.push_back(footprint);
        } else if (item.tag() == "segment") {
            auto* layer = item.child("layer");
            auto name = layer ? layer->value() : QString();
            auto net = nets[static_cast<int>(number(item.child("net"), 1))];
            if (net.empty() || !layerIndices.contains(name)) {
                project.warnings.append("Trilha sem rede/camada suportada");
                continue;
            }
            board.addTrack(
                point(item.child("start")), point(item.child("end")), layerIndices[name], net,
                static_cast<core::Coord>(number(item.child("width"), 1) * core::nmPerMm));
        } else if (item.tag() == "via") {
            if (item.children.size() > 1 && (item.value() == "blind" || item.value() == "micro")) {
                project.warnings.append("Via cega/microvia nao importada");
                continue;
            }
            auto net = nets[static_cast<int>(number(item.child("net"), 1))];
            if (net.empty())
                continue;
            board.addVia(point(item.child("at")), net);
            board.vias.back().diameter =
                static_cast<core::Coord>(number(item.child("size"), 1) * core::nmPerMm);
            board.vias.back().drill =
                static_cast<core::Coord>(number(item.child("drill"), 1) * core::nmPerMm);
        } else if (item.tag() == "gr_line" || item.tag() == "gr_rect") {
            auto* layer = item.child("layer");
            if (layer && layer->value() == "Edge.Cuts") {
                auto a = point(item.child("start")), b = point(item.child("end"));
                if (item.tag() == "gr_rect")
                    board.outline.push_back({a, {b.x, a.y}, b, {a.x, b.y}, a});
                else
                    board.outline.push_back({a, b});
            }
        } else if (item.tag() == "zone" || item.tag() == "arc" || item.tag() == "gr_arc")
            project.warnings.append("Geometria sem equivalente completo: " + item.tag());
    }
    core::Coord minX = 0, minY = 0, maxX = board.width, maxY = board.height;
    bool hasBounds = false;
    for (const auto& stroke : board.outline)
        for (auto p : stroke) {
            if (!hasBounds) {
                minX = maxX = p.x;
                minY = maxY = p.y;
                hasBounds = true;
            }
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
        }
    if (hasBounds && maxX > minX && maxY > minY) {
        board.width = maxX - minX;
        board.height = maxY - minY;
        for (auto& footprint : board.footprints) {
            footprint.position.x -= minX;
            footprint.position.y -= minY;
        }
        for (auto& track : board.tracks) {
            track.from.x -= minX;
            track.from.y -= minY;
            track.to.x -= minX;
            track.to.y -= minY;
        }
        for (auto& via : board.vias) {
            via.position.x -= minX;
            via.position.y -= minY;
        }
        for (auto& stroke : board.outline)
            for (auto& p : stroke) {
                p.x -= minX;
                p.y -= minY;
            }
    }
    board.normalizeIds();
    project.hasBoard = true;
}
} // namespace

ProjectImport importExternalProject(const QString& path) {
    ProjectImport result;
    auto extension = QFileInfo(path).suffix().toLower();
    if (extension == "kicad_pro") {
        auto base = QFileInfo(path).absolutePath() + "/" + QFileInfo(path).completeBaseName();
        if (QFileInfo::exists(base + ".kicad_sch"))
            schematic(result, base + ".kicad_sch");
        else
            result.warnings.append("O projeto nao tem um esquematico junto ao arquivo principal.");
        if (QFileInfo::exists(base + ".kicad_pcb"))
            board(result, base + ".kicad_pcb");
        else
            result.warnings.append("O projeto nao tem um PCB junto ao arquivo principal.");
        if (!result.hasSchematic && !result.hasBoard)
            throw std::runtime_error("No project files found beside the project file");
    } else if (extension == "kicad_sch")
        schematic(result, path);
    else if (extension == "kicad_pcb")
        board(result, path);
    else
        throw std::runtime_error("Unsupported external project format");
    result.warnings.removeDuplicates();
    return result;
}
} // namespace levelcad::app
