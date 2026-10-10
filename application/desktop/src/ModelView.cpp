#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QSaveFile>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <levelcad/app/ExportDialog.hpp>
#include <levelcad/app/ModelView.hpp>

namespace levelcad::app {
namespace {
struct Vector {
    double x, y, z;
};

struct Face {
    QPolygonF polygon;
    QColor color;
    double depth;
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
    zoom_ = mode_ == Mode::Board ? 5 : 12;
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
                      y * std::sin(pitch_) + v.z * std::cos(pitch_)};
    };
    auto project = [&](Vector v) {
        v = transform(v);
        const auto perspective = 300 / std::max(50.0, 300 + v.z);
        return QPointF(width() / 2.0 + pan_.x() + v.x * zoom_ * perspective,
                       height() / 2.0 + pan_.y() + v.y * zoom_ * perspective);
    };
    std::vector<Face> faces;
    projectedComponents_.clear();
    auto box = [&](double x, double y, double z, double w, double l, double h, int rotation,
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
            }
            faces.push_back(std::move(polygon));
        }
    };
    if (mode_ == Mode::Board) {
        box(double(board_->width) / mm / 2, double(board_->height) / mm / 2, 0,
            double(board_->width) / mm, double(board_->height) / mm, double(board_->thickness) / mm,
            0, QColor("#28705c"));
        for (const auto& footprint : board_->footprints) {
            if (!footprint.mesh.empty()) {
                const auto angle = footprint.rotation * std::acos(-1.0) / 180;
                for (const auto& triangle : footprint.mesh) {
                    Face polygon;
                    polygon.depth = 0;
                    const auto a = triangle[0], b = triangle[1], c = triangle[2];
                    const auto normalZ = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                    polygon.color = QColor(footprint.symbolId == selected_ ? "#cb8a46" : "#71839c")
                                        .lighter(normalZ > 0 ? 125 : 85);
                    for (auto vertex : triangle) {
                        vertex.x += footprint.modelOffsetX;
                        vertex.y += footprint.modelOffsetY;
                        Vector world{double(footprint.position.x) / mm +
                                         vertex.x * std::cos(angle) - vertex.y * std::sin(angle),
                                     double(footprint.position.y) / mm +
                                         vertex.x * std::sin(angle) + vertex.y * std::cos(angle),
                                     double(board_->thickness) / mm + vertex.z +
                                         footprint.modelOffsetZ};
                        polygon.polygon << project(world);
                        polygon.depth += transform(world).z / 3;
                    }
                    faces.push_back(std::move(polygon));
                }
            } else
                box(double(footprint.position.x) / mm, double(footprint.position.y) / mm,
                    double(board_->thickness) / mm, double(footprint.width) / mm,
                    double(footprint.length) / mm, double(footprint.height) / mm,
                    footprint.rotation,
                    footprint.symbolId == selected_ ? QColor("#cb8a46") : QColor("#3e4b62"));
            projectedComponents_.push_back(
                {footprint.symbolId,
                 project({double(footprint.position.x) / mm, double(footprint.position.y) / mm,
                          double(board_->thickness + footprint.height) / mm})});
        }
    } else {
        for (const auto& symbol : document_->symbols()) {
            box(double(symbol.origin.x) / mm, double(symbol.origin.y) / mm, 0, 6, 2, 0.8,
                symbol.rotation, symbol.id == selected_ ? QColor("#dcad5e") : QColor("#657faf"));
            projectedComponents_.push_back({symbol.id, project({double(symbol.origin.x) / mm,
                                                                double(symbol.origin.y) / mm, 1})});
        }
    }
    // Ordene as faces pela profundidade: a câmera funciona sem exigir uma GPU específica.
    std::sort(faces.begin(), faces.end(),
              [](const auto& first, const auto& second) { return first.depth > second.depth; });
    for (const auto& face : faces) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(face.color);
        painter.drawPolygon(face.polygon);
    }
    if (mode_ == Mode::Board) {
        for (const auto& track : board_->tracks) {
            const double z = track.layer == 0 ? double(board_->thickness) / mm + 0.02 : -0.02;
            painter.setPen(QPen(track.layer == 0 ? QColor("#ed9b65") : QColor("#6ca7dc"),
                                std::max(1.0, double(track.width) / mm * zoom_), Qt::SolidLine,
                                Qt::RoundCap));
            painter.drawLine(project({double(track.from.x) / mm, double(track.from.y) / mm, z}),
                             project({double(track.to.x) / mm, double(track.to.y) / mm, z}));
        }
        for (const auto& footprint : board_->footprints)
            for (const auto& pad : footprint.pads) {
                const auto point = board_->padPosition(footprint, pad);
                const auto p = project({double(point.x) / mm, double(point.y) / mm,
                                        double(board_->thickness) / mm + 0.04});
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor("#ddb767"));
                painter.drawEllipse(p, std::max(2.0, double(pad.diameter) / mm * zoom_ / 2),
                                    std::max(1.0, double(pad.diameter) / mm * zoom_ / 3));
                painter.setBrush(QColor("#101a26"));
                painter.drawEllipse(p, std::max(1.0, double(pad.drill) / mm * zoom_ / 2),
                                    std::max(1.0, double(pad.drill) / mm * zoom_ / 3));
            }
    } else {
        painter.setPen(QPen(QColor("#74d9bf"), 2));
        for (const auto& connection : document_->wires())
            painter.drawLine(
                project({double(connection.from.x) / mm, double(connection.from.y) / mm, 0.1}),
                project({double(connection.to.x) / mm, double(connection.to.y) / mm, 0.1}));
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
                         ? tr("Modelo 3D da placa / selecione um componente para editar a altura")
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
