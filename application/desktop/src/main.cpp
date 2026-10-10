#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSplashScreen>
#include <QStyleFactory>
#include <QTimer>
#include <QVBoxLayout>
#include <levelcad/app/MainWindow.hpp>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setFont(QFont("Segoe UI", 10));
    app.setWindowIcon(QIcon(":/branding/icon.png"));
    app.setApplicationName("LevelCAD");
    app.setOrganizationName("The Archlevel Foundation");
    app.setStyle(QStyleFactory::create("Fusion"));
    QDialog launcher;
    launcher.setWindowTitle("LevelCAD Launcher");
    launcher.resize(820, 500);
    launcher.setStyleSheet(
        "QDialog{background:#101b2a;color:#e5efff;} QLabel{color:#d6e4f7;} "
        "QLineEdit,QComboBox{background:#22364b;color:white;padding:10px;border:1px solid "
        "#3d5973;border-radius:7px;} "
        "QPushButton{background:#24415b;color:white;padding:20px;border:1px solid "
        "#395c78;border-radius:9px;} QPushButton:hover{background:#366482;}");
    QVBoxLayout layout(&launcher);
    layout.setContentsMargins(40, 32, 40, 28);
    layout.setSpacing(20);
    QLabel mark;
    mark.setPixmap(QPixmap(":/branding/icon.png")
                       .scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout.addWidget(&mark);
    QLabel title("LevelCAD");
    title.setFont(QFont("Segoe UI", 38, QFont::Bold));
    layout.addWidget(&title);
    QLabel subtitle("Versao oficial  |  Schematic Capture  /  PCB Layout  /  3D Viewer");
    layout.addWidget(&subtitle);
    QLineEdit name(QSettings().value("profile/name").toString());
    name.setPlaceholderText("Seu nome - usado nos comentarios e no historico de edicao");
    layout.addWidget(&name);
    QHBoxLayout choices;
    QPushButton create("Novo projeto\nDesenhe seu circuito"),
        open("Abrir projeto\nContinue seu trabalho"), host("Hospedar\nTrabalhe em equipe"),
        join("Conectar\nIPv4 e porta");
    for (auto* button : {&create, &open, &host, &join})
        choices.addWidget(button);
    layout.addLayout(&choices);
    QString selectedFile, operation;
    auto accept = [&](const QString& action) {
        if (name.text().trimmed().isEmpty()) {
            name.setFocus();
            QMessageBox::information(&launcher, "Seu nome", "Informe seu nome para continuar.");
            return;
        }
        operation = action;
        launcher.accept();
    };
    QObject::connect(&create, &QPushButton::clicked, &launcher, [&] { accept("new"); });
    QObject::connect(&host, &QPushButton::clicked, &launcher, [&] { accept("host"); });
    QObject::connect(&join, &QPushButton::clicked, &launcher, [&] { accept("join"); });
    QObject::connect(&open, &QPushButton::clicked, &launcher, [&] {
        selectedFile = QFileDialog::getOpenFileName(
            &launcher, "Abrir projeto", {},
            "LevelCAD (*.levelsch);;Projeto externo (*.kicad_pro *.kicad_sch *.kicad_pcb)");
        if (!selectedFile.isEmpty())
            accept("open");
    });
    QComboBox recent;
    recent.addItem("Projetos recentes");
    for (auto path : QSettings().value("files/recent").toStringList())
        if (QFileInfo::exists(path))
            recent.addItem(QFileInfo(path).fileName(), path);
    QObject::connect(&recent, &QComboBox::activated, &launcher, [&](int index) {
        if (index > 0) {
            selectedFile = recent.itemData(index).toString();
            accept("open");
        }
    });
    layout.addWidget(&recent);
    layout.addStretch();
    // Widgets nativos mantem o selo nitido em diferentes escalas de tela.
    auto* brazil = new QWidget(&launcher);
    auto* brazilLayout = new QVBoxLayout(brazil);
    brazilLayout->setContentsMargins(0, 0, 0, 0);
    brazilLayout->setSpacing(5);
    auto* made = new QLabel("Made in <b>Brazil</b>", brazil);
    made->setFont(QFont("Segoe UI", 12));
    brazilLayout->addWidget(made);
    auto* colors = new QHBoxLayout;
    colors->setSpacing(0);
    colors->setContentsMargins(0, 0, 0, 0);
    for (auto color : {"#00c853", "#ffe100", "#168aff"}) {
        auto* stripe = new QFrame(brazil);
        stripe->setFixedSize(38, 2);
        stripe->setStyleSheet(QString("background:%1;").arg(color));
        colors->addWidget(stripe);
    }
    colors->addStretch();
    brazilLayout->addLayout(colors);
    layout.addWidget(brazil);
    QLabel credits("Criado por Wenderson Dias\nPublicado pela The Archlevel Foundation");
    layout.addWidget(&credits);
    if (launcher.exec() != QDialog::Accepted)
        return 0;
    QSettings().setValue("profile/name", name.text().trimmed());
    QPixmap image(620, 280);
    image.fill(QColor("#101b2a"));
    {
        QPainter painter(&image);
        painter.setPen(QColor("#7fd9c5"));
        painter.setFont(QFont("Segoe UI", 32, QFont::Bold));
        painter.drawText(QRect(30, 35, 560, 70), Qt::AlignCenter, "LevelCAD");
        painter.setFont(QFont("Segoe UI", 12));
        painter.setPen(QColor("#d6e4f7"));
        painter.drawText(QRect(30, 140, 560, 80), Qt::AlignCenter,
                         "Carregando biblioteca, PCB e ferramentas 3D...");
    }
    QSplashScreen splash(image);
    splash.show();
    app.processEvents();
    levelcad::app::MainWindow window;
    window.show();
    splash.finish(&window);
    QTimer::singleShot(0, &window, [&window, operation, selectedFile] {
        if (operation == "open")
            window.openProject(selectedFile);
        else if (operation == "host" || operation == "join")
            for (auto* action : window.findChildren<QAction*>())
                if (action->text() == (operation == "host" ? "Hospedar projeto..."
                                                           : "Conectar por IPv4 e porta...")) {
                    action->trigger();
                    break;
                }
    });
    return app.exec();
}
