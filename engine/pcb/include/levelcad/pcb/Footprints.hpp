#pragma once
#include <levelcad/pcb/Board.hpp>
#include <optional>

namespace levelcad::pcb {
// Encapsulamentos nativos com numeracao fisica explicita; nunca invente pads para um CI
// desconhecido.
std::vector<std::string> footprintCatalog();
std::optional<Footprint> makeFootprint(const core::Symbol& symbol, const std::string& package = {});
bool matchesPins(const Footprint& footprint, const core::Symbol& symbol);
} // namespace levelcad::pcb
