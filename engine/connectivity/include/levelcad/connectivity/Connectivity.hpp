#pragma once
#include <levelcad/core/Document.hpp>
#include <optional>
#include <string>
#include <vector>

namespace levelcad::connectivity {
enum class VertexKind { Pin, Wire, Label };
enum class ContactKind {
    CoincidentPins,
    PinOnWire,
    WireJunction,
    GlobalGround,
    LabelContact,
    NamedNet
};
enum class Severity { Warning, Error };

struct Vertex {
    VertexKind kind;
    std::uint64_t objectId;
    std::optional<core::Pin> pin;
    std::size_t netIndex{};
};

struct Contact {
    std::size_t from{}, to{};
    ContactKind kind;
};

struct Diagnostic {
    Severity severity;
    std::string code;
    std::string message;
    std::uint64_t objectId{};
    int pinNumber{};
};

// Components link nets, but never merge their two terminals into one electrical node.
struct ComponentEdge {
    std::uint64_t symbolId{};
    std::size_t fromNet{}, toNet{};
};

struct Result {
    std::vector<Vertex> vertices;
    std::vector<Contact> contacts;
    std::vector<core::Net> nets;
    std::vector<ComponentEdge> components;
    std::vector<Diagnostic> diagnostics;
    const core::Net* netForPin(std::uint64_t symbolId, int pinNumber) const;
    bool hasErrors() const;
};

// Exact integer geometry: large nanometer coordinates must not create false contacts.
bool pointOnWire(core::Point point, const core::Wire& wire);
Result analyze(const core::Document& document);
} // namespace levelcad::connectivity
