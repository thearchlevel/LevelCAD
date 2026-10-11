#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <levelcad/app/CadCommands.hpp>
#include <levelcad/app/MeshLibrary.hpp>
#include <levelcad/app/OfficialLibrary.hpp>
#include <levelcad/pcb/Footprints.hpp>
#include <map>
#include <set>
#include <stdexcept>

namespace levelcad::app {
namespace {
void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

double numeric(const QJsonObject& object, const QString& key, double fallback = NAN) {
    auto value = object.contains(key) ? object[key].toDouble(NAN) : fallback;
    if (!std::isfinite(value))
        fail("Numero invalido: " + key);
    return value;
}

core::Coord dimension(const QJsonObject& object, const QString& key, double fallback = NAN) {
    auto value = numeric(object, key, fallback);
    if (value <= 0 || value > 500)
        fail("Dimensao fora do intervalo: " + key);
    return static_cast<core::Coord>(std::llround(value * core::nmPerMm));
}

core::Point position(const QJsonObject& object, const QString& x = "x_mm",
                     const QString& y = "y_mm") {
    auto a = numeric(object, x), b = numeric(object, y);
    if (std::abs(a) > 1000 || std::abs(b) > 1000)
        fail("Coordenada fora do intervalo");
    return {static_cast<core::Coord>(std::llround(a * core::nmPerMm)),
            static_cast<core::Coord>(std::llround(b * core::nmPerMm))};
}

core::Symbol symbol(const core::Document& document, const QString& reference) {
    for (const auto& item : document.symbols())
        if (QString::fromStdString(item.reference) == reference)
            return item;
    fail("Componente inexistente: " + reference);
    return {};
}

core::Pin pin(const core::Document& document, const QString& reference, int number) {
    auto item = symbol(document, reference);
    for (const auto& p : core::symbolPins(item))
        if (p.number == number)
            return p;
    fail("Pino inexistente: " + reference + "." + QString::number(number));
    return {};
}

pcb::Footprint& footprint(pcb::Board& board, const QString& reference) {
    for (auto& item : board.footprints)
        if (QString::fromStdString(item.reference) == reference)
            return item;
    fail("Footprint inexistente: " + reference);
    throw std::runtime_error("Unreachable");
}

std::string color(const QJsonObject& object, const QString& key, const std::string& fallback) {
    if (!object.contains(key))
        return fallback;
    QColor value(object[key].toString());
    if (!value.isValid())
        fail("Cor invalida: " + key);
    return value.name().toStdString();
}
} // namespace

CommandResult previewCommands(const core::Document& document, const pcb::Board& board,
                              const QJsonArray& commands, const QString& author) {
    CommandResult result;
    result.document = document;
    result.board = board;
    try {
        if (commands.isEmpty() || commands.size() > 256)
            fail("A proposta deve ter de 1 a 256 comandos");
        for (auto value : commands) {
            if (!value.isObject())
                fail("Comando deve ser um objeto");
            auto object = value.toObject();
            auto op = object["op"].toString();
            if (op == "add_symbol") {
                static const std::map<QString, core::SymbolKind> types = {
                    {"resistor", core::SymbolKind::Resistor},
                    {"capacitor", core::SymbolKind::Capacitor},
                    {"ground", core::SymbolKind::Ground},
                    {"inductor", core::SymbolKind::Inductor},
                    {"diode", core::SymbolKind::Diode},
                    {"led", core::SymbolKind::LED},
                    {"connector", core::SymbolKind::Connector},
                    {"switch", core::SymbolKind::Switch}};
                auto type = types.find(object["kind"].toString());
                if (type == types.end())
                    fail("Tipo de componente nao suportado");
                auto id = result.document.addSymbol(type->second, position(object));
                auto added = *std::find_if(result.document.symbols().begin(),
                                           result.document.symbols().end(),
                                           [&](const auto& s) { return s.id == id; });
                if (object.contains("reference"))
                    added.reference = object["reference"].toString().toStdString();
                if (object.contains("value"))
                    added.value = object["value"].toString().toStdString();
                added.footprint = object["footprint"].toString().toStdString();
                if (added.reference.empty() || added.reference.size() > 64)
                    fail("Referencia invalida");
                for (const auto& s : result.document.symbols())
                    if (s.id != id && s.reference == added.reference)
                        fail("Referencia repetida");
                if (!result.document.updateSymbol(added))
                    fail("Nao foi possivel atualizar componente");
                result.changes.append("Adicionar " + QString::fromStdString(added.reference));
            } else if (op == "set_symbol") {
                auto item = symbol(result.document, object["reference"].toString());
                if (object.contains("value"))
                    item.value = object["value"].toString().toStdString();
                if (object.contains("footprint"))
                    item.footprint = object["footprint"].toString().toStdString();
                if (!item.footprint.empty() && !pcb::makeFootprint(item))
                    fail("Footprint incompativel com os pinos");
                result.document.updateSymbol(item);
                result.changes.append("Atualizar " + QString::fromStdString(item.reference));
            } else if (op == "connect") {
                auto a =
                    pin(result.document, object["from_ref"].toString(), object["from_pin"].toInt());
                auto b =
                    pin(result.document, object["to_ref"].toString(), object["to_pin"].toInt());
                core::Point bend{b.position.x, a.position.y};
                if (a.position != bend)
                    result.document.addWire(a.position, bend);
                if (bend != b.position)
                    result.document.addWire(bend, b.position);
                result.changes.append("Conectar " + object["from_ref"].toString() + " a " +
                                      object["to_ref"].toString());
            } else if (op == "sync_pcb") {
                auto report = result.board.synchronize(result.document,
                                                       {true, false, true, officialFootprint});
                for (const auto& warning : report.warnings)
                    result.warnings.append(QString::fromStdString(warning));
                result.changes.append("Sincronizar pads e redes da PCB");
            } else if (op == "move_footprint") {
                auto& item = footprint(result.board, object["reference"].toString());
                item.position = position(object);
                item.rotation = numeric(object, "rotation", item.rotation);
                if (item.position.x < 0 || item.position.y < 0 ||
                    item.position.x > result.board.width || item.position.y > result.board.height)
                    fail("Footprint fora da placa");
                result.changes.append("Posicionar " + QString::fromStdString(item.reference));
            } else if (op == "assign_footprint") {
                auto item = symbol(result.document, object["reference"].toString());
                item.footprint = object["package"].toString().toStdString();
                auto mapped = officialFootprint(item);
                if (!mapped)
                    fail("Encapsulamento nao suportado ou pinos incompativeis");
                auto& existing = footprint(result.board, object["reference"].toString());
                for (auto& p : mapped->pads)
                    for (const auto& old : existing.pads)
                        if (old.number == p.number)
                            p.net = old.net;
                existing.source = mapped->source;
                existing.pads = mapped->pads;
                existing.width = mapped->width;
                existing.length = mapped->length;
                existing.height = mapped->height;
                item.footprint = mapped->source;
                result.document.updateSymbol(item);
                result.changes.append("Atribuir " + object["package"].toString());
            } else if (op == "board_settings") {
                if (object.contains("width_mm"))
                    result.board.width = dimension(object, "width_mm");
                if (object.contains("height_mm"))
                    result.board.height = dimension(object, "height_mm");
                if (object.contains("thickness_mm"))
                    result.board.thickness = dimension(object, "thickness_mm");
                if (object.contains("clearance_mm"))
                    result.board.clearance = dimension(object, "clearance_mm");
                result.board.solderMask = color(object, "mask_color", result.board.solderMask);
                result.board.silkColor = color(object, "silk_color", result.board.silkColor);
                result.changes.append("Personalizar placa");
            } else if (op == "add_layer") {
                int index = object["index"].toInt(-1);
                auto name = object["name"].toString().trimmed();
                if (index < 0 || index > 31 || name.isEmpty() ||
                    std::any_of(result.board.layers.begin(), result.board.layers.end(),
                                [&](const auto& l) { return l.index == index; }))
                    fail("Camada invalida ou repetida");
                result.board.layers.push_back(
                    {index, name.toStdString(), color(object, "color", "#aa88ff"), true});
                result.changes.append("Adicionar camada " + name);
            } else if (op == "add_track") {
                auto net = object["net"].toString().toStdString();
                bool known = false;
                for (const auto& f : result.board.footprints)
                    for (const auto& p : f.pads)
                        if (p.net == net && !net.empty())
                            known = true;
                if (!known)
                    fail("Rede inexistente na PCB");
                pcb::Track track{0,
                                 position(object, "from_x_mm", "from_y_mm"),
                                 position(object, "to_x_mm", "to_y_mm"),
                                 object["layer"].toInt(-1),
                                 dimension(object, "width_mm", .35),
                                 net};
                if (!result.board.canPlaceTrack(track))
                    fail("Trilha viola separacao, limites ou camada");
                result.board.addTrack(track.from, track.to, track.layer, track.net, track.width);
                result.changes.append("Rotear " + QString::fromStdString(net));
            } else if (op == "assign_model") {
                auto file = object["file"].toString();
                if (!bundledModels().contains(file))
                    fail("Modelo fora da biblioteca incorporada");
                assignMesh(footprint(result.board, object["reference"].toString()),
                           ":/models3d/" + file);
                result.changes.append("Atribuir modelo 3D " + file);
            } else if (op == "comment") {
                auto message = object["text"].toString().trimmed();
                if (message.isEmpty() || message.size() > 10000)
                    fail("Comentario invalido");
                result.document.addNote(position(object), message.toStdString(),
                                        author.toStdString());
                result.changes.append("Adicionar comentario");
            } else
                fail("Comando nao permitido: " + op);
        }
        for (const auto& issue : result.board.checkRules())
            result.warnings.append(QString::fromStdString(issue));
        result.success = true;
    } catch (const std::exception& error) {
        result.error = QString::fromUtf8(error.what());
        result.changes.clear();
        result.document = document;
        result.board = board;
    }
    return result;
}

QString commandInstructions() {
    return R"(You edit LevelCAD through a validated JSON plan. Return only {"version":1,"summary":"...","commands":[...]}.
All coordinates and dimensions are in millimeters. Never invent component pin numbers or nets. No shell commands, files, credentials, URLs or executable code.
Supported ops:
add_symbol(kind: resistor|capacitor|ground|inductor|diode|led|connector|switch,reference,value,x_mm,y_mm,footprint optional);
set_symbol(reference,value optional,footprint optional); connect(from_ref,from_pin,to_ref,to_pin);
sync_pcb(); move_footprint(reference,x_mm,y_mm,rotation optional);
assign_footprint(reference,package from footprint_catalog);
board_settings(width_mm,height_mm,thickness_mm,clearance_mm,mask_color,silk_color, all optional);
add_layer(index 0..31,name,color); add_track(net,layer,width_mm,from_x_mm,from_y_mm,to_x_mm,to_y_mm);
assign_model(reference,file from models); comment(text,x_mm,y_mm).
Use sync_pcb after schematic changes, before PCB edits. New components use schematic placement. Connections become ratsnest; they are not fabricated copper until add_track. Preserve existing design, copper, compatible footprints, and layers unless prompt asks to change them. Keep commands <=256. Do not claim fabrication readiness.)";
}

QJsonObject commandContext(const core::Document& document, const pcb::Board& board) {
    QJsonArray symbols, footprints, layers, packages, models;
    for (const auto& s : document.symbols()) {
        QJsonArray pins;
        for (const auto& p : core::symbolPins(s))
            pins.append(QJsonObject{{"number", p.number},
                                    {"x_mm", double(p.position.x) / core::nmPerMm},
                                    {"y_mm", double(p.position.y) / core::nmPerMm}});
        symbols.append(QJsonObject{{"reference", QString::fromStdString(s.reference)},
                                   {"value", QString::fromStdString(s.value)},
                                   {"footprint", QString::fromStdString(s.footprint)},
                                   {"pins", pins}});
    }
    for (const auto& f : board.footprints) {
        QJsonArray pads;
        for (const auto& p : f.pads) {
            auto position = board.padPosition(f, p);
            pads.append(QJsonObject{{"number", p.number},
                                    {"net", QString::fromStdString(p.net)},
                                    {"x_mm", double(position.x) / core::nmPerMm},
                                    {"y_mm", double(position.y) / core::nmPerMm}});
        }
        footprints.append(QJsonObject{{"reference", QString::fromStdString(f.reference)},
                                      {"package", QString::fromStdString(f.source)},
                                      {"pads", pads}});
    }
    for (const auto& l : board.layers)
        layers.append(QJsonObject{{"index", l.index}, {"name", QString::fromStdString(l.name)}});
    for (const auto& p : pcb::footprintCatalog())
        packages.append(QString::fromStdString(p));
    for (const auto& m : bundledModels())
        models.append(m);
    return {{"symbols", symbols},
            {"footprints", footprints},
            {"layers", layers},
            {"footprint_catalog", packages},
            {"models", models},
            {"board_width_mm", double(board.width) / core::nmPerMm},
            {"board_height_mm", double(board.height) / core::nmPerMm}};
}
} // namespace levelcad::app
