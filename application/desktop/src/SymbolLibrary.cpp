#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <levelcad/app/SExpression.hpp>
#include <levelcad/app/SymbolLibrary.hpp>
#include <map>
#include <set>
#include <stdexcept>

namespace levelcad::app {
namespace {
using sexpr::Expression;
using sexpr::Parser;

double number(const Expression* expression, int index) {
    if (!expression)
        throw std::runtime_error("Missing KiCad geometry");
    bool ok = false;
    const auto value = expression->value(index).toDouble(&ok);
    if (!ok || !std::isfinite(value) || std::abs(value) > 100000)
        throw std::runtime_error("Invalid KiCad coordinate");
    return value;
}

core::Point point(const Expression* expression) {
    return {static_cast<core::Coord>(std::llround(number(expression, 1) * core::nmPerMm)),
            static_cast<core::Coord>(std::llround(-number(expression, 2) * core::nmPerMm))};
}

QString property(const Expression& symbol, const QString& name) {
    for (const auto& item : symbol.children)
        if (item.tag() == "property" && item.value() == name)
            return item.value(2);
    return {};
}

void circle(core::Symbol& symbol, core::Point center, double radius) {
    std::vector<core::Point> stroke;
    for (int step = 0; step <= 48; ++step) {
        const auto angle = step * 2 * std::acos(-1.0) / 48;
        stroke.push_back(
            {center.x + static_cast<core::Coord>(std::llround(radius * std::cos(angle))),
             center.y + static_cast<core::Coord>(std::llround(radius * std::sin(angle)))});
    }
    symbol.strokes.push_back(std::move(stroke));
}

void arc(core::Symbol& symbol, core::Point start, core::Point middle, core::Point end) {
    const double ax = start.x, ay = start.y, bx = middle.x, by = middle.y, cx = end.x, cy = end.y;
    const double determinant = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(determinant) < 1) {
        symbol.strokes.push_back({start, middle, end});
        return;
    }
    const double a = ax * ax + ay * ay, b = bx * bx + by * by, c = cx * cx + cy * cy;
    const double x = (a * (by - cy) + b * (cy - ay) + c * (ay - by)) / determinant;
    const double y = (a * (cx - bx) + b * (ax - cx) + c * (bx - ax)) / determinant;
    const auto tau = 2 * std::acos(-1.0);
    auto normalized = [tau](double angle) {
        while (angle < 0)
            angle += tau;
        while (angle >= tau)
            angle -= tau;
        return angle;
    };
    const auto first = std::atan2(ay - y, ax - x);
    const auto midAngle = normalized(std::atan2(by - y, bx - x) - first);
    auto sweep = normalized(std::atan2(cy - y, cx - x) - first);
    if (midAngle > sweep)
        sweep -= tau;
    const auto radius = std::hypot(ax - x, ay - y);
    std::vector<core::Point> stroke;
    for (int step = 0; step <= 32; ++step) {
        const auto angle = first + sweep * step / 32;
        stroke.push_back({static_cast<core::Coord>(std::llround(x + radius * std::cos(angle))),
                          static_cast<core::Coord>(std::llround(y + radius * std::sin(angle)))});
    }
    symbol.strokes.push_back(std::move(stroke));
}

void geometry(const Expression& unit, core::Symbol& symbol) {
    for (const auto& item : unit.children) {
        if (item.tag() == "polyline") {
            const auto* points = item.child("pts");
            if (!points)
                continue;
            std::vector<core::Point> stroke;
            for (const auto& coordinate : points->children)
                if (coordinate.tag() == "xy")
                    stroke.push_back(point(&coordinate));
            if (stroke.size() >= 2)
                symbol.strokes.push_back(std::move(stroke));
        } else if (item.tag() == "rectangle") {
            const auto first = point(item.child("start")), last = point(item.child("end"));
            symbol.strokes.push_back({first, {last.x, first.y}, last, {first.x, last.y}, first});
        } else if (item.tag() == "circle") {
            circle(symbol, point(item.child("center")),
                   number(item.child("radius"), 1) * core::nmPerMm);
        } else if (item.tag() == "arc") {
            arc(symbol, point(item.child("start")), point(item.child("mid")),
                point(item.child("end")));
        } else if (item.tag() == "pin") {
            const auto* pinNumber = item.child("number");
            const auto* pinName = item.child("name");
            bool ok = false;
            const int id = pinNumber ? pinNumber->value().toInt(&ok) : 0;
            if (!ok || id <= 0)
                throw std::runtime_error("Alphanumeric pin numbers are not supported yet");
            const auto position = point(item.child("at"));
            const auto angle = number(item.child("at"), 3) * std::acos(-1.0) / 180;
            const auto length = number(item.child("length"), 1) * core::nmPerMm;
            const core::Point inner{
                position.x + static_cast<core::Coord>(std::llround(length * std::cos(angle))),
                position.y - static_cast<core::Coord>(std::llround(length * std::sin(angle)))};
            symbol.pinDefinitions.push_back({id, pinName ? pinName->value().toStdString() : "",
                                             item.value().toStdString(), position});
            symbol.strokes.push_back({position, inner});
        }
    }
}
} // namespace

LibraryImport loadKiCadLibrary(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(file.errorString().toStdString());
    if (file.size() > 32 * 1024 * 1024)
        throw std::runtime_error("Library is larger than 32 MB");
    return loadKiCadLibraryText(QString::fromUtf8(file.readAll()),
                                QFileInfo(path).completeBaseName());
}

LibraryImport loadKiCadLibraryText(const QString& text, const QString& category) {
    const auto root = Parser(text).read();
    if (root.tag() != "kicad_symbol_lib")
        throw std::runtime_error("Invalid symbol library");
    std::map<QString, const Expression*> symbols;
    for (const auto& item : root.children)
        if (item.tag() == "symbol")
            symbols[item.value()] = &item;
    LibraryImport result;
    for (const auto& [name, expression] : symbols) {
        try {
            std::vector<const Expression*> inheritance;
            std::set<QString> visited;
            auto* current = expression;
            while (current) {
                if (!visited.insert(current->value()).second)
                    throw std::runtime_error("Circular symbol inheritance");
                inheritance.push_back(current);
                const auto* parent = current->child("extends");
                if (!parent)
                    break;
                const auto found = symbols.find(parent->value());
                if (found == symbols.end())
                    throw std::runtime_error("Missing parent symbol");
                current = found->second;
            }
            core::Symbol prototype;
            prototype.kind = core::SymbolKind::Library;
            prototype.libraryId = (category + ":" + name).toStdString();
            prototype.value = name.toStdString();
            QString description;
            for (auto parent = inheritance.rbegin(); parent != inheritance.rend(); ++parent) {
                const auto reference = property(**parent, "Reference");
                if (!reference.isEmpty())
                    prototype.reference = reference.toStdString();
                const auto footprint = property(**parent, "Footprint");
                if (!footprint.isEmpty())
                    prototype.footprint = footprint.toStdString();
                const auto datasheet = property(**parent, "Datasheet");
                if (!datasheet.isEmpty())
                    prototype.datasheet = datasheet.toStdString();
                const auto detail = property(**parent, "Description");
                if (!detail.isEmpty())
                    description = detail;
            }
            const Expression* body = nullptr;
            for (const auto* parent : inheritance) {
                if (parent->child("symbol")) {
                    body = parent;
                    break;
                }
            }
            if (!body)
                throw std::runtime_error("No symbol geometry");
            for (const auto& unit : body->children) {
                if (unit.tag() != "symbol")
                    continue;
                const auto suffix = unit.value().split('_');
                if (suffix.size() < 3)
                    throw std::runtime_error("Invalid symbol unit");
                const int unitNumber = suffix[suffix.size() - 2].toInt();
                const int bodyStyle = suffix.back().toInt();
                // Multi-unit parts need a sheet-level unit model; don't silently overlap their
                // pins.
                if (unitNumber > 1)
                    throw std::runtime_error("Multi-unit symbol requires unit support");
                if (bodyStyle <= 1)
                    geometry(unit, prototype);
            }
            if (prototype.pinDefinitions.empty())
                throw std::runtime_error("Symbol has no electrical pins");
            std::set<int> pinNumbers;
            for (const auto& pin : prototype.pinDefinitions)
                if (!pinNumbers.insert(pin.number).second)
                    throw std::runtime_error(
                        "Stacked or duplicate pin numbers require explicit support");
            if (category == "Device") {
                if (name == "R" || name == "R_US") {
                    prototype.spiceType = "R";
                    prototype.value = "10k";
                }
                if (name == "C" || name == "C_Polarized") {
                    prototype.spiceType = "C";
                    prototype.value = "100n";
                }
                if (name == "L") {
                    prototype.spiceType = "L";
                    prototype.value = "10m";
                }
                if (name == "D" || name == "LED") {
                    prototype.spiceType = "D";
                    for (const auto& role : {"A", "K"})
                        for (const auto& pin : prototype.pinDefinitions)
                            if (pin.name == role)
                                prototype.spicePinOrder.push_back(pin.number);
                }
            }
            if (category == "Transistor_BJT") {
                for (const auto& role : {"C", "B", "E"})
                    for (const auto& pin : prototype.pinDefinitions)
                        if (pin.name == role)
                            prototype.spicePinOrder.push_back(pin.number);
                if (prototype.spicePinOrder.size() == 3 && prototype.pinDefinitions.size() == 3)
                    prototype.spiceType = "Q";
                else
                    prototype.spicePinOrder.clear();
            }
            if (!prototype.spiceType.empty() && prototype.spicePinOrder.empty())
                prototype.spicePinOrder = {1, 2};
            result.entries.push_back({category + ":" + name, description, std::move(prototype)});
        } catch (const std::exception& error) {
            result.skipped.append(name + ": " + QString::fromUtf8(error.what()));
        }
    }
    return result;
}

std::vector<LibraryEntry> bundledLibrary() {
    std::vector<LibraryEntry> entries;
    QString local;
    for (auto base : {QCoreApplication::applicationDirPath(), QDir::currentPath()}) {
        QDir directory(base);
        for (int depth = 0; depth < 8; ++depth) {
            if (directory.exists("libraries/official/kicad-symbols")) {
                local = directory.filePath("libraries/official/kicad-symbols");
                break;
            }
            if (!directory.cdUp())
                break;
        }
        if (!local.isEmpty())
            break;
    }
    if (!local.isEmpty()) {
        auto cache = QDir(local).filePath("../symbol-cache");
        if (QDir(cache).exists() && !QDir(cache).entryList({"*.kicad_sym"}, QDir::Files).empty()) {
            static QString cachedPath;
            static std::vector<LibraryEntry> cachedEntries;
            if (cachedPath == cache && !cachedEntries.empty())
                return cachedEntries;
            for (const auto& file :
                 QDir(cache).entryList({"*.kicad_sym"}, QDir::Files, QDir::Name)) {
                auto library = loadKiCadLibrary(QDir(cache).filePath(file));
                entries.insert(entries.end(), std::make_move_iterator(library.entries.begin()),
                               std::make_move_iterator(library.entries.end()));
            }
            cachedPath = cache;
            cachedEntries = entries;
            return entries;
        }
        for (const auto& file : QDir(local).entryList({"*.kicad_sym"}, QDir::Files, QDir::Name)) {
            auto library = loadKiCadLibrary(QDir(local).filePath(file));
            entries.insert(entries.end(), std::make_move_iterator(library.entries.begin()),
                           std::make_move_iterator(library.entries.end()));
        }
        // Version 10 also distributes one symbol per file, grouped in .kicad_symdir directories.
        // Combine each category before resolving inherited symbols.
        for (const auto& folder :
             QDir(local).entryList({"*.kicad_symdir"}, QDir::Dirs, QDir::Name)) {
            QString combined = "(kicad_symbol_lib ";
            QDir category(QDir(local).filePath(folder));
            for (const auto& file : category.entryList({"*.kicad_sym"}, QDir::Files, QDir::Name)) {
                QFile input(category.filePath(file));
                if (!input.open(QIODevice::ReadOnly))
                    continue;
                auto text = QString::fromUtf8(input.readAll());
                auto begin = text.indexOf("(symbol ");
                auto end = text.lastIndexOf(')');
                if (begin >= 0 && end > begin)
                    combined += text.mid(begin, end - begin);
            }
            combined += ")";
            auto library = loadKiCadLibraryText(combined, QFileInfo(folder).completeBaseName());
            entries.insert(entries.end(), std::make_move_iterator(library.entries.begin()),
                           std::make_move_iterator(library.entries.end()));
        }
        if (!entries.empty())
            return entries;
    }
    for (const auto& name : {"Device", "Transistor_BJT", "Transistor_FET", "Connector_Generic",
                             "Regulator_Linear", "Timer"}) {
        auto library = loadKiCadLibrary(QString(":/kicad/") + name + ".kicad_sym");
        entries.insert(entries.end(), std::make_move_iterator(library.entries.begin()),
                       std::make_move_iterator(library.entries.end()));
    }
    return entries;
}
} // namespace levelcad::app
