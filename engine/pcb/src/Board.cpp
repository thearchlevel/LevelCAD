#include <algorithm>
#include <cmath>
#include <levelcad/connectivity/Connectivity.hpp>
#include <levelcad/pcb/Board.hpp>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>

namespace levelcad::pcb {
namespace {
struct Union {
    std::vector<std::size_t> parents;

    explicit Union(std::size_t size) : parents(size) {
        std::iota(parents.begin(), parents.end(), 0);
    }

    std::size_t root(std::size_t index) {
        return parents[index] == index ? index : parents[index] = root(parents[index]);
    }

    void join(std::size_t first, std::size_t second) {
        parents[root(first)] = root(second);
    }
};

double distance(core::Point a, core::Point b) {
    return std::hypot(double(a.x) - b.x, double(a.y) - b.y);
}

bool contains(core::Point point, const Track& track) {
    return connectivity::pointOnWire(point, {track.id, track.from, track.to});
}

double pointDistance(core::Point point, const Track& track) {
    const double dx = double(track.to.x) - track.from.x, dy = double(track.to.y) - track.from.y;
    const double lengthSquared = dx * dx + dy * dy;
    const double projection = lengthSquared == 0
                                  ? 0
                                  : std::clamp(((double(point.x) - track.from.x) * dx +
                                                (double(point.y) - track.from.y) * dy) /
                                                   lengthSquared,
                                               0.0, 1.0);
    return std::hypot(double(point.x) - track.from.x - projection * dx,
                      double(point.y) - track.from.y - projection * dy);
}

bool crosses(const Track& first, const Track& second) {
    auto cross = [](core::Point a, core::Point b, core::Point c) {
        return (double(b.x) - a.x) * (double(c.y) - a.y) -
               (double(b.y) - a.y) * (double(c.x) - a.x);
    };
    const auto a = cross(first.from, first.to, second.from),
               b = cross(first.from, first.to, second.to);
    const auto c = cross(second.from, second.to, first.from),
               d = cross(second.from, second.to, first.to);
    return ((a > 0 && b < 0) || (a < 0 && b > 0)) && ((c > 0 && d < 0) || (c < 0 && d > 0));
}
} // namespace

core::Point Board::padPosition(const Footprint& footprint, const Pad& pad) const {
    const auto radians = footprint.rotation * std::acos(-1.0) / 180;
    return {footprint.position.x +
                static_cast<core::Coord>(std::llround(pad.offset.x * std::cos(radians) -
                                                      pad.offset.y * std::sin(radians))),
            footprint.position.y +
                static_cast<core::Coord>(std::llround(pad.offset.x * std::sin(radians) +
                                                      pad.offset.y * std::cos(radians)))};
}

void Board::synchronize(const core::Document& document) {
    const auto graph = connectivity::analyze(document);
    const auto previous = footprints;
    footprints.clear();
    for (const auto& symbol : document.symbols()) {
        if (symbol.kind == core::SymbolKind::Ground)
            continue;
        Footprint footprint;
        const auto found = std::find_if(previous.begin(), previous.end(), [&](const auto& item) {
            return item.symbolId == symbol.id;
        });
        if (found != previous.end())
            footprint = *found;
        else
            footprint.position = {
                10000000 + static_cast<core::Coord>(footprints.size() % 4) * 22000000,
                10000000 + static_cast<core::Coord>(footprints.size() / 4) * 16000000};
        footprint.symbolId = symbol.id;
        footprint.reference = symbol.reference;
        footprint.value = symbol.value;
        footprint.source = "Generic through-hole";
        footprint.pads.clear();
        const auto pins = core::symbolPins(symbol);
        if (pins.size() == 2) {
            footprint.width = 9000000;
            footprint.length = 4000000;
            for (std::size_t index = 0; index < 2; ++index) {
                const auto* net = graph.netForPin(symbol.id, pins[index].number);
                footprint.pads.push_back({pins[index].number,
                                          {index == 0 ? -3810000 : 3810000, 0},
                                          1800000,
                                          800000,
                                          net ? net->name : ""});
            }
        } else {
            const auto rows = (pins.size() + 1) / 2;
            footprint.width = 9000000;
            footprint.length =
                std::max<core::Coord>(5000000, static_cast<core::Coord>(rows) * 2540000);
            for (std::size_t index = 0; index < pins.size(); ++index) {
                const bool left = index < rows;
                const auto row = left ? index : pins.size() - 1 - index;
                const auto* net = graph.netForPin(symbol.id, pins[index].number);
                const core::Point offset{left ? -3810000 : 3810000,
                                         static_cast<core::Coord>(row) * 2540000 -
                                             static_cast<core::Coord>(rows - 1) * 1270000};
                footprint.pads.push_back(
                    {pins[index].number, offset, 1800000, 800000, net ? net->name : ""});
            }
        }
        footprints.push_back(std::move(footprint));
    }
    std::set<std::string> validNets;
    for (const auto& net : graph.nets)
        validNets.insert(net.name);
    std::erase_if(tracks, [&](const auto& track) { return !validNets.contains(track.net); });
    std::erase_if(vias, [&](const auto& via) { return !validNets.contains(via.net); });
    normalizeIds();
}

void Board::normalizeIds() {
    nextId_ = 1;
    for (const auto& track : tracks)
        nextId_ = std::max(nextId_, track.id + 1);
    for (const auto& via : vias)
        nextId_ = std::max(nextId_, via.id + 1);
}

std::uint64_t Board::addTrack(core::Point from, core::Point to, int layer, const std::string& net,
                              core::Coord traceWidth) {
    if (from == to || net.empty() || layer < 0 || layer > 31 || traceWidth <= 0)
        throw std::invalid_argument("Invalid copper track");
    const auto id = nextId_++;
    tracks.push_back({id, from, to, layer, traceWidth, net});
    return id;
}

std::uint64_t Board::addVia(core::Point position, const std::string& net) {
    if (net.empty())
        throw std::invalid_argument("Via requires a net");
    const auto id = nextId_++;
    vias.push_back({id, position, 900000, 400000, net});
    return id;
}

std::vector<Airwire> Board::airwires() const {
    struct Terminal {
        core::Point point;
        std::string net;
        std::uint32_t layerMask;
    };

    std::vector<Terminal> terminals;
    for (const auto& footprint : footprints)
        for (const auto& pad : footprint.pads)
            if (!pad.net.empty())
                terminals.push_back({padPosition(footprint, pad), pad.net, pad.layerMask});
    Union sets(terminals.size() + tracks.size() + vias.size());
    for (std::size_t first = 0; first < terminals.size(); ++first) {
        for (std::size_t second = 0; second < first; ++second)
            if (terminals[first].net == terminals[second].net &&
                terminals[first].point == terminals[second].point)
                sets.join(first, second);
        for (std::size_t track = 0; track < tracks.size(); ++track)
            if (terminals[first].net == tracks[track].net &&
                (terminals[first].layerMask & (1u << tracks[track].layer)) &&
                contains(terminals[first].point, tracks[track]))
                sets.join(first, terminals.size() + track);
        for (std::size_t via = 0; via < vias.size(); ++via)
            if (terminals[first].net == vias[via].net &&
                terminals[first].point == vias[via].position)
                sets.join(first, terminals.size() + tracks.size() + via);
    }
    for (std::size_t first = 0; first < tracks.size(); ++first) {
        for (std::size_t second = 0; second < first; ++second)
            if (tracks[first].net == tracks[second].net &&
                tracks[first].layer == tracks[second].layer &&
                (contains(tracks[first].from, tracks[second]) ||
                 contains(tracks[first].to, tracks[second]) ||
                 contains(tracks[second].from, tracks[first]) ||
                 contains(tracks[second].to, tracks[first]) ||
                 crosses(tracks[first], tracks[second])))
                sets.join(terminals.size() + first, terminals.size() + second);
        for (std::size_t via = 0; via < vias.size(); ++via)
            if (tracks[first].net == vias[via].net && contains(vias[via].position, tracks[first]))
                sets.join(terminals.size() + first, terminals.size() + tracks.size() + via);
    }
    std::vector<Airwire> result;
    // Link disconnected copper groups with their shortest remaining airwire, avoiding a noisy mesh.
    while (true) {
        double closest = std::numeric_limits<double>::max();
        std::size_t first = 0, second = 0;
        bool found = false;
        for (std::size_t a = 0; a < terminals.size(); ++a)
            for (std::size_t b = 0; b < a; ++b)
                if (terminals[a].net == terminals[b].net && sets.root(a) != sets.root(b)) {
                    const auto length = distance(terminals[a].point, terminals[b].point);
                    if (length < closest) {
                        closest = length;
                        first = a;
                        second = b;
                        found = true;
                    }
                }
        if (!found)
            break;
        result.push_back({terminals[first].point, terminals[second].point, terminals[first].net});
        sets.join(first, second);
    }
    return result;
}

std::vector<std::string> Board::checkRules() const {
    std::vector<std::string> issues;
    auto outside = [&](core::Point point, core::Coord radius) {
        return point.x - radius < 0 || point.y - radius < 0 || point.x + radius > width ||
               point.y + radius > height;
    };
    for (const auto& footprint : footprints)
        for (const auto& pad : footprint.pads)
            if (outside(padPosition(footprint, pad), pad.diameter / 2))
                issues.push_back("Pad fora da placa: " + footprint.reference + "." +
                                 std::to_string(pad.number));
    for (const auto& track : tracks) {
        if (outside(track.from, track.width / 2) || outside(track.to, track.width / 2))
            issues.push_back("Trilha fora da placa: #" + std::to_string(track.id));
        for (const auto& footprint : footprints)
            for (const auto& pad : footprint.pads)
                if (pad.net != track.net && pointDistance(padPosition(footprint, pad), track) <
                                                clearance + (pad.diameter + track.width) / 2)
                    issues.push_back("Distancia insuficiente entre trilha e pad: #" +
                                     std::to_string(track.id) + " / " + footprint.reference + "." +
                                     std::to_string(pad.number));
    }
    for (std::size_t first = 0; first < tracks.size(); ++first)
        for (std::size_t second = 0; second < first; ++second) {
            const auto& a = tracks[first];
            const auto& b = tracks[second];
            if (a.layer != b.layer || a.net == b.net)
                continue;
            const auto separation =
                crosses(a, b) ? 0
                              : std::min({pointDistance(a.from, b), pointDistance(a.to, b),
                                          pointDistance(b.from, a), pointDistance(b.to, a)});
            if (separation < clearance + (a.width + b.width) / 2)
                issues.push_back("Distancia insuficiente entre trilhas: #" + std::to_string(a.id) +
                                 " / #" + std::to_string(b.id));
        }
    for (const auto& via : vias)
        if (outside(via.position, via.diameter / 2))
            issues.push_back("Via fora da placa: #" + std::to_string(via.id));
    const auto remaining = airwires().size();
    if (remaining)
        issues.push_back(std::to_string(remaining) + " ligacao(oes) ainda sem trilha.");
    return issues;
}

std::string Board::exportObj() const {
    std::ostringstream output;
    output << "# LevelCAD PCB model in millimeters\n# Wenderson Dias | The Archlevel Foundation\n";
    std::size_t vertex = 1;
    auto box = [&](double x, double y, double z, double w, double l, double h,
                   const std::string& name, double rotation = 0) {
        output << "o " << name << '\n';
        for (const auto& point : std::vector<std::vector<double>>{{x, y, z},
                                                                  {x + w, y, z},
                                                                  {x + w, y + l, z},
                                                                  {x, y + l, z},
                                                                  {x, y, z + h},
                                                                  {x + w, y, z + h},
                                                                  {x + w, y + l, z + h},
                                                                  {x, y + l, z + h}}) {
            const auto angle = rotation * std::acos(-1.0) / 180.0;
            const auto dx = point[0] - x - w / 2, dy = point[1] - y - l / 2;
            output << "v " << x + w / 2 + dx * std::cos(angle) - dy * std::sin(angle) << ' '
                   << y + l / 2 + dx * std::sin(angle) + dy * std::cos(angle) << ' ' << point[2]
                   << '\n';
        }
        for (const auto& face : std::vector<std::vector<int>>{{0, 3, 2, 1},
                                                              {4, 5, 6, 7},
                                                              {0, 1, 5, 4},
                                                              {1, 2, 6, 5},
                                                              {2, 3, 7, 6},
                                                              {3, 0, 4, 7}}) {
            output << "f";
            for (auto index : face)
                output << ' ' << vertex + index;
            output << '\n';
        }
        vertex += 8;
    };
    box(0, 0, 0, double(width) / core::nmPerMm, double(height) / core::nmPerMm,
        double(thickness) / core::nmPerMm, "PCB");
    for (const auto& footprint : footprints) {
        if (!footprint.mesh.empty()) {
            output << "o " << footprint.reference << '\n';
            auto angle = footprint.rotation * std::acos(-1.0) / 180;
            for (const auto& triangle : footprint.mesh) {
                for (auto point : triangle) {
                    point.x += footprint.modelOffsetX;
                    point.y += footprint.modelOffsetY;
                    output << "v "
                           << double(footprint.position.x) / core::nmPerMm +
                                  point.x * std::cos(angle) - point.y * std::sin(angle)
                           << ' '
                           << double(footprint.position.y) / core::nmPerMm +
                                  point.x * std::sin(angle) + point.y * std::cos(angle)
                           << ' '
                           << double(thickness) / core::nmPerMm + point.z + footprint.modelOffsetZ
                           << '\n';
                }
                output << "f " << vertex << ' ' << vertex + 1 << ' ' << vertex + 2 << '\n';
                vertex += 3;
            }
            continue;
        }
        box(double(footprint.position.x - footprint.width / 2) / core::nmPerMm,
            double(footprint.position.y - footprint.length / 2) / core::nmPerMm,
            double(thickness) / core::nmPerMm, double(footprint.width) / core::nmPerMm,
            double(footprint.length) / core::nmPerMm, double(footprint.height) / core::nmPerMm,
            footprint.reference, footprint.rotation);
    }
    return output.str();
}
} // namespace levelcad::pcb
