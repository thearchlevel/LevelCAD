#pragma once
#include <QString>
#include <levelcad/core/Document.hpp>
#include <levelcad/pcb/Board.hpp>

namespace levelcad::app {
// Exporta uma fotografia do projeto. Nunca modifica a placa enquanto gera os arquivos.
QString productionBoard(const pcb::Board& board);
QString exportProduction(const pcb::Board& board, const core::Document& document,
                         const QString& destination, const QString& tool);
QString productionTool();
} // namespace levelcad::app