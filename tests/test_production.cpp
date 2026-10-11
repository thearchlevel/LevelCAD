#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <levelcad/app/BoardIO.hpp>
#include <levelcad/app/Production.hpp>
#include <stdexcept>
using namespace levelcad;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        core::Document doc;
        doc.addSymbol(core::SymbolKind::Resistor, {0, 0});
        doc.addSymbol(core::SymbolKind::Resistor, {10000000, 0});
        doc.addWire({3000000, 0}, {7000000, 0});
        pcb::Board board;
        board.synchronize(doc);
        board.projectName = "Production fixture";
        board.revision = "2.1";
        board.creator = "Wenderson Dias";
        QTemporaryDir directory;
        bool blocked = false;
        try {
            app::exportProduction(board, doc, directory.path(), app::productionTool());
        } catch (const std::exception&) {
            blocked = true;
        }
        require(blocked &&
                    QDir(directory.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).empty(),
                "unrouted export leaves no misleading package");
        require(board.routePending(0) == 1, "route manufacturing fixture");
        auto text = app::productionBoard(board);
        require(text.contains("(rev \"2.1\")") && text.contains("(company \"Wenderson Dias\")"),
                "manufacturing metadata");
        require(text.contains("(segment ") && text.contains("(drill ") &&
                    text.contains("\"*.Mask\""),
                "physical copper and solder pads exported");
        pcb::Board restored;
        QString error;
        require(app::boardFromJson(restored, app::boardToJson(board), &error),
                "native board roundtrip");
        require(restored.creator == board.creator && restored.revision == board.revision,
                "metadata persists");
        if (argc > 1 && QString::fromLocal8Bit(argv[1]) == "--integration") {
            auto path = app::exportProduction(board, doc, directory.path(), app::productionTool());
            QFile manifest(path + "/manifest.json");
            require(manifest.open(QIODevice::ReadOnly), "manifest exists");
            auto object = QJsonDocument::fromJson(manifest.readAll()).object();
            require(object["creator"].toString() == "Wenderson Dias", "manifest author");
            int gerbers = 0, drills = 0;
            QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                auto file = it.next();
                QFile input(file);
                input.open(QIODevice::ReadOnly);
                auto bytes = input.readAll();
                if (file.endsWith(".gbr") || file.endsWith(".gtl") || file.endsWith(".gbl") ||
                    file.endsWith(".gts") || file.endsWith(".gbs") || file.endsWith(".gko")) {
                    require(bytes.contains("M02*"), "Gerber terminator");
                    ++gerbers;
                }
                if (file.endsWith(".drl")) {
                    require(bytes.contains("M48") && bytes.contains("METRIC"),
                            "Excellon drill format");
                    ++drills;
                }
            }
            require(gerbers >= 2 && drills >= 1, "real fabrication files generated");
            std::cout << "Gerber and Excellon integration passed\n";
        }
        std::cout << "Manufacturing validation passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}