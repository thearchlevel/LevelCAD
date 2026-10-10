#pragma once
#include <QString>
#include <QStringList>
#include <levelcad/pcb/Board.hpp>

namespace levelcad::app {
QStringList bundledModels();
std::vector<pcb::MeshTriangle> loadStl(const QString& path);
void assignMesh(pcb::Footprint& footprint, const QString& path);
} // namespace levelcad::app
