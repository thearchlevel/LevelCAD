#include <algorithm>
#include <cmath>
#include <levelcad/connectivity/Connectivity.hpp>
#include <levelcad/pcb/Board.hpp>
#include <levelcad/pcb/Footprints.hpp>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace levelcad::pcb {
namespace {
struct Union {
    std::vector<std::size_t> parents;
    std::vector<unsigned> ranks;

    explicit Union(std::size_t size) : parents(size), ranks(size) {
        std::iota(parents.begin(), parents.end(), 0);
    }

    std::size_t root(std::size_t index) {
        return parents[index] == index ? index : parents[index] = root(parents[index]);
    }

    void join(std::size_t first, std::size_t second) {
        first = root(first);
        second = root(second);
        if (first == second)
            return;
        if (ranks[first] < ranks[second])
            std::swap(first, second);
        parents[second] = first;
        if (ranks[first] == ranks[second])
            ++ranks[first];
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

SyncReport Board::synchronize(const core::Document& document, SyncOptions options) {
    SyncReport report;
    const auto graph = connectivity::analyze(document);
    using PinKey = std::pair<std::uint64_t, int>;
    std::map<PinKey, std::string> pinNets;
    for (const auto& net : graph.nets)
        for (const auto& pin : net.pins)
            pinNets[{pin.symbolId, pin.number}] = net.name;
    std::unordered_map<std::uint64_t, std::size_t> previous;
    std::map<std::string, std::set<PinKey>> oldMembers, newMembers;
    for (std::size_t i = 0; i < footprints.size(); ++i) {
        previous[footprints[i].symbolId] = i;
        for (const auto& pad : footprints[i].pads)
            if (!pad.net.empty())
                oldMembers[pad.net].insert({footprints[i].symbolId, pad.number});
    }
    core::Coord minX = 0, minY = 0;
    bool first = true;
    for (const auto& symbol : document.symbols()) {
        if (symbol.kind == core::SymbolKind::Ground || symbol.libraryId.starts_with("power:"))
            continue;
        if (first) {
            minX = symbol.origin.x;
            minY = symbol.origin.y;
            first = false;
        }
        minX = std::min(minX, symbol.origin.x);
        minY = std::min(minY, symbol.origin.y);
    }
    std::vector<Footprint> updated;
    updated.reserve(document.symbols().size());
    for (const auto& symbol : document.symbols()) {
        if (symbol.kind == core::SymbolKind::Ground || symbol.libraryId.starts_with("power:") ||
            ((symbol.kind == core::SymbolKind::VoltageSource ||
              symbol.kind == core::SymbolKind::CurrentSource) &&
             symbol.footprint.empty())) {
            if (symbol.kind == core::SymbolKind::VoltageSource ||
                symbol.kind == core::SymbolKind::CurrentSource)
                report.warnings.push_back(symbol.reference +
                                          ": fonte virtual sem pads. Use 'Fonte com conector "
                                          "fisico' para levar a alimentacao a PCB.");
            continue;
        }
        auto found = previous.find(symbol.id);
        bool existing = found != previous.end();
        Footprint footprint;
        if (existing) {
            footprint = footprints[found->second];
            ++report.updated;
            previous.erase(found);
        } else {
            ++report.added;
        }
        if (!existing || footprint.pads.empty()) {
            if (auto mapped = options.resolveFootprint ? options.resolveFootprint(symbol)
                                                       : makeFootprint(symbol)) {
                auto position = footprint.position;
                footprint = std::move(*mapped);
                if (existing)
                    footprint.position = position;
            } else {
                ++report.unmapped;
                report.warnings.push_back(
                    symbol.reference +
                    ": escolha um footprint compativel; nenhum pad foi inventado.");
            }
        }
        footprint.symbolId = symbol.id;
        footprint.reference = symbol.reference;
        footprint.value = symbol.value;
        if (!existing || options.repositionExisting) {
            if (options.placementFromSchematic) {
                footprint.position = {15000000 + symbol.origin.x - minX,
                                      15000000 + symbol.origin.y - minY};
                footprint.rotation = symbol.rotation;
            } else {
                footprint.position = {
                    15000000 + static_cast<core::Coord>(updated.size() % 4) * 22000000,
                    15000000 + static_cast<core::Coord>(updated.size() / 4) * 16000000};
            }
        }
        if (!footprint.pads.empty() && !matchesPins(footprint, symbol)) {
            ++report.unmapped;
            report.warnings.push_back(symbol.reference +
                                      ": numeros dos pads nao correspondem aos pinos do simbolo.");
        }
        for (auto& pad : footprint.pads) {
            auto net = pinNets.find({symbol.id, pad.number});
            pad.net = net == pinNets.end() ? "" : net->second;
            if (!pad.net.empty())
                newMembers[pad.net].insert({symbol.id, pad.number});
        }
        updated.push_back(std::move(footprint));
    }
    report.removed = previous.size();
    // Identificadores de rede podem mudar; remapeie apenas grupos com os mesmos pinos fisicos.
    std::map<std::set<PinKey>, std::string> byMembers;
    for (const auto& [name, members] : newMembers)
        byMembers[members] = name;
    std::map<std::string, std::string> renamed;
    for (const auto& [name, members] : oldMembers)
        if (auto found = byMembers.find(members); found != byMembers.end())
            renamed[name] = found->second;
    std::set<std::string> stale;
    auto updateNet = [&](std::string& net) {
        if (auto found = renamed.find(net); found != renamed.end())
            net = found->second;
        else if (!newMembers.contains(net) ||
                 (oldMembers.contains(net) && oldMembers[net] != newMembers[net]))
            stale.insert(net);
    };
    for (auto& track : tracks)
        updateNet(track.net);
    for (auto& via : vias)
        updateNet(via.net);
    for (const auto& name : stale)
        report.warnings.push_back("Revise o cobre da rede alterada: " + name +
                                  ". As trilhas foram preservadas.");
    if (options.placementFromSchematic && (options.repositionExisting || footprints.empty()) &&
        !updated.empty()) {
        // Ajuste a distribuicao ao espaco util. As medidas fisicas dos encapsulamentos nao sao
        // escaladas.
        core::Coord minPX = updated.front().position.x, minPY = updated.front().position.y,
                    maxPX = minPX, maxPY = minPY;
        double envelope = 3000000;
        for (const auto& f : updated) {
            minPX = std::min(minPX, f.position.x);
            minPY = std::min(minPY, f.position.y);
            maxPX = std::max(maxPX, f.position.x);
            maxPY = std::max(maxPY, f.position.y);
            envelope =
                std::max(envelope, std::hypot(double(f.width), double(f.length)) / 2 + 2000000);
            for (const auto& pad : f.pads)
                envelope =
                    std::max(envelope, std::hypot(double(pad.offset.x), double(pad.offset.y)) +
                                           pad.diameter / 2 + 2000000);
        }
        const double availableX = std::max(0.0, double(width) - 2 * envelope),
                     availableY = std::max(0.0, double(height) - 2 * envelope);
        auto scale = std::min({1.0, maxPX > minPX ? availableX / (maxPX - minPX) : 1.0,
                               maxPY > minPY ? availableY / (maxPY - minPY) : 1.0});
        auto offsetX = (double(width) - (maxPX - minPX) * scale) / 2,
             offsetY = (double(height) - (maxPY - minPY) * scale) / 2;
        std::vector<std::array<double, 4>> occupied;
        for (auto& f : updated) {
            auto desired =
                core::Point{static_cast<core::Coord>(offsetX + (f.position.x - minPX) * scale),
                            static_cast<core::Coord>(offsetY + (f.position.y - minPY) * scale)};
            const auto radians = f.rotation * std::acos(-1.0) / 180;
            double halfX = (std::abs(std::cos(radians)) * f.width +
                            std::abs(std::sin(radians)) * f.length) /
                               2 +
                           clearance,
                   halfY = (std::abs(std::sin(radians)) * f.width +
                            std::abs(std::cos(radians)) * f.length) /
                               2 +
                           clearance;
            for (const auto& pad : f.pads) {
                auto pos = padPosition(f, pad);
                halfX = std::max(
                    halfX, double(std::abs(pos.x - f.position.x) + pad.diameter / 2 + clearance));
                halfY = std::max(
                    halfY, double(std::abs(pos.y - f.position.y) + pad.diameter / 2 + clearance));
            }
            bool placed = false;
            for (int attempt = 0; attempt < 512; ++attempt) {
                double radius = attempt == 0 ? 0 : 1500000 * std::sqrt(double(attempt));
                auto candidate = core::Point{
                    desired.x + static_cast<core::Coord>(radius * std::cos(attempt * 2.399963)),
                    desired.y + static_cast<core::Coord>(radius * std::sin(attempt * 2.399963))};
                if (candidate.x - halfX < 0 || candidate.x + halfX > width ||
                    candidate.y - halfY < 0 || candidate.y + halfY > height)
                    continue;
                bool overlap = false;
                for (const auto& box : occupied)
                    if (candidate.x + halfX > box[0] && candidate.x - halfX < box[2] &&
                        candidate.y + halfY > box[1] && candidate.y - halfY < box[3]) {
                        overlap = true;
                        break;
                    }
                if (overlap)
                    continue;
                f.position = candidate;
                occupied.push_back({candidate.x - halfX, candidate.y - halfY, candidate.x + halfX,
                                    candidate.y + halfY});
                placed = true;
                break;
            }
            if (!placed) {
                f.position = desired;
                report.warnings.push_back("Espaco insuficiente para " + f.reference +
                                          "; aumente a placa ou reposicione o componente.");
            }
        }
        if (options.repositionExisting && !tracks.empty())
            report.warnings.push_back(
                "Componentes reposicionados: revise as trilhas existentes e execute DRC.");
    }
    footprints = std::move(updated);
    if (options.autoSize && !footprints.empty() && outline.empty()) {
        // Grow the rectangular board around physical bodies, pads and existing copper.
        // Translate the whole layout together if a new component extends beyond the origin.
        constexpr core::Coord margin = 5000000;
        core::Coord left = 0, top = 0, right = width - margin, bottom = height - margin;
        auto include = [&](core::Point point, core::Coord rx, core::Coord ry) {
            left = std::min(left, point.x - rx - margin);
            top = std::min(top, point.y - ry - margin);
            right = std::max(right, point.x + rx);
            bottom = std::max(bottom, point.y + ry);
        };
        for (const auto& footprint : footprints) {
            const auto angle = footprint.rotation * std::acos(-1.0) / 180;
            include(footprint.position,
                    static_cast<core::Coord>((std::abs(std::cos(angle)) * footprint.width +
                                              std::abs(std::sin(angle)) * footprint.length) /
                                             2),
                    static_cast<core::Coord>((std::abs(std::sin(angle)) * footprint.width +
                                              std::abs(std::cos(angle)) * footprint.length) /
                                             2));
            for (const auto& pad : footprint.pads)
                include(padPosition(footprint, pad), std::max(pad.diameter, pad.sizeX) / 2,
                        std::max(pad.diameter, pad.sizeY) / 2);
        }
        for (const auto& track : tracks) {
            include(track.from, track.width / 2, track.width / 2);
            include(track.to, track.width / 2, track.width / 2);
        }
        for (const auto& via : vias)
            include(via.position, via.diameter / 2, via.diameter / 2);
        if (left || top) {
            for (auto& footprint : footprints) {
                footprint.position.x -= left;
                footprint.position.y -= top;
            }
            for (auto& track : tracks) {
                track.from.x -= left;
                track.to.x -= left;
                track.from.y -= top;
                track.to.y -= top;
            }
            for (auto& via : vias) {
                via.position.x -= left;
                via.position.y -= top;
            }
        }
        width = std::max(width, right - left + margin);
        height = std::max(height, bottom - top + margin);
    }
    normalizeIds();
    return report;
}

std::vector<std::string> Board::schematicIssues(const core::Document& document) const {
    const auto graph = connectivity::analyze(document);
    std::map<std::pair<std::uint64_t, int>, std::string> expected;
    for (const auto& net : graph.nets)
        for (const auto& pin : net.pins)
            expected[{pin.symbolId, pin.number}] = net.name;
    std::vector<std::string> issues;
    std::set<std::uint64_t> required;
    for (const auto& symbol : document.symbols()) {
        if (symbol.kind == core::SymbolKind::Ground || symbol.libraryId.starts_with("power:"))
            continue;
        if ((symbol.kind == core::SymbolKind::VoltageSource ||
             symbol.kind == core::SymbolKind::CurrentSource) &&
            symbol.footprint.empty()) {
            issues.push_back(symbol.reference + ": fonte virtual precisa de um conector fisico.");
            continue;
        }
        required.insert(symbol.id);
        auto footprint = std::find_if(footprints.begin(), footprints.end(),
                                      [&](const auto& f) { return f.symbolId == symbol.id; });
        if (footprint == footprints.end()) {
            issues.push_back(symbol.reference + ": componente ausente na PCB.");
            continue;
        }
        std::set<int> pins, pads;
        for (const auto& pin : core::symbolPins(symbol))
            pins.insert(pin.number);
        for (const auto& pad : footprint->pads) {
            if (!pads.insert(pad.number).second)
                issues.push_back(symbol.reference + ": pad duplicado.");
            auto net = expected.find({symbol.id, pad.number});
            if (net == expected.end() || net->second != pad.net)
                issues.push_back(symbol.reference + "." + std::to_string(pad.number) +
                                 ": rede diferente do esquematico.");
        }
        if (pins != pads || pads.empty())
            issues.push_back(symbol.reference + ": pinos e pads incompativeis.");
        if (footprint->value != symbol.value)
            issues.push_back(symbol.reference + ": valor desatualizado.");
    }
    for (const auto& footprint : footprints)
        if (!required.contains(footprint.symbolId))
            issues.push_back(footprint.reference + ": componente sem origem no esquematico.");
    return issues;
}

bool Board::fitOutline(core::Coord margin) {
    if (footprints.empty() || !outline.empty() || margin < clearance)
        return false;
    core::Coord minX = std::numeric_limits<core::Coord>::max(), minY = minX, maxX = -minX,
                maxY = -minX;
    auto include = [&](core::Point p, core::Coord rx, core::Coord ry) {
        minX = std::min(minX, p.x - rx);
        minY = std::min(minY, p.y - ry);
        maxX = std::max(maxX, p.x + rx);
        maxY = std::max(maxY, p.y + ry);
    };
    for (const auto& f : footprints) {
        auto angle = f.rotation * std::acos(-1.0) / 180;
        include(
            f.position,
            static_cast<core::Coord>(
                (std::abs(std::cos(angle)) * f.width + std::abs(std::sin(angle)) * f.length) / 2),
            static_cast<core::Coord>(
                (std::abs(std::sin(angle)) * f.width + std::abs(std::cos(angle)) * f.length) / 2));
        for (const auto& pad : f.pads)
            include(padPosition(f, pad), std::max(pad.sizeX, pad.diameter) / 2,
                    std::max(pad.sizeY, pad.diameter) / 2);
    }
    for (const auto& track : tracks) {
        include(track.from, track.width / 2, track.width / 2);
        include(track.to, track.width / 2, track.width / 2);
    }
    for (const auto& via : vias)
        include(via.position, via.diameter / 2, via.diameter / 2);
    auto dx = margin - minX, dy = margin - minY;
    for (auto& f : footprints) {
        f.position.x += dx;
        f.position.y += dy;
    }
    for (auto& t : tracks) {
        t.from.x += dx;
        t.to.x += dx;
        t.from.y += dy;
        t.to.y += dy;
    }
    for (auto& v : vias) {
        v.position.x += dx;
        v.position.y += dy;
    }
    width = std::max<core::Coord>(10000000, maxX - minX + 2 * margin);
    height = std::max<core::Coord>(10000000, maxY - minY + 2 * margin);
    return true;
}

std::size_t Board::routePending(int layer) {
    if (std::none_of(layers.begin(), layers.end(), [&](const auto& l) { return l.index == layer; }))
        throw std::invalid_argument("Unknown routing layer");
    auto pending = airwires();
    std::sort(pending.begin(), pending.end(), [](const auto& a, const auto& b) {
        return distance(a.from, a.to) < distance(b.from, b.to);
    });
    std::size_t routed = 0;
    auto available = [&](core::Point p, const std::string& net) {
        for (const auto& f : footprints)
            for (const auto& pad : f.pads)
                if (pad.net == net && padPosition(f, pad) == p && (pad.layerMask & (1u << layer)))
                    return true;
        return false;
    };
    for (const auto& connection : pending) {
        if (!available(connection.from, connection.net) ||
            !available(connection.to, connection.net))
            continue;
        const auto a = connection.from, b = connection.to;
        std::vector<std::vector<core::Point>> candidates{{a, {b.x, a.y}, b}, {a, {a.x, b.y}, b}};
        for (int step = -8; step <= 8; ++step) {
            auto x = (a.x + b.x) / 2 + step * 1000000, y = (a.y + b.y) / 2 + step * 1000000;
            candidates.push_back({a, {x, a.y}, {x, b.y}, b});
            candidates.push_back({a, {a.x, y}, {b.x, y}, b});
        }
        for (const auto& path : candidates) {
            auto proposed = *this;
            bool valid = true;
            for (std::size_t i = 1; i < path.size(); ++i)
                if (path[i - 1] != path[i]) {
                    Track track{0, path[i - 1], path[i], layer, 350000, connection.net};
                    if (!proposed.canPlaceTrack(track)) {
                        valid = false;
                        break;
                    }
                    proposed.addTrack(track.from, track.to, layer, track.net, track.width);
                }
            if (valid) {
                *this = std::move(proposed);
                ++routed;
                break;
            }
        }
    }
    return routed;
}

MeshVertex modelVertex(const Footprint& footprint, MeshVertex vertex) {
    const auto pi = std::acos(-1.0) / 180;
    vertex.x *= footprint.modelScale;
    vertex.y *= footprint.modelScale;
    vertex.z *= footprint.modelScale;
    auto rotate = [&](double& a, double& b, double angle) {
        auto x = a * std::cos(angle * pi) - b * std::sin(angle * pi);
        b = a * std::sin(angle * pi) + b * std::cos(angle * pi);
        a = x;
    };
    rotate(vertex.y, vertex.z, footprint.modelRotationX);
    rotate(vertex.z, vertex.x, footprint.modelRotationY);
    rotate(vertex.x, vertex.y, footprint.modelRotationZ);
    vertex.x += footprint.modelOffsetX;
    vertex.y += footprint.modelOffsetY;
    vertex.z += footprint.modelOffsetZ;
    return vertex;
}

bool Board::canPlaceTrack(const Track& track) const {
    if (track.from == track.to || track.width <= 0 || track.net.empty() ||
        std::none_of(layers.begin(), layers.end(),
                     [&](const auto& layer) { return layer.index == track.layer; }))
        return false;
    auto inside = [&](core::Point p) {
        auto margin = track.width / 2 + clearance;
        return p.x >= margin && p.y >= margin && p.x <= width - margin && p.y <= height - margin;
    };
    if (!inside(track.from) || !inside(track.to))
        return false;
    for (const auto& footprint : footprints)
        for (const auto& pad : footprint.pads)
            if (pad.net != track.net && (pad.layerMask & (1u << track.layer)) &&
                pointDistance(padPosition(footprint, pad), track) <
                    clearance + (pad.diameter + track.width) / 2)
                return false;
    for (const auto& existing : tracks) {
        if (existing.layer != track.layer || existing.net == track.net)
            continue;
        auto separation =
            crosses(existing, track)
                ? 0
                : std::min({pointDistance(track.from, existing), pointDistance(track.to, existing),
                            pointDistance(existing.from, track),
                            pointDistance(existing.to, track)});
        if (separation < clearance + (existing.width + track.width) / 2)
            return false;
    }
    for (const auto& via : vias)
        if (via.net != track.net &&
            pointDistance(via.position, track) < clearance + (via.diameter + track.width) / 2)
            return false;
    return true;
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
                terminals[first].point == terminals[second].point &&
                (terminals[first].layerMask & terminals[second].layerMask))
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
    // Boruvka por rede: encontre uma aresta por grupo, sem repetir a busca global para cada
    // ligacao.
    std::map<std::string, std::vector<std::size_t>> byNet;
    for (std::size_t i = 0; i < terminals.size(); ++i)
        byNet[terminals[i].net].push_back(i);
    for (const auto& [net, indices] : byNet) {
        while (true) {
            struct Edge {
                double length;
                std::size_t a, b;
            };

            std::map<std::size_t, Edge> closest;
            for (std::size_t a = 0; a < indices.size(); ++a)
                for (std::size_t b = 0; b < a; ++b) {
                    auto i = indices[a], j = indices[b], ri = sets.root(i), rj = sets.root(j);
                    if (ri == rj)
                        continue;
                    Edge edge{distance(terminals[i].point, terminals[j].point), i, j};
                    for (auto root : {ri, rj}) {
                        auto old = closest.find(root);
                        if (old == closest.end() || edge.length < old->second.length)
                            closest[root] = edge;
                    }
                }
            if (closest.empty())
                break;
            bool joined = false;
            for (const auto& [root, edge] : closest) {
                if (sets.root(edge.a) == sets.root(edge.b))
                    continue;
                result.push_back({terminals[edge.a].point, terminals[edge.b].point, net});
                sets.join(edge.a, edge.b);
                joined = true;
            }
            if (!joined)
                break;
        }
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
                if ((pad.layerMask & (1u << track.layer)) && pad.net != track.net &&
                    pointDistance(padPosition(footprint, pad), track) <
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
                    point = modelVertex(footprint, point);
                    output << "v "
                           << double(footprint.position.x) / core::nmPerMm +
                                  point.x * std::cos(angle) - point.y * std::sin(angle)
                           << ' '
                           << double(footprint.position.y) / core::nmPerMm +
                                  point.x * std::sin(angle) + point.y * std::cos(angle)
                           << ' ' << double(thickness) / core::nmPerMm + point.z << '\n';
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
