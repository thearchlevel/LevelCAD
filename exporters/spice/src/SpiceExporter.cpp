#include <algorithm>
#include <cctype>
#include <cmath>
#include <levelcad/exporters/SpiceExporter.hpp>
#include <regex>
#include <set>
#include <sstream>

namespace levelcad::exporters {
namespace {
bool identifier(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char letter) {
        return std::isalnum(letter) || letter == '_';
    });
}

std::string lower(std::string value) {
    for (auto& letter : value)
        letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    return value;
}

std::string scalar(const std::string& input, bool positiveOnly) {
    auto value = input;
    for (const auto& [symbol, replacement] :
         std::vector<std::pair<std::string, std::string>>{{"µ", "u"}, {"μ", "u"}, {"Ω", "ohm"}}) {
        std::size_t position = 0;
        while ((position = value.find(symbol, position)) != std::string::npos) {
            value.replace(position, symbol.size(), replacement);
            position += replacement.size();
        }
    }
    // Accept common editor values (5V, 1mA, 100nF), but never arbitrary SPICE directives.
    static const std::regex pattern(
        R"(^([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)(meg|[tgkmunpf])?(ohm|[vahf])?$)",
        std::regex::icase);
    std::smatch match;
    if (!std::regex_match(value, match, pattern))
        return {};
    try {
        const double number = std::stod(match[1].str());
        if (!std::isfinite(number) || (positiveOnly && number <= 0))
            return {};
    } catch (...) {
        return {};
    }
    return match[1].str() + match[2].str();
}

char prefix(core::SymbolKind kind) {
    switch (kind) {
    case core::SymbolKind::Resistor:
        return 'R';
    case core::SymbolKind::Capacitor:
        return 'C';
    case core::SymbolKind::Inductor:
        return 'L';
    case core::SymbolKind::Diode:
    case core::SymbolKind::LED:
        return 'D';
    case core::SymbolKind::VoltageSource:
        return 'V';
    case core::SymbolKind::CurrentSource:
        return 'I';
    default:
        return '\0';
    }
}
} // namespace

SpiceResult exportSpice(const core::Document& document, const SpiceOptions& options) {
    const auto graph = connectivity::analyze(document);
    SpiceResult result;
    result.diagnostics = graph.diagnostics;
    auto error = [&](const std::string& code, const std::string& message, std::uint64_t id = 0) {
        result.diagnostics.push_back({connectivity::Severity::Error, code, message, id, 0});
    };
    if (document.symbols().empty())
        error("spice-empty", "Não há componentes para exportar.");
    if (std::none_of(graph.nets.begin(), graph.nets.end(),
                     [](const auto& net) { return net.name == "0"; })) {
        error("spice-ground", "Adicione um símbolo GND: o SPICE precisa do nó de referência 0.");
    }
    if (options.title.find_first_of("\r\n") != std::string::npos)
        error("spice-title", "O título deve ocupar uma única linha.");
    if (options.modelLibrary.find_first_of("\r\n\"") != std::string::npos)
        error("spice-library", "O caminho dos modelos contém caracteres inválidos.");
    std::ostringstream deck;
    deck << (options.title.empty() ? "LevelCAD circuit" : options.title) << '\n';
    deck << "* Credits: Wenderson Dias\n* Published by The Archlevel Foundation\n";
    if (!options.modelLibrary.empty())
        deck << ".include \"" << options.modelLibrary << "\"\n";
    std::set<std::string> usedReferences;
    auto symbols = document.symbols();
    std::sort(symbols.begin(), symbols.end(),
              [](const auto& first, const auto& second) { return first.id < second.id; });
    for (const auto& symbol : symbols) {
        if (symbol.kind == core::SymbolKind::Ground)
            continue;
        if (symbol.kind == core::SymbolKind::Connector) {
            // A physical connector does not electrically bridge its independent pins.
            deck << "* Connector #" << symbol.id
                 << ": independent terminals; no simulated device\n";
            result.diagnostics.push_back(
                {connectivity::Severity::Warning, "spice-connector",
                 "Conector físico omitido da simulação: " + symbol.reference, symbol.id, 0});
            continue;
        }
        const auto type = symbol.kind == core::SymbolKind::Library && !symbol.spiceType.empty()
                              ? symbol.spiceType[0]
                              : prefix(symbol.kind);
        if (!type || std::string("RCLDVIQ").find(type) == std::string::npos) {
            error("spice-unsupported",
                  "Componente ainda sem modelo SPICE: " + symbol.reference +
                      ". Substitua a chave por um circuito equivalente.",
                  symbol.id);
            continue;
        }
        if (!identifier(symbol.reference)) {
            error("spice-reference",
                  "Use letras, números e underscore na referência: " + symbol.reference, symbol.id);
            continue;
        }
        // SPICE identifies device type by the first character, even after the user renames it.
        auto reference = symbol.reference;
        if (std::toupper(static_cast<unsigned char>(reference[0])) != type)
            reference = std::string(1, type) + "_" + reference;
        if (!usedReferences.insert(lower(reference)).second) {
            error("spice-duplicate", "Referências SPICE repetidas: " + reference, symbol.id);
            continue;
        }
        const auto order = symbol.kind == core::SymbolKind::Library ? symbol.spicePinOrder
                                                                    : std::vector<int>{1, 2};
        const std::size_t expected = type == 'Q' ? 3 : 2;
        if (order.size() != expected) {
            error("spice-pin-map", "Mapeamento SPICE incompleto: " + symbol.reference, symbol.id);
            continue;
        }
        std::vector<const core::Net*> nodes;
        for (const auto number : order)
            nodes.push_back(graph.netForPin(symbol.id, number));
        if (std::any_of(nodes.begin(), nodes.end(),
                        [](const auto* node) { return node == nullptr; })) {
            error("spice-pin", "Não foi possível resolver os terminais: " + symbol.reference,
                  symbol.id);
            continue;
        }
        std::string value;
        if (type == 'D' || type == 'Q') {
            if (!identifier(symbol.value) || options.modelLibrary.empty()) {
                error("spice-model",
                      "Defina o nome do modelo e selecione um arquivo .model para " +
                          symbol.reference + ".",
                      symbol.id);
                continue;
            }
            value = symbol.value;
        } else {
            value = scalar(symbol.value, type == 'R' || type == 'C' || type == 'L');
            if (value.empty()) {
                error("spice-value",
                      "Valor SPICE inválido: " + symbol.reference + " = " + symbol.value +
                          ". Exemplos: 10k, 100n, 5V, 1mA; use meg para mega.",
                      symbol.id);
                continue;
            }
        }
        deck << reference;
        for (const auto* node : nodes)
            deck << ' ' << node->name;
        deck << ' ';
        if (type == 'V' || type == 'I')
            deck << "DC ";
        deck << value << '\n';
    }
    // Pin 1 is positive for sources and the anode for diodes; pin 2 is negative/cathode.
    deck << ".op\n.end\n";
    if (std::none_of(result.diagnostics.begin(), result.diagnostics.end(),
                     [](const auto& diagnostic) {
                         return diagnostic.severity == connectivity::Severity::Error;
                     }))
        result.text = deck.str();
    return result;
}
} // namespace levelcad::exporters
