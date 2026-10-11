#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QStyleFactory>
#include <QTimer>
#include <levelcad/app/Launcher.hpp>
#include <levelcad/app/MainWindow.hpp>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setFont(QFont("Segoe UI",10));app.setWindowIcon(QIcon(":/branding/icon.png"));
    app.setApplicationName("LevelCAD");app.setOrganizationName("The Archlevel Foundation");app.setStyle(QStyleFactory::create("Fusion"));
    levelcad::app::Launcher launcher;
    if(launcher.exec()!=QDialog::Accepted) return 0;
    levelcad::app::LoadingScreen splash;splash.show();splash.progress(5,"Iniciando o nucleo do projeto...");
    levelcad::app::MainWindow window([&](int value,const QString& stage){splash.progress(value,stage);});
    window.show();splash.progress(100,"Ambiente pronto");splash.finish(&window);
    const auto operation=launcher.operation(),file=launcher.selectedFile();
    QTimer::singleShot(0,&window,[&window,operation,file] {
        if(operation=="open") window.openProject(file);
        else if(operation=="host" || operation=="join")
            for(auto* action:window.findChildren<QAction*>())
                if(action->text()==(operation=="host"?"Hospedar projeto...":"Conectar por IPv4 e porta...")){action->trigger();break;}
    });
    return app.exec();
}
