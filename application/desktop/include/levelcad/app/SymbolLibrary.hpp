#pragma once
#include <QString>
#include <QStringList>
#include <levelcad/core/Document.hpp>

namespace levelcad::app {
struct LibraryEntry {
    QString name;
    QString description;
    core::Symbol prototype;
};

struct LibraryImport {
    std::vector<LibraryEntry> entries;
    QStringList skipped;
};

// Imported geometry travels with each placed symbol, so projects stay self-contained.
LibraryImport loadKiCadLibrary(const QString& path);
LibraryImport loadKiCadLibraryText(const QString& text, const QString& category);
std::vector<LibraryEntry> bundledLibrary();
} // namespace levelcad::app
