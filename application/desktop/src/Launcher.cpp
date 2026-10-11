#include <QApplication>
#include <algorithm>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <levelcad/app/Launcher.hpp>

namespace levelcad::app {
Launcher::Launcher(QWidget* parent):QDialog(parent) {
    setWindowTitle("LevelCAD | Versao oficial"); resize(1080,740); setMinimumSize(960,690);
    setStyleSheet("QDialog{background:#111923;color:#edf3fa;} QLabel{color:#d7e2ee;} "
                  "QFrame#Brand{background:#0b121b;border:0;border-radius:12px;} "
                  "QLineEdit{background:#1c2939;color:white;padding:12px;border:1px solid #32475d;border-radius:7px;} "
                  "QPushButton{background:#1d2e40;color:#eef5fd;padding:18px;text-align:left;border:1px solid #32485e;border-radius:9px;} "
                  "QPushButton:hover{background:#294a60;border-color:#67cdb2;} "
                  "QPushButton#Primary{background:#286858;border-color:#58b99e;} "
                  "QListWidget{background:#141f2c;color:#d5e4f4;border:1px solid #293c50;border-radius:8px;padding:7px;} QListWidget::item{padding:10px;} QListWidget::item:selected{background:#295147;}");
    auto* outer=new QHBoxLayout(this); outer->setContentsMargins(18,18,18,18); outer->setSpacing(26);
    auto* brand=new QFrame; brand->setObjectName("Brand"); brand->setFixedWidth(250);
    auto* side=new QVBoxLayout(brand); side->setContentsMargins(24,30,24,26); side->setSpacing(18);
    auto* logo=new QLabel; logo->setPixmap(QPixmap(":/branding/icon.png").scaled(80,80,Qt::KeepAspectRatio,Qt::SmoothTransformation)); side->addWidget(logo);
    auto* title=new QLabel("LevelCAD"); title->setFont(QFont("Segoe UI",28,QFont::Bold)); side->addWidget(title);
    auto* official=new QLabel("VERSAO OFICIAL"); official->setStyleSheet("color:#80d5bc;font-weight:600;font-size:11px;letter-spacing:2px;"); side->addWidget(official);
    auto* subtitle=new QLabel("Engenharia eletronica\nC++20 / desktop nativo"); subtitle->setStyleSheet("color:#94aabd;line-height:1.5;"); side->addWidget(subtitle);
    auto* capabilities=new QLabel("Esquematico\n\nPCB e camadas\n\nModelos e materiais 3D\n\nColaboracao em tempo real\n\nAssistente IA e plugins"); capabilities->setStyleSheet("color:#c2d3e4;"); side->addWidget(capabilities);
    side->addStretch();
    auto* made=new QLabel("Made in <b>Brazil</b>"); made->setFont(QFont("Segoe UI",12)); side->addWidget(made);
    auto* colors=new QHBoxLayout; colors->setSpacing(0);
    for(const auto* color:{"#00c853","#ffe100","#168aff"}) { auto* stripe=new QFrame; stripe->setFixedSize(38,2); stripe->setStyleSheet(QString("background:%1;").arg(color));colors->addWidget(stripe); } colors->addStretch(); side->addLayout(colors);
    auto* credits=new QLabel("Criado por Wenderson Dias\nPublicado pela\nThe Archlevel Foundation"); credits->setStyleSheet("color:#8da3b8;font-size:11px;"); side->addWidget(credits);
    outer->addWidget(brand);
    auto* main=new QVBoxLayout; main->setContentsMargins(0,18,22,8); main->setSpacing(16); outer->addLayout(main,1);
    auto* greeting=new QLabel("Seu proximo circuito comeca aqui"); greeting->setFont(QFont("Segoe UI",22,QFont::DemiBold)); main->addWidget(greeting);
    auto* intro=new QLabel("Crie, revise e compartilhe projetos em um unico ambiente."); intro->setWordWrap(true); main->addWidget(intro);
    name_=new QLineEdit(QSettings().value("profile/name").toString()); name_->setPlaceholderText("Seu nome - identifica comentarios e edicoes"); name_->setMaxLength(80); name_->setObjectName("UserName"); main->addWidget(name_);
    auto* grid=new QGridLayout; grid->setSpacing(12);
    auto* create=new QPushButton("Novo projeto\nEsquematico, PCB e visualizacao 3D"); create->setObjectName("Primary");
    auto* open=new QPushButton("Abrir projeto\nContinue um projeto salvo ou externo");
    auto* host=new QPushButton("Hospedar projeto\nInicie o servidor e convide sua equipe");
    auto* join=new QPushButton("Conectar a equipe\nEntre usando IPv4 e porta");
    grid->addWidget(create,0,0);grid->addWidget(open,0,1);grid->addWidget(host,1,0);grid->addWidget(join,1,1);main->addLayout(grid);
    connect(create,&QPushButton::clicked,this,[this]{choose("new");});
    connect(host,&QPushButton::clicked,this,[this]{choose("host");});
    connect(join,&QPushButton::clicked,this,[this]{choose("join");});
    connect(open,&QPushButton::clicked,this,[this]{file_=QFileDialog::getOpenFileName(this,"Abrir projeto",{},"Projetos (*.levelsch *.kicad_pro *.kicad_sch *.kicad_pcb);;Todos os arquivos (*)");if(!file_.isEmpty()) choose("open");});
    auto* recentTitle=new QLabel("PROJETOS RECENTES");recentTitle->setStyleSheet("color:#95b0c9;font-weight:600;font-size:11px;");main->addWidget(recentTitle);
    recent_=new QListWidget;recent_->setObjectName("RecentProjects");main->addWidget(recent_,1);
    for(const auto& path:QSettings().value("files/recent").toStringList()) if(QFileInfo::exists(path)) {
        QFileInfo file(path);auto* item=new QListWidgetItem(file.completeBaseName()+"\n"+file.absolutePath()+"  |  "+file.lastModified().toString("dd/MM/yyyy"),recent_);item->setData(Qt::UserRole,path);item->setToolTip(path);
    }
    if(recent_->count()==0) {auto* empty=new QListWidgetItem("Seus projetos recentes aparecerao aqui.",recent_);empty->setFlags(Qt::NoItemFlags);}
    connect(recent_,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem* item){file_=item->data(Qt::UserRole).toString();if(!file_.isEmpty()) choose("open");});
    auto* hint=new QLabel("Dica: confira ERC, encapsulamentos e DRC antes de fabricar uma placa.");hint->setWordWrap(true);hint->setStyleSheet("color:#8fa8bd;font-size:11px;");main->addWidget(hint);
}

void Launcher::choose(const QString& operation) {
    if(name_->text().trimmed().isEmpty()) {name_->setFocus();name_->setStyleSheet("border:1px solid #d7a05d;");name_->setPlaceholderText("Informe seu nome para continuar");return;}
    QSettings().setValue("profile/name",name_->text().trimmed());operation_=operation;accept();
}

LoadingScreen::LoadingScreen():QSplashScreen(QPixmap(680,350)) {
    setWindowFlag(Qt::WindowStaysOnTopHint);progress(0,"Preparando o ambiente...");
}
void LoadingScreen::progress(int value,const QString& stage) {
    progress_=std::clamp(value,0,100);stage_=stage;repaint();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}
void LoadingScreen::drawContents(QPainter* painter) {
    painter->setRenderHint(QPainter::Antialiasing);painter->fillRect(rect(),QColor("#101a25"));
    painter->drawPixmap(42,38,QPixmap(":/branding/icon.png").scaled(64,64,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    painter->setPen(QColor("#eff5fc"));painter->setFont(QFont("Segoe UI",30,QFont::Bold));painter->drawText(QRect(128,34,510,58),Qt::AlignVCenter,"LevelCAD");
    painter->setFont(QFont("Segoe UI",10));painter->setPen(QColor("#83d6bc"));painter->drawText(QRect(131,94,500,25),"VERSAO OFICIAL / ENGENHARIA ELETRONICA");
    painter->setPen(QColor("#d4e2f1"));painter->setFont(QFont("Segoe UI",12));painter->drawText(QRect(42,166,590,48),Qt::AlignVCenter,stage_);
    painter->setPen(Qt::NoPen);painter->setBrush(QColor("#263b50"));painter->drawRoundedRect(QRectF(42,230,596,6),3,3);
    painter->setBrush(QColor("#75cdb1"));painter->drawRoundedRect(QRectF(42,230,596*progress_/100.0,6),3,3);
    painter->setPen(QColor("#8aa3bb"));painter->setFont(QFont("Segoe UI",10));painter->drawText(QRect(42,255,550,35),Qt::AlignLeft,"Wenderson Dias / The Archlevel Foundation");
    painter->drawText(QRect(570,255,68,35),Qt::AlignRight,QString::number(progress_)+"%");
}
} // namespace levelcad::app
