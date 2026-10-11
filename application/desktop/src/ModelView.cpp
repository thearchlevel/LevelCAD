#include <QFileDialog>
#include <QImage>
#include <QInputDialog>
#include <QMap>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QSaveFile>
#include <QTransform>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <levelcad/app/ExportDialog.hpp>
#include <levelcad/app/MeshLibrary.hpp>
#include <levelcad/app/ModelView.hpp>
#include <levelcad/app/OfficialLibrary.hpp>
#include <levelcad/pcb/Footprints.hpp>
#include <limits>

namespace levelcad::app {
namespace {
struct Vector {
    double x, y, z;
};

struct Face {
    QPolygonF polygon;
    QColor color;
    double depth;
    QImage texture;
    QPolygonF uv;
    std::vector<double> depths;
};

constexpr double mm = core::nmPerMm;
} // namespace

ModelView::ModelView(Mode mode, pcb::Board* board, core::Document* document, QWidget* parent)
    : QWidget(parent), mode_(mode), board_(board), document_(document) {
    setMinimumSize(500, 350);
    setMouseTracking(true);
}

void ModelView::resetCamera() {
    yaw_ = -0.55;
    pitch_ = 0.9;
    auto extent = std::max(double(board_->width), double(board_->height)) / mm;
    if (mode_ == Mode::Schematic && !document_->symbols().empty()) {
        core::Coord minX = document_->symbols().front().origin.x, maxX = minX;
        core::Coord minY = document_->symbols().front().origin.y, maxY = minY;
        for (const auto& s : document_->symbols()) {
            minX = std::min(minX, s.origin.x);
            maxX = std::max(maxX, s.origin.x);
            minY = std::min(minY, s.origin.y);
            maxY = std::max(maxY, s.origin.y);
        }
        extent = std::max(double(maxX - minX), double(maxY - minY)) / mm + 20;
    }
    zoom_ = std::clamp(std::min(width(), height()) * .65 / std::max(10.0, extent), .3, 40.0);
    pan_ = {};
    update();
}

void ModelView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#121c2a"));
    double centerX = double(board_->width) / mm / 2, centerY = double(board_->height) / mm / 2;
    if (mode_ == Mode::Schematic && !document_->symbols().empty()) {
        centerX = 0;
        centerY = 0;
        for (const auto& symbol : document_->symbols()) {
            centerX += double(symbol.origin.x) / mm;
            centerY += double(symbol.origin.y) / mm;
        }
        centerX /= document_->symbols().size();
        centerY /= document_->symbols().size();
    }
    auto transform = [&](Vector v) {
        v.x -= centerX;
        v.y -= centerY;
        const auto x = v.x * std::cos(yaw_) - v.y * std::sin(yaw_),
                   y = v.x * std::sin(yaw_) + v.y * std::cos(yaw_);
        return Vector{x, y * std::cos(pitch_) - v.z * std::sin(pitch_),
                      -(y * std::sin(pitch_) + v.z * std::cos(pitch_))};
    };
    auto project = [&](Vector v) {
        v = transform(v);
        const auto perspective = 300 / std::max(50.0, 300 + v.z);
        return QPointF(width() / 2.0 + pan_.x() + v.x * zoom_ * perspective,
                       height() / 2.0 + pan_.y() + v.y * zoom_ * perspective);
    };
    std::vector<Face> faces;
    projectedComponents_.clear();
    auto box = [&](double x, double y, double z, double w, double l, double h, double rotation,
                   QColor color) {
        std::array<Vector, 8> vertices{{{-w / 2, -l / 2, z},
                                        {w / 2, -l / 2, z},
                                        {w / 2, l / 2, z},
                                        {-w / 2, l / 2, z},
                                        {-w / 2, -l / 2, z + h},
                                        {w / 2, -l / 2, z + h},
                                        {w / 2, l / 2, z + h},
                                        {-w / 2, l / 2, z + h}}};
        const auto angle = rotation * std::acos(-1.0) / 180;
        for (auto& vertex : vertices) {
            const auto a = vertex.x * std::cos(angle) - vertex.y * std::sin(angle),
                       b = vertex.x * std::sin(angle) + vertex.y * std::cos(angle);
            vertex.x = a + x;
            vertex.y = b + y;
        }
        const std::array<std::array<int, 4>, 6> indices{
            {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}}};
        for (std::size_t face = 0; face < indices.size(); ++face) {
            Face polygon;
            polygon.color = color.lighter(face == 1 ? 135 : face % 2 ? 105 : 80);
            polygon.depth = 0;
            for (auto index : indices[face]) {
                polygon.polygon << project(vertices[index]);
                polygon.depth += transform(vertices[index]).z / 4;
                polygon.depths.push_back(transform(vertices[index]).z);
            }
            faces.push_back(std::move(polygon));
        }
    };
    auto face = [&](const std::vector<Vector>& vertices, QColor color) {
        Face result;
        result.color = color;
        result.depth = 0;
        for (auto vertex : vertices) {
            result.polygon << project(vertex);
            result.depth += transform(vertex).z / vertices.size();
            result.depths.push_back(transform(vertex).z);
        }
        faces.push_back(std::move(result));
    };
    std::vector<const pcb::Layer*> stack;
    for (const auto& layer : board_->layers)
        if (layer.index == 0)
            stack.push_back(&layer);
    for (const auto& layer : board_->layers)
        if (layer.index != 0 && layer.index != 1)
            stack.push_back(&layer);
    for (const auto& layer : board_->layers)
        if (layer.index == 1)
            stack.push_back(&layer);
    auto layerZ = [&](int index) {
        std::size_t rank = 0;
        for (; rank < stack.size(); ++rank)
            if (stack[rank]->index == index)
                break;
        if (rank == stack.size())
            return 0.0;
        if (display_ == BoardDisplay::Exploded)
            return double(board_->thickness) / mm + (stack.size() - 1 - rank) * explodeSpacing_ +
                   .05;
        return double(board_->thickness) / mm *
                   (stack.size() == 1 ? 1.0 : 1.0 - double(rank) / (stack.size() - 1)) +
               (index == 0   ? .035
                : index == 1 ? -.035
                             : 0);
    };
    auto component = [&](const pcb::Footprint& footprint, double substrate) {
        QImage texture;
        if (!footprint.texturePng.empty())
            texture.loadFromData(
                QByteArray::fromBase64(QByteArray::fromStdString(footprint.texturePng)), "PNG");
        if (!footprint.mesh.empty()) {
            double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
            for (const auto& triangle : footprint.mesh)
                for (auto vertex : triangle) {
                    vertex = pcb::modelVertex(footprint, vertex);
                    minX = std::min(minX, vertex.x);
                    minY = std::min(minY, vertex.y);
                    maxX = std::max(maxX, vertex.x);
                    maxY = std::max(maxY, vertex.y);
                }
            const auto angle = footprint.rotation * std::acos(-1.0) / 180;
            for (const auto& triangle : footprint.mesh) {
                Face polygon;
                polygon.depth = 0;
                auto a = pcb::modelVertex(footprint, triangle[0]),
                     b = pcb::modelVertex(footprint, triangle[1]),
                     c = pcb::modelVertex(footprint, triangle[2]);
                Vector normal{(b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
                              (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                              (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
                auto length = std::hypot(normal.x, normal.y, normal.z);
                if (length < 1e-12)
                    continue;
                double lighting =
                    .48 +
                    .52 * std::abs((normal.x * .3 - normal.y * .35 + normal.z * .88) / length);
                auto base = triangle[0].color ? QColor::fromRgb(triangle[0].color)
                                              : QColor(QString::fromStdString(footprint.bodyColor));
                auto cx = (a.x + b.x + c.x) / 3, cy = (a.y + b.y + c.y) / 3;
                bool lead = std::abs(cx) > double(footprint.width) / mm / 2 + .05 ||
                            std::abs(cy) > double(footprint.length) / mm / 2 + .05;
                if (lead && !triangle[0].color)
                    base = QColor("#bac3c9");
                if (footprint.symbolId == selected_)
                    base = base.lighter(140);
                polygon.color = base.lighter(static_cast<int>(lighting * 100));
                for (auto vertex : triangle) {
                    vertex = pcb::modelVertex(footprint, vertex);
                    if (maxX > minX && maxY > minY)
                        polygon.uv << QPointF((vertex.x - minX) / (maxX - minX) * texture.width(),
                                              (vertex.y - minY) / (maxY - minY) * texture.height());
                    Vector world{double(footprint.position.x) / mm + vertex.x * std::cos(angle) -
                                     vertex.y * std::sin(angle),
                                 double(footprint.position.y) / mm + vertex.x * std::sin(angle) +
                                     vertex.y * std::cos(angle),
                                 substrate + vertex.z};
                    polygon.polygon << project(world);
                    polygon.depth += transform(world).z / 3;
                    polygon.depths.push_back(transform(world).z);
                }
                if (!texture.isNull() && !lead && normal.z / length > .6)
                    polygon.texture = texture;
                faces.push_back(std::move(polygon));
            }
        } else {
            box(double(footprint.position.x) / mm, double(footprint.position.y) / mm, substrate,
                double(footprint.width) / mm, double(footprint.length) / mm,
                double(footprint.height) / mm, footprint.rotation,
                QColor(QString::fromStdString(footprint.bodyColor)));
            if (!texture.isNull() && faces.size() >= 6) {
                auto& top = faces[faces.size() - 5];
                top.texture = texture;
                top.uv = QPolygonF() << QPointF(0, 0) << QPointF(texture.width(), 0)
                                     << QPointF(texture.width(), texture.height())
                                     << QPointF(0, texture.height());
            }
        }
        projectedComponents_.push_back(
            {footprint.symbolId,
             project({double(footprint.position.x) / mm, double(footprint.position.y) / mm,
                      substrate + double(footprint.height) / mm})});
    };
    if (mode_ == Mode::Board) {
        if (display_ != BoardDisplay::CopperOnly) {
            auto mask = QColor(QString::fromStdString(board_->solderMask));
            if (display_ == BoardDisplay::Transparent)
                mask.setAlpha(55);
            box(double(board_->width) / mm / 2, double(board_->height) / mm / 2, 0,
                double(board_->width) / mm, double(board_->height) / mm,
                double(board_->thickness) / mm, 0, mask);
        }
        if (display_ != BoardDisplay::CopperOnly && componentsVisible_)
            for (const auto& footprint : board_->footprints)
                component(footprint, display_ == BoardDisplay::Exploded
                                         ? layerZ(0)
                                         : double(board_->thickness) / mm);
        if (display_ == BoardDisplay::Exploded)
            for (const auto* layer : stack)
                if (layer->visible) {
                    auto tint = QColor(QString::fromStdString(layer->color));
                    tint.setAlpha(30);
                    auto z = layerZ(layer->index) - .02;
                    face({{0, 0, z},
                          {double(board_->width) / mm, 0, z},
                          {double(board_->width) / mm, double(board_->height) / mm, z},
                          {0, double(board_->height) / mm, z}},
                         tint);
                }
        for (const auto& track : board_->tracks) {
            auto layer = std::find_if(stack.begin(), stack.end(),
                                      [&](const auto* l) { return l->index == track.layer; });
            if (!copperVisible_ || layer == stack.end() || !(*layer)->visible)
                continue;
            auto z = layerZ(track.layer), x1 = double(track.from.x) / mm,
                 y1 = double(track.from.y) / mm, x2 = double(track.to.x) / mm,
                 y2 = double(track.to.y) / mm;
            auto length = std::hypot(x2 - x1, y2 - y1);
            if (length == 0)
                continue;
            auto dx = -(y2 - y1) / length * double(track.width) / mm / 2,
                 dy = (x2 - x1) / length * double(track.width) / mm / 2;
            auto color = QColor(QString::fromStdString((*layer)->color));
            face({{x1 + dx, y1 + dy, z},
                  {x2 + dx, y2 + dy, z},
                  {x2 - dx, y2 - dy, z},
                  {x1 - dx, y1 - dy, z}},
                 color);
        }
        // Pads em cada camada efetiva; SMD nao ganha um furo artificial.
        for (const auto& footprint : board_->footprints)
            for (const auto& pad : footprint.pads) {
                auto center = board_->padPosition(footprint, pad);
                auto angle = (footprint.rotation + pad.rotation) * std::acos(-1.0) / 180;
                for (const auto* layer : stack)
                    if (copperVisible_ && layer->visible &&
                        (pad.layerMask & (1u << layer->index))) {
                        auto z = layerZ(layer->index) + .005;
                        std::vector<Vector> vertices;
                        double rx = double(pad.sizeX ? pad.sizeX : pad.diameter) / mm / 2,
                               ry = double(pad.sizeY ? pad.sizeY : pad.diameter) / mm / 2;
                        for (int i = 0; i < 24; ++i) {
                            auto theta = i * 2 * std::acos(-1.0) / 24;
                            auto x = rx * std::cos(theta), y = ry * std::sin(theta);
                            if (pad.shape == "rect" || pad.shape == "roundrect" ||
                                pad.shape == "oval") {
                                double radius =
                                    pad.shape == "rect"
                                        ? 0
                                        : std::min(rx, ry) *
                                              (pad.shape == "oval" ? 1 : 2 * pad.cornerRatio);
                                int quadrant = i / 6;
                                theta = (quadrant + (i % 6) / 5.0) * std::acos(-1.0) / 2;
                                double cx =
                                    (quadrant == 0 || quadrant == 3) ? rx - radius : -rx + radius;
                                double cy = quadrant < 2 ? ry - radius : -ry + radius;
                                x = cx + radius * std::cos(theta);
                                y = cy + radius * std::sin(theta);
                            }
                            vertices.push_back(
                                {double(center.x) / mm + x * std::cos(angle) - y * std::sin(angle),
                                 double(center.y) / mm + x * std::sin(angle) + y * std::cos(angle),
                                 z});
                        }
                        face(vertices, QColor(QString::fromStdString(board_->finishColor)));
                        if (pad.drill > 0) {
                            vertices.clear();
                            for (int i = 0; i < 24; ++i) {
                                auto theta = i * 2 * std::acos(-1.0) / 24;
                                vertices.push_back({double(center.x) / mm + double(pad.drill) / mm /
                                                                                2 * std::cos(theta),
                                                    double(center.y) / mm + double(pad.drill) / mm /
                                                                                2 * std::sin(theta),
                                                    z + (layer->index == 1 ? -.002 : .002)});
                            }
                            face(vertices, QColor("#0c131b"));
                        }
                    }
            }
        for (const auto& via : board_->vias)
            for (const auto* layer : stack)
                if (copperVisible_ && layer->visible) {
                    auto z = layerZ(layer->index) + .006;
                    std::vector<Vector> vertices;
                    for (int i = 0; i < 24; ++i) {
                        auto theta = i * 2 * std::acos(-1.0) / 24;
                        vertices.push_back({double(via.position.x) / mm +
                                                double(via.diameter) / mm / 2 * std::cos(theta),
                                            double(via.position.y) / mm +
                                                double(via.diameter) / mm / 2 * std::sin(theta),
                                            z});
                    }
                    face(vertices, QColor(QString::fromStdString(board_->finishColor)));
                    for (auto& vertex : vertices) {
                        vertex.x =
                            double(via.position.x) / mm + (vertex.x - double(via.position.x) / mm) *
                                                              double(via.drill) / via.diameter;
                        vertex.y =
                            double(via.position.y) / mm + (vertex.y - double(via.position.y) / mm) *
                                                              double(via.drill) / via.diameter;
                        vertex.z += layer->index == 1 ? -.002 : .002;
                    }
                    face(vertices, QColor("#0c131b"));
                }
    } else {
        for (const auto& symbol : document_->symbols()) {
            if (symbol.kind == core::SymbolKind::Ground || symbol.libraryId.starts_with("power:"))
                continue;
            auto found = std::find_if(board_->footprints.begin(), board_->footprints.end(),
                                      [&](const auto& f) { return f.symbolId == symbol.id; });
            pcb::Footprint footprint;
            if (found != board_->footprints.end())
                footprint = *found;
            else {
                auto& cached = schematicAssets_[symbol.id];
                if (cached.reference != symbol.reference || cached.value != symbol.value ||
                    cached.symbolId == 0) {
                    if (auto mapped = officialFootprint(symbol))
                        cached = std::move(*mapped);
                    else {
                        cached.symbolId = symbol.id;
                        cached.reference = symbol.reference;
                        cached.value = symbol.value;
                        cached.width = 6000000;
                        cached.length = 2500000;
                        cached.height = 2500000;
                    }
                    assignAutomaticModel(cached);
                }
                footprint = cached;
            }
            footprint.position = symbol.origin;
            footprint.rotation = symbol.rotation;
            footprint.modelScale *= componentScale_;
            footprint.width = static_cast<core::Coord>(footprint.width * componentScale_);
            footprint.length = static_cast<core::Coord>(footprint.length * componentScale_);
            footprint.height = static_cast<core::Coord>(footprint.height * componentScale_);
            component(footprint, .2);
        }
    }
    // Teste a profundidade por pixel: uma face grande da placa nao pode
    // esconder um componente proximo nem trazer um terminal de tras para frente.
    QImage image(size(), QImage::Format_RGB32);
    image.fill(QColor("#121c2a"));
    std::vector<double> depthBuffer(static_cast<std::size_t>(width()) * height(),
                                    std::numeric_limits<double>::infinity());
    std::stable_sort(faces.begin(), faces.end(), [](const auto& a, const auto& b) {
        if ((a.color.alpha() == 255) != (b.color.alpha() == 255))
            return a.color.alpha() == 255;
        return a.depth > b.depth;
    });
    for (const auto& item : faces) {
        for (qsizetype corner = 1; corner + 1 < item.polygon.size(); ++corner) {
            const std::array<qsizetype, 3> indices{0, corner, corner + 1};
            auto a = item.polygon[0], b = item.polygon[corner], c = item.polygon[corner + 1];
            auto area = (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
            if (std::abs(area) < 1e-8 || !std::isfinite(area))
                continue;
            int left = std::max(0, int(std::floor(std::min({a.x(), b.x(), c.x()}))));
            int right = std::min(width() - 1, int(std::ceil(std::max({a.x(), b.x(), c.x()}))));
            int top = std::max(0, int(std::floor(std::min({a.y(), b.y(), c.y()}))));
            int bottom = std::min(height() - 1, int(std::ceil(std::max({a.y(), b.y(), c.y()}))));
            for (int y = top; y <= bottom; ++y) {
                auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(y));
                for (int x = left; x <= right; ++x) {
                    double px = x + .5, py = y + .5;
                    double wb =
                        ((px - a.x()) * (c.y() - a.y()) - (py - a.y()) * (c.x() - a.x())) / area;
                    double wc =
                        ((b.x() - a.x()) * (py - a.y()) - (b.y() - a.y()) * (px - a.x())) / area;
                    double wa = 1 - wb - wc;
                    if (wa < -1e-8 || wb < -1e-8 || wc < -1e-8)
                        continue;
                    // Interpolacao de 1/distancia evita erros com a perspectiva.
                    double inverse = wa / (300 + item.depths[indices[0]]) +
                                     wb / (300 + item.depths[indices[1]]) +
                                     wc / (300 + item.depths[indices[2]]);
                    if (inverse <= 0)
                        continue;
                    double z = 1 / inverse - 300;
                    auto offset = static_cast<std::size_t>(y) * width() + x;
                    if (z > depthBuffer[offset] + 1e-7)
                        continue;
                    QColor color = item.color;
                    if (!item.texture.isNull() && item.uv.size() == item.polygon.size()) {
                        auto uv = (item.uv[indices[0]] * (wa / (300 + item.depths[indices[0]])) +
                                   item.uv[indices[1]] * (wb / (300 + item.depths[indices[1]])) +
                                   item.uv[indices[2]] * (wc / (300 + item.depths[indices[2]]))) /
                                  inverse;
                        color = item.texture.pixelColor(
                            std::clamp(int(uv.x()), 0, item.texture.width() - 1),
                            std::clamp(int(uv.y()), 0, item.texture.height() - 1));
                    }
                    if (color.alpha() == 255) {
                        pixels[x] = color.rgb();
                        depthBuffer[offset] = z;
                    } else {
                        double alpha = color.alphaF();
                        auto old = QColor::fromRgb(pixels[x]);
                        pixels[x] = qRgb(int(color.red() * alpha + old.red() * (1 - alpha)),
                                         int(color.green() * alpha + old.green() * (1 - alpha)),
                                         int(color.blue() * alpha + old.blue() * (1 - alpha)));
                    }
                }
            }
        }
    }
    painter.drawImage(QPoint(0, 0), image);
    if (mode_ == Mode::Board && display_ == BoardDisplay::Exploded) {
        int y = 80;
        for (const auto* layer : stack)
            if (layer->visible) {
                painter.setPen(QColor(QString::fromStdString(layer->color)));
                painter.drawText(16, y, QString::fromStdString(layer->name));
                y += 20;
            }
    }
    if (mode_ == Mode::Schematic) {
        painter.setPen(QPen(QColor("#74d9bf"), 2));
        for (const auto& connection : document_->wires())
            painter.drawLine(
                project({double(connection.from.x) / mm, double(connection.from.y) / mm, .1}),
                project({double(connection.to.x) / mm, double(connection.to.y) / mm, .1}));
    }
    painter.setPen(QColor("#e5edf8"));
    for (const auto& [id, position] : projectedComponents_) {
        QString reference;
        if (mode_ == Mode::Board) {
            for (const auto& footprint : board_->footprints)
                if (footprint.symbolId == id)
                    reference = QString::fromStdString(footprint.reference);
        } else
            for (const auto& symbol : document_->symbols())
                if (symbol.id == id)
                    reference = QString::fromStdString(symbol.reference);
        painter.drawText(position + QPointF(6, -7), reference);
    }
    painter.drawText(QRect(16, 14, width() - 32, 55), Qt::AlignLeft | Qt::AlignTop,
                     tr("Arraste para girar / roda para zoom / botao do meio para deslocar / duplo "
                        "clique restaura"));
    painter.setPen(QColor("#92a9c6"));
    painter.drawText(QRect(16, height() - 45, width() - 32, 35), Qt::AlignLeft | Qt::AlignBottom,
                     mode_ == Mode::Board
                         ? tr("%1 trilhas / %2 vias / %3 ligacoes pendentes (ainda sem cobre)")
                               .arg(board_->tracks.size())
                               .arg(board_->vias.size())
                               .arg(board_->airwires().size())
                         : tr("Esquematico em 3D / visualizacao espacial das conexoes"));
}

void ModelView::mousePressEvent(QMouseEvent* event) {
    last_ = pressed_ = event->pos();
}

void ModelView::mouseMoveEvent(QMouseEvent* event) {
    const auto delta = event->pos() - last_;
    if (event->buttons() & Qt::LeftButton) {
        yaw_ += delta.x() * 0.009;
        pitch_ = std::clamp(pitch_ + delta.y() * 0.009, 0.05, 3.05);
        update();
    }
    if (event->buttons() & Qt::MiddleButton) {
        pan_ += delta;
        update();
    }
    last_ = event->pos();
}

void ModelView::mouseReleaseEvent(QMouseEvent* event) {
    if ((event->pos() - pressed_).manhattanLength() > 5)
        return;
    double closest = 40;
    for (const auto& [id, position] : projectedComponents_) {
        const auto distance = QLineF(position, event->position()).length();
        if (distance < closest) {
            closest = distance;
            selected_ = id;
        }
    }
    update();
}

void ModelView::wheelEvent(QWheelEvent* event) {
    zoom_ = std::clamp(zoom_ * (event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15), 0.3, 80.0);
    update();
    event->accept();
}

void ModelView::mouseDoubleClickEvent(QMouseEvent*) {
    resetCamera();
}

void ModelView::editHeight() {
    if (mode_ != Mode::Board)
        return;
    for (auto& footprint : board_->footprints)
        if (footprint.symbolId == selected_) {
            bool ok = false;
            const auto height = QInputDialog::getDouble(
                this, tr("Altura 3D"), QString::fromStdString(footprint.reference) + " (mm)",
                double(footprint.height) / mm, 0.1, 100, 2, &ok);
            if (ok) {
                footprint.height = static_cast<core::Coord>(height * mm);
                update();
                if (onChanged)
                    onChanged();
            }
            return;
        }
    QMessageBox::information(this, tr("Selecionar componente"),
                             tr("Clique em um componente na vista 3D antes de editar sua altura."));
}

void ModelView::exportModel() {
    auto path = chooseExportFile(this, tr("Exportar modelo 3D"), {}, "Wavefront OBJ (*.obj)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".obj", Qt::CaseInsensitive))
        path += ".obj";
    pcb::Board model = *board_;
    if (mode_ == Mode::Schematic) {
        model.footprints.clear();
        for (const auto& symbol : document_->symbols()) {
            pcb::Footprint item;
            item.symbolId = symbol.id;
            item.reference = symbol.reference;
            item.position = symbol.origin;
            item.width = 6000000;
            item.length = 2000000;
            item.height = 800000;
            item.rotation = symbol.rotation;
            model.footprints.push_back(item);
        }
        model.thickness = 100000;
    }
    QSaveFile file(path);
    const auto bytes = QByteArray::fromStdString(model.exportObj());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(this, tr("Falha ao salvar"), file.errorString());
}
} // namespace levelcad::app
