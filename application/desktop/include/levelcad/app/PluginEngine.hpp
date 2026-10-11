#pragma once
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <vector>

namespace levelcad::app {
struct PluginAction {
    QString name;
    QJsonArray commands;
};

struct CadPlugin {
    QString id, name, description, path;
    std::vector<PluginAction> actions;
};

CadPlugin readPlugin(const QString& path);
std::vector<CadPlugin> discoverPlugins(const QString& directory, QStringList* errors = nullptr);
} // namespace levelcad::app
