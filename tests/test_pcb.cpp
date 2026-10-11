#include <iostream>
#include <levelcad/pcb/Board.hpp>
#include <stdexcept>
using namespace levelcad;

void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}

int main() {
    try {
        core::Document circuit;
        auto a = circuit.addSymbol(core::SymbolKind::Resistor, {0, 0});
        auto b = circuit.addSymbol(core::SymbolKind::Resistor, {10000000, 0});
        circuit.addWire({3000000, 0}, {7000000, 0});
        pcb::Board board;
        board.synchronize(circuit);
        require(board.footprints.size() == 2, "schematic becomes PCB footprints");
        require(board.airwires().size() == 1, "shared net produces one airwire");
        auto& first = board.footprints[0];
        auto& second = board.footprints[1];
        auto p = board.padPosition(first, first.pads[1]);
        auto q = board.padPosition(second, second.pads[0]);
        auto net = first.pads[1].net;
        core::Point center{(p.x + q.x) / 2, p.y};
        board.addTrack(p, center, 0, net, 350000);
        board.addTrack(center, q, 1, net, 350000);
        require(board.airwires().size() == 1, "different layers require a via");
        board.addVia(center, net);
        require(board.airwires().empty(), "via closes the copper connection");
        require(board.checkRules().empty(), "valid layout passes available DRC");
        first.position.x += 2000000;
        first.rotation = 90;
        auto moved = first.position;
        board.synchronize(circuit);
        require(board.footprints[0].position == moved && board.footprints[0].rotation == 90,
                "resync preserves layout");
        require(!board.airwires().empty(), "moving footprint exposes unrouted connection");
        auto model = board.exportObj();
        require(model.find("o PCB") != std::string::npos &&
                    model.find("o R1") != std::string::npos &&
                    model.find("\nf ") != std::string::npos,
                "3D export contains substrate and component faces");
        core::Document growing;
        growing.addSymbol(core::SymbolKind::Resistor, {0, 0});
        pcb::Board expanding;
        expanding.synchronize(growing);
        const auto originalPosition = expanding.footprints.front().position;
        growing.addSymbol(core::SymbolKind::Resistor, {400000000, 200000000});
        expanding.synchronize(growing);
        require(expanding.width > 100000000 && expanding.height > 80000000,
                "new distant components expand board");
        require(expanding.footprints.front().position == originalPosition,
                "board expansion preserves existing placement");
        for (const auto& footprint : expanding.footprints)
            for (const auto& pad : footprint.pads) {
                auto position = expanding.padPosition(footprint, pad);
                require(position.x >= 0 && position.y >= 0 && position.x < expanding.width &&
                            position.y < expanding.height,
                        "synchronized pads remain inside board");
            }
        pcb::Board invalid;
        invalid.addTrack({10000000, 20000000}, {30000000, 20000000}, 0, "A", 350000);
        invalid.addTrack({20000000, 10000000}, {20000000, 30000000}, 0, "B", 350000);
        require(!invalid.checkRules().empty(), "DRC detects copper short");
        pcb::Board audited;
        audited.synchronize(circuit);
        require(audited.schematicIssues(circuit).empty(),
                "pin by pin audit accepts synchronized board");
        audited.footprints[0].pads[0].net = "wrong-net";
        require(!audited.schematicIssues(circuit).empty(), "audit detects wrong pad net");
        audited.synchronize(circuit);
        require(audited.routePending(0) == 1 && audited.airwires().empty(),
                "simple router closes real copper connectivity");
        require(audited.checkRules().empty(), "router respects copper clearance");
        const auto separation = audited.footprints[1].position.x - audited.footprints[0].position.x;
        require(audited.fitOutline(), "fit rectangular outline");
        require(audited.width < 100000000 && audited.height < 80000000,
                "fit shrinks oversized board");
        require(audited.footprints[1].position.x - audited.footprints[0].position.x == separation &&
                    audited.airwires().empty(),
                "fit preserves relative placement and tracks");
        audited.outline = {{{0, 0}, {10000000, 0}, {10000000, 10000000}, {0, 0}}};
        require(!audited.fitOutline(), "fit preserves custom outline");
        core::Document virtualSource;
        virtualSource.addSymbol(core::SymbolKind::VoltageSource, {0, 0});
        pcb::Board supply;
        supply.synchronize(virtualSource);
        require(!supply.schematicIssues(virtualSource).empty(),
                "virtual source requires physical mapping");
        std::cout << "PCB synchronization, layers, vias, DRC and 3D mesh passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
