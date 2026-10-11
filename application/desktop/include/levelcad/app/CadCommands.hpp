#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <levelcad/core/Document.hpp>
#include <levelcad/pcb/Board.hpp>

namespace levelcad::app {
struct CommandResult {
    core::Document document;
    pcb::Board board;
    QStringList changes, warnings;
    bool success{};
    QString error;
};

// IA e plugins usam o mesmo conjunto de comandos: valide toda a proposta antes de publicar.
CommandResult previewCommands(const core::Document& document, const pcb::Board& board,
                              const QJsonArray& commands, const QString& author);
QString commandInstructions();
QJsonObject commandContext(const core::Document& document, const pcb::Board& board);
} // namespace levelcad::app
