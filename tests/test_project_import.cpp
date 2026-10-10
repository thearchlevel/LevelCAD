#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <levelcad/app/ProjectImport.hpp>
#include <stdexcept>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        auto path = directory.filePath("fixture.kicad_pcb");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            throw std::runtime_error("Cannot create fixture");
        file.write(R"((kicad_pcb (version 20240108)
          (layers (0 "F.Cu" signal) (31 "B.Cu" signal))
          (net 1 "GND")
          (footprint "Connector:Test" (at 12 15 90)
            (property "Reference" "J1") (property "Value" "Connector")
            (pad "1" thru_hole oval (at 0 0) (size 2 3)
              (drill oval 0.8 1.2) (layers "*.Cu") (net 1 "GND")))
          (segment (start 12 15) (end 20 15) (width 0.25) (layer "F.Cu") (net 1))
          (gr_rect (start 10 10) (end 30 30) (layer "Edge.Cuts"))))");
        file.close();
        auto imported = levelcad::app::importExternalProject(path);
        if (!imported.hasBoard || imported.board.footprints.size() != 1 ||
            imported.board.tracks.size() != 1 || imported.board.layers.size() != 2)
            throw std::runtime_error("PCB objects missing");
        const auto& pad = imported.board.footprints.front().pads.front();
        if (pad.drill != 800000 || pad.net != "GND" || imported.warnings.isEmpty())
            throw std::runtime_error("Oval drill approximation must be reported");
        if (imported.board.footprints.front().position != levelcad::core::Point{2000000, 5000000})
            throw std::runtime_error("Board origin normalization failed");
        std::cout << "External PCB import passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
