#pragma once
#include <QJsonObject>
#include <QString>
#include <levelcad/pcb/Board.hpp>

namespace levelcad::app {
QJsonObject boardToJson(const pcb::Board& board);
bool boardFromJson(pcb::Board& destination, const QJsonObject& root, QString* error);
} // namespace levelcad::app
