#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace levelcad::core {
using Coord = std::int64_t;
constexpr Coord nmPerMm = 1'000'000;

struct Point {
    Coord x{}, y{};
    bool operator==(const Point&) const = default;
};
enum class SymbolKind {
    Resistor,
    Capacitor,
    Ground,
    Inductor,
    Diode,
    LED,
    VoltageSource,
    CurrentSource,
    Switch,
    Connector,
    Library
};

struct PinDefinition {
    int number{};
    std::string name;
    std::string electricalType;
    Point position;
};

struct Symbol {
    std::uint64_t id{};
    SymbolKind kind{SymbolKind::Resistor};
    Point origin{};
    int rotation{};
    std::string reference, value;
    std::string libraryId, footprint, datasheet;
    std::vector<PinDefinition> pinDefinitions;
    std::vector<std::vector<Point>> strokes;
    std::string spiceType;
    std::vector<int> spicePinOrder;
};

struct Wire {
    std::uint64_t id{};
    Point from{}, to{};
};

struct Note {
    std::uint64_t id{};
    Point position;
    std::string text, author;
    bool electrical{};
    // Areas are visual annotations; they never change electrical connectivity.
    bool background{};
    double widthMm{40.0}, heightMm{30.0};
    std::string color{"#4b8096"};
};

struct Pin {
    std::uint64_t symbolId{};
    int number{};
    Point position{};
};

struct Net {
    std::string name;
    std::vector<Pin> pins;
    std::vector<std::uint64_t> wires;
};

std::string kindName(SymbolKind kind);
std::vector<Pin> symbolPins(const Symbol& symbol);

class Document {
  public:
    std::uint64_t addSymbol(SymbolKind kind, Point origin);
    std::uint64_t addLibrarySymbol(const Symbol& prototype, Point origin);
    std::uint64_t addWire(Point from, Point to);
    bool updateSymbol(const Symbol& symbol);
    bool moveObject(std::uint64_t id, Point delta);
    bool moveConnection(std::uint64_t wireId, Point delta);
    std::vector<std::uint64_t> connectionWires(std::uint64_t wireId) const;
    bool removeObject(std::uint64_t id);
    std::uint64_t addNote(Point position, std::string text, std::string author,
                          bool electrical = false);
    bool updateNote(const Note& note);

    const std::vector<Note>& notes() const {
        return notes_;
    }

    void setIdFloor(std::uint64_t floor) {
        idFloor_ = floor;
        nextId_ = std::max(nextId_, floor);
    }

    void clear();
    void load(std::vector<Symbol> symbols, std::vector<Wire> wires, std::vector<Note> notes = {});
    std::vector<Net> nets() const;
    std::vector<std::string> checkElectrical() const;

    const std::vector<Symbol>& symbols() const noexcept {
        return symbols_;
    }

    const std::vector<Wire>& wires() const noexcept {
        return wires_;
    }

  private:
    std::vector<Symbol> symbols_;
    std::vector<Wire> wires_;
    std::vector<Note> notes_;
    std::uint64_t idFloor_{1};
    std::uint64_t nextId_{1};
};
} // namespace levelcad::core
