#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QRegularExpression>
#include <QtEndian>
#include <bit>
#include <cmath>
#include <levelcad/app/MeshLibrary.hpp>
#include <levelcad/app/OfficialLibrary.hpp>
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
    static QMutex mutex;
    static QMap<QString, pcb::SharedMesh> cache;
    QMutexLocker lock(&mutex);
    if (!cache.contains(path)) {
        pcb::SharedMesh mesh;
        mesh = path.endsWith(".wrl", Qt::CaseInsensitive) ? loadOfficialVrml(path) : loadStl(path);
        if (cache.size() > 64)
            cache.clear();
        cache[path] = std::move(mesh);
    }
    footprint.mesh = cache[path];
    footprint.modelName = path.toStdString();
    footprint.modelOffsetX = footprint.modelOffsetY = footprint.modelOffsetZ = 0;
    footprint.modelRotationX = footprint.modelRotationY = footprint.modelRotationZ = 0;
    footprint.modelScale = 1;
}

void assignAutomaticModel(pcb::Footprint& footprint) {
    if (assignOfficialModel(footprint))
        return;
    auto package = QString::fromStdString(footprint.source).toLower();
    QString file;
    if (package.contains("axial"))
        file = "AXIAL-0_3.STL";
    else if (package.contains("r_1206"))
        file = "R1206.stl";
    else if (package.contains("r_0402"))
        file = "0402-RES.stl";
    else if (package.contains("r_0603"))
        file = "0603-RES.stl";
    else if (package.contains("c_0402"))
        file = "0402-CAP.stl";
    else if (package.contains("c_0603"))
        file = "0603-CAP.stl";
    else if (package.contains("c_0805"))
        file = "0805-CAP.stl";
    else if (package.contains("c_1206"))
        file = "1206-CAP.stl";
    else if (package.contains("soic-8"))
        file = "SOIC-8.stl";
    else if (package.contains("sot-223"))
        file = "SOT-223.STL";
    else if (package.contains("sot-23"))
        file = "SOT23-3.STL";
    else if (package.contains("tqfp-48"))
        file = "TQFP-48.STL";
    else if (package.contains("header_1x08"))
        file = "1X08_PTH.STL";
    if (!file.isEmpty() && footprint.mesh.empty())
        assignMesh(footprint, ":/models3d/" + file);
    auto ref = QString::fromStdString(footprint.reference);
    footprint.bodyColor = ref.startsWith("C")   ? "#c4a27c"
                          : ref.startsWith("D") ? "#516d5f"
                                                : "#303744";
    if (!footprint.texturePng.empty())
        return;
    // Texturas autorais: marcacao do componente e bandas; os STLs originais nao possuem UVs.
    QImage texture(256, 128, QImage::Format_ARGB32_Premultiplied);
    texture.fill(QColor(QString::fromStdString(footprint.bodyColor)));
    QPainter paint(&texture);
    if (ref.startsWith("R") && package.contains("axial")) {
        texture.fill(QColor("#b89468"));
        for (auto band : std::vector<std::pair<int, QColor>>{{45, QColor("#493026")},
                                                             {70, Qt::black},
                                                             {95, QColor("#b54235")},
                                                             {200, QColor("#cfad5c")}})
            paint.fillRect(band.first, 0, 14, 128, band.second);
    } else {
        paint.setPen(QColor("#ebedef"));
        paint.setFont(QFont("Segoe UI", 15, QFont::Bold));
        paint.drawText(texture.rect().adjusted(12, 12, -12, -12), Qt::AlignCenter,
                       QString::fromStdString(footprint.value));
        paint.setBrush(QColor("#dcdfe8"));
        paint.setPen(Qt::NoPen);
        paint.drawEllipse(QPoint(18, 18), 5, 5);
    }
    paint.end();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    texture.save(&buffer, "PNG");
    footprint.texturePng = bytes.toBase64().toStdString();
}
} // namespace levelcad::app
