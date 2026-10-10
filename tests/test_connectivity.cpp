#include <algorithm>
#include <fstream>
#include <iostream>
#include <levelcad/connectivity/Connectivity.hpp>
#include <levelcad/exporters/SpiceExporter.hpp>
#include <limits>
#include <stdexcept>
using namespace levelcad;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool hasCode(const connectivity::Result& graph, const std::string& code) {
    return std::any_of(graph.diagnostics.begin(), graph.diagnostics.end(),
                       [&](const auto& diagnostic) { return diagnostic.code == code; });
}

core::Document divider() {
    core::Document document;
    document.addSymbol(core::SymbolKind::VoltageSource, {0, 0});
    document.addSymbol(core::SymbolKind::Resistor, {0, 10000000});
    document.addSymbol(core::SymbolKind::Resistor, {10000000, 10000000});
    document.addSymbol(core::SymbolKind::Ground, {3000000, 3000000});
    document.addSymbol(core::SymbolKind::Ground, {13000000, 13000000});
    auto symbols = document.symbols();
    symbols[0].value = "10V";
    symbols[1].value = symbols[2].value = "1k";
    for (const auto& symbol : symbols)
        document.updateSymbol(symbol);
    document.addWire({-3000000, 0}, {-3000000, 10000000});
    document.addWire({3000000, 10000000}, {7000000, 10000000});
    return document;
}

int main(int argc, char** argv) {
    try {
        auto circuit = divider();
        const auto graph = connectivity::analyze(circuit);
        require(graph.nets.size() == 3, "divider has input, output and ground nodes");
        require(graph.diagnostics.empty(), "complete divider has no topological diagnostics");
        require(graph.netForPin(1, 1)->name == "N1_1", "deterministic input node");
        require(graph.netForPin(1, 2)->name == "0", "source negative terminal grounded");
        require(graph.netForPin(2, 2)->name == graph.netForPin(3, 1)->name,
                "divider midpoint connected");
        require(!graph.contacts.empty() && graph.components.size() == 3,
                "explicit graph contacts and components");
        auto symbols = circuit.symbols();
        auto wires = circuit.wires();
        std::reverse(symbols.begin(), symbols.end());
        std::reverse(wires.begin(), wires.end());
        core::Document reordered;
        reordered.load(symbols, wires);
        require(connectivity::analyze(reordered).netForPin(2, 2)->name ==
                    graph.netForPin(2, 2)->name,
                "node IDs unaffected by file order");
        const auto spice = exporters::exportSpice(circuit);
        const std::string expected =
            "LevelCAD circuit\n* Credits: Wenderson Dias\n* Published by The Archlevel "
            "Foundation\nV1 N1_1 0 DC 10\nR1 N1_1 N2_2 1k\nR2 N2_2 0 1k\n.op\n.end\n";
        require(spice.success() && spice.text == expected, "SPICE voltage divider golden fixture");
        if (argc > 1) {
            std::ofstream file(argv[1]);
            file << spice.text;
            require(file.good(), "write example deck");
        }
        core::Document open;
        open.addSymbol(core::SymbolKind::Resistor, {0, 0});
        require(hasCode(connectivity::analyze(open), "open-pin"), "open pin diagnosed");
        require(hasCode(connectivity::analyze(open), "missing-ground"), "missing ground diagnosed");
        require(hasCode(connectivity::analyze(open), "floating-island"),
                "floating component group diagnosed");
        require(!exporters::exportSpice(open).success(), "SPICE needs reference ground");
        auto invalidValue = circuit.symbols()[1];
        invalidValue.value = "1k\n.end";
        circuit.updateSymbol(invalidValue);
        require(!exporters::exportSpice(circuit).success(),
                "multiline value cannot inject SPICE directives");
        invalidValue.value = "-1k";
        circuit.updateSymbol(invalidValue);
        require(!exporters::exportSpice(circuit).success(), "negative passive value rejected");
        invalidValue.value = "1meg";
        circuit.updateSymbol(invalidValue);
        require(exporters::exportSpice(circuit).success(), "mega multiplier supported");
        auto shorted = divider();
        shorted.addWire({-3000000, 0}, {3000000, 0});
        require(hasCode(connectivity::analyze(shorted), "shorted-voltage-source"),
                "shorted ideal source detected");
        require(!exporters::exportSpice(shorted).success(), "shorted source blocks SPICE");
        auto dragged = divider();
        const auto beforeMove = connectivity::analyze(dragged);
        dragged.moveObject(2, {3000000, 2000000});
        const auto afterMove = connectivity::analyze(dragged);
        require(afterMove.netForPin(1, 1)->name == afterMove.netForPin(2, 1)->name &&
                    afterMove.netForPin(2, 2)->name == afterMove.netForPin(3, 1)->name,
                "moving a component preserves both terminal connections");
        auto rotated = dragged.symbols()[1];
        rotated.rotation = 90;
        dragged.updateSymbol(rotated);
        const auto afterRotation = connectivity::analyze(dragged);
        require(afterRotation.netForPin(1, 1)->name == afterRotation.netForPin(2, 1)->name,
                "rotating a connected component preserves its input net");
        auto draggedConnection = divider();
        const auto connectionId = draggedConnection.wires()[0].id;
        draggedConnection.moveConnection(connectionId, {2000000, 1000000});
        const auto afterConnection = connectivity::analyze(draggedConnection);
        require(afterConnection.netForPin(1, 1)->name == afterConnection.netForPin(2, 1)->name,
                "dragging a connection keeps its fixed terminals connected");
        const auto missing = connectivity::analyze(open).diagnostics;
        require(std::any_of(missing.begin(), missing.end(),
                            [](const auto& item) {
                                return item.message.find("referencia") != std::string::npos;
                            }),
                "interface diagnostics use ASCII");
        core::Document detached;
        detached.addWire({0, 0}, {1000000, 0});
        require(hasCode(connectivity::analyze(detached), "dangling-wire"),
                "dangling wire detected");
        core::Document overlap;
        overlap.addWire({0, 0}, {10000000, 0});
        overlap.addWire({2000000, 0}, {12000000, 0});
        require(overlap.nets().size() == 1, "collinear overlap joins nets");
        const core::Wire huge{
            1,
            {std::numeric_limits<core::Coord>::min(), std::numeric_limits<core::Coord>::min()},
            {std::numeric_limits<core::Coord>::max(), std::numeric_limits<core::Coord>::max()}};
        require(connectivity::pointOnWire({0, 0}, huge), "full-range exact collinearity");
        require(!connectivity::pointOnWire({0, 1}, huge),
                "no floating-point false contact at huge coordinates");
        core::Symbol transistor;
        transistor.kind = core::SymbolKind::Library;
        transistor.reference = "Q";
        transistor.value = "TEST_NPN";
        transistor.libraryId = "Test:NPN";
        transistor.pinDefinitions = {{1, "B", "input", {0, 0}},
                                     {2, "C", "passive", {1000000, 0}},
                                     {3, "E", "passive", {2000000, 0}}};
        transistor.spiceType = "Q";
        transistor.spicePinOrder = {2, 1, 3};
        core::Document bipolar;
        bipolar.addLibrarySymbol(transistor, {0, 0});
        bipolar.addSymbol(core::SymbolKind::Ground, {2000000, 3000000});
        require(!exporters::exportSpice(bipolar).success(), "BJT requires a model library");
        const auto bjt = exporters::exportSpice(bipolar, {"BJT test", "models.lib"});
        require(bjt.success() && bjt.text.find("Q1 N1_2 N1_1 0 TEST_NPN") != std::string::npos,
                "BJT terminal order is collector, base, emitter");
        std::cout << "Connectivity graph, diagnostics, exact geometry and SPICE checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
