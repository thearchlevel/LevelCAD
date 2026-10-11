#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <levelcad/app/PluginEngine.hpp>
#include <set>
#include <stdexcept>

namespace levelcad::app {
CadPlugin readPlugin(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        throw std::runtime_error("Plugin nao pode ser lido (limite: 1 MB)");
    QJsonParseError error;
    auto document = QJsonDocument::fromJson(file.readAll(), &error);
    auto root = document.object();
    if (error.error != QJsonParseError::NoError || root["apiVersion"].toInt() != 1 ||
        root["format"] != "levelcad-plugin")
        throw std::runtime_error("Formato de plugin invalido");
    CadPlugin plugin{
        root["id"].toString(), root["name"].toString(), root["description"].toString(), path, {}};
    static const QRegularExpression id("^[a-z0-9][a-z0-9._-]{0,79}$");
    if (!id.match(plugin.id).hasMatch() || plugin.name.isEmpty() || plugin.name.size() > 120)
        throw std::runtime_error("Identidade do plugin invalida");
    for (auto value : root["actions"].toArray()) {
        auto action = value.toObject();
        auto name = action["name"].toString();
        auto commands = action["commands"].toArray();
        if (name.isEmpty() || commands.isEmpty() || commands.size() > 256)
            throw std::runtime_error("Acao do plugin invalida");
        plugin.actions.push_back({name, commands});
    }
    if (plugin.actions.empty() || plugin.actions.size() > 32)
        throw std::runtime_error("Plugin precisa de 1 a 32 acoes");
    return plugin;
}

std::vector<CadPlugin> discoverPlugins(const QString& directory, QStringList* errors) {
    std::vector<CadPlugin> plugins;
    std::set<QString> ids;
    for (const auto& name :
         QDir(directory).entryList({"*.levelplugin.json"}, QDir::Files, QDir::Name)) {
        try {
            auto plugin = readPlugin(QDir(directory).filePath(name));
            if (!ids.insert(plugin.id).second)
                throw std::runtime_error("ID de plugin repetido");
            plugins.push_back(std::move(plugin));
        } catch (const std::exception& error) {
            if (errors)
                errors->append(name + ": " + QString::fromUtf8(error.what()));
        }
    }
    return plugins;
}
} // namespace levelcad::app
