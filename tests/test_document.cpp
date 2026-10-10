#include <algorithm>
#include <iostream>
#include <levelcad/connectivity/Connectivity.hpp>
#include <levelcad/core/Document.hpp>
#include <stdexcept>
using namespace levelcad::core;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main() {
    try {
        Document d;
        auto r = d.addSymbol(SymbolKind::Resistor, {0, 0});
        auto r2 = d.addSymbol(SymbolKind::Resistor, {10000000, 0});
        require(r == 1 && r2 == 2, "IDs");
        d.removeObject(r);
        auto r3 = d.addSymbol(SymbolKind::Resistor, {0, 0});
        require(d.symbols().back().reference == "R1", "reuse free reference");
        auto s = d.symbols().back();
        s.reference = "R2";
        bool rejected = false;
        try {
            d.updateSymbol(s);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "duplicate reference rejected");
        s.reference = "R3";
        s.rotation = 90;
        d.updateSymbol(s);
        require(symbolPins(s)[0].position == Point{0, -3000000}, "rotated pins");
        d.moveObject(r3, {1000000, 2000000});
        require(d.symbols().back().origin == Point{1000000, 2000000}, "move symbol");
        rejected = false;
        try {
            d.addWire({0, 0}, {0, 0});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "zero wire");
        Document circuit;
        auto a = circuit.addSymbol(SymbolKind::Resistor, {0, 0});
        auto b = circuit.addSymbol(SymbolKind::Capacitor, {10000000, 0});
        circuit.addWire({3000000, 0}, {7000000, 0});
        auto nets = circuit.nets();
        require(nets.size() == 3, "two pin connection nets");
        require(
            std::count_if(nets.begin(), nets.end(), [](auto n) { return n.pins.size() == 2; }) == 1,
            "connected pin pair");
        const auto diagnostics = levelcad::connectivity::analyze(circuit).diagnostics;
        require(std::count_if(diagnostics.begin(), diagnostics.end(),
                              [](const auto& issue) { return issue.code == "open-pin"; }) == 2,
                "isolated pins reported");
        Document branches;
        branches.addWire({0, 0}, {10000000, 0});
        branches.addWire({5000000, 0}, {5000000, 10000000});
        require(branches.nets().size() == 1, "T junction connects");
        Document crossing;
        crossing.addWire({0, 5000000}, {10000000, 5000000});
        crossing.addWire({5000000, 0}, {5000000, 10000000});
        require(crossing.nets().size() == 2, "interior crossing does not connect");
        Document grounds;
        grounds.addSymbol(SymbolKind::Ground, {0, 0});
        grounds.addSymbol(SymbolKind::Ground, {10000000, 0});
        require(grounds.nets().size() == 1 && grounds.nets()[0].name == "0", "global ground");
        Document kinds;
        for (int i = 0; i < 10; ++i)
            kinds.addSymbol(static_cast<SymbolKind>(i), {i * 10000000, 0});
        require(kinds.symbols()[4].reference != kinds.symbols()[5].reference,
                "diode LED unique references");
        auto before = circuit.symbols();
        auto invalid = before;
        invalid[1].id = invalid[0].id;
        rejected = false;
        try {
            circuit.load(invalid, {});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected && circuit.symbols().size() == before.size(), "atomic invalid load");
        Document copy = circuit;
        require(copy.addSymbol(SymbolKind::Inductor, {0, 0}) == 4, "copied ID allocator");
        circuit.removeObject(a);
        circuit.removeObject(b);
        circuit.clear();
        require(circuit.symbols().empty() && circuit.wires().empty(), "clear");
        std::cout << "Document, connectivity, editing and validation checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
