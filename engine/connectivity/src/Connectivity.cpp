#include <algorithm>
#include <array>
#include <cctype>
#include <levelcad/connectivity/Connectivity.hpp>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace levelcad::connectivity {
namespace {
class DisjointSet {
  public:
    explicit DisjointSet(std::size_t size) : parent_(size), rank_(size) {
        std::iota(parent_.begin(), parent_.end(), 0);
    }

    std::size_t root(std::size_t index) {
        if (parent_[index] != index) {
            parent_[index] = root(parent_[index]);
        }
        return parent_[index];
    }

    void join(std::size_t first, std::size_t second) {
        first = root(first);
        second = root(second);
        if (first == second)
            return;
        if (rank_[first] < rank_[second])
            std::swap(first, second);
        parent_[second] = first;
        if (rank_[first] == rank_[second])
            ++rank_[first];
    }

  private:
    std::vector<std::size_t> parent_;
    std::vector<unsigned> rank_;
};

std::uint64_t distance(core::Coord first, core::Coord second) {
    // Unsigned subtraction also handles distances spanning the signed 64-bit range.
    return first >= second ? std::uint64_t(first) - std::uint64_t(second)
                           : std::uint64_t(second) - std::uint64_t(first);
}

std::string lower(std::string value) {
    for (auto& letter : value)
        letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    return value;
}
} // namespace

bool pointOnWire(core::Point point, const core::Wire& wire) {
    if (point.x < std::min(wire.from.x, wire.to.x) || point.x > std::max(wire.from.x, wire.to.x) ||
        point.y < std::min(wire.from.y, wire.to.y) || point.y > std::max(wire.from.y, wire.to.y))
        return false;
    if (point == wire.from || point == wire.to)
        return true;
    const auto dx = distance(wire.from.x, wire.to.x);
    const auto dy = distance(wire.from.y, wire.to.y);
    const auto px = distance(wire.from.x, point.x);
    const auto py = distance(wire.from.y, point.y);
    const auto direction = std::gcd(dx, dy);
    const auto offset = std::gcd(px, py);
    if (direction == 0 || offset == 0)
        return false;
    // Reduce both direction vectors instead of multiplying coordinates and risking overflow.
    return dx / direction == px / offset && dy / direction == py / offset;
}

const core::Net* Result::netForPin(std::uint64_t symbolId, int pinNumber) const {
    for (const auto& vertex : vertices) {
        if (vertex.pin && vertex.pin->symbolId == symbolId && vertex.pin->number == pinNumber) {
            return &nets[vertex.netIndex];
        }
    }
    return nullptr;
}

bool Result::hasErrors() const {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const auto& item) { return item.severity == Severity::Error; });
}

Result analyze(const core::Document& document) {
    Result result;
    auto symbols = document.symbols();
    auto wires = document.wires();
    std::sort(symbols.begin(), symbols.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(wires.begin(), wires.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::map<std::uint64_t, const core::Symbol*> byId;
    std::vector<core::Pin> pins;
    std::vector<std::size_t> grounds;
    for (const auto& symbol : symbols) {
        byId[symbol.id] = &symbol;
        for (const auto& pin : core::symbolPins(symbol)) {
            if (symbol.kind == core::SymbolKind::Ground ||
                (symbol.kind == core::SymbolKind::Library && symbol.value == "GND" &&
                 symbol.pinDefinitions.size() == 1))
                grounds.push_back(pins.size());
            pins.push_back(pin);
            result.vertices.push_back({VertexKind::Pin, symbol.id, pin, 0});
        }
    }
    for (const auto& wire : wires)
        result.vertices.push_back({VertexKind::Wire, wire.id, std::nullopt, 0});
    std::vector<core::Note> labels;
    for (const auto& note : document.notes())
        if (note.electrical)
            labels.push_back(note);
    for (const auto& label : labels) {
        if (label.text == "GND" || label.text == "0")
            grounds.push_back(result.vertices.size());
        result.vertices.push_back({VertexKind::Label, label.id, std::nullopt, 0});
    }
    DisjointSet sets(result.vertices.size());
    auto contact = [&](std::size_t first, std::size_t second, ContactKind kind) {
        result.contacts.push_back({first, second, kind});
        sets.join(first, second);
    };
    // Indice espacial limitado: trechos muito longos ficam numa lista global para nao explodir a memoria.
    constexpr core::Coord cellSize=5000000;
    using Cell=std::pair<core::Coord,core::Coord>;
    auto cell=[](core::Coord value) { auto q=value/cellSize; return value%cellSize<0?q-1:q; };
    std::map<Cell,std::vector<std::size_t>> wireCells;
    std::vector<std::size_t> longWires;
    auto bounds=[&](const core::Wire& wire) {
        return std::array<core::Coord,4>{cell(std::min(wire.from.x,wire.to.x)),cell(std::min(wire.from.y,wire.to.y)),cell(std::max(wire.from.x,wire.to.x)),cell(std::max(wire.from.y,wire.to.y))};
    };
    for(std::size_t i=0;i<wires.size();++i) {
        auto box=bounds(wires[i]); auto nx=box[2]-box[0]+1,ny=box[3]-box[1]+1;
        if(nx>64 || ny>64 || nx*ny>256) {longWires.push_back(i);continue;}
        for(auto x=box[0];x<=box[2];++x) for(auto y=box[1];y<=box[3];++y) wireCells[{x,y}].push_back(i);
    }
    auto candidates=[&](const core::Wire& wire) {
        std::set<std::size_t> result(longWires.begin(),longWires.end());auto box=bounds(wire);
        auto nx=box[2]-box[0]+1,ny=box[3]-box[1]+1;
        if(nx>64 || ny>64 || nx*ny>256) { for(std::size_t i=0;i<wires.size();++i) result.insert(i); }
        else for(auto x=box[0];x<=box[2];++x) for(auto y=box[1];y<=box[3];++y)
            if(auto bucket=wireCells.find({x,y});bucket!=wireCells.end()) result.insert(bucket->second.begin(),bucket->second.end());
        return result;
    };
    std::map<Cell,std::vector<std::size_t>> pinsAt;
    for(std::size_t first=0;first<pins.size();++first) {
        auto& coincident=pinsAt[{pins[first].position.x,pins[first].position.y}];
        if(!coincident.empty()) contact(first,coincident.front(),ContactKind::CoincidentPins);
        coincident.push_back(first);
        for(auto wire:candidates({0,pins[first].position,pins[first].position}))
            if(pointOnWire(pins[first].position,wires[wire])) contact(first,pins.size()+wire,ContactKind::PinOnWire);
    }
    for(std::size_t first=0;first<wires.size();++first)
        for(auto second:candidates(wires[first])) {
            if(second>=first) continue;
            // Cruzamentos sem extremidade continuam separados; T e sobreposicoes conectam.
            if(pointOnWire(wires[first].from,wires[second]) || pointOnWire(wires[first].to,wires[second]) || pointOnWire(wires[second].from,wires[first]) || pointOnWire(wires[second].to,wires[first]))
                contact(pins.size()+first,pins.size()+second,ContactKind::WireJunction);
        }
    std::map<std::string,std::size_t> namedLabels;
    for(std::size_t index=0;index<labels.size();++index) {
        auto vertex=pins.size()+wires.size()+index;
        auto found=pinsAt.find({labels[index].position.x,labels[index].position.y});
        if(found!=pinsAt.end()) for(auto pin:found->second) contact(vertex,pin,ContactKind::LabelContact);
        for(auto wire:candidates({0,labels[index].position,labels[index].position}))
            if(pointOnWire(labels[index].position,wires[wire])) contact(vertex,pins.size()+wire,ContactKind::LabelContact);
        auto [entry,inserted]=namedLabels.emplace(labels[index].text,vertex);
        if(!inserted) contact(vertex,entry->second,ContactKind::NamedNet);
    }
    for (std::size_t i = 1; i < grounds.size(); ++i)
        contact(grounds[0], grounds[i], ContactKind::GlobalGround);
    std::map<std::size_t, std::size_t> netIndices;
    for (std::size_t vertex = 0; vertex < result.vertices.size(); ++vertex) {
        auto root = sets.root(vertex);
        auto [entry, inserted] = netIndices.emplace(root, result.nets.size());
        if (inserted) {
            const auto& anchor = result.vertices[vertex];
            const auto name = anchor.pin ? "N" + std::to_string(anchor.objectId) + "_" +
                                               std::to_string(anchor.pin->number)
                                         : "NW" + std::to_string(anchor.objectId);
            result.nets.push_back({name, {}, {}});
        }
        auto& node = result.nets[entry->second];
        result.vertices[vertex].netIndex = entry->second;
        if (vertex < pins.size())
            node.pins.push_back(pins[vertex]);
        else if (vertex < pins.size() + wires.size())
            node.wires.push_back(wires[vertex - pins.size()].id);
        else
            node.name = labels[vertex - pins.size() - wires.size()].text;
        if (!grounds.empty() && root == sets.root(grounds[0]))
            node.name = "0";
    }
    auto report = [&](Severity severity, const std::string& code, const std::string& message,
                      std::uint64_t object = 0, int pin = 0) {
        result.diagnostics.push_back({severity, code, message, object, pin});
    };
    if (symbols.empty())
        report(Severity::Warning, "empty-circuit", "O esquematico esta vazio.");
    if (!symbols.empty() && grounds.empty())
        report(Severity::Warning, "missing-ground",
               "O circuito nao tem uma referencia GND (no 0).");
    std::set<std::string> references;
    for (const auto& symbol : symbols) {
        if (symbol.reference.empty() || !references.insert(lower(symbol.reference)).second) {
            report(Severity::Error, "duplicate-reference",
                   "Referencia vazia ou repetida: " + symbol.reference, symbol.id);
        }
        if (symbol.kind != core::SymbolKind::Ground && symbol.value.empty()) {
            report(Severity::Warning, "missing-value", "Componente sem valor: " + symbol.reference,
                   symbol.id);
        }
    }
    for (const auto& net : result.nets) {
        if (net.pins.empty())
            report(Severity::Warning, "wire-only-net", "Rede sem pinos: " + net.name,
                   net.wires.empty() ? 0 : net.wires[0]);
        for (const auto& pin : net.pins) {
            if (net.pins.size() == 1) {
                report(Severity::Warning, "open-pin",
                       "Pino sem ligacao a outro componente: " + byId.at(pin.symbolId)->reference +
                           "." + std::to_string(pin.number),
                       pin.symbolId, pin.number);
            }
        }
    }
    for (const auto& wire : wires) {
        for (const auto& endpoint : {wire.from, wire.to}) {
            bool connected =
                std::any_of(labels.begin(), labels.end(),
                            [&](const auto& label) { return label.position == endpoint; }) ||
                std::any_of(pins.begin(), pins.end(),
                            [&](const auto& pin) { return pin.position == endpoint; });
            for (const auto& other : wires) {
                if (other.id != wire.id && pointOnWire(endpoint, other))
                    connected = true;
            }
            if (!connected)
                report(Severity::Warning, "dangling-wire",
                       "Extremidade de ligacao aberta: #" + std::to_string(wire.id), wire.id);
        }
    }
    DisjointSet islands(result.nets.size());
    for (const auto& symbol : symbols) {
        const auto terminals = core::symbolPins(symbol);
        if (terminals.size() < 2)
            continue;
        std::vector<std::size_t> terminalNets;
        for (const auto& terminal : terminals) {
            for (const auto& vertex : result.vertices) {
                if (vertex.pin && vertex.objectId == symbol.id &&
                    vertex.pin->number == terminal.number)
                    terminalNets.push_back(vertex.netIndex);
            }
        }
        for (std::size_t pin = 1; pin < terminalNets.size(); ++pin) {
            result.components.push_back({symbol.id, terminalNets[0], terminalNets[pin]});
            islands.join(terminalNets[0], terminalNets[pin]);
        }
        if (terminalNets.size() == 2 && terminalNets[0] == terminalNets[1]) {
            const bool source = symbol.kind == core::SymbolKind::VoltageSource;
            report(source ? Severity::Error : Severity::Warning,
                   source ? "shorted-voltage-source" : "shorted-component",
                   "Os dois pinos estao na mesma rede: " + symbol.reference, symbol.id);
        }
    }
    // O tipo elétrico vem da biblioteca; saídas ligadas diretamente merecem um aviso específico.
    for (const auto& net : result.nets) {
        int outputs = 0, powerOutputs = 0;
        for (const auto& pin : net.pins) {
            for (const auto& definition : byId.at(pin.symbolId)->pinDefinitions) {
                if (definition.number != pin.number)
                    continue;
                if (definition.electricalType == "output")
                    ++outputs;
                if (definition.electricalType == "power_out")
                    ++powerOutputs;
                if (definition.electricalType == "no_connect" &&
                    (net.pins.size() > 1 || !net.wires.empty()))
                    report(Severity::Error, "no-connect-pin", "Pino NC ligado a rede: " + net.name,
                           pin.symbolId, pin.number);
            }
        }
        if (outputs > 1 || powerOutputs > 1)
            report(Severity::Warning, "output-conflict",
                   "Multiplas saidas na mesma rede: " + net.name,
                   net.pins.empty() ? 0 : net.pins[0].symbolId);
    }
    std::set<std::size_t> groundedIslands, reportedIslands;
    for (std::size_t i = 0; i < result.nets.size(); ++i) {
        if (result.nets[i].name == "0")
            groundedIslands.insert(islands.root(i));
    }
    for (std::size_t i = 0; i < result.nets.size(); ++i) {
        const auto island = islands.root(i);
        if (!result.nets[i].pins.empty() && !groundedIslands.contains(island) &&
            reportedIslands.insert(island).second) {
            report(Severity::Warning, "floating-island",
                   "Grupo de componentes sem ligacao ao GND: " + result.nets[i].name,
                   result.nets[i].pins[0].symbolId);
        }
    }
    return result;
}
} // namespace levelcad::connectivity
