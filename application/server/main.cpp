#include <QCoreApplication>
#include <QNetworkInterface>
#include <QTextStream>
#include <QDir>
#include <levelcad/app/Collaboration.hpp>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout), input(stdin);
    out << "LevelCAD Server  Wenderson Dias\nPublicado pela The Archlevel Foundation\n";
    auto args = app.arguments();
    bool ok = false;
    int port = args.size() > 1 ? args[1].toInt(&ok) : 0;
    if (args.size() < 2) { out << "Porta TCP [45454]: " << Qt::flush; auto text = input.readLine().trimmed(); port = text.isEmpty() ? 45454 : text.toInt(&ok); if (text.isEmpty()) ok = true; }
    if (!ok || port < 1 || port > 65535) { out << "Porta inv�lida\n"; return 1; }
    QString directory = args.size() > 2 ? args[2] : QDir::currentPath() + "/LevelCAD-session";
    levelcad::app::CollaborationServer server(directory);
    if (!server.listen(static_cast<quint16>(port))) { out << "Nao foi poss�vel abrir a porta\n"; return 1; }
    out << "IPv4 disponiveis (use o endere�o da interface conectada � sua rede):\n";
    for (const auto& interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp) || interface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const auto& address : interface.addressEntries()) if (address.ip().protocol() == QAbstractSocket::IPv4Protocol) out << interface.humanReadableName() << ": " << address.ip().toString() << ":" << port << "\n";
    }
    out << "Local: 127.0.0.1:" << port << "\nProjeto e logs: " << QDir(directory).absolutePath() << "\n" << Qt::flush;
    return app.exec();
}
