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
        pcb::Board invalid;
        invalid.addTrack({10000000, 20000000}, {30000000, 20000000}, 0, "A", 350000);
        invalid.addTrack({20000000, 10000000}, {20000000, 30000000}, 0, "B", 350000);
        require(!invalid.checkRules().empty(), "DRC detects copper short");
        std::cout << "PCB synchronization, layers, vias, DRC and 3D mesh passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
