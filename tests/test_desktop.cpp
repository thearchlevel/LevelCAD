#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMdiSubWindow>
#include <QMouseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <levelcad/app/BoardEditor.hpp>
#include <levelcad/app/DocumentIO.hpp>
#include <levelcad/app/ExportDialog.hpp>
#include <levelcad/app/Language.hpp>
#include <levelcad/app/MainWindow.hpp>
#include <levelcad/app/ModelView.hpp>
#include <levelcad/app/SchematicView.hpp>
#include <levelcad/app/SymbolLibrary.hpp>
#include <stdexcept>
using namespace levelcad;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setOrganizationName("LevelCADTests");
    application.setApplicationName("DesktopValidation");
    QStandardPaths::setTestModeEnabled(true);
    try {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        core::Document d;
        for (int i = 0; i < 10; ++i)
            d.addSymbol(static_cast<core::SymbolKind>(i), {i * 10000000, 0});
        auto rotated = d.symbols()[4];
        rotated.rotation = 270;
        rotated.value = "custom model";
        d.updateSymbol(rotated);
        d.addWire({0, 0}, {1000000, 2000000});
        QString error;
        auto path = dir.filePath("roundtrip.levelsch");
        require(app::saveDocument(d, path, &error), "save");
        core::Document loaded;
        require(app::loadDocument(loaded, path, &error), "load");
        require(loaded.symbols().size() == 10 && loaded.wires().size() == 1, "roundtrip count");
        require(loaded.symbols()[4].rotation == 270 && loaded.symbols()[4].value == "custom model",
                "roundtrip properties");
        QFile input(path);
        require(input.open(QIODevice::ReadOnly), "read saved JSON");
        auto root = QJsonDocument::fromJson(input.readAll()).object();
        input.close();
        auto symbols = root["symbols"].toArray();
        auto bad = symbols[0].toObject();
        bad["id"] = symbols[1].toObject()["id"];
        symbols[0] = bad;
        root["symbols"] = symbols;
        QFile invalid(dir.filePath("invalid.levelsch"));
        require(invalid.open(QIODevice::WriteOnly), "write invalid fixture");
        invalid.write(QJsonDocument(root).toJson());
        invalid.close();
        require(!app::loadDocument(loaded, invalid.fileName(), &error) &&
                    loaded.symbols().size() == 10,
                "invalid load preserves document");
        app::SchematicView view;
        core::Document editable;
        view.setDocument(&editable);
        view.resize(800, 600);
        view.show();
        application.processEvents();
        int changes = 0;
        view.onChanged = [&] { ++changes; };
        auto click = [&](QPointF scenePosition) {
            auto local = view.mapFromScene(scenePosition);
            QMouseEvent event(QEvent::MouseButtonPress, QPointF(local),
                              QPointF(view.viewport()->mapToGlobal(local)), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view.viewport(), &event);
        };
        view.setTool(app::SchematicView::Tool::Resistor);
        click({0, 0});
        require(editable.symbols().size() == 1 && changes == 1, "component placement callback");
        view.setTool(app::SchematicView::Tool::Select);
        view.selectObject(editable.symbols()[0].id);
        view.rotateSelected();
        require(editable.symbols()[0].rotation == 90, "editor rotation");
        view.duplicateSelected();
        require(editable.symbols().size() == 2, "editor duplication");
        view.setTool(app::SchematicView::Tool::Wire);
        click({100, 100});
        click({150, 150});
        require(editable.wires().size() == 2, "orthogonal routing creates two segments");
        view.setTool(app::SchematicView::Tool::Select);
        auto id = editable.symbols()[0].id;
        view.selectObject(id);
        view.deleteSelected();
        require(editable.symbols().size() == 1, "editor deletion");
        auto pdf = dir.filePath("schematic.pdf");
        view.exportPdf(pdf);
        QFile pdfFile(pdf);
        require(pdfFile.open(QIODevice::ReadOnly) && pdfFile.read(4) == "%PDF", "PDF export");
        const auto library = app::bundledLibrary();
        std::cout << "Bundled KiCad symbols: " << library.size() << '\n';
        require(library.size() > 1000, "large real component library bundled");
        const auto found = std::find_if(library.begin(), library.end(), [](const auto& entry) {
            return entry.name == "Timer:NE555D";
        });
        require(found != library.end() && found->prototype.pinDefinitions.size() == 8,
                "real NE555 timer has eight pins");
        core::Document imported;
        imported.addLibrarySymbol(found->prototype, {0, 0});
        require(imported.nets().size() == 8, "connectivity respects eight imported terminals");
        require(app::saveDocument(imported, path, &error), "save imported symbol");
        require(app::loadDocument(loaded, path, &error), "load imported symbol");
        require(loaded.symbols()[0].libraryId == "Timer:NE555D" &&
                    loaded.symbols()[0].pinDefinitions.size() == 8 &&
                    !loaded.symbols()[0].strokes.empty(),
                "embedded library geometry and pins roundtrip");
        view.setDocument(&imported);
        view.setTool(app::SchematicView::Tool::Select);
        view.selectObject(imported.symbols()[0].id);
        view.duplicateSelected();
        require(imported.symbols().size() == 2 && imported.symbols()[1].pinDefinitions.size() == 8,
                "duplicate preserves imported pins");
        QSettings().setValue("ui/language", "pt_BR");
        // Um teste anterior pode deixar a recuperacao automatica neste diretorio de testes.
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                      "/recovery.levelsch");
        app::MainWindow window;
        window.show();
        application.processEvents();
        require(window.windowTitle().startsWith("LevelCAD"), "application title");
        bool publisherVisible = false;
        for (auto* label : window.findChildren<QLabel*>())
            if (label->text().contains("The Archlevel Foundation"))
                publisherVisible = true;
        require(publisherVisible, "publisher credits visible");
        app::SchematicView* mainView = nullptr;
        for (auto* widget : window.findChildren<QWidget*>())
            if (auto* candidate = dynamic_cast<app::SchematicView*>(widget))
                mainView = candidate;
        require(mainView != nullptr, "schematic view in managed workspace");
        int operations = 0;
        auto originalCallback = mainView->onChanged;
        mainView->onChanged = [&] {
            ++operations;
            originalCallback();
        };
        mainView->setTool(app::SchematicView::Tool::Resistor);
        auto local = mainView->mapFromScene(QPointF(0, 0));
        QMouseEvent placement(QEvent::MouseButtonPress, QPointF(local),
                              QPointF(mainView->viewport()->mapToGlobal(local)), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(mainView->viewport(), &placement);
        require(operations == 1 && window.windowTitle().contains(" *"),
                "main window records edits and dirty title");
        QAction* undo = nullptr;
        QAction* redo = nullptr;
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->text() == "Desfazer" && action->isEnabled())
                undo = action;
            if (action->text() == "Refazer")
                redo = action;
        }
        require(undo && redo && undo->isEnabled(), "history actions available");
        undo->trigger();
        require(!window.windowTitle().contains(" *"), "undo returns to clean state");
        redo->trigger();
        require(window.windowTitle().contains(" *"), "redo restores dirty state");
        undo->trigger();
        for (auto* dock : window.findChildren<QDockWidget*>())
            dock->hide();
        QAction* restore = nullptr;
        for (auto* action : window.findChildren<QAction*>())
            if (action->text() == "Restaurar layout")
                restore = action;
        require(restore != nullptr, "restore layout action");
        restore->trigger();
        for (auto* dock : window.findChildren<QDockWidget*>())
            require(!dock->isHidden(), "closed side panel restored");
        app::setUiLanguage("en", &window);
        require(QCoreApplication::translate("MainWindow", "Excluir selecao") == "Delete selection",
                "English translation is active");
        app::setUiLanguage("es", &window);
        require(QCoreApplication::translate("MainWindow", "Excluir selecao") ==
                    "Eliminar seleccion",
                "Spanish translation is active");
        app::setUiLanguage("pt_BR", &window);
        require(QCoreApplication::translate("MainWindow", "Excluir selecao") ==
                    QString::fromUtf8("Excluir selecao"),
                "Portuguese translation restored");
        bool saveMode = false;
        const auto exported = dir.filePath("explicit-export.csv");
        QTimer exportDriver;
        exportDriver.setInterval(50);
        QObject::connect(&exportDriver, &QTimer::timeout, [&] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            exportDriver.stop();
            saveMode = dialog->acceptMode() == QFileDialog::AcceptSave &&
                       dialog->fileMode() == QFileDialog::AnyFile;
            dialog->selectFile(exported);
            QMetaObject::invokeMethod(dialog, "accept");
        });
        exportDriver.start();
        const auto selected = app::chooseExportFile(&window, "Exportar", {}, "CSV (*.csv)");
        require(saveMode && selected == exported, "export uses an explicit save dialog");
        auto unicode = imported.symbols()[0];
        unicode.reference = "CI_ação_Ω";
        unicode.value = "Amplificação µF — çãé 中文";
        imported.updateSymbol(unicode);
        require(app::saveDocument(imported, path, &error), "save Unicode properties");
        require(app::loadDocument(loaded, path, &error), "load Unicode properties");
        require(loaded.symbols()[0].reference == unicode.reference &&
                    loaded.symbols()[0].value == unicode.value,
                "Unicode roundtrip preserves accents, symbols and non-Latin text");
        for (auto* action : window.findChildren<QAction*>())
            for (const auto character : action->text())
                require(character.unicode() < 128, "interface actions use ASCII");
        pcb::Board board;
        board.synchronize(d);
        app::BoardEditor boardEditor(&board);
        boardEditor.resize(1000, 700);
        boardEditor.show();
        boardEditor.fitBoard();
        application.processEvents();
        require(!boardEditor.grab().isNull(), "PCB view renders");
        app::ModelView model(app::ModelView::Mode::Board, &board, &d);
        model.resize(1000, 700);
        model.show();
        model.resetCamera();
        application.processEvents();
        require(!model.grab().isNull(), "PCB 3D view renders");
        if (argc > 1) {
            const auto preview = QString::fromLocal8Bit(argv[1]);
            require(window.grab().save(preview), "preview screenshot");
            require(boardEditor.grab().save(preview + ".pcb.png"), "PCB screenshot");
            require(model.grab().save(preview + ".3d.png"), "3D screenshot");
        }
        std::cout << "Desktop placement, editing, routing, persistence, validation and PDF checks "
                     "passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
