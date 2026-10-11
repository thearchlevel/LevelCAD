#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHostAddress>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <levelcad/app/AiAssistant.hpp>
#include <levelcad/app/BoardIO.hpp>
#include <levelcad/app/CadCommands.hpp>
#include <levelcad/app/DocumentIO.hpp>
#include <levelcad/app/ExportDialog.hpp>
#include <levelcad/app/Language.hpp>
#include <levelcad/app/MainWindow.hpp>
#include <levelcad/app/MeshLibrary.hpp>
#include <levelcad/app/OfficialLibrary.hpp>
#include <levelcad/app/PluginEngine.hpp>
#include <levelcad/app/Production.hpp>
#include <levelcad/app/ProjectImport.hpp>
#include <levelcad/connectivity/Connectivity.hpp>
#include <levelcad/exporters/SpiceExporter.hpp>
#include <map>
#include <set>

namespace levelcad::app {
namespace {
bool writeFile(QWidget* parent, const QString& path, const QByteArray& bytes) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        QMessageBox::warning(parent, QObject::tr("Falha na exportacao"), f.errorString());
        return false;
    }
    return true;
}

QString csv(const std::string& v) {
    auto s = QString::fromStdString(v);
    s.replace('"', "\"\"");
    return '"' + s + '"';
}

QString recoveryPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
           "/recovery.levelsch";
}
} // namespace

MainWindow::MainWindow(std::function<void(int, const QString&)> progress) {
    if (progress)
        progress(10, "Preparando ferramentas e paineis...");
    resize(1440, 900);
    setWindowIcon(QIcon(":/branding/icon.png"));
    view_ = new SchematicView(this);
    view_->setDocument(&document_);
    workspace_ = new QMdiArea(this);
    workspace_->setViewMode(QMdiArea::TabbedView);
    workspace_->setTabsClosable(true);
    workspace_->setTabsMovable(true);
    setCentralWidget(workspace_);
    auto* file = menuBar()->addMenu(QObject::tr("&Arquivo"));
    auto add = [this](QMenu* menu, const QString& text, const QKeySequence& shortcut,
                      auto callback) {
        auto* a = menu->addAction(text);
        a->setShortcut(shortcut);
        connect(a, &QAction::triggered, this, callback);
        return a;
    };
    add(file, QObject::tr("&Novo"), QKeySequence::New, [this] { newDocument(); });
    add(file, QObject::tr("&Abrir..."), QKeySequence::Open, [this] { openDocument(); });
    recentMenu_ = file->addMenu(QObject::tr("Arquivos recentes"));
    rebuildRecent();
    add(file, QObject::tr("&Salvar"), QKeySequence::Save, [this] { saveDocument(); });
    add(file, QObject::tr("Salvar &como..."), QKeySequence::SaveAs, [this] { saveDocumentAs(); });
    add(file, QObject::tr("Importar biblioteca de simbolos..."), {}, [this] { importLibrary(); });
    file->addSeparator();
    auto* exp = file->addMenu(QObject::tr("Exportar"));
    add(exp, QObject::tr("Lista de materiais (CSV)"), {}, [this] { exportBom(); });
    add(exp, QObject::tr("Netlist de conexAues (JSON)"), {}, [this] { exportNetlist(); });
    add(exp, QObject::tr("Netlist SPICE (.cir)"), QKeySequence("F9"), [this] { exportSpice(); });
    add(exp, QObject::tr("Esquematico (PDF)"), {}, [this] {
        auto p = chooseExportFile(this, QObject::tr("Exportar PDF"), {}, "PDF (*.pdf)");
        if (p.isEmpty())
            return;
        if (!p.endsWith(".pdf", Qt::CaseInsensitive))
            p += ".pdf";
        view_->exportPdf(p);
        statusBar()->showMessage(QObject::tr("PDF exportado"), 3000);
    });
    file->addSeparator();
    add(file, QObject::tr("Sair"), QKeySequence::Quit, [this] { close(); });
    auto* edit = menuBar()->addMenu(QObject::tr("&Editar"));
    undoAction_ = add(edit, QObject::tr("Desfazer"), QKeySequence::Undo, [this] { undo(); });
    redoAction_ = add(edit, QObject::tr("Refazer"), QKeySequence::Redo, [this] { redo(); });
    edit->addSeparator();
    add(edit, QObject::tr("Excluir selecao"), QKeySequence::Delete,
        [this] { view_->deleteSelected(); });
    add(edit, QObject::tr("Duplicar selecao"), QKeySequence("Ctrl+D"),
        [this] { view_->duplicateSelected(); });
    add(edit, QObject::tr("Girar 90 deg"), QKeySequence("R"), [this] { view_->rotateSelected(); });
    add(edit, QObject::tr("Propriedades..."), QKeySequence("Alt+Return"),
        [this] { view_->editSelected(); });
    auto* display = menuBar()->addMenu(QObject::tr("&Visualizar"));
    add(display, QObject::tr("Ajustar desenho"), QKeySequence("F"),
        [this] { view_->fitDrawing(); });
    add(display, QObject::tr("Zoom original"), QKeySequence("Ctrl+0"),
        [this] { view_->resetTransform(); });
    auto* lightTheme = display->addAction("Tema claro do esquematico");
    lightTheme->setCheckable(true);
    connect(lightTheme, &QAction::toggled, view_, &SchematicView::setLightTheme);
    auto* grid = display->addAction(QObject::tr("Mostrar grade"));
    grid->setCheckable(true);
    grid->setChecked(true);
    connect(grid, &QAction::toggled, view_, &SchematicView::setGridVisible);
    auto* snap = display->addAction(QObject::tr("Ajustar a grade"));
    snap->setCheckable(true);
    snap->setChecked(true);
    connect(snap, &QAction::toggled, view_, &SchematicView::setSnapEnabled);
    auto* tools = menuBar()->addMenu(QObject::tr("&Circuito"));
    add(tools, QObject::tr("Verificar circuito (Electrical Connectivity Engine)"),
        QKeySequence("F8"), [this] { runErc(); });
    auto* help = menuBar()->addMenu(QObject::tr("&Ajuda"));
    add(help, QObject::tr("Sobre o LevelCAD"), {}, [this] {
        QMessageBox::about(
            this, QObject::tr("Sobre o LevelCAD"),
            "<h2>LevelCAD - Versao oficial</h2><p>Schematic Capture / PCB Layout / 3D "
            "Viewer</p><p><b>Creditos: "
            "Wenderson Dias</b><br>Publicado pela The Archlevel Foundation.</p><p>Software livre / "
            "GPL-3.0-or-later</p><p>Motor de conectividade, grafo eletrico e exportacao SPICE. "
            "PCB Layout, visualizador 3D e colaboracao TCP integrados. Footprints genericos; "
            "Gerber e simulacao interna ainda nao implementados.</p>");
    });
    add(help, QObject::tr("Creditos da biblioteca"), {}, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle(QObject::tr("Creditos e licenca da biblioteca"));
        dialog.resize(760, 550);
        QVBoxLayout layout(&dialog);
        QTextBrowser text;
        QFile license(":/kicad/LICENSE.md");
        if (license.open(QIODevice::ReadOnly))
            text.setMarkdown(
                "# KiCad Library Contributors\n\nFonte: "
                "https://gitlab.com/kicad/libraries\n\n"
                "Simbolos, footprints e modelos 3D: KiCad Library Contributors.\n\n"
                "Bibliotecas locais oficiais; consulte libraries/official/inventory.json "
                "para as revisoes e os arquivos LICENSE.md e CREDITS.md para os autores.\n\n" +
                QString::fromUtf8(license.readAll()));
        layout.addWidget(&text);
        dialog.exec();
    });
    auto* toolbar = addToolBar(QObject::tr("Ferramentas"));
    toolbar->setObjectName("DrawingTools");
    toolbar->setMovable(false);
    auto* group = new QActionGroup(this);
    group->setExclusive(true);
    std::map<SchematicView::Tool, QAction*> actions;
    auto tool = [&](const QString& text, SchematicView::Tool t, const QString& key) {
        auto* a = toolbar->addAction(text);
        a->setCheckable(true);
        a->setShortcut(QKeySequence(key));
        group->addAction(a);
        actions[t] = a;
        connect(a, &QAction::triggered, this, [this, t] { view_->setTool(t); });
        return a;
    };
    tool(QObject::tr("Selecionar"), SchematicView::Tool::Select, "S")->setChecked(true);
    tool("Pan", SchematicView::Tool::Pan, "P");
    tool(QObject::tr("Ligacao"), SchematicView::Tool::Wire, "W");
    toolbar->addSeparator();
    tool(QObject::tr("Resistor"), SchematicView::Tool::Resistor, "");
    tool(QObject::tr("Capacitor"), SchematicView::Tool::Capacitor, "");
    tool(QObject::tr("Terra"), SchematicView::Tool::Ground, "G");
    tool(QObject::tr("Rotulo de rede"), SchematicView::Tool::NetLabel, "L");
    tool(QObject::tr("Comentario"), SchematicView::Tool::Comment, "T");
    tool(QObject::tr("Area de fundo"), SchematicView::Tool::Area, "Shift+A");
    toolbar->addSeparator();
    toolbar->addAction(undoAction_);
    toolbar->addAction(redoAction_);
    auto* workflow = addToolBar(QObject::tr("Projeto e verificacao"));
    workflow->setObjectName("ProjectWorkflow");
    workflow->addAction(QObject::tr("Salvar"), this, [this] { saveDocument(); });
    workflow->addSeparator();
    workflow->addAction(QObject::tr("Ajustar desenho"), view_, &SchematicView::fitDrawing);
    workflow->addAction(grid);
    workflow->addSeparator();
    workflow->addAction(QObject::tr("Verificar circuito"), this, [this] { runErc(); });
    view_->onToolChanged = [this, actions](SchematicView::Tool t) {
        auto it = actions.find(t);
        if (it != actions.end())
            it->second->setChecked(true);
        else
            for (auto [k, a] : actions)
                a->setChecked(false);
        statusBar()->showMessage(
            t == SchematicView::Tool::Wire
                ? "Ligacoes: clique nos pontos; Esc encerra. Cruzamentos sem extremidade nao "
                  "conectam."
                : "Selecione e arraste objetos / R gira / duplo clique edita / roda aproxima",
            5000);
    };
    auto* library = new QDockWidget(QObject::tr("Biblioteca de componentes"), this);
    library->setObjectName("ComponentLibrary");
    auto* panel = new QWidget(library);
    auto* layout = new QVBoxLayout(panel);
    auto* search = new QLineEdit(panel);
    search->setPlaceholderText(QObject::tr("Buscar componente..."));
    auto* list = new QListWidget(panel);
    libraryList_ = list;
    libraryCount_ = new QLabel(panel);
    layout->addWidget(search);
    layout->addWidget(list);
    layout->addWidget(libraryCount_);
    const QStringList names = {QObject::tr("Resistor"),        QObject::tr("Capacitor"),
                               QObject::tr("Terra (GND)"),     QObject::tr("Indutor"),
                               QObject::tr("Diodo"),           "LED",
                               QObject::tr("Fonte de tensao"), QObject::tr("Fonte de corrente"),
                               QObject::tr("Chave SPST"),      QObject::tr("Conector 2 pinos")};
    for (int i = 0; i < names.size(); ++i) {
        auto* item = new QListWidgetItem(names[i], list);
        item->setData(Qt::UserRole, i + 1);
    }
    connect(search, &QLineEdit::textChanged, this, [list](const QString& s) {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setHidden(!(list->item(i)->text() + list->item(i)->toolTip())
                                          .contains(s, Qt::CaseInsensitive));
    });
    connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* i) {
        const int index = i->data(Qt::UserRole).toInt();
        if (index >= 100)
            view_->setLibrarySymbol(libraryEntries_.at(index - 100).prototype);
        else
            view_->setTool(static_cast<SchematicView::Tool>(index));
    });
    try {
        if (progress)
            progress(25, "Carregando a biblioteca de componentes...");
        appendLibrary(bundledLibrary());
        if (progress)
            progress(60, "Biblioteca pronta. Preparando PCB e modelos 3D...");
        for (const auto& path : QSettings().value("libraries/imported").toStringList()) {
            if (QFileInfo::exists(path))
                appendLibrary(loadKiCadLibrary(path).entries);
        }
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QObject::tr("Falha ao carregar biblioteca"),
                             QString::fromUtf8(error.what()));
    }
    library->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, library);
    auto* explorer = new QDockWidget(QObject::tr("Objetos do projeto"), this);
    explorer->setObjectName("ProjectExplorer");
    tree_ = new QTreeWidget(explorer);
    tree_->setHeaderLabels({QObject::tr("Referencia"), QObject::tr("Valor / tipo")});
    explorer->setWidget(tree_);
    addDockWidget(Qt::RightDockWidgetArea, explorer);
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* i, int) {
        view_->scene()->clearSelection();
        view_->selectObject(i->data(0, Qt::UserRole).toULongLong());
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem*, int) { view_->editSelected(); });
    auto* guide = new QDockWidget(QObject::tr("Guia e creditos"), this);
    guide->setObjectName("Guide");
    auto* label = new QLabel(
        "<h3>LevelCAD</h3><p>Selecione componentes na biblioteca e clique no "
        "desenho.</p><p><b>S</b> selecionar e mover<br><b>P</b> deslocar tela<br><b>W</b> fios "
        "ortogonais<br><b>R</b> girar selecao<br><b>F</b> ajustar desenho<br><b>F8</b> verificar "
        "conexAues<br><b>Esc</b> cancelar ferramenta<br><b>Ctrl+D</b> duplicar<br><b>Delete</b> "
        "excluir<br>Duplo clique: propriedades</p><p>Mover ou girar componentes preserva as "
        "conexoes. "
        "Arrastar uma ligacao move seu conjunto conectado. Ctrl+Alt+T insere "
        "comentarios.</p><p><b>Criado por Wenderson "
        "Dias</b><br>Publicado "
        "pela The Archlevel Foundation.</p>",
        guide);
    label->setWordWrap(true);
    label->setMargin(12);
    label->setAlignment(Qt::AlignTop);
    guide->setWidget(label);
    addDockWidget(Qt::RightDockWidgetArea, guide);
    setStyleSheet(
        "QMainWindow,QDockWidget,QMenuBar,QToolBar,QTreeWidget,QListWidget,QLabel,QStatusBar "
        "{background:#222d40;color:#e4edfa;} QMenu,QDialog {background:#222d40;color:#e4edfa;} "
        "QLineEdit,QComboBox {background:#141b28;color:#e4edfa;padding:7px;border:1px solid "
        "#40526c;border-radius:4px;} "
        "QMenuBar::item:selected,QMenu::item:selected,QToolButton:checked,QListWidget::item:"
        "selected,QTreeWidget::item:selected {background:#365a75;} QToolButton {padding:7px;} "
        "QToolButton:hover {background:#40526c;} QDockWidget::title {padding:8px;} ");
    view_->onChanged = [this] { recordChange(); };
    resetHistory();
    rebuildTree();
    setupWorkspace();
    setupAutomation();
    if (progress)
        progress(85, "Preparando colaboracao, IA e preferencias...");
    defaultLayout_ = saveState();
    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    restoreState(settings.value("window/state").toByteArray());
    // Recovery is a separate file, so autosave never overwrites the project chosen by the user.
    auto* timer = new QTimer(this);
    timer->setInterval(60000);
    connect(timer, &QTimer::timeout, this, [this] {
        if (historyIndex_ == savedIndex_)
            return;
        QDir().mkpath(QFileInfo(recoveryPath()).absolutePath());
        QString error;
        if (!app::saveDocument(document_, recoveryPath(), &error, &board_))
            statusBar()->showMessage("Falha ao salvar recuperacao: " + error, 5000);
        else
            statusBar()->showMessage(QObject::tr("Copia de recuperacao atualizada"), 2500);
    });
    timer->start();
    if (QFileInfo::exists(recoveryPath())) {
        if (QMessageBox::question(
                this, QObject::tr("Recuperar trabalho"),
                QObject::tr("Existe uma copia de recuperacao. Deseja abri-la?")) ==
            QMessageBox::Yes) {
            QString error;
            if (app::loadDocument(document_, recoveryPath(), &error, &board_)) {
                view_->refresh();
                resetHistory();
                savedIndex_ = -1;
                rebuildTree();
                updateTitle();
            } else
                QMessageBox::warning(this, QObject::tr("Falha na recuperacao"), error);
        }
    }
    statusBar()->addPermanentWidget(new QLabel("Powered by The Archlevel Foundation", this));
    auto* collaborate = menuBar()->addMenu(tr("Colaboracao"));
    auto* labelAction = collaborate->addAction(tr("Inserir rotulo de rede"));
    connect(labelAction, &QAction::triggered, this, [this] {
        view_->setTool(SchematicView::Tool::NetLabel);
        showWorkspace(0);
    });
    auto* comment = collaborate->addAction(tr("Inserir comentario"));
    comment->setShortcut(QKeySequence("Ctrl+Alt+T"));
    connect(comment, &QAction::triggered, this, [this] {
        view_->setTool(SchematicView::Tool::Comment);
        showWorkspace(0);
    });
    collaboration_ = new CollaborationClient(this);
    auto* auditDock = new QDockWidget(tr("Historico de colaboracao"), this);
    auditDock->setObjectName("collaborationAudit");
    auto* auditTree = new QTreeWidget(auditDock);
    auditTree->setHeaderLabels(
        {tr("Horario"), tr("Pessoa"), tr("Objeto"), tr("Campo"), tr("Antes"), tr("Depois")});
    auditDock->setWidget(auditTree);
    addDockWidget(Qt::BottomDockWidgetArea, auditDock);
    auditDock->hide();
    collaborate->addAction(auditDock->toggleViewAction());
    collaboration_->onAudit = [auditTree](const QJsonObject& entry) {
        auto display = [](const QJsonValue& value) {
            return value.isString()
                       ? value.toString()
                       : QString::fromUtf8(
                             QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact));
        };
        auto* item = new QTreeWidgetItem(
            auditTree,
            {entry["time"].toString(), entry["author"].toString(),
             entry["collection"].toString() + ":" + entry["id"].toString(),
             entry["field"].toString(), display(entry["before"]), display(entry["after"])});
        for (int column = 0; column < 6; ++column)
            item->setToolTip(column, item->text(column));
        auditTree->scrollToItem(item);
    };
    collaboration_->onBusy = [this](bool busy) {
        view_->setEnabled(!busy);
        boardView_->setEnabled(!busy);
        boardModel_->setEnabled(!busy);
    };
    collaboration_->onStatus = [this](const QString& status) {
        statusBar()->showMessage(status, 30000);
    };
    collaboration_->onState = [this](const QJsonObject& state, bool initial) {
        QString error;
        if (!documentFromJson(document_, state, &error)) {
            statusBar()->showMessage(error);
            return;
        }
        if (state.contains("pcb") && !boardFromJson(board_, state["pcb"].toObject(), &error)) {
            statusBar()->showMessage(error);
            return;
        }
        boardView_->refresh();
        boardModel_->update();
        rebuildBoardPanels();
        document_.setIdFloor(collaboration_->idFloor());
        resetHistory();
        savedIndex_ = hosting_ || collaboration_->active() ? 0 : -1;
        if (hosting_)
            path_ = hostedDirectory_ + "/project.levelsch";
        view_->refresh();
        rebuildTree();
        updateTitle();
        schematicModel_->update();
        if (initial)
            view_->fitDrawing();
    };
    auto* hostAction = collaborate->addAction(tr("Hospedar projeto..."));
    connect(hostAction, &QAction::triggered, this, &MainWindow::hostProject);
    auto* stopHost = collaborate->addAction(tr("Encerrar servidor"));
    connect(stopHost, &QAction::triggered, this, &MainWindow::stopHosting);
    auto* participantsDock = new QDockWidget(tr("Participantes"), this);
    participantsDock->setObjectName("CollaborationParticipants");
    auto* participantsPanel = new QWidget;
    auto* participantsLayout = new QVBoxLayout(participantsPanel);
    auto* participants = new QTreeWidget;
    participants->setHeaderLabels({tr("Nome"), tr("Funcao"), tr("IPv4")});
    participantsLayout->addWidget(participants);
    auto* kick = new QPushButton(tr("Expulsar"));
    auto* ban = new QPushButton(tr("Bloquear IPv4"));
    auto* unban = new QPushButton(tr("Desbloquear IPv4..."));
    for (auto* button : {kick, ban, unban}) {
        participantsLayout->addWidget(button);
        button->setEnabled(false);
    }
    participantsDock->setWidget(participantsPanel);
    addDockWidget(Qt::RightDockWidgetArea, participantsDock);
    participantsDock->hide();
    collaborate->addAction(participantsDock->toggleViewAction());
    collaboration_->onRoster = [participants, kick, ban, unban,
                                participantsDock](const QJsonObject& message) {
        participants->clear();
        for (auto value : message["participants"].toArray()) {
            auto entry = value.toObject();
            auto* item =
                new QTreeWidgetItem(participants, {entry["name"].toString(),
                                                   entry["host"].toBool() ? "Host" : "Participante",
                                                   entry["ip"].toString()});
            item->setData(0, Qt::UserRole, entry["actor"].toVariant());
        }
        for (auto* button : {kick, ban, unban})
            button->setEnabled(message["admin"].toBool());
        participantsDock->show();
    };
    auto moderate = [this, participants](const QString& action) {
        if (auto* item = participants->currentItem())
            collaboration_->adminAction(action, item->data(0, Qt::UserRole).toString());
    };
    connect(kick, &QPushButton::clicked, this, [moderate] { moderate("kick"); });
    connect(ban, &QPushButton::clicked, this, [moderate] { moderate("ban"); });
    connect(unban, &QPushButton::clicked, this, [this] {
        bool ok = false;
        auto ip = QInputDialog::getText(this, tr("Desbloquear acesso"), tr("IPv4 bloqueado"),
                                        QLineEdit::Normal, {}, &ok);
        if (ok)
            collaboration_->adminAction("unban", {}, ip.trimmed());
    });
    collaboration_->onPresence = [this](const QJsonObject& presence) {
        view_->showPresence(presence);
        boardView_->showPresence(presence);
    };
    auto publish = [this](const QString& workspace, const std::vector<std::uint64_t>& ids) {
        QJsonArray values;
        for (auto id : ids)
            values.append(QString::number(id));
        collaboration_->publishSelection(workspace, values);
    };
    view_->onSelectionChanged = [publish](const auto& ids) { publish("schematic", ids); };
    boardView_->onSelectionChanged = [publish](const auto& ids) { publish("pcb", ids); };
    auto* join = collaborate->addAction(tr("Conectar por IPv4 e porta..."));
    connect(join, &QAction::triggered, this, [this, auditDock] {
        if (hosting_) {
            statusBar()->showMessage(tr("Encerre o servidor local antes de conectar a outro."),
                                     5000);
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Conectar ao projeto compartilhado"));
        QFormLayout layout(&dialog);
        QLineEdit address(QSettings().value("collaboration/host", "127.0.0.1").toString());
        QSpinBox port;
        port.setRange(1, 65535);
        port.setValue(QSettings().value("collaboration/port", 45454).toInt());
        QLineEdit name(QSettings().value("profile/name").toString());
        QLabel explanation(tr("O projeto do servidor sera aberto. Uma copia do desenho atual sera "
                              "salva antes de conectar."));
        explanation.setWordWrap(true);
        layout.addRow(&explanation);
        layout.addRow(tr("IPv4"), &address);
        layout.addRow(tr("Porta"), &port);
        layout.addRow(tr("Seu nome"), &name);
        QPushButton button(tr("Conectar"));
        layout.addRow(&button);
        connect(&button, &QPushButton::clicked, &dialog, &QDialog::accept);
        if (dialog.exec() != QDialog::Accepted)
            return;
        QHostAddress host;
        if (!host.setAddress(address.text().trimmed()) ||
            host.protocol() != QAbstractSocket::IPv4Protocol || name.text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Conexao"), tr("Informe um IPv4 valido e seu nome."));
            return;
        }
        QSettings settings;
        settings.setValue("profile/name", name.text().trimmed());
        settings.setValue("collaboration/host", address.text().trimmed());
        settings.setValue("collaboration/port", port.value());
        QDir().mkpath(QFileInfo(recoveryPath()).absolutePath());
        QString error;
        if (!levelcad::app::saveDocument(document_, recoveryPath() + ".before-join.levelsch",
                                         &error)) {
            QMessageBox::warning(this, tr("Conexao"), error);
            return;
        }
        auto state = documentToJson(document_);
        state["pcb"] = boardToJson(board_);
        collaboration_->adminToken.clear();
        collaboration_->join(address.text().trimmed(), static_cast<quint16>(port.value()),
                             name.text().trimmed(), state);
        auditDock->show();
    });
    auto* leave = collaborate->addAction(tr("Desconectar"));
    connect(leave, &QAction::triggered, this, [this] {
        collaboration_->disconnect();
        view_->clearPresence();
        boardView_->clearPresence();
    });
    auto* identity = collaborate->addAction(tr("Alterar meu nome..."));
    connect(identity, &QAction::triggered, this, [this] {
        bool ok = false;
        auto name =
            QInputDialog::getText(this, tr("Identificacao"), tr("Seu nome"), QLineEdit::Normal,
                                  QSettings().value("profile/name").toString(), &ok);
        if (ok && !name.trimmed().isEmpty())
            QSettings().setValue("profile/name", name.trimmed());
    });
    setUiLanguage(QSettings().value("ui/language", "pt_BR").toString(), this);
    if (progress)
        progress(95, "Finalizando o ambiente de engenharia...");
}

void MainWindow::resetHistory() {
    ++designRevision_;
    history_ = {document_};
    boardHistory_ = {board_};
    if (boardView_)
        boardView_->resetHistory();
    historyIndex_ = savedIndex_ = 0;
    updateTitle();
}

// A new edit after undo starts a new branch; the old redo steps no longer apply.
void MainWindow::recordChange() {
    ++designRevision_;
    if (autoSync_ && !syncing_ && syncTimer_)
        syncTimer_->start();
    if (collaboration_) {
        auto state = documentToJson(document_);
        state["pcb"] = boardToJson(board_);
        collaboration_->submit(state);
    }
    if (savedIndex_ > historyIndex_)
        savedIndex_ = -1;
    history_.resize(historyIndex_ + 1);
    history_.push_back(document_);
    boardHistory_.resize(historyIndex_ + 1);
    boardHistory_.push_back(board_);
    ++historyIndex_;
    rebuildTree();
    updateTitle();
    if (schematicModel_)
        schematicModel_->update();
}

void MainWindow::undo() {
    if (collaboration_ && collaboration_->active()) {
        statusBar()->showMessage(
            tr("Desfazer global indisponivel durante colaboracao; edite o campo desejado."), 5000);
        return;
    }
    if (historyIndex_ == 0)
        return;
    ++designRevision_;
    document_ = history_[--historyIndex_];
    board_ = boardHistory_[historyIndex_];
    boardView_->resetHistory();
    boardView_->refresh();
    rebuildBoardPanels();
    boardModel_->update();
    view_->setTool(SchematicView::Tool::Select);
    view_->refresh();
    rebuildTree();
    updateTitle();
    if (schematicModel_)
        schematicModel_->update();
}

void MainWindow::redo() {
    if (collaboration_ && collaboration_->active())
        return;
    if (historyIndex_ + 1 >= static_cast<int>(history_.size()))
        return;
    ++designRevision_;
    document_ = history_[++historyIndex_];
    board_ = boardHistory_[historyIndex_];
    boardView_->resetHistory();
    boardView_->refresh();
    rebuildBoardPanels();
    boardModel_->update();
    view_->setTool(SchematicView::Tool::Select);
    view_->refresh();
    rebuildTree();
    updateTitle();
    if (schematicModel_)
        schematicModel_->update();
}

void MainWindow::updateTitle() {
    setWindowTitle(
        "LevelCAD - " +
        (path_.isEmpty() ? QString(QObject::tr("Sem titulo")) : QFileInfo(path_).fileName()) +
        (historyIndex_ != savedIndex_ ? " *" : "") + " | Versao oficial");
    if (undoAction_)
        undoAction_->setEnabled(historyIndex_ > 0);
    if (redoAction_)
        redoAction_->setEnabled(historyIndex_ + 1 < static_cast<int>(history_.size()));
}

// A cancelled or failed save must keep the current document open.
bool MainWindow::confirmDiscard() {
    if (collaboration_ && collaboration_->active())
        return true;
    if (historyIndex_ == savedIndex_)
        return true;
    auto choice = QMessageBox::warning(
        this, QObject::tr("Alteracoes nao salvas"),
        QObject::tr("Deseja salvar as alteracoes antes de continuar?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return choice == QMessageBox::Discard || (choice == QMessageBox::Save && saveDocument());
}

void MainWindow::newDocument() {
    if (collaboration_ && collaboration_->active()) {
        statusBar()->showMessage(tr("Desconecte da colaboracao antes de abrir outro projeto."),
                                 5000);
        return;
    }
    if (!confirmDiscard())
        return;
    document_.clear();
    board_ = pcb::Board{};
    boardView_->refresh();
    boardModel_->update();
    path_.clear();
    view_->setTool(SchematicView::Tool::Select);
    view_->refresh();
    resetHistory();
    rebuildTree();
    QFile::remove(recoveryPath());
}

void MainWindow::rebuildTree() {
    tree_->clear();
    auto* symbols = new QTreeWidgetItem(
        tree_, {QObject::tr("Componentes"), QString::number(document_.symbols().size())});
    for (const auto& s : document_.symbols()) {
        auto* i = new QTreeWidgetItem(
            symbols, {QString::fromStdString(s.reference), QString::fromStdString(s.value)});
        i->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(s.id));
    }
    auto* wires = new QTreeWidgetItem(
        tree_, {QObject::tr("Ligacoes"), QString::number(document_.wires().size())});
    for (const auto& w : document_.wires()) {
        auto* i = new QTreeWidgetItem(
            wires, {"Ligacao #" + QString::number(w.id), QObject::tr("Conexao")});
        i->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(w.id));
    }
    auto graph = connectivity::analyze(document_);
    auto* networks = new QTreeWidgetItem(
        tree_, {QObject::tr("Redes eletricas"), QString::number(graph.nets.size())});
    for (const auto& net : graph.nets) {
        auto* node = new QTreeWidgetItem(networks, {QString::fromStdString(net.name),
                                                    QString::number(net.pins.size()) + " pinos"});
        for (const auto& pin : net.pins) {
            QString reference;
            for (const auto& symbol : document_.symbols())
                if (symbol.id == pin.symbolId)
                    reference = QString::fromStdString(symbol.reference);
            auto* item = new QTreeWidgetItem(
                node, {reference + "." + QString::number(pin.number), QObject::tr("Pino")});
            item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(pin.symbolId));
        }
    }
    tree_->expandToDepth(0);
}

void MainWindow::openDocument(const QString& name) {
    if (collaboration_ && collaboration_->active()) {
        statusBar()->showMessage(tr("Desconecte da colaboracao antes de abrir outro projeto."),
                                 5000);
        return;
    }
    auto file = name;
    if (file.isEmpty())
        file = QFileDialog::getOpenFileName(this, QObject::tr("Abrir esquematico"), {},
                                            "LevelCAD (*.levelsch);;Projeto externo (*.kicad_pro "
                                            "*.kicad_sch *.kicad_pcb);;JSON (*.json)");
    if (file.isEmpty() || !confirmDiscard())
        return;
    QString error;
    if (QFileInfo(file).suffix().startsWith("kicad_")) {
        QProgressDialog progress(tr("Importando projeto externo..."), QString(), 0, 0, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.show();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        try {
            auto project = importExternalProject(file);
            for (auto& footprint : project.board.footprints) {
                try {
                    if (assignOfficialModel(footprint))
                        project.warnings.removeAll("Modelo externo nao incorporado: " +
                                                   QString::fromStdString(footprint.reference) +
                                                   ". Atribua um STL na biblioteca 3D.");
                } catch (const std::exception& error) {
                    project.warnings.append(
                        QString::fromStdString(footprint.reference) +
                        ": modelo 3D nao carregado: " + QString::fromUtf8(error.what()));
                }
            }
            document_ = std::move(project.document);
            board_ = std::move(project.board);
            path_.clear();
            view_->refresh();
            view_->fitDrawing();
            boardView_->refresh();
            boardView_->fitBoard();
            boardModel_->update();
            schematicModel_->update();
            rebuildBoardPanels();
            resetHistory();
            savedIndex_ = -1;
            rebuildTree();
            updateTitle();
            progress.close();
            if (project.hasBoard)
                showWorkspace(1);
            if (!project.warnings.isEmpty())
                QMessageBox::information(this, tr("Relatorio de compatibilidade"),
                                         project.warnings.mid(0, 30).join("\n") +
                                             tr("\n\nO arquivo original foi preservado. Salve a "
                                                "copia no formato LevelCAD."));
        } catch (const std::exception& exception) {
            progress.close();
            QMessageBox::warning(this, tr("Falha ao importar"),
                                 QString::fromUtf8(exception.what()));
        }
        return;
    }
    if (!app::loadDocument(document_, file, &error, &board_)) {
        QMessageBox::warning(this, QObject::tr("Falha ao abrir"), error);
        return;
    }
    // Older projects may contain generic meshes. Refresh only missing or bundled placeholders.
    QStringList modelWarnings;
    for (auto& footprint : board_.footprints) {
        if (!footprint.mesh.empty() && !footprint.modelName.empty() &&
            !footprint.modelName.starts_with(":/models3d/"))
            continue;
        try {
            assignOfficialModel(footprint);
        } catch (const std::exception& error) {
            modelWarnings.append(QString::fromStdString(footprint.reference) + ": " +
                                 QString::fromUtf8(error.what()));
        }
    }
    boardView_->refresh();
    boardModel_->update();
    schematicModel_->update();
    rebuildBoardPanels();
    if (!modelWarnings.isEmpty())
        statusBar()->showMessage(modelWarnings.join(" / "), 15000);
    path_ = file;
    view_->setTool(SchematicView::Tool::Select);
    view_->refresh();
    view_->fitDrawing();
    resetHistory();
    rebuildTree();
    rememberFile();
    QFile::remove(recoveryPath());
}

bool MainWindow::saveDocumentAs() {
    if (collaboration_ && collaboration_->active())
        return saveDocument();
    auto file = QFileDialog::getSaveFileName(this, QObject::tr("Salvar esquematico"), path_,
                                             "LevelCAD (*.levelsch)");
    if (file.isEmpty())
        return false;
    if (!file.endsWith(".levelsch", Qt::CaseInsensitive))
        file += ".levelsch";
    QString error;
    if (!app::saveDocument(document_, file, &error, &board_)) {
        QMessageBox::warning(this, QObject::tr("Falha ao salvar"), error);
        return false;
    }
    path_ = file;
    savedIndex_ = historyIndex_;
    updateTitle();
    rememberFile();
    QFile::remove(recoveryPath());
    statusBar()->showMessage(QObject::tr("Esquematico salvo"), 3000);
    return true;
}

bool MainWindow::saveDocument() {
    if (collaboration_ && collaboration_->active()) {
        statusBar()->showMessage(
            hosting_
                ? tr("O servidor salva automaticamente o projeto e os logs na pasta escolhida.")
                : tr("O anfitriao e responsavel por salvar o projeto compartilhado."),
            7000);
        return hosting_;
    }
    if (path_.isEmpty())
        return saveDocumentAs();
    QString error;
    if (!app::saveDocument(document_, path_, &error, &board_)) {
        QMessageBox::warning(this, QObject::tr("Falha ao salvar"), error);
        return false;
    }
    savedIndex_ = historyIndex_;
    updateTitle();
    QFile::remove(recoveryPath());
    statusBar()->showMessage(QObject::tr("Esquematico salvo"), 3000);
    return true;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirmDiscard()) {
        e->ignore();
        return;
    }
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState());
    QFile::remove(recoveryPath());
    e->accept();
}

void MainWindow::rememberFile() {
    QSettings s;
    auto recent = s.value("files/recent").toStringList();
    recent.removeAll(path_);
    recent.prepend(path_);
    while (recent.size() > 8)
        recent.removeLast();
    s.setValue("files/recent", recent);
    rebuildRecent();
}

void MainWindow::rebuildRecent() {
    recentMenu_->clear();
    for (const auto& p : QSettings().value("files/recent").toStringList()) {
        auto* a = recentMenu_->addAction(p);
        connect(a, &QAction::triggered, this, [this, p] { openDocument(p); });
    }
    recentMenu_->setEnabled(!recentMenu_->actions().isEmpty());
}

void MainWindow::exportBom() {
    auto path =
        chooseExportFile(this, QObject::tr("Exportar lista de materiais"), {}, "CSV (*.csv)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".csv", Qt::CaseInsensitive))
        path += ".csv";
    QString data = "Reference,Type,Value\r\n";
    for (const auto& s : document_.symbols())
        if (s.kind != core::SymbolKind::Ground)
            data +=
                csv(s.reference) + "," + csv(core::kindName(s.kind)) + "," + csv(s.value) + "\r\n";
    if (writeFile(this, path, data.toUtf8()))
        statusBar()->showMessage(QObject::tr("Lista de materiais exportada"), 3000);
}

void MainWindow::exportNetlist() {
    auto path = chooseExportFile(this, QObject::tr("Exportar netlist"), {}, "JSON (*.json)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".json", Qt::CaseInsensitive))
        path += ".json";
    QJsonArray components, nets;
    for (const auto& s : document_.symbols())
        components.append(QJsonObject{{"id", QString::number(s.id)},
                                      {"reference", QString::fromStdString(s.reference)},
                                      {"type", QString::fromStdString(core::kindName(s.kind))},
                                      {"value", QString::fromStdString(s.value)}});
    for (const auto& n : document_.nets()) {
        QJsonArray pins, wires;
        for (auto pin : n.pins)
            pins.append(
                QJsonObject{{"component", QString::number(pin.symbolId)}, {"pin", pin.number}});
        for (auto id : n.wires)
            wires.append(QString::number(id));
        nets.append(QJsonObject{
            {"name", QString::fromStdString(n.name)}, {"pins", pins}, {"wires", wires}});
    }
    auto data = QJsonDocument(QJsonObject{{"format", "levelcad-netlist"},
                                          {"version", 1},
                                          {"components", components},
                                          {"nets", nets},
                                          {"author", "Wenderson Dias"},
                                          {"publisher", "The Archlevel Foundation"}})
                    .toJson();
    if (writeFile(this, path, data))
        statusBar()->showMessage(QObject::tr("Netlist exportada"), 3000);
}

void MainWindow::runErc() {
    const auto graph = connectivity::analyze(document_);
    QDialog dialog(this);
    dialog.setWindowTitle("Electrical Connectivity Engine - diagnostico");
    dialog.resize(760, 460);
    QVBoxLayout layout(&dialog);
    QLabel summary(QString::number(graph.nets.size()) + " redes / " +
                   QString::number(graph.vertices.size()) + " vertices / " +
                   QString::number(graph.contacts.size()) + " contatos / " +
                   QString::number(graph.diagnostics.size()) + " diagnosticos");
    layout.addWidget(&summary);
    QListWidget issues;
    for (const auto& diagnostic : graph.diagnostics) {
        const auto severity = diagnostic.severity == connectivity::Severity::Error
                                  ? QObject::tr("Erro")
                                  : QObject::tr("Aviso");
        auto* item =
            new QListWidgetItem(QString(severity) + " [" + QString::fromStdString(diagnostic.code) +
                                    "]: " + QString::fromStdString(diagnostic.message),
                                &issues);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(diagnostic.objectId));
        item->setToolTip(QObject::tr("Duplo clique para localizar o objeto no desenho."));
    }
    if (graph.diagnostics.empty())
        issues.addItem(QObject::tr("Nenhum problema encontrado nas verificacoes disponiveis."));
    layout.addWidget(&issues);
    QLabel note("Duplo clique localiza o objeto. A analise e topologica: nao calcula tensoes, "
                "correntes ou caminhos DC.");
    note.setWordWrap(true);
    layout.addWidget(&note);
    connect(&issues, &QListWidget::itemDoubleClicked, this, [this, &dialog](QListWidgetItem* item) {
        const auto id = item->data(Qt::UserRole).toULongLong();
        if (id) {
            view_->setTool(SchematicView::Tool::Select);
            view_->scene()->clearSelection();
            view_->selectObject(id);
            dialog.accept();
        }
    });
    dialog.exec();
}

void MainWindow::exportSpice() {
    exporters::SpiceOptions options;
    options.title = "LevelCAD circuit";
    const bool needsModels =
        std::any_of(document_.symbols().begin(), document_.symbols().end(), [](const auto& symbol) {
            return symbol.kind == core::SymbolKind::Diode || symbol.kind == core::SymbolKind::LED ||
                   symbol.spiceType == "D" || symbol.spiceType == "Q";
        });
    if (needsModels) {
        const auto library = QFileDialog::getOpenFileName(
            this, "Selecionar biblioteca SPICE com os modelos dos diodos/LEDs", {},
            "Modelos SPICE (*.lib *.mod *.model *.cir);;Todos os arquivos (*)");
        if (library.isEmpty())
            return;
        options.modelLibrary =
            QDir::fromNativeSeparators(QFileInfo(library).absoluteFilePath()).toStdString();
    }
    const auto result = exporters::exportSpice(document_, options);
    if (!result.success()) {
        QString errors;
        for (const auto& diagnostic : result.diagnostics) {
            if (diagnostic.severity == connectivity::Severity::Error)
                errors += "a " + QString::fromStdString(diagnostic.message) + "\n";
        }
        QMessageBox::warning(this, QObject::tr("Corrija o circuito antes de exportar SPICE"),
                             errors);
        return;
    }
    auto path =
        chooseExportFile(this, QObject::tr("Exportar circuito SPICE"), {}, "Netlist SPICE (*.cir)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".cir", Qt::CaseInsensitive))
        path += ".cir";
    if (!writeFile(this, path, QByteArray::fromStdString(result.text)))
        return;
    QString warnings;
    for (const auto& diagnostic : result.diagnostics)
        warnings += "a " + QString::fromStdString(diagnostic.message) + "\n";
    if (!warnings.isEmpty())
        QMessageBox::information(this, QObject::tr("SPICE exportado com avisos"), warnings);
    statusBar()->showMessage(QObject::tr("SPICE exportado - abra o .cir no simulador externo"),
                             5000);
}

void MainWindow::appendLibrary(const std::vector<LibraryEntry>& entries) {
    QSet<QString> known;
    for (const auto& existing : libraryEntries_)
        known.insert(existing.name);
    libraryList_->setUpdatesEnabled(false);
    for (const auto& entry : entries) {
        if (known.contains(entry.name))
            continue;
        known.insert(entry.name);
        const int index = static_cast<int>(libraryEntries_.size());
        libraryEntries_.push_back(entry);
        auto* item = new QListWidgetItem(entry.name, libraryList_);
        item->setData(Qt::UserRole, index + 100);
        item->setToolTip(entry.description + "\n" +
                         QString::number(entry.prototype.pinDefinitions.size()) +
                         " pinos\nFootprint: " + QString::fromStdString(entry.prototype.footprint));
    }
    libraryList_->setUpdatesEnabled(true);
    libraryCount_->setText(QString::number(libraryEntries_.size()) + " simbolos / uso offline");
}

void MainWindow::importLibrary() {
    const auto path =
        QFileDialog::getOpenFileName(this, QObject::tr("Importar biblioteca de simbolos"), {},
                                     "Biblioteca de simbolos (*.kicad_sym)");
    if (path.isEmpty())
        return;
    try {
        const auto library = loadKiCadLibrary(path);
        appendLibrary(library.entries);
        QSettings settings;
        auto paths = settings.value("libraries/imported").toStringList();
        if (!paths.contains(path)) {
            paths.append(path);
            settings.setValue("libraries/imported", paths);
        }
        QString message = QString::number(library.entries.size()) + " simbolos importados.\n" +
                          QString::number(library.skipped.size()) + " simbolos nao suportados.";
        if (!library.skipped.isEmpty())
            message += "\n\n" + library.skipped.mid(0, 15).join("\n");
        QMessageBox::information(this, QObject::tr("Importacao concluida"), message);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QObject::tr("Falha na importacao"),
                             QString::fromUtf8(error.what()));
    }
}

namespace {
class EditorWindow final : public QMdiSubWindow {
  protected:
    void closeEvent(QCloseEvent* event) override {
        // Fechar uma vista sÃ³ a oculta: o gerenciador pode reabri-la sem perder o documento.
        hide();
        event->ignore();
    }
};
} // namespace

void MainWindow::setupWorkspace() {
    auto registerWindow = [&](QWidget* content, const QString& title) {
        auto* window = new EditorWindow;
        window->setWidget(content);
        window->setWindowTitle(title);
        workspace_->addSubWindow(window);
        editorWindows_.push_back(window);
        window->resize(1000, 680);
        return window;
    };
    registerWindow(view_, QObject::tr("Esquematico"));
    boardView_ = new BoardEditor(&board_, this);
    auto* pcbPanel = new QWidget;
    auto* pcbLayout = new QVBoxLayout(pcbPanel);
    pcbLayout->setContentsMargins(0, 0, 0, 0);
    auto* pcbTools = new QToolBar(QObject::tr("Ferramentas PCB"));
    pcbLayout->addWidget(pcbTools);
    pcbLayout->addWidget(boardView_);
    auto command = [&](const QString& title, auto operation) {
        auto* action = pcbTools->addAction(title);
        connect(action, &QAction::triggered, this, operation);
        return action;
    };
    command(QObject::tr("Selecionar / mover"), [this] { boardView_->setRouting(false); });
    command(QObject::tr("Rotear trilha"), [this] { boardView_->setRouting(true); });
    auto* layers = new QComboBox;
    copperLayers_ = layers;
    layers->addItems({QObject::tr("Camada superior"), QObject::tr("Camada inferior")});
    pcbTools->addWidget(layers);
    connect(layers, &QComboBox::currentIndexChanged, this,
            [this](int index) { boardView_->setLayer(copperLayers_->itemData(index).toInt()); });
    auto* traceWidth = new QDoubleSpinBox;
    traceWidth->setRange(0.1, 5);
    traceWidth->setDecimals(2);
    traceWidth->setValue(0.35);
    traceWidth->setSuffix(" mm");
    pcbTools->addWidget(traceWidth);
    connect(traceWidth, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        boardView_->setTraceWidth(static_cast<core::Coord>(value * core::nmPerMm));
    });
    command(QObject::tr("Desfazer"), [this] { boardView_->undo(); });
    command(QObject::tr("Refazer"), [this] { boardView_->redo(); });
    command(QObject::tr("Ajustar placa"), [this] { boardView_->fitBoard(); });
    command("DRC", [this] { boardView_->checkRules(); });
    registerWindow(pcbPanel, "PCB");
    boardModel_ = new ModelView(ModelView::Mode::Board, &board_, &document_, this);
    schematicModel_ = new ModelView(ModelView::Mode::Schematic, &board_, &document_, this);
    auto registerModel = [&](ModelView* model, const QString& title) {
        auto* panel = new QWidget;
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* toolbar = new QToolBar;
        layout->addWidget(toolbar);
        layout->addWidget(model);
        auto* reset = toolbar->addAction(QObject::tr("Restaurar camera"));
        connect(reset, &QAction::triggered, model, &ModelView::resetCamera);
        auto* exportAction = toolbar->addAction(QObject::tr("Exportar modelo 3D"));
        connect(exportAction, &QAction::triggered, model, &ModelView::exportModel);
        if (model == boardModel_) {
            auto* display = new QComboBox;
            display->addItems({tr("Placa montada"), tr("Camadas separadas"), tr("Somente cobre"),
                               tr("Placa transparente")});
            toolbar->addWidget(display);
            connect(display, &QComboBox::currentIndexChanged, model, [model](int index) {
                model->setBoardDisplay(static_cast<ModelView::BoardDisplay>(index));
            });
            auto* separation = new QDoubleSpinBox;
            separation->setRange(1, 20);
            separation->setValue(4);
            separation->setSuffix(" mm");
            separation->setToolTip(tr("Distancia visual entre camadas"));
            toolbar->addWidget(separation);
            connect(separation, &QDoubleSpinBox::valueChanged, model,
                    &ModelView::setExplodeSpacing);
            auto* camera = new QComboBox;
            camera->addItems({tr("Isometrica"), tr("Superior"), tr("Inferior")});
            toolbar->addWidget(camera);
            connect(camera, &QComboBox::currentIndexChanged, model, &ModelView::setCameraPreset);
            auto* bodies = toolbar->addAction(tr("Componentes"));
            bodies->setCheckable(true);
            bodies->setChecked(true);
            connect(bodies, &QAction::toggled, model, &ModelView::setComponentsVisible);
            auto* copper = toolbar->addAction(tr("Cobre"));
            copper->setCheckable(true);
            copper->setChecked(true);
            connect(copper, &QAction::toggled, model, &ModelView::setCopperVisible);
            auto* height = toolbar->addAction(QObject::tr("Altura 3D"));
            connect(height, &QAction::triggered, model, &ModelView::editHeight);
        }
        if (model == schematicModel_) {
            auto* scale = new QDoubleSpinBox;
            scale->setRange(.5, 6);
            scale->setValue(1.8);
            scale->setSingleStep(.2);
            scale->setSuffix(" x");
            scale->setToolTip(tr("Escala visual dos componentes"));
            toolbar->addWidget(scale);
            connect(scale, &QDoubleSpinBox::valueChanged, model, &ModelView::setComponentScale);
        }
        registerWindow(panel, title);
    };
    registerModel(boardModel_, QObject::tr("PCB 3D"));
    registerModel(schematicModel_, QObject::tr("Esquematico 3D"));
    boardView_->onChanged = [this] {
        recordChange();
        rebuildBoardPanels();
        boardModel_->update();
    };
    boardModel_->onChanged = [this] {
        boardView_->refresh();
        if (boardView_->onChanged)
            boardView_->onChanged();
    };
    auto* layerDock = new QDockWidget(tr("Camadas e redes PCB"), this);
    layerDock->setObjectName("PcbLayers");
    auto* layerPanel = new QWidget;
    auto* layerLayout = new QVBoxLayout(layerPanel);
    boardLayers_ = new QTreeWidget;
    boardLayers_->setHeaderLabels({tr("Camada"), tr("Cobre")});
    layerLayout->addWidget(boardLayers_);
    boardNets_ = new QComboBox;
    layerLayout->addWidget(boardNets_);
    layerDock->setWidget(layerPanel);
    addDockWidget(Qt::RightDockWidgetArea, layerDock);
    connect(boardNets_, &QComboBox::currentIndexChanged, this, [this](int index) {
        boardView_->setNetFilter(boardNets_->itemData(index).toString());
    });
    connect(boardLayers_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
        for (auto& layer : board_.layers)
            if (layer.index == item->data(0, Qt::UserRole).toInt())
                layer.visible = item->checkState(0) == Qt::Checked;
        boardView_->refresh();
        boardModel_->update();
    });
    rebuildBoardPanels();
    auto* pcbMenu = menuBar()->addMenu("PCB");
    auto add = [&](QMenu* menu, const QString& title, auto operation) {
        auto* action = menu->addAction(title);
        connect(action, &QAction::triggered, this, operation);
        return action;
    };
    add(pcbMenu, QObject::tr("Sincronizar PCB com esquematico"), [this] { synchronizeBoard(); });
    add(pcbMenu, tr("Organizar como o esquematico"), [this] { synchronizeBoard(true); });
    add(pcbMenu, tr("Ajustar contorno ao circuito"), [this] {
        if (board_.fitOutline()) {
            boardView_->commitExternalChange();
            boardView_->refresh();
            boardModel_->resetCamera();
        } else
            QMessageBox::information(this, tr("Contorno"),
                                     tr("Nenhum ajuste: placa vazia ou contorno personalizado."));
    });
    add(pcbMenu, tr("Verificar esquema x PCB"), [this] {
        auto issues = board_.schematicIssues(document_);
        QStringList lines;
        for (const auto& issue : issues)
            lines << QString::fromStdString(issue);
        QMessageBox::information(this, tr("Esquema x PCB"),
                                 lines.empty()
                                     ? tr("Pinos, pads e redes correspondem ao esquematico.")
                                     : lines.join("\n"));
    });
    add(pcbMenu, tr("Rotear ligacoes simples"), [this] {
        auto count = board_.routePending(0);
        if (count) {
            boardView_->commitExternalChange();
            boardView_->refresh();
            boardModel_->update();
        }
        QMessageBox::information(this, tr("Roteamento"),
                                 tr("%1 ligacoes roteadas; %2 pendentes. Revise o DRC.")
                                     .arg(count)
                                     .arg(board_.airwires().size()));
    });
    add(pcbMenu, tr("Atribuir encapsulamento..."),
        [this] { boardView_->assignFootprint(document_); });
    add(pcbMenu, tr("Editar camadas..."), [this] { boardView_->layerProperties(); });
    add(pcbMenu, QObject::tr("Configuracao da placa"), [this] { boardView_->boardProperties(); });
    add(pcbMenu, tr("Adicionar camada de cobre..."), [this] {
        if (board_.layers.size() >= 32)
            return;
        bool ok = false;
        auto name = QInputDialog::getText(this, tr("Camada de cobre"), tr("Nome"),
                                          QLineEdit::Normal, "Inner copper", &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        int index = 2;
        while (std::any_of(board_.layers.begin(), board_.layers.end(),
                           [&](const auto& layer) { return layer.index == index; }))
            ++index;
        board_.layers.push_back({index, name.toStdString(),
                                 QColor::fromHsv((index * 67) % 360, 160, 235).name().toStdString(),
                                 true});
        rebuildBoardPanels();
        if (boardView_->onChanged)
            boardView_->onChanged();
    });
    add(pcbMenu, tr("Propriedades / modelo 3D"), [this] { boardView_->editSelected(); });
    add(pcbMenu, QObject::tr("Salvar PCB"), [this] { boardView_->saveBoard(); });
    add(pcbMenu, QObject::tr("Abrir PCB"), [this] { boardView_->loadBoard(); });
    add(pcbMenu, QObject::tr("Verificacao da placa"), [this] { boardView_->checkRules(); });
    add(pcbMenu, QObject::tr("Exportar modelo 3D"), [this] { boardView_->exportModel(); });
    add(pcbMenu, tr("Fonte com conector fisico..."), [this] {
        QStringList choices;
        std::vector<core::Symbol> sources;
        for (const auto& s : document_.symbols())
            if (s.kind == core::SymbolKind::VoltageSource ||
                s.kind == core::SymbolKind::CurrentSource) {
                choices << QString::fromStdString(s.reference + " / " + s.value);
                sources.push_back(s);
            }
        if (choices.empty()) {
            QMessageBox::information(this, tr("Alimentacao"), tr("Nenhuma fonte no esquema."));
            return;
        }
        bool ok = false;
        auto choice = QInputDialog::getItem(
            this, tr("Conector de alimentacao"),
            tr("A fonte selecionada sera representada por um conector fisico de 2 pinos na PCB."),
            choices, 0, false, &ok);
        if (!ok)
            return;
        auto s = sources[choices.indexOf(choice)];
        s.footprint = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical";
        document_.updateSymbol(s);
        recordChange();
        synchronizeBoard();
    });
    add(pcbMenu, tr("Exportar pacote de fabricacao..."), [this] {
        if (syncing_) {
            QMessageBox::information(this, tr("Fabricacao"), tr("Aguarde a sincronizacao da PCB."));
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Pacote de fabricacao"));
        QFormLayout form(&dialog);
        QLineEdit name(QString::fromStdString(board_.projectName));
        QLineEdit revision(QString::fromStdString(board_.revision));
        QLineEdit creator(board_.creator.empty() ? QSettings().value("profile/name").toString()
                                                 : QString::fromStdString(board_.creator));
        QLineEdit tool(QSettings().value("production/tool", productionTool()).toString());
        form.addRow(tr("Projeto"), &name);
        form.addRow(tr("Revisao"), &revision);
        form.addRow(tr("Autor"), &creator);
        form.addRow(tr("Gerador CLI"), &tool);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        if (name.text().trimmed().isEmpty() || revision.text().trimmed().isEmpty() ||
            creator.text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Fabricacao"), tr("Preencha projeto, revisao e autor."));
            return;
        }
        auto destination = QFileDialog::getExistingDirectory(this, tr("Destino do pacote"));
        if (destination.isEmpty())
            return;
        board_.projectName = name.text().trimmed().toStdString();
        board_.revision = revision.text().trimmed().toStdString();
        board_.creator = creator.text().trimmed().toStdString();
        boardView_->commitExternalChange();
        QSettings().setValue("production/tool", tool.text());
        auto* progress = new QProgressDialog(tr("Verificando redes e gerando Gerber e furos..."),
                                             QString(), 0, 0, this);
        progress->setWindowModality(Qt::NonModal);
        progress->show();
        using ProductionResult = std::pair<QString, QString>;
        auto* watcher = new QFutureWatcher<ProductionResult>(this);
        connect(watcher, &QFutureWatcher<ProductionResult>::finished, this,
                [this, watcher, progress] {
                    progress->close();
                    progress->deleteLater();
                    try {
                        auto result = watcher->result();
                        if (!result.second.isEmpty())
                            throw std::runtime_error(result.second.toStdString());
                        auto path = result.first;
                        QMessageBox::information(this, tr("Pacote gerado"),
                                                 tr("Gerber, furos, BOM, posicionamento e "
                                                    "metadados:\n%1\n\nRevise os arquivos "
                                                    "com o fabricante antes de encomendar.")
                                                     .arg(path));
                    } catch (const std::exception& e) {
                        QMessageBox::warning(this, tr("Falha na fabricacao"),
                                             QString::fromUtf8(e.what()));
                    }
                    watcher->deleteLater();
                });
        watcher->setFuture(QtConcurrent::run([board = board_, document = document_, destination,
                                              executable = tool.text()] {
            try {
                return ProductionResult{exportProduction(board, document, destination, executable),
                                        {}};
            } catch (const std::exception& error) {
                return ProductionResult{{}, QString::fromUtf8(error.what())};
            }
        }));
    });
    auto* windows = menuBar()->addMenu(QObject::tr("Janelas"));
    const QStringList names = {QObject::tr("Esquematico"), "PCB", QObject::tr("PCB 3D"),
                               QObject::tr("Esquematico 3D")};
    for (int index = 0; index < names.size(); ++index)
        add(windows, names[index], [this, index] { showWorkspace(index); });
    windows->addSeparator();
    add(windows, QObject::tr("Gerenciar janelas..."), [this] { manageWindows(); });
    auto* panels = windows->addMenu(QObject::tr("Paineis laterais"));
    for (auto* dock : findChildren<QDockWidget*>())
        panels->addAction(dock->toggleViewAction());
    add(windows, QObject::tr("Mostrar todos os paineis"), [this] {
        for (auto* dock : findChildren<QDockWidget*>())
            dock->show();
    });
    add(windows, QObject::tr("Restaurar layout"), [this] { restoreLayout(); });
    add(windows, QObject::tr("Organizar lado a lado"), [this] {
        workspace_->setViewMode(QMdiArea::SubWindowView);
        for (auto* window : editorWindows_)
            window->show();
        workspace_->tileSubWindows();
    });
    add(windows, QObject::tr("Organizar em cascata"), [this] {
        workspace_->setViewMode(QMdiArea::SubWindowView);
        for (auto* window : editorWindows_)
            window->show();
        workspace_->cascadeSubWindows();
    });
    add(windows, QObject::tr("Usar abas"),
        [this] { workspace_->setViewMode(QMdiArea::TabbedView); });
    auto* languages = menuBar()->addMenu(QObject::tr("Idiomas"));
    auto* group = new QActionGroup(this);
    group->setExclusive(true);
    for (const auto& entry : std::vector<std::pair<QString, QString>>{
             {"Portugues (Brasil)", "pt_BR"}, {"English", "en"}, {"Espanol", "es"}}) {
        auto* action =
            add(languages, entry.first, [this, code = entry.second] { setUiLanguage(code, this); });
        action->setCheckable(true);
        action->setChecked(QSettings().value("ui/language", "pt_BR").toString() == entry.second);
        group->addAction(action);
    }
    showWorkspace(0);
}

void MainWindow::showWorkspace(int index) {
    if (index < 0 || index >= static_cast<int>(editorWindows_.size()))
        return;
    auto* window = editorWindows_[index];
    window->widget()->show();
    window->show();
    workspace_->setActiveSubWindow(window);
    if (index == 1)
        boardView_->fitBoard();
    if (index >= 2)
        window->widget()->update();
}

void MainWindow::restoreLayout() {
    restoreState(defaultLayout_);
    for (auto* dock : findChildren<QDockWidget*>())
        dock->show();
    workspace_->setViewMode(QMdiArea::TabbedView);
    showWorkspace(0);
}

void MainWindow::manageWindows() {
    QDialog dialog(this);
    dialog.setWindowTitle(QObject::tr("Gerenciador de janelas"));
    dialog.resize(480, 420);
    QVBoxLayout layout(&dialog);
    QListWidget list;
    layout.addWidget(&list);
    std::vector<QWidget*> targets;
    for (auto* dock : findChildren<QDockWidget*>())
        targets.push_back(dock);
    for (auto* window : editorWindows_)
        targets.push_back(window);
    for (std::size_t index = 0; index < targets.size(); ++index) {
        auto* item = new QListWidgetItem(targets[index]->windowTitle(), &list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(targets[index]->isHidden() ? Qt::Unchecked : Qt::Checked);
        item->setData(Qt::UserRole, static_cast<int>(index));
    }
    connect(&list, &QListWidget::itemChanged, this, [&](QListWidgetItem* item) {
        auto* target = targets.at(item->data(Qt::UserRole).toInt());
        target->setVisible(item->checkState() == Qt::Checked);
    });
    QPushButton restore(QObject::tr("Restaurar layout"));
    layout.addWidget(&restore);
    connect(&restore, &QPushButton::clicked, this, [this, &dialog] {
        restoreLayout();
        dialog.accept();
    });
    dialog.exec();
}
} // namespace levelcad::app

namespace levelcad::app {
MainWindow::~MainWindow() {
    stopHosting();
}

void MainWindow::stopHosting() {
    if (!serverProcess_)
        return;
    collaboration_->disconnect();
    serverProcess_->terminate();
    if (!serverProcess_->waitForFinished(2500)) {
        serverProcess_->kill();
        serverProcess_->waitForFinished(1000);
    }
    delete serverProcess_;
    serverProcess_ = nullptr;
    hosting_ = false;
    view_->clearPresence();
    boardView_->clearPresence();
}

void MainWindow::hostProject() {
    if (serverProcess_) {
        statusBar()->showMessage(tr("O servidor ja esta em execucao."), 5000);
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Hospedar projeto colaborativo"));
    QFormLayout layout(&dialog);
    QSpinBox port;
    port.setObjectName("hostPort");
    port.setRange(1, 65535);
    port.setValue(QSettings().value("collaboration/port", 45454).toInt());
    QLineEdit name(QSettings().value("profile/name").toString());
    name.setObjectName("hostName");
    QLineEdit directory(
        QSettings()
            .value("collaboration/directory",
                   QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                       "/LevelCAD/Shared")
            .toString());
    directory.setObjectName("hostDirectory");
    QPushButton browse(tr("Escolher pasta..."));
    connect(&browse, &QPushButton::clicked, &dialog, [&] {
        auto path = QFileDialog::getExistingDirectory(&dialog, tr("Pasta do projeto compartilhado"),
                                                      directory.text());
        if (!path.isEmpty())
            directory.setText(path);
    });
    QString addresses;
    for (const auto& interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp) ||
            interface.flags() & QNetworkInterface::IsLoopBack)
            continue;
        for (const auto& entry : interface.addressEntries())
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                addresses += entry.ip().toString() + " / ";
    }
    QLabel explanation(tr("Seu computador hospedara o projeto e salvara as edicoes "
                          "automaticamente. Os participantes usam seu IPv4 e a porta escolhida."));
    explanation.setWordWrap(true);
    layout.addRow(&explanation);
    layout.addRow(tr("IPv4 disponiveis"), new QLabel(addresses, &dialog));
    layout.addRow(tr("Porta"), &port);
    layout.addRow(tr("Seu nome"), &name);
    layout.addRow(tr("Pasta do projeto"), &directory);
    layout.addRow(&browse);
    QPushButton start(tr("Iniciar servidor"));
    layout.addRow(&start);
    connect(&start, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted || name.text().trimmed().isEmpty() ||
        directory.text().trimmed().isEmpty())
        return;
    hostedDirectory_ = QDir(directory.text()).absolutePath();
    hostedPort_ = static_cast<quint16>(port.value());
    if (!QDir().mkpath(hostedDirectory_)) {
        QMessageBox::warning(this, tr("Servidor"),
                             tr("Nao foi possivel criar a pasta do projeto."));
        return;
    }
    QString executable = QCoreApplication::applicationDirPath() + "/LevelCADServer.exe";
#ifndef Q_OS_WIN
    executable = QCoreApplication::applicationDirPath() + "/LevelCADServer";
#endif
    if (!QFileInfo::exists(executable)) {
        QMessageBox::warning(
            this, tr("Servidor"),
            tr("O executavel LevelCADServer precisa estar na mesma pasta do LevelCAD."));
        return;
    }
    QSettings settings;
    settings.setValue("profile/name", name.text().trimmed());
    settings.setValue("collaboration/directory", hostedDirectory_);
    settings.setValue("collaboration/port", port.value());
    QString error;
    QDir().mkpath(QFileInfo(recoveryPath()).absolutePath());
    if (!app::saveDocument(document_, recoveryPath() + ".before-host.levelsch", &error, &board_)) {
        QMessageBox::warning(this, tr("Servidor"), error);
        return;
    }
    serverProcess_ = new QProcess(this);
#ifdef Q_OS_WIN
    serverProcess_->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* arguments) { arguments->flags |= 0x08000000; });
#endif
    serverProcess_->setProcessChannelMode(QProcess::MergedChannels);
    connect(serverProcess_, &QProcess::readyReadStandardOutput, this, [this] {
        const auto output = QString::fromUtf8(serverProcess_->readAllStandardOutput());
        if (output.contains("Local:") && !hosting_) {
            hosting_ = true;
            auto state = documentToJson(document_);
            state["pcb"] = boardToJson(board_);
            collaboration_->adminToken = hostToken_;
            collaboration_->join("127.0.0.1", hostedPort_,
                                 QSettings().value("profile/name").toString(), state);
            statusBar()->showMessage(tr("Servidor iniciado. Compartilhe seu IPv4 e porta."), 10000);
        }
    });
    connect(serverProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        statusBar()->showMessage(serverProcess_->errorString(), 15000);
    });
    hostToken_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    serverProcess_->start(executable, {QString::number(hostedPort_), hostedDirectory_, hostToken_});
}
} // namespace levelcad::app

namespace levelcad::app {
void MainWindow::rebuildBoardPanels() {
    if (!boardLayers_ || !copperLayers_ || !boardNets_)
        return;
    QSignalBlocker a(boardLayers_), b(copperLayers_), c(boardNets_);
    auto selected = copperLayers_->currentData();
    auto net = boardNets_->currentData();
    boardLayers_->clear();
    copperLayers_->clear();
    for (const auto& layer : board_.layers) {
        auto* item = new QTreeWidgetItem(
            boardLayers_, {QString::fromStdString(layer.name), QString::number(layer.index)});
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, layer.visible ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole, layer.index);
        item->setForeground(0, QColor(QString::fromStdString(layer.color)));
        copperLayers_->addItem(QString::fromStdString(layer.name), layer.index);
    }
    copperLayers_->setCurrentIndex(qMax(0, copperLayers_->findData(selected)));
    boardNets_->clear();
    boardNets_->addItem(tr("Todas as redes"), QString());
    std::set<std::string> nets;
    for (const auto& footprint : board_.footprints)
        for (const auto& pad : footprint.pads)
            if (!pad.net.empty())
                nets.insert(pad.net);
    for (const auto& name : nets)
        boardNets_->addItem(QString::fromStdString(name), QString::fromStdString(name));
    boardNets_->setCurrentIndex(qMax(0, boardNets_->findData(net)));
}
} // namespace levelcad::app

namespace levelcad::app {
namespace {
QString pluginDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/plugins";
}

struct SyncWork {
    pcb::Board board;
    pcb::SyncReport report;
    qint64 elapsed{};
};
} // namespace

void MainWindow::synchronizeBoard(bool reposition) {
    if (syncing_ || !boardView_->isEnabled()) {
        if (autoSync_)
            syncTimer_->start();
        return;
    }
    if (collaboration_ && collaboration_->active() && !hosting_) {
        statusBar()->showMessage(tr("A sincronizacao compartilhada e feita pelo host."), 5000);
        return;
    }
    syncing_ = true;
    const auto revision = designRevision_;
    auto* watcher = new QFutureWatcher<SyncWork>(this);
    statusBar()->showMessage(tr("Sincronizando encapsulamentos e redes em segundo plano..."));
    connect(
        watcher, &QFutureWatcher<SyncWork>::finished, this, [this, watcher, revision, reposition] {
            try {
                auto work = watcher->result();
                if (revision != designRevision_) {
                    syncing_ = false;
                    watcher->deleteLater();
                    statusBar()->showMessage(
                        tr("O projeto mudou durante o calculo; recalculando a sincronizacao..."));
                    if (autoSync_)
                        syncTimer_->start();
                    else
                        QTimer::singleShot(0, this,
                                           [this, reposition] { synchronizeBoard(reposition); });
                    return;
                }
                board_ = std::move(work.board);
                boardView_->commitExternalChange();
                boardModel_->update();
                if (reposition || work.report.added)
                    boardModel_->resetCamera();
                rebuildBoardPanels();
                if (reposition || work.report.added)
                    boardView_->fitBoard();
                statusBar()->showMessage(QString("PCB sincronizada em %1 ms: %2 novos / %3 "
                                                 "atualizados / %4 removidos / %5 sem mapeamento")
                                             .arg(work.elapsed)
                                             .arg(work.report.added)
                                             .arg(work.report.updated)
                                             .arg(work.report.removed)
                                             .arg(work.report.unmapped),
                                         15000);
                if (!work.report.warnings.empty() && !autoSync_) {
                    QStringList warnings;
                    for (const auto& warning : work.report.warnings)
                        warnings.append(QString::fromStdString(warning));
                    QMessageBox::information(this, tr("Revisao da sincronizacao"),
                                             warnings.mid(0, 30).join('\n'));
                }
            } catch (const std::exception& error) {
                QMessageBox::warning(this, tr("Falha na sincronizacao"),
                                     QString::fromUtf8(error.what()));
            }
            syncing_ = false;
            watcher->deleteLater();
        });
    watcher->setFuture(QtConcurrent::run([document = document_, board = board_,
                                          reposition]() mutable {
        QElapsedTimer timer;
        timer.start();
        // Migrate older native placeholders without changing the user's PCB placement.
        for (auto& footprint : board.footprints) {
            if (!footprint.source.starts_with("LevelCAD:"))
                continue;
            auto symbol =
                std::find_if(document.symbols().begin(), document.symbols().end(),
                             [&](const auto& item) { return item.id == footprint.symbolId; });
            if (symbol == document.symbols().end())
                continue;
            auto prototype = *symbol;
            prototype.footprint = footprint.source;
            auto mapped = officialFootprint(prototype);
            if (mapped && mapped->source != footprint.source) {
                footprint.pads = mapped->pads;
                footprint.source = mapped->source;
                footprint.width = mapped->width;
                footprint.length = mapped->length;
                if (footprint.modelName.empty() || footprint.modelName.starts_with(":/models3d/")) {
                    footprint.mesh.clear();
                    footprint.texturePng.clear();
                    footprint.modelName.clear();
                }
            }
        }
        const bool firstPlacement = board.footprints.empty();
        auto report = board.synchronize(document, {true, reposition, true, officialFootprint});
        if (firstPlacement || reposition)
            board.fitOutline();
        board.showAirwires = true;
        board.showReferences = true;
        board.showPadNumbers = true;
        for (auto& footprint : board.footprints)
            if (footprint.mesh.empty() || footprint.modelName.starts_with(":/models3d/")) {
                try {
                    assignAutomaticModel(footprint);
                } catch (const std::exception& error) {
                    report.warnings.push_back(footprint.reference +
                                              ": modelo 3D nao carregado: " + error.what());
                }
            }
        return SyncWork{std::move(board), std::move(report), timer.elapsed()};
    }));
}

void MainWindow::setupAutomation() {
    syncTimer_ = new QTimer(this);
    syncTimer_->setInterval(250);
    syncTimer_->setSingleShot(true);
    connect(syncTimer_, &QTimer::timeout, this, [this] { synchronizeBoard(); });
    auto* automation = menuBar()->addMenu(tr("Automacao"));
    auto* automatic = automation->addAction(tr("Sincronizacao PCB automatica"));
    automatic->setCheckable(true);
    autoSync_ = QSettings().value("pcb/automaticSync", false).toBool();
    automatic->setChecked(autoSync_);
    connect(automatic, &QAction::toggled, this, [this](bool enabled) {
        autoSync_ = enabled;
        QSettings().setValue("pcb/automaticSync", enabled);
        if (enabled)
            syncTimer_->start();
        else
            syncTimer_->stop();
    });
    auto* dock = new QDockWidget(tr("Assistente de engenharia / IA"), this);
    dock->setObjectName("AiAssistant");
    auto* assistant = new AiAssistant;
    assistant->context = [this] { return commandContext(document_, board_); };
    assistant->revision = [this] { return designRevision_; };
    assistant->onProposal = [this](const QJsonObject& plan, std::uint64_t revision) {
        reviewProposal(plan, revision);
    };
    dock->setWidget(assistant);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->hide();
    automation->addAction(dock->toggleViewAction());
    auto* plugins = automation->addAction(tr("Gerenciador de plugins..."));
    connect(plugins, &QAction::triggered, this, &MainWindow::managePlugins);
    auto* plan = automation->addAction(tr("Abrir proposta de comandos JSON..."));
    connect(plan, &QAction::triggered, this, [this] {
        auto path = QFileDialog::getOpenFileName(this, tr("Abrir proposta"), {}, "JSON (*.json)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
            return;
        QJsonParseError error;
        auto json = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError) {
            QMessageBox::warning(this, tr("Proposta invalida"), error.errorString());
            return;
        }
        reviewProposal(json.object(), designRevision_);
    });
}

void MainWindow::reviewProposal(const QJsonObject& proposal, std::uint64_t revision) {
    if (revision != designRevision_) {
        QMessageBox::information(
            this, tr("Proposta desatualizada"),
            tr("O projeto mudou. Gere uma nova proposta com o contexto atual."));
        return;
    }
    if (!view_->isEnabled() || !boardView_->isEnabled())
        return;
    if (proposal["version"].toInt() != 1 || !proposal["commands"].isArray()) {
        QMessageBox::warning(this, tr("Proposta invalida"),
                             tr("Use version 1 e uma lista commands."));
        return;
    }
    auto preview = previewCommands(document_, board_, proposal["commands"].toArray(),
                                   QSettings().value("profile/name", "Usuario").toString());
    if (!preview.success) {
        QMessageBox::warning(this, tr("Comandos rejeitados"), preview.error);
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Revisar alteracoes do CAD"));
    dialog.resize(700, 540);
    QVBoxLayout layout(&dialog);
    QTextBrowser report;
    report.setPlainText(proposal["summary"].toString() + "\n\n" + preview.changes.join('\n') +
                        "\n\nAvisos:\n" + preview.warnings.join('\n'));
    layout.addWidget(&report);
    QDialogButtonBox buttons(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    connect(buttons.button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog,
            &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (revision != designRevision_ || !view_->isEnabled()) {
        statusBar()->showMessage(tr("Proposta desatualizada. Nada foi aplicado."));
        return;
    }
    document_ = std::move(preview.document);
    board_ = std::move(preview.board);
    boardView_->resetHistory();
    boardView_->refresh();
    view_->refresh();
    boardModel_->update();
    rebuildBoardPanels();
    recordChange();
    statusBar()->showMessage(tr("Proposta aplicada. Desfazer restaura o projeto anterior."), 5000);
}

void MainWindow::managePlugins() {
    QDir().mkpath(pluginDirectory());
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Plugins LevelCAD / API 1"));
    dialog.resize(650, 480);
    QVBoxLayout layout(&dialog);
    QLabel description(
        tr("Plugins adicionam acoes com comandos validados. Nenhum script ou DLL e executado."));
    description.setWordWrap(true);
    layout.addWidget(&description);
    QListWidget list;
    layout.addWidget(&list);
    std::vector<std::pair<QString, PluginAction>> actions;
    auto reload = [&] {
        list.clear();
        actions.clear();
        QStringList errors;
        for (const auto& plugin : discoverPlugins(pluginDirectory(), &errors))
            for (const auto& action : plugin.actions) {
                list.addItem(plugin.name + " / " + action.name);
                actions.push_back({plugin.id, action});
            }
        if (!errors.isEmpty())
            statusBar()->showMessage(errors.join("; "), 15000);
    };
    reload();
    QPushButton install(tr("Instalar plugin JSON...")), run(tr("Revisar acao selecionada"));
    layout.addWidget(&install);
    layout.addWidget(&run);
    connect(&install, &QPushButton::clicked, &dialog, [&] {
        auto path = QFileDialog::getOpenFileName(&dialog, tr("Instalar plugin"), {},
                                                 "LevelCAD plugin (*.levelplugin.json)");
        if (path.isEmpty())
            return;
        try {
            auto plugin = readPlugin(path);
            QFile source(path);
            if (!source.open(QIODevice::ReadOnly))
                throw std::runtime_error("Falha ao ler plugin");
            QSaveFile output(pluginDirectory() + "/" + plugin.id + ".levelplugin.json");
            auto bytes = source.readAll();
            if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
                !output.commit())
                throw std::runtime_error("Falha ao instalar plugin");
            reload();
        } catch (const std::exception& error) {
            QMessageBox::warning(&dialog, tr("Plugin invalido"), QString::fromUtf8(error.what()));
        }
    });
    connect(&run, &QPushButton::clicked, &dialog, [&] {
        auto index = list.currentRow();
        if (index < 0 || index >= static_cast<int>(actions.size()))
            return;
        auto action = actions[index].second;
        reviewProposal({{"version", 1}, {"summary", action.name}, {"commands", action.commands}},
                       designRevision_);
    });
    dialog.exec();
}
} // namespace levelcad::app