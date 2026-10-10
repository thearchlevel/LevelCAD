#pragma once
#include <levelcad/connectivity/Connectivity.hpp>
#include <string>

namespace levelcad::exporters {
struct SpiceOptions {
    std::string title{"LevelCAD circuit"};
    std::string modelLibrary;
};

struct SpiceResult {
    std::string text;
    std::vector<connectivity::Diagnostic> diagnostics;

    bool success() const {
        return !text.empty();
    }
};

// Generates a standalone SPICE deck; running a simulator remains a separate step.
SpiceResult exportSpice(const core::Document& document, const SpiceOptions& options = {});
} // namespace levelcad::exporters
