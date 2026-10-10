#pragma once
#include <array>
#include <levelcad/core/Document.hpp>
#include <string>
#include <vector>

namespace levelcad::pcb {
struct Pad {
    int number{};
    core::Point offset;
    core::Coord diameter{1800000};
    core::Coord drill{800000};
    std::string net;
    std::string shape{"circle"};
    core::Coord sizeX{}, sizeY{};
    double rotation{};
    std::uint32_t layerMask{0xffffffffu};
};

struct MeshVertex {
    double x{}, y{}, z{};
};

using MeshTriangle = std::array<MeshVertex, 3>;

struct Footprint {
    std::uint64_t symbolId{};
    std::string reference, value, source;
    core::Point position;
    double rotation{};
    core::Coord width{8000000}, length{6000000}, height{4000000};
    std::vector<Pad> pads;
    std::string modelName;
    std::vector<MeshTriangle> mesh;
    double modelOffsetX{}, modelOffsetY{}, modelOffsetZ{};
};

struct Track {
    std::uint64_t id{};
    core::Point from, to;
    int layer{};
    core::Coord width{350000};
    std::string net;
};

struct Via {
    std::uint64_t id{};
    core::Point position;
    core::Coord diameter{900000}, drill{400000};
    std::string net;
};

struct Airwire {
    core::Point from, to;
    std::string net;
};

struct Layer {
    int index{};
    std::string name;
    std::string color;
    bool visible{true};
};

class Board {
  public:
    core::Coord width{100000000}, height{80000000}, thickness{1600000}, clearance{250000};
    std::vector<Footprint> footprints;
    std::vector<Track> tracks;
    std::vector<Via> vias;
    std::vector<Layer> layers{{0, "Top copper", "#ff766e", true},
                              {1, "Bottom copper", "#72aaff", true}};
    std::vector<std::vector<core::Point>> outline;
    void synchronize(const core::Document& document);
    std::uint64_t addTrack(core::Point from, core::Point to, int layer, const std::string& net,
                           core::Coord width);
    std::uint64_t addVia(core::Point position, const std::string& net);
    core::Point padPosition(const Footprint& footprint, const Pad& pad) const;
    std::vector<Airwire> airwires() const;
    std::vector<std::string> checkRules() const;
    std::string exportObj() const;
    void normalizeIds();

  private:
    std::uint64_t nextId_{1};
};
} // namespace levelcad::pcb
