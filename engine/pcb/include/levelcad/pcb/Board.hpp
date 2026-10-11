#pragma once
#include <array>
#include <functional>
#include <levelcad/core/Document.hpp>
#include <memory>
#include <optional>
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
    double cornerRatio{0.25};
};

struct MeshVertex {
    double x{}, y{}, z{};
    std::uint32_t color{};
};

using MeshTriangle = std::array<MeshVertex, 3>;

// Copias de historico e tarefas de sincronizacao compartilham a malha imutavel.
// Edicoes da malha fazem uma copia apenas quando necessario.
class SharedMesh {
  public:
    SharedMesh() = default;

    SharedMesh& operator=(std::vector<MeshTriangle> value) {
        data_ = std::make_shared<std::vector<MeshTriangle>>(std::move(value));
        return *this;
    }

    bool empty() const {
        return !data_ || data_->empty();
    }

    std::size_t size() const {
        return data_ ? data_->size() : 0;
    }

    auto begin() const {
        return storage().begin();
    }

    auto end() const {
        return storage().end();
    }

    const MeshTriangle& operator[](std::size_t i) const {
        return storage()[i];
    }

    void clear() {
        data_.reset();
    }

    void push_back(const MeshTriangle& triangle) {
        if (!data_)
            data_ = std::make_shared<std::vector<MeshTriangle>>();
        else if (data_.use_count() > 1)
            data_ = std::make_shared<std::vector<MeshTriangle>>(*data_);
        data_->push_back(triangle);
    }

  private:
    const std::vector<MeshTriangle>& storage() const {
        static const std::vector<MeshTriangle> empty;
        return data_ ? *data_ : empty;
    }

    std::shared_ptr<std::vector<MeshTriangle>> data_;
};

struct Footprint {
    std::uint64_t symbolId{};
    std::string reference, value, source;
    core::Point position;
    double rotation{};
    core::Coord width{8000000}, length{6000000}, height{4000000};
    std::vector<Pad> pads;
    std::string modelName;
    SharedMesh mesh;
    double modelOffsetX{}, modelOffsetY{}, modelOffsetZ{};
    double modelRotationX{}, modelRotationY{}, modelRotationZ{}, modelScale{1};
    std::string bodyColor{"#343a46"};
    std::string texturePng; // PNG incorporado em base64, sem dependencias de caminhos locais.
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

struct SyncOptions {
    bool placementFromSchematic{true};
    bool repositionExisting{false};
    bool autoSize{true};
    std::function<std::optional<Footprint>(const core::Symbol&)> resolveFootprint;
};

struct SyncReport {
    std::size_t added{}, updated{}, removed{}, unmapped{};
    std::vector<std::string> warnings;
};

MeshVertex modelVertex(const Footprint& footprint, MeshVertex vertex);

class Board {
  public:
    core::Coord width{100000000}, height{80000000}, thickness{1600000}, clearance{250000};
    std::vector<Footprint> footprints;
    std::vector<Track> tracks;
    std::vector<Via> vias;
    std::vector<Layer> layers{{0, "Top copper", "#ff766e", true},
                              {1, "Bottom copper", "#72aaff", true}};
    std::vector<std::vector<core::Point>> outline;
    std::string projectName{"Untitled"}, revision{"1.0"}, creator;
    std::string solderMask{"#174b3c"}, silkColor{"#e9eef2"}, finishColor{"#d8b767"};
    bool showAirwires{true}, showReferences{true}, showPadNumbers{true};
    SyncReport synchronize(const core::Document& document, SyncOptions options = {});
    bool canPlaceTrack(const Track& track) const;
    std::vector<std::string> schematicIssues(const core::Document& document) const;
    std::size_t routePending(int layer);
    bool fitOutline(core::Coord margin = 5000000);
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
