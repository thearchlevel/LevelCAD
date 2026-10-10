#include <algorithm>
#include <cctype>
#include <levelcad/connectivity/Connectivity.hpp>
#include <levelcad/core/Document.hpp>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

namespace levelcad::core {
namespace {
const char* names[] = {"Resistor",       "Capacitor",      "Ground", "Inductor",  "Diode",  "LED",
                       "Voltage source", "Current source", "Switch", "Connector", "Library"};
const char* prefixes[] = {"R", "C", "GND", "L", "D", "D", "V", "I", "SW", "J"};
const char* defaults[] = {"10k", "100n", "GND", "10m",  "1N4148",
                          "LED", "5V",   "1mA", "SPST", "2 pins"};

void reconnectPins(Document& document, const Symbol& before, const Symbol& after) {
    const auto oldPins = symbolPins(before);
    const auto newPins = symbolPins(after);
    const auto originalWires = document.wires();
    auto wires = originalWires;
    std::vector<std::pair<Point, Point>> movedNodes;
    std::vector<std::pair<Point, Point>> tethers;
    for (std::size_t index = 0; index < oldPins.size() && index < newPins.size(); ++index) {
        if (oldPins[index].position == newPins[index].position)
            continue;
        const auto oldPosition = oldPins[index].position, newPosition = newPins[index].position;
        bool shared = false, interior = false, connected = false;
        for (const auto& symbol : document.symbols()) {
            if (symbol.id == before.id)
                continue;
            for (const auto& pin : symbolPins(symbol))
                if (pin.position == oldPosition)
                    shared = true;
        }
        for (const auto& wire : originalWires) {
            if (connectivity::pointOnWire(oldPosition, wire)) {
                connected = true;
                if (wire.from != oldPosition && wire.to != oldPosition)
                    interior = true;
            }
        }
        if (!connected && !shared)
            continue;
        // Preserve a shared junction; moving one component must not pull a neighbour's pin away.
        if (shared || interior)
            tethers.push_back({newPosition, oldPosition});
        else
            movedNodes.push_back({oldPosition, newPosition});
    }
    auto replacement = [&](Point point) {
        for (const auto& [oldPosition, newPosition] : movedNodes)
            if (point == oldPosition)
                return newPosition;
        return point;
    };
    for (auto& wire : wires) {
        wire.from = replacement(wire.from);
        wire.to = replacement(wire.to);
    }
    std::vector<Wire> routed;
    std::vector<std::pair<Point, Point>> extensions;
    for (std::size_t index = 0; index < wires.size(); ++index) {
        auto wire = wires[index];
        if (wire.from == wire.to)
            continue;
        if ((wire.from != originalWires[index].from || wire.to != originalWires[index].to) &&
            wire.from.x != wire.to.x && wire.from.y != wire.to.y) {
            Point bend{wire.to.x, wire.from.y};
            if (wire.from != originalWires[index].from && wire.to == originalWires[index].to &&
                originalWires[index].from.y == originalWires[index].to.y)
                bend = {wire.from.x, wire.to.y};
            routed.push_back({wire.id, wire.from, bend});
            extensions.push_back({bend, wire.to});
        } else
            routed.push_back(wire);
    }
    // Keep old object IDs for existing segments, then allocate IDs only for the new bends.
    document.load(document.symbols(), std::move(routed), document.notes());
    for (const auto& [from, to] : extensions)
        if (from != to)
            document.addWire(from, to);
    for (const auto& [from, to] : tethers) {
        const Point bend{to.x, from.y};
        if (from != bend)
            document.addWire(from, bend);
        if (bend != to)
            document.addWire(bend, to);
    }
}
} // namespace

std::string kindName(SymbolKind kind) {
    if (kind == SymbolKind::Library)
        return "Library";
    const int index = static_cast<int>(kind);
    if (index < 0 || index > 9)
        throw std::invalid_argument("Unknown symbol kind");
    return names[index];
}

std::vector<Pin> symbolPins(const Symbol& symbol) {
    std::vector<PinDefinition> definitions = symbol.pinDefinitions;
    if (symbol.kind != SymbolKind::Library) {
        definitions = symbol.kind == SymbolKind::Ground
                          ? std::vector<PinDefinition>{{1, "GND", "power_in", {0, -3000000}}}
                          : std::vector<PinDefinition>{{1, "1", "passive", {-3000000, 0}},
                                                       {2, "2", "passive", {3000000, 0}}};
    }
    std::vector<Pin> pins;
    for (const auto& definition : definitions) {
        auto point = definition.position;
        for (int turn = 0; turn < ((symbol.rotation % 360 + 360) % 360) / 90; ++turn)
            point = {-point.y, point.x};
        pins.push_back(
            {symbol.id, definition.number, {symbol.origin.x + point.x, symbol.origin.y + point.y}});
    }
    return pins;
}

std::uint64_t Document::addLibrarySymbol(const Symbol& prototype, Point origin) {
    if (prototype.kind != SymbolKind::Library || prototype.pinDefinitions.empty())
        throw std::invalid_argument("Library symbol needs pin definitions");
    auto symbol = prototype;
    std::string prefix = prototype.reference.empty() ? "U" : prototype.reference;
    while (!prefix.empty() && std::isdigit(static_cast<unsigned char>(prefix.back())))
        prefix.pop_back();
    if (prefix.empty())
        prefix = "U";
    int number = 1;
    do {
        symbol.reference = prefix + std::to_string(number++);
    } while (std::any_of(symbols_.begin(), symbols_.end(), [&](const auto& existing) {
        return existing.reference == symbol.reference;
    }));
    symbol.id = nextId_++;
    symbol.origin = origin;
    symbols_.push_back(symbol);
    return symbol.id;
}

std::uint64_t Document::addSymbol(SymbolKind kind, Point origin) {
    const int k = static_cast<int>(kind);
    if (k < 0 || k > 9)
        throw std::invalid_argument("Unknown symbol kind");
    std::string ref;
    int n = 1;
    do {
        ref = std::string(prefixes[k]) + std::to_string(n++);
    } while (std::any_of(symbols_.begin(), symbols_.end(),
                         [&](const Symbol& s) { return s.reference == ref; }));
    const auto id = nextId_++;
    symbols_.push_back({id, kind, origin, 0, ref, defaults[k]});
    return id;
}

std::uint64_t Document::addWire(Point from, Point to) {
    if (from == to)
        throw std::invalid_argument("Wire endpoints must differ");
    auto id = nextId_++;
    wires_.push_back({id, from, to});
    return id;
}

bool Document::updateSymbol(const Symbol& s) {
    auto it = std::find_if(symbols_.begin(), symbols_.end(),
                           [&](const Symbol& v) { return v.id == s.id; });
    if (it == symbols_.end())
        return false;
    if (s.reference.empty() || s.rotation % 90 != 0 || static_cast<int>(s.kind) < 0 ||
        static_cast<int>(s.kind) > 10)
        throw std::invalid_argument("Invalid symbol properties");
    if (std::any_of(symbols_.begin(), symbols_.end(),
                    [&](const Symbol& v) { return v.id != s.id && v.reference == s.reference; }))
        throw std::invalid_argument("Reference already exists");
    const auto before = *it;
    *it = s;
    if (before.origin != s.origin || before.rotation != s.rotation)
        reconnectPins(*this, before, s);
    return true;
}

bool Document::moveObject(std::uint64_t id, Point delta) {
    for (auto symbol : symbols_) {
        if (symbol.id != id)
            continue;
        symbol.origin.x += delta.x;
        symbol.origin.y += delta.y;
        return updateSymbol(symbol);
    }
    for (auto& note : notes_)
        if (note.id == id) {
            note.position.x += delta.x;
            note.position.y += delta.y;
            return true;
        }
    return moveConnection(id, delta);
}

std::vector<std::uint64_t> Document::connectionWires(std::uint64_t wireId) const {
    const auto graph = connectivity::analyze(*this);
    std::vector<std::size_t> pending;
    std::vector<bool> visited(graph.vertices.size());
    for (std::size_t index = 0; index < graph.vertices.size(); ++index)
        if (graph.vertices[index].kind == connectivity::VertexKind::Wire &&
            graph.vertices[index].objectId == wireId) {
            pending.push_back(index);
            visited[index] = true;
        }
    for (std::size_t head = 0; head < pending.size(); ++head) {
        for (const auto& contact : graph.contacts) {
            // Terras distantes pertencem à mesma rede, mas não são a mesma ligação desenhada.
            if (contact.kind == connectivity::ContactKind::GlobalGround ||
                contact.kind == connectivity::ContactKind::NamedNet)
                continue;
            std::size_t neighbor = graph.vertices.size();
            if (contact.from == pending[head])
                neighbor = contact.to;
            if (contact.to == pending[head])
                neighbor = contact.from;
            if (neighbor < visited.size() && !visited[neighbor]) {
                visited[neighbor] = true;
                pending.push_back(neighbor);
            }
        }
    }
    std::vector<std::uint64_t> wires;
    for (auto index : pending)
        if (graph.vertices[index].kind == connectivity::VertexKind::Wire)
            wires.push_back(graph.vertices[index].objectId);
    return wires;
}

bool Document::moveConnection(std::uint64_t wireId, Point delta) {
    const auto ids = connectionWires(wireId);
    if (ids.empty())
        return false;
    auto belongs = [&](std::uint64_t id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    const auto original = wires_;
    std::vector<Point> anchors;
    for (const auto& symbol : symbols_)
        for (const auto& pin : symbolPins(symbol)) {
            const bool attached =
                std::any_of(original.begin(), original.end(), [&](const auto& wire) {
                    return belongs(wire.id) && connectivity::pointOnWire(pin.position, wire);
                });
            if (attached &&
                std::find(anchors.begin(), anchors.end(), pin.position) == anchors.end())
                anchors.push_back(pin.position);
        }
    for (auto& wire : wires_)
        if (belongs(wire.id)) {
            wire.from.x += delta.x;
            wire.from.y += delta.y;
            wire.to.x += delta.x;
            wire.to.y += delta.y;
        }
    // Os componentes ficam no lugar; trechos de retorno mantêm cada pino preso à ligação movida.
    for (const auto& anchor : anchors) {
        const Point moved{anchor.x + delta.x, anchor.y + delta.y}, bend{moved.x, anchor.y};
        if (anchor != bend)
            addWire(anchor, bend);
        if (bend != moved)
            addWire(bend, moved);
    }
    return true;
}

bool Document::removeObject(std::uint64_t id) {
    auto count = symbols_.size() + wires_.size() + notes_.size();
    std::erase_if(symbols_, [&](const Symbol& s) { return s.id == id; });
    std::erase_if(wires_, [&](const Wire& w) { return w.id == id; });
    std::erase_if(notes_, [&](const Note& n) { return n.id == id; });
    return count != symbols_.size() + wires_.size() + notes_.size();
}

void Document::clear() {
    symbols_.clear();
    wires_.clear();
    notes_.clear();
    nextId_ = idFloor_;
}

void Document::load(std::vector<Symbol> symbols, std::vector<Wire> wires, std::vector<Note> notes) {
    std::set<std::uint64_t> ids;
    std::uint64_t maxId = 0;
    auto validate = [&](std::uint64_t id) {
        if (id == 0 || id == std::numeric_limits<std::uint64_t>::max() || !ids.insert(id).second)
            throw std::invalid_argument("Invalid or duplicate object ID");
        maxId = std::max(maxId, id);
    };
    for (const auto& s : symbols) {
        validate(s.id);
        if (static_cast<int>(s.kind) < 0 || static_cast<int>(s.kind) > 10 || s.rotation % 90 != 0)
            throw std::invalid_argument("Invalid symbol");
    }
    for (const auto& w : wires) {
        validate(w.id);
        if (w.from == w.to)
            throw std::invalid_argument("Zero length wire");
    }
    for (const auto& note : notes) {
        validate(note.id);
        if (note.text.empty())
            throw std::invalid_argument("Empty comment");
    }
    notes_ = std::move(notes);
    symbols_ = std::move(symbols);
    wires_ = std::move(wires);
    if (idFloor_ == 1)
        nextId_ = maxId + 1;
    else {
        nextId_ = std::max(nextId_, idFloor_);
        for (auto id : ids)
            if (id / 1000000000ULL == idFloor_ / 1000000000ULL)
                nextId_ = std::max(nextId_, id + 1);
    }
}

std::uint64_t Document::addNote(Point position, std::string text, std::string author,
                                bool electrical) {
    if (text.empty())
        throw std::invalid_argument("Empty comment");
    auto id = nextId_++;
    notes_.push_back({id, position, std::move(text), std::move(author), electrical});
    return id;
}

bool Document::updateNote(const Note& note) {
    if (note.text.empty())
        return false;
    for (auto& existing : notes_)
        if (existing.id == note.id) {
            existing = note;
            return true;
        }
    return false;
}

std::vector<Net> Document::nets() const {
    return connectivity::analyze(*this).nets;
}

std::vector<std::string> Document::checkElectrical() const {
    std::vector<std::string> messages;
    for (const auto& diagnostic : connectivity::analyze(*this).diagnostics) {
        messages.push_back(diagnostic.message);
    }
    return messages;
}
} // namespace levelcad::core
