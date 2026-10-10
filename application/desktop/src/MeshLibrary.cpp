#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtEndian>
#include <bit>
#include <cmath>
#include <levelcad/app/MeshLibrary.hpp>
#include <limits>
#include <stdexcept>

namespace levelcad::app {
QStringList bundledModels() {
    QFile file(":/models3d/manifest.json");
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QStringList result;
    for (const auto& value : QJsonDocument::fromJson(file.readAll()).array())
        result.append(value.toObject()["file"].toString());
    return result;
}

std::vector<pcb::MeshTriangle> loadStl(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 50 * 1024 * 1024)
        throw std::runtime_error("Cannot read STL model (limit: 50 MB)");
    const auto bytes = file.readAll();
    std::vector<pcb::MeshTriangle> triangles;
    auto scalar = [&](qsizetype offset) {
        return std::bit_cast<float>(
            qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset)));
    };
    if (bytes.size() >= 84 &&
        84ULL + 50ULL * qFromLittleEndian<quint32>(
                            reinterpret_cast<const uchar*>(bytes.constData() + 80)) ==
            static_cast<quint64>(bytes.size())) {
        for (qsizetype offset = 84; offset < bytes.size(); offset += 50) {
            pcb::MeshTriangle triangle;
            for (int vertex = 0; vertex < 3; ++vertex)
                triangle[vertex] = {scalar(offset + 12 + vertex * 12),
                                    scalar(offset + 16 + vertex * 12),
                                    scalar(offset + 20 + vertex * 12)};
            triangles.push_back(triangle);
        }
    } else {
        const auto text = QString::fromUtf8(bytes);
        QRegularExpression vertex(R"(vertex\s+([-+\d.eE]+)\s+([-+\d.eE]+)\s+([-+\d.eE]+))");
        auto matches = vertex.globalMatch(text);
        pcb::MeshTriangle triangle;
        int index = 0;
        while (matches.hasNext()) {
            auto match = matches.next();
            bool x = false, y = false, z = false;
            triangle[index++] = {match.captured(1).toDouble(&x), match.captured(2).toDouble(&y),
                                 match.captured(3).toDouble(&z)};
            if (!x || !y || !z)
                throw std::runtime_error("Invalid ASCII STL vertex");
            if (index == 3) {
                triangles.push_back(triangle);
                index = 0;
            }
        }
        if (index != 0)
            throw std::runtime_error("Incomplete STL triangle");
    }
    if (triangles.empty() || triangles.size() > 200000)
        throw std::runtime_error("Invalid or overly complex STL mesh");
    double minX = std::numeric_limits<double>::max(), minY = minX, maxX = -minX, maxY = -minX;
    for (const auto& triangle : triangles)
        for (const auto& vertex : triangle) {
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z) ||
                std::abs(vertex.x) > 1000 || std::abs(vertex.y) > 1000 || std::abs(vertex.z) > 1000)
                throw std::runtime_error("Invalid STL coordinates");
            minX = std::min(minX, vertex.x);
            maxX = std::max(maxX, vertex.x);
            minY = std::min(minY, vertex.y);
            maxY = std::max(maxY, vertex.y);
        }
    // Center X/Y without changing units or Z. The board plane remains the author's original plane.
    for (auto& triangle : triangles)
        for (auto& vertex : triangle) {
            vertex.x -= (minX + maxX) / 2;
            vertex.y -= (minY + maxY) / 2;
        }
    return triangles;
}

void assignMesh(pcb::Footprint& footprint, const QString& path) {
    auto mesh = loadStl(path);
    footprint.mesh = std::move(mesh);
    footprint.modelName = path.toStdString();
    footprint.modelOffsetX = footprint.modelOffsetY = footprint.modelOffsetZ = 0;
}
} // namespace levelcad::app
