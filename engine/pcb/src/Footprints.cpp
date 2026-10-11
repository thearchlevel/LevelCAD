#include <algorithm>
#include <cctype>
#include <cmath>
#include <levelcad/pcb/Footprints.hpp>
#include <regex>
#include <set>

namespace levelcad::pcb {
namespace {
core::Coord nm(double mm) {
    return static_cast<core::Coord>(std::llround(mm * core::nmPerMm));
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return value;
}
} // namespace

std::vector<std::string> footprintCatalog() {
    return {"LevelCAD:R_Axial_P7.62mm",
            "LevelCAD:C_Radial_P2.50mm",
            "LevelCAD:D_Axial_P7.62mm",
            "LevelCAD:LED_D5.0mm",
            "LevelCAD:Header_1x02_P2.54mm",
            "LevelCAD:Header_1x03_P2.54mm",
            "LevelCAD:Header_1x04_P2.54mm",
            "LevelCAD:Header_1x08_P2.54mm",
            "LevelCAD:R_0402",
            "LevelCAD:R_0603",
            "LevelCAD:R_0805",
            "LevelCAD:R_1206",
            "LevelCAD:C_0402",
            "LevelCAD:C_0603",
            "LevelCAD:C_0805",
            "LevelCAD:C_1206",
            "LevelCAD:DIP-8",
            "LevelCAD:DIP-14",
            "LevelCAD:DIP-16",
            "LevelCAD:DIP-20",
            "LevelCAD:DIP-28",
            "LevelCAD:SOIC-8",
            "LevelCAD:SOIC-14",
            "LevelCAD:SOIC-16",
            "LevelCAD:SOT-23",
            "LevelCAD:SOT-223",
            "LevelCAD:TQFP-32",
            "LevelCAD:TQFP-48",
            "LevelCAD:TQFP-64"};
}

bool matchesPins(const Footprint& footprint, const core::Symbol& symbol) {
    std::set<int> physical, electrical;
    for (const auto& pad : footprint.pads)
        physical.insert(pad.number);
    for (const auto& pin : core::symbolPins(symbol))
        electrical.insert(pin.number);
    return !physical.empty() && physical == electrical;
}

std::optional<Footprint> makeFootprint(const core::Symbol& symbol, const std::string& requested) {
    auto package = requested.empty() ? symbol.footprint : requested;
    const auto pins = core::symbolPins(symbol);
    if (package.empty()) {
        switch (symbol.kind) {
        case core::SymbolKind::Resistor:
            package = "LevelCAD:R_Axial_P7.62mm";
            break;
        case core::SymbolKind::Capacitor:
            package = "LevelCAD:C_Radial_P2.50mm";
            break;
        case core::SymbolKind::Diode:
            package = "LevelCAD:D_Axial_P7.62mm";
            break;
        case core::SymbolKind::LED:
            package = "LevelCAD:LED_D5.0mm";
            break;
        case core::SymbolKind::Connector:
            package = "LevelCAD:Header_1x02_P2.54mm";
            break;
        case core::SymbolKind::Inductor:
            package = "LevelCAD:R_Axial_P7.62mm";
            break;
        case core::SymbolKind::Switch:
            package = "LevelCAD:Header_1x02_P2.54mm";
            break;
        case core::SymbolKind::Library:
            if (pins.size() == 2 && symbol.libraryId.starts_with("Device:R"))
                package = "LevelCAD:R_0603";
            else if (pins.size() == 2 && symbol.libraryId.starts_with("Device:C"))
                package = "LevelCAD:C_0603";
            break;
        default:
            break;
        }
    }
    auto key = lower(package);
    Footprint result;
    result.source = package;
    result.symbolId = symbol.id;
    result.reference = symbol.reference;
    result.value = symbol.value;
    auto pad = [&](int number, double x, double y, double sx, double sy, double drill = 0) {
        Pad p;
        p.number = number;
        p.offset = {nm(x), nm(y)};
        p.sizeX = nm(sx);
        p.sizeY = nm(sy);
        p.diameter = std::max(p.sizeX, p.sizeY);
        p.drill = nm(drill);
        p.shape = drill == 0 ? "roundrect" : "circle";
        p.layerMask = drill == 0 ? 1u : 0xffffffffu;
        result.pads.push_back(p);
    };
    std::smatch match;
    if (std::regex_search(key, match, std::regex(R"((?:r|c)_(0402|0603|0805|1206))"))) {
        const auto code = match[1].str();
        double pitch = code == "0402" ? 1.05 : code == "0603" ? 1.65 : code == "0805" ? 2.1 : 3.2;
        double sx = code == "0402" ? .55 : code == "0603" ? .8 : 1.0;
        double sy = code == "0402" ? .65 : code == "0603" ? .95 : code == "0805" ? 1.35 : 1.75;
        pad(1, -pitch / 2, 0, sx, sy);
        pad(2, pitch / 2, 0, sx, sy);
        result.width = nm(pitch + sx);
        result.length = nm(sy);
        result.height = nm(.8);
    } else if (key.find("axial") != std::string::npos) {
        pad(1, -3.81, 0, 1.8, 1.8, .8);
        pad(2, 3.81, 0, 1.8, 1.8, .8);
        result.width = nm(6);
        result.length = nm(2.5);
        result.height = nm(2.5);
    } else if (key.find("radial") != std::string::npos || key.find("led_d5") != std::string::npos) {
        pad(1, -1.25, 0, 1.8, 1.8, .8);
        pad(2, 1.25, 0, 1.8, 1.8, .8);
        result.width = result.length = nm(5);
        result.height = nm(key.find("led") != std::string::npos ? 8 : 6);
    } else if (std::regex_search(key, match, std::regex(R"((?:header|pinheader)_1x0?(\d+))"))) {
        int count = std::stoi(match[1].str());
        if (count < 1 || count > 40)
            return {};
        for (int i = 0; i < count; ++i)
            pad(i + 1, 0, (i - (count - 1) / 2.0) * 2.54, 1.8, 1.8, 1);
        result.width = nm(2.54);
        result.length = nm(count * 2.54);
        result.height = nm(8);
    } else if (std::regex_search(key, match, std::regex(R"((dip|soic)-(\d+))"))) {
        int count = std::stoi(match[2].str());
        if (count < 4 || count > 64 || count % 2)
            return {};
        bool dip = match[1] == "dip";
        double pitch = dip ? 2.54 : 1.27, span = dip ? (count > 20 ? 15.24 : 7.62) : 5.4;
        for (int i = 0; i < count; ++i) {
            bool left = i < count / 2;
            int row = left ? i : count - 1 - i;
            pad(i + 1, left ? -span / 2 : span / 2, (row - (count / 2 - 1) / 2.0) * pitch,
                dip ? 1.8 : 1.6, dip ? 1.8 : .6, dip ? .8 : 0);
        }
        result.width = nm(dip ? 6.5 : 3.9);
        result.length = nm(count / 2 * pitch);
        result.height = nm(dip ? 3.5 : 1.75);
    } else if (key.find("sot-23") != std::string::npos &&
               key.find("sot-23-5") == std::string::npos) {
        pad(1, -1, -.95, 1, .6);
        pad(2, -1, .95, 1, .6);
        pad(3, 1, 0, 1, .6);
        result.width = nm(1.3);
        result.length = nm(2.9);
        result.height = nm(1.1);
    } else if (key.find("sot-223") != std::string::npos) {
        for (int i = 0; i < 3; ++i)
            pad(i + 1, -3.2, (i - 1) * 2.3, 2.2, 1.2);
        pad(4, 3.2, 0, 2.2, 3.8);
        result.width = nm(3.5);
        result.length = nm(6.5);
        result.height = nm(1.8);
    } else if (std::regex_search(key, match, std::regex(R"(tqfp-(\d+))"))) {
        int count = std::stoi(match[1].str());
        if (count < 16 || count > 144 || count % 4)
            return {};
        double pitch = count == 32 ? .8 : .5,
               body = count == 32   ? 7
                      : count == 48 ? 7
                                    : 10,
               span = body / 2 + .7;
        for (int i = 0; i < count / 4; ++i) {
            double along = (i - (count / 4 - 1) / 2.0) * pitch;
            pad(i + 1, -span, along, 1.5, .3);
            pad(count / 4 + i + 1, along, span, .3, 1.5);
            pad(count / 2 + i + 1, span, -along, 1.5, .3);
            pad(3 * count / 4 + i + 1, -along, -span, .3, 1.5);
        }
        result.width = result.length = nm(body);
        result.height = nm(1.4);
    } else
        return {};
    if (!matchesPins(result, symbol))
        return {};
    return result;
}
} // namespace levelcad::pcb
