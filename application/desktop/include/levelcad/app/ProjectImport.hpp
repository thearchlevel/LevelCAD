#pragma once
#include <QString>
#include <QStringList>
#include <levelcad/core/Document.hpp>
#include <levelcad/pcb/Board.hpp>

namespace levelcad::app {
struct ProjectImport {
    core::Document document;
    pcb::Board board;
    QStringList warnings;
    bool hasSchematic{}, hasBoard{};
};

ProjectImport importExternalProject(const QString& path);
} // namespace levelcad::app
