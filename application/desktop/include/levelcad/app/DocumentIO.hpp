#pragma once
#include <QJsonObject>
#include <QString>
#include <levelcad/core/Document.hpp>
#include <levelcad/pcb/Board.hpp>

namespace levelcad::app {
QJsonObject documentToJson(const core::Document& doc);
bool documentFromJson(core::Document& doc, const QJsonObject& object, QString* error);
bool saveDocument(const core::Document& doc, const QString& fileName, QString* error,
                  pcb::Board* board = nullptr);
bool loadDocument(core::Document& doc, const QString& fileName, QString* error,
                  pcb::Board* board = nullptr);
} // namespace levelcad::app
