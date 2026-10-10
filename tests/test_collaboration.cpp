#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <levelcad/app/BoardIO.hpp>
#include <levelcad/app/Collaboration.hpp>
#include <levelcad/app/DocumentIO.hpp>
#include <stdexcept>
using namespace levelcad;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool waitFor(const std::function<bool()>& condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    return condition();
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        core::Document document;
        document.addSymbol(core::SymbolKind::Resistor, {0, 0});
        auto note = document.addNote({2000000, 3000000}, "Revisão: tensão máxima 5 V — Ω", "Ana");
        auto base = app::documentToJson(document), current = base;
        auto first = document, second = document;
        auto symbol = first.symbols()[0];
        symbol.value = "22k";
        first.updateSymbol(symbol);
        symbol = second.symbols()[0];
        symbol.origin.x = 1000000;
        second.updateSymbol(symbol);
        QJsonArray audit;
        QString error;
        require(app::mergeEdits(base, app::documentToJson(first), current, audit, error),
                "first field accepted");
        require(app::mergeEdits(base, app::documentToJson(second), current, audit, error),
                "different fields merge");
        core::Document merged;
        require(app::documentFromJson(merged, current, &error), "merged state valid");
        require(merged.symbols()[0].value == "22k" && merged.symbols()[0].origin.x == 1000000,
                "both changes preserved");
        symbol = second.symbols()[0];
        symbol.value = "47k";
        second.updateSymbol(symbol);
        auto saved = current;
        require(!app::mergeEdits(base, app::documentToJson(second), current, audit, error),
                "same field conflict rejected");
        require(current == saved, "conflict is atomic");
        require(merged.notes()[0].text == document.notes()[0].text, "Unicode comments roundtrip");
        merged.moveObject(note, {1000000, 0});
        require(merged.notes()[0].position.x == 3000000, "comment moves independently");
        merged.setIdFloor(2000000000ULL);
        auto remote = document;
        remote.setIdFloor(3000000000ULL);
        remote.addSymbol(core::SymbolKind::Capacitor, {0, 0});
        require(app::documentFromJson(merged, app::documentToJson(remote), &error),
                "load remote namespace");
        require(merged.addNote({0, 0}, "local", "B") / 1000000000ULL == 2,
                "own namespace survives remote state");
        pcb::Board board;
        board.synchronize(document);
        base["pcb"] = app::boardToJson(board);
        QTemporaryDir directory;
        app::CollaborationServer server(directory.path());
        require(server.listen(0), "TCP server starts");
        app::CollaborationClient a, b;
        QJsonObject aState, bState;
        int aUpdates = 0, bUpdates = 0, auditCount = 0;
        a.onState = [&](const QJsonObject& state, bool) {
            aState = state;
            ++aUpdates;
        };
        b.onState = [&](const QJsonObject& state, bool) {
            bState = state;
            ++bUpdates;
        };
        b.onAudit = [&](const QJsonObject& entry) {
            require(entry["author"] == "Ana", "audit identifies author");
            ++auditCount;
        };
        a.join("127.0.0.1", server.port(), "Ana", base);
        require(waitFor([&] { return a.active(); }), "first client connects");
        b.join("127.0.0.1", server.port(), "Bruno", base);
        require(waitFor([&] { return b.active(); }), "second client connects");
        require(aState == bState, "clients share initial project");
        core::Document local;
        require(app::documentFromJson(local, aState, &error), "client decodes project");
        local.setIdFloor(a.idFloor());
        auto added = local.addNote({0, 0}, "Teste em tempo real", "Ana");
        require(added / 1000000000ULL == a.idFloor() / 1000000000ULL, "collaborative object ID");
        auto edit = app::documentToJson(local);
        edit["pcb"] = aState["pcb"];
        a.submit(edit);
        require(waitFor([&] { return aUpdates >= 2 && bUpdates >= 2; }),
                "live edit reaches both clients");
        require(aState == bState && auditCount > 0, "clients converge and audit delivered");
        board.footprints[0].position.x += 1000000;
        edit = aState;
        edit["pcb"] = app::boardToJson(board);
        a.submit(edit);
        require(waitFor([&] { return aUpdates >= 3 && bUpdates >= 3; }), "PCB layout synchronized");
        require(aState == bState, "PCB convergence");
        require(QFileInfo::exists(directory.filePath("edits.jsonl")), "audit persists on disk");
        require(app::saveDocument(local, directory.filePath("roundtrip.levelsch"), &error, &board),
                "integrated project saved");
        pcb::Board loadedBoard;
        core::Document loaded;
        require(app::loadDocument(loaded, directory.filePath("roundtrip.levelsch"), &error,
                                  &loadedBoard),
                "integrated project loaded");
        require(app::boardToJson(board) == app::boardToJson(loadedBoard), "PCB roundtrip");
        a.disconnect();
        b.disconnect();
        std::cout << "Comments, field merging, conflicts, IDs, two-client TCP and PCB "
                     "synchronization passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
