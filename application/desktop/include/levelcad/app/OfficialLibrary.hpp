#pragma once
#include <QString>
#include <levelcad/pcb/Board.hpp>
#include <optional>

namespace levelcad::app {
QString officialLibraryRoot();
QString officialFootprintPath(const QString& identifier);
std::optional<pcb::Footprint> officialFootprint(const core::Symbol& symbol);
bool assignOfficialModel(pcb::Footprint& footprint);
std::vector<pcb::MeshTriangle> loadOfficialVrml(const QString& path);
} // namespace levelcad::app