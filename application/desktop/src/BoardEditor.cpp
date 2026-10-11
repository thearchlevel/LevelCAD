#include <QBuffer>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsItemGroup>
#include <QGraphicsSimpleTextItem>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <levelcad/app/BoardEditor.hpp>
#include <levelcad/app/BoardIO.hpp>
#include <levelcad/app/ExportDialog.hpp>
#include <levelcad/app/MeshLibrary.hpp>
#include <levelcad/app/OfficialLibrary.hpp>
#include <levelcad/pcb/Footprints.hpp>

namespace levelcad::app {
namespace {
QPointF scenePoint(core::Point point) {
    return {double(point.x) / 100000, double(point.y) / 100000};
}

core::Point corePoint(QPointF point) {
    return {static_cast<core::Coord>(std::llround(point.x() * 100000)),
            static_cast<core::Coord>(std::llround(point.y() * 100000))};
}

QString text(const std::string& value) {
    return QString::fromStdString(value);
}

QJsonObject pointJson(core::Point point) {
    return {{"x", QString::number(point.x)}, {"y", QString::number(point.y)}};
}

core::Point readPoint(const QJsonObject& point) {
    bool first = false, second = false;
    auto x = point["x"].toString().toLongLong(&first),
         y = point["y"].toString().toLongLong(&second);
    if (!first || !second)
        throw std::runtime_error("Invalid PCB point");
    return {x, y};
}

bool write(QWidget* parent, const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        QMessageBox::warning(parent, QObject::tr("Falha ao salvar"), file.errorString());
        return false;
    }
    return true;
}
} // namespace

BoardEditor::BoardEditor(pcb::Board* board, QWidget* parent)
    : QGraphicsView(parent), board_(board) {
    setScene(&scene_);
    connect(&scene_, &QGraphicsScene::selectionChanged, this, [this] {
        if (refreshing_ || !onSelectionChanged)
            return;
        std::vector<std::uint64_t> ids;
        for (auto* item : scene_.selectedItems())
            if (auto id = item->data(0).toULongLong())
                ids.push_back(id);
        onSelectionChanged(ids);
    });
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setDragMode(RubberBandDrag);
    setMouseTracking(true);
    scene_.setSceneRect(-300, -300, 3000, 2400);
    history_.push_back(*board_);
    refresh();
}

void BoardEditor::setRouting(bool enabled) {
    routing_ = enabled;
    routeStart_.reset();
    routeNet_.clear();
    setDragMode(enabled ? NoDrag : RubberBandDrag);
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    refresh();
}

void BoardEditor::setLayer(int layer) {
    layer_ = layer;
}

void BoardEditor::setTraceWidth(core::Coord width) {
    traceWidth_ = width;
}

core::Point BoardEditor::snap(QPointF position) const {
    return {static_cast<core::Coord>(std::llround(position.x() / 5) * 500000),
            static_cast<core::Coord>(std::llround(position.y() / 5) * 500000)};
}

std::optional<std::pair<core::Point, std::string>> BoardEditor::nearestPad(QPointF position) const {
    double closest = 12 / transform().m11();
    std::optional<std::pair<core::Point, std::string>> result;
    for (const auto& footprint : board_->footprints)
        for (const auto& pad : footprint.pads) {
            if (!(pad.layerMask & (1u << layer_)))
                continue;
            auto location = board_->padPosition(footprint, pad);
            auto distance = QLineF(position, scenePoint(location)).length();
            if (distance < closest) {
                closest = distance;
                result = std::make_pair(location, pad.net);
            }
        }
    for (const auto& via : board_->vias) {
        auto distance = QLineF(position, scenePoint(via.position)).length();
        if (distance < closest) {
            closest = distance;
            result = std::make_pair(via.position, via.net);
        }
    }
    for (const auto& track : board_->tracks)
        if (track.layer == layer_)
            for (const auto& endpoint : {track.from, track.to}) {
                auto distance = QLineF(position, scenePoint(endpoint)).length();
                if (distance < closest) {
                    closest = distance;
                    result = std::make_pair(endpoint, track.net);
                }
            }
    return result;
}

void BoardEditor::record() {
    history_.resize(index_ + 1);
    history_.push_back(*board_);
    ++index_;
    refresh();
    if (onChanged)
        onChanged();
}

void BoardEditor::undo() {
    if (!isEnabled())
        return;
    if (index_ == 0)
        return;
    *board_ = history_[--index_];
    routeStart_.reset();
    refresh();
    if (onChanged)
        onChanged();
}

void BoardEditor::redo() {
    if (!isEnabled())
        return;
    if (index_ + 1 >= static_cast<int>(history_.size()))
        return;
    *board_ = history_[++index_];
    routeStart_.reset();
    refresh();
    if (onChanged)
        onChanged();
}

void BoardEditor::fitBoard() {
    fitInView(QRectF(-50, -50, double(board_->width) / 100000 + 100,
                     double(board_->height) / 100000 + 100),
              Qt::KeepAspectRatio);
}

void BoardEditor::drawBackground(QPainter* painter, const QRectF& area) {
    painter->fillRect(area, QColor("#111b25"));
    painter->setPen(QPen(QColor("#293747"), 0));
    const auto step = transform().m11() < 0.4 ? 25.0 : 5.0;
    for (double x = std::floor(area.left() / step) * step; x < area.right(); x += step)
        for (double y = std::floor(area.top() / step) * step; y < area.bottom(); y += step)
            painter->drawPoint(QPointF(x, y));
}

void BoardEditor::wheelEvent(QWheelEvent* event) {
    const auto factor = event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
    if (transform().m11() * factor > 0.1 && transform().m11() * factor < 15)
        scale(factor, factor);
    event->accept();
}

void BoardEditor::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        routeStart_.reset();
        refresh();
        QGraphicsView::mousePressEvent(event);
        return;
    }
    if (event->button() != Qt::LeftButton || !routing_) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    const auto position = mapToScene(event->pos());
    const auto target = nearestPad(position);
    if (!routeStart_) {
        if (!target || target->second.empty())
            return;
        routeStart_ = target->first;
        routeNet_ = target->second;
        return;
    }
    if (target && target->second != routeNet_) {
        QMessageBox::warning(
            this, tr("Redes diferentes"),
            tr("Essa ligacao uniria redes diferentes. Escolha um pad da mesma rede."));
        return;
    }
    const auto end = target ? target->first : snap(position);
    if (end == *routeStart_)
        return;
    const core::Point bend{end.x, routeStart_->y};
    auto proposed = *board_;
    if (*routeStart_ != bend &&
        !proposed.canPlaceTrack({0, *routeStart_, bend, layer_, traceWidth_, routeNet_})) {
        QMessageBox::warning(this, tr("Trilha bloqueada"),
                             tr("O trecho viola a separacao ou os limites da placa."));
        return;
    }
    if (bend != end && !proposed.canPlaceTrack({0, bend, end, layer_, traceWidth_, routeNet_})) {
        QMessageBox::warning(this, tr("Trilha bloqueada"),
                             tr("O trecho viola a separacao ou os limites da placa."));
        return;
    }
    if (*routeStart_ != bend)
        board_->addTrack(*routeStart_, bend, layer_, routeNet_, traceWidth_);
    if (bend != end)
        board_->addTrack(bend, end, layer_, routeNet_, traceWidth_);
    if (target)
        routeStart_.reset();
    else
        routeStart_ = end;
    record();
    event->accept();
}

void BoardEditor::mouseMoveEvent(QMouseEvent* event) {
    if (routing_ && routeStart_) {
        const auto target = nearestPad(mapToScene(event->pos()));
        const auto end = target ? target->first : snap(mapToScene(event->pos()));
        QPainterPath path(scenePoint(*routeStart_));
        path.lineTo(scenePoint({end.x, routeStart_->y}));
        path.lineTo(scenePoint(end));
        if (!preview_)
            preview_ = scene_.addPath(
                path, QPen(layer_ == 0 ? QColor("#ff766e") : QColor("#72aaff"), 2, Qt::DashLine));
        else
            preview_->setPath(path);
    }
    QGraphicsView::mouseMoveEvent(event);
}

void BoardEditor::mouseReleaseEvent(QMouseEvent* event) {
    QGraphicsView::mouseReleaseEvent(event);
    if (routing_)
        return;
    bool changed = false;
    for (auto* item : scene_.selectedItems()) {
        if (item->data(1).toString() != "footprint")
            continue;
        const auto id = item->data(0).toULongLong();
        for (auto& footprint : board_->footprints)
            if (footprint.symbolId == id) {
                const auto position = snap(item->pos());
                if (position != footprint.position) {
                    footprint.position = position;
                    changed = true;
                }
            }
    }
    if (changed)
        record();
}

void BoardEditor::mouseDoubleClickEvent(QMouseEvent* event) {
    QGraphicsView::mousePressEvent(event);
    editSelected();
}

void BoardEditor::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        setRouting(false);
        return;
    }
    if (event->key() == Qt::Key_Delete) {
        deleteSelected();
        return;
    }
    if (event->key() == Qt::Key_R) {
        rotateSelected();
        return;
    }
    if (event->key() == Qt::Key_V && routeStart_) {
        board_->addVia(*routeStart_, routeNet_);
        layer_ = layer_ == 0 ? 1 : 0;
        record();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void BoardEditor::contextMenuEvent(QContextMenuEvent* event) {
    auto* item = itemAt(event->pos());
    while (item && item->data(1).toString().isEmpty())
        item = item->parentItem();
    if (item && !item->isSelected()) {
        scene_.clearSelection();
        item->setSelected(true);
    }
    QMenu menu(this);
    menu.addAction(tr("Excluir selecao"), this, &BoardEditor::deleteSelected);
    menu.addAction(tr("Girar 90 deg"), this, &BoardEditor::rotateSelected);
    menu.addAction(tr("Propriedades..."), this, &BoardEditor::editSelected);
    menu.addSeparator();
    menu.addAction(tr("Ajustar placa"), this, &BoardEditor::fitBoard);
    menu.exec(event->globalPos());
}

void BoardEditor::deleteSelected() {
    if (!isEnabled())
        return;
    bool changed = false;
    for (auto* item : scene_.selectedItems()) {
        const auto id = item->data(0).toULongLong();
        const auto kind = item->data(1).toString();
        if (kind == "track") {
            std::erase_if(board_->tracks, [&](const auto& track) { return track.id == id; });
            changed = true;
        }
        if (kind == "via") {
            std::erase_if(board_->vias, [&](const auto& via) { return via.id == id; });
            changed = true;
        }
    }
    if (changed)
        record();
}

void BoardEditor::rotateSelected() {
    if (!isEnabled())
        return;
    bool changed = false;
    for (auto* item : scene_.selectedItems())
        if (item->data(1).toString() == "footprint")
            for (auto& footprint : board_->footprints)
                if (footprint.symbolId == item->data(0).toULongLong()) {
                    footprint.rotation = std::fmod(footprint.rotation + 90, 360.0);
                    changed = true;
                }
    if (changed)
        record();
}

void BoardEditor::editSelected() {
    if (!isEnabled())
        return;
    for (auto* item : scene_.selectedItems())
        if (item->data(1).toString() == "footprint")
            for (auto& footprint : board_->footprints)
                if (footprint.symbolId == item->data(0).toULongLong()) {
                    QDialog dialog(this);
                    dialog.setWindowTitle(tr("Propriedades do footprint"));
                    QFormLayout layout(&dialog);
                    QLabel reference(text(footprint.reference) + " / " + text(footprint.value));
                    layout.addRow(&reference);
                    QDoubleSpinBox w, l, h;
                    for (auto* spin : {&w, &l, &h}) {
                        spin->setRange(0.5, 100);
                        spin->setSuffix(" mm");
                        spin->setDecimals(2);
                    }
                    w.setValue(double(footprint.width) / core::nmPerMm);
                    l.setValue(double(footprint.length) / core::nmPerMm);
                    h.setValue(double(footprint.height) / core::nmPerMm);
                    layout.addRow(tr("Largura"), &w);
                    layout.addRow(tr("Comprimento"), &l);
                    layout.addRow(tr("Altura 3D"), &h);
                    QComboBox model;
                    model.addItem(tr("Modelo generico"), QString());
                    for (const auto& name : bundledModels())
                        model.addItem(name, ":/models3d/" + name);
                    auto libraryRoot = officialLibraryRoot();
                    if (!libraryRoot.isEmpty()) {
                        QDir cache(QDir(libraryRoot).filePath("mesh-cache"));
                        for (const auto& folder :
                             cache.entryList({"*.3dshapes"}, QDir::Dirs, QDir::Name)) {
                            QDir category(cache.filePath(folder));
                            for (const auto& file :
                                 category.entryList({"*.wrl"}, QDir::Files, QDir::Name))
                                model.addItem(folder + "/" + file, category.filePath(file));
                        }
                    }
                    model.setEditable(true);
                    auto current = QString::fromStdString(footprint.modelName);
                    if (!current.isEmpty() && model.findData(current) < 0)
                        model.addItem(tr("Modelo personalizado"), current);
                    model.setCurrentIndex(qMax(0, model.findData(current)));
                    layout.addRow(tr("Modelo 3D"), &model);
                    QDoubleSpinBox rotateX, rotateY, rotateZ, scale;
                    for (auto* spin : {&rotateX, &rotateY, &rotateZ}) {
                        spin->setRange(-360, 360);
                        spin->setSuffix(" deg");
                    }
                    rotateX.setValue(footprint.modelRotationX);
                    rotateY.setValue(footprint.modelRotationY);
                    rotateZ.setValue(footprint.modelRotationZ);
                    scale.setRange(.01, 100);
                    scale.setDecimals(3);
                    scale.setValue(footprint.modelScale);
                    layout.addRow(tr("Rotacao modelo X"), &rotateX);
                    layout.addRow(tr("Rotacao modelo Y"), &rotateY);
                    layout.addRow(tr("Rotacao modelo Z"), &rotateZ);
                    layout.addRow(tr("Escala do modelo"), &scale);
                    auto body = QColor(text(footprint.bodyColor));
                    QPushButton bodyButton(tr("Cor do material...")),
                        textureButton(tr("Importar textura PNG/JPG...")),
                        clearTexture(tr("Remover textura"));
                    auto texture = footprint.texturePng;
                    connect(&bodyButton, &QPushButton::clicked, &dialog, [&] {
                        auto color =
                            QColorDialog::getColor(body, &dialog, tr("Material do componente"));
                        if (color.isValid())
                            body = color;
                    });
                    connect(&textureButton, &QPushButton::clicked, &dialog, [&] {
                        auto path =
                            QFileDialog::getOpenFileName(&dialog, tr("Textura do componente"), {},
                                                         "Imagem (*.png *.jpg *.jpeg)");
                        if (path.isEmpty())
                            return;
                        QImageReader reader(path);
                        auto size = reader.size();
                        if (!size.isValid() || size.width() > 2048 || size.height() > 2048) {
                            QMessageBox::warning(&dialog, tr("Textura"),
                                                 tr("Use uma imagem de ate 2048 x 2048 pixels."));
                            return;
                        }
                        auto image = reader.read();
                        if (image.isNull()) {
                            QMessageBox::warning(&dialog, tr("Textura"), reader.errorString());
                            return;
                        }
                        QByteArray bytes;
                        QBuffer buffer(&bytes);
                        buffer.open(QIODevice::WriteOnly);
                        image.save(&buffer, "PNG");
                        if (bytes.size() > 2 * 1024 * 1024) {
                            QMessageBox::warning(&dialog, tr("Textura"),
                                                 tr("Textura acima de 2 MB."));
                            return;
                        }
                        texture = bytes.toBase64().toStdString();
                        textureButton.setText(tr("Textura carregada"));
                    });
                    connect(&clearTexture, &QPushButton::clicked, &dialog, [&] {
                        texture.clear();
                        textureButton.setText(tr("Importar textura PNG/JPG..."));
                    });
                    layout.addRow(&bodyButton);
                    layout.addRow(&textureButton);
                    layout.addRow(&clearTexture);
                    QDoubleSpinBox offsetX, offsetY, offsetZ;
                    for (auto* spin : {&offsetX, &offsetY, &offsetZ}) {
                        spin->setRange(-500, 500);
                        spin->setDecimals(3);
                        spin->setSuffix(" mm");
                    }
                    offsetX.setValue(footprint.modelOffsetX);
                    offsetY.setValue(footprint.modelOffsetY);
                    offsetZ.setValue(footprint.modelOffsetZ);
                    layout.addRow(tr("Modelo: deslocamento X"), &offsetX);
                    layout.addRow(tr("Modelo: deslocamento Y"), &offsetY);
                    layout.addRow(tr("Modelo: deslocamento Z"), &offsetZ);
                    QPushButton importModel(tr("Importar STL..."));
                    layout.addRow(&importModel);
                    connect(&importModel, &QPushButton::clicked, &dialog, [&] {
                        auto path = QFileDialog::getOpenFileName(&dialog, tr("Importar modelo 3D"),
                                                                 {}, "STL (*.stl *.STL)");
                        if (!path.isEmpty()) {
                            model.addItem(QFileInfo(path).fileName(), path);
                            model.setCurrentIndex(model.count() - 1);
                        }
                    });
                    QLabel note(
                        tr("Confira a numeracao e as medidas do encapsulamento "
                           "antes de fabricar. Texturas sao projetadas sobre a superficie."));
                    note.setWordWrap(true);
                    layout.addRow(&note);
                    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
                    layout.addRow(&buttons);
                    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
                    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                    if (dialog.exec() != QDialog::Accepted)
                        return;
                    auto modelPath = model.currentData().toString();
                    if (modelPath.toStdString() != footprint.modelName) {
                        try {
                            if (modelPath.isEmpty()) {
                                footprint.mesh.clear();
                                footprint.modelName.clear();
                            } else
                                assignMesh(footprint, modelPath);
                        } catch (const std::exception& error) {
                            QMessageBox::warning(this, tr("Modelo 3D"),
                                                 QString::fromUtf8(error.what()));
                            return;
                        }
                    }
                    footprint.modelRotationX = rotateX.value();
                    footprint.modelRotationY = rotateY.value();
                    footprint.modelRotationZ = rotateZ.value();
                    footprint.modelScale = scale.value();
                    footprint.bodyColor = body.name().toStdString();
                    footprint.texturePng = texture;
                    footprint.modelOffsetX = offsetX.value();
                    footprint.modelOffsetY = offsetY.value();
                    footprint.modelOffsetZ = offsetZ.value();
                    footprint.width = static_cast<core::Coord>(w.value() * core::nmPerMm);
                    footprint.length = static_cast<core::Coord>(l.value() * core::nmPerMm);
                    footprint.height = static_cast<core::Coord>(h.value() * core::nmPerMm);
                    record();
                    return;
                }
}

void BoardEditor::boardProperties() {
    if (!isEnabled())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Configuracao da placa"));
    QFormLayout layout(&dialog);
    QDoubleSpinBox w, h, t, c;
    for (auto* spin : {&w, &h, &t, &c}) {
        spin->setRange(0.05, 500);
        spin->setDecimals(2);
        spin->setSuffix(" mm");
    }
    w.setValue(double(board_->width) / core::nmPerMm);
    h.setValue(double(board_->height) / core::nmPerMm);
    t.setValue(double(board_->thickness) / core::nmPerMm);
    c.setValue(double(board_->clearance) / core::nmPerMm);
    layout.addRow(tr("Largura"), &w);
    layout.addRow(tr("Altura"), &h);
    layout.addRow(tr("Espessura"), &t);
    layout.addRow(tr("Separacao minima"), &c);
    QColor mask(text(board_->solderMask)), silk(text(board_->silkColor)),
        finish(text(board_->finishColor));
    QPushButton maskButton(tr("Cor da mascara...")), silkButton(tr("Cor da serigrafia...")),
        finishButton(tr("Cor do acabamento..."));
    auto pick = [&](QColor& target) {
        auto value = QColorDialog::getColor(target, &dialog, tr("Aparencia da PCB"));
        if (value.isValid())
            target = value;
    };
    connect(&maskButton, &QPushButton::clicked, &dialog, [&] { pick(mask); });
    connect(&silkButton, &QPushButton::clicked, &dialog, [&] { pick(silk); });
    connect(&finishButton, &QPushButton::clicked, &dialog, [&] { pick(finish); });
    layout.addRow(&maskButton);
    layout.addRow(&silkButton);
    layout.addRow(&finishButton);
    QCheckBox airwires(tr("Mostrar ligacoes pendentes")), references(tr("Mostrar referencias")),
        padNumbers(tr("Mostrar numeros dos pads"));
    airwires.setChecked(board_->showAirwires);
    references.setChecked(board_->showReferences);
    padNumbers.setChecked(board_->showPadNumbers);
    layout.addRow(&airwires);
    layout.addRow(&references);
    layout.addRow(&padNumbers);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        board_->solderMask = mask.name().toStdString();
        board_->silkColor = silk.name().toStdString();
        board_->finishColor = finish.name().toStdString();
        board_->showAirwires = airwires.isChecked();
        board_->showReferences = references.isChecked();
        board_->showPadNumbers = padNumbers.isChecked();
        board_->width = static_cast<core::Coord>(w.value() * core::nmPerMm);
        board_->height = static_cast<core::Coord>(h.value() * core::nmPerMm);
        board_->thickness = static_cast<core::Coord>(t.value() * core::nmPerMm);
        board_->clearance = static_cast<core::Coord>(c.value() * core::nmPerMm);
        record();
        fitBoard();
    }
}

void BoardEditor::checkRules() {
    QString issues;
    for (const auto& issue : board_->checkRules())
        issues += "- " + text(issue) + "\n";
    QMessageBox::information(
        this, tr("Verificacao da placa"),
        issues.isEmpty() ? tr("Nenhum problema encontrado nas verificacoes disponiveis.") : issues);
}

void BoardEditor::exportModel() {
    auto path = chooseExportFile(this, tr("Exportar modelo 3D"), {}, "Wavefront OBJ (*.obj)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".obj", Qt::CaseInsensitive))
        path += ".obj";
    write(this, path, QByteArray::fromStdString(board_->exportObj()));
}

void BoardEditor::saveBoard() {
    auto path =
        QFileDialog::getSaveFileName(this, tr("Salvar PCB"), {}, "LevelCAD PCB (*.levelpcb)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".levelpcb", Qt::CaseInsensitive))
        path += ".levelpcb";
    write(this, path, QJsonDocument(boardToJson(*board_)).toJson());
}

void BoardEditor::loadBoard() {
    if (!isEnabled())
        return;
    auto path =
        QFileDialog::getOpenFileName(this, tr("Abrir PCB"), {}, "LevelCAD PCB (*.levelpcb)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Falha ao abrir"), file.errorString());
        return;
    }
    QJsonParseError parseError;
    if (file.size() > 64 * 1024 * 1024) {
        QMessageBox::warning(this, tr("Falha ao abrir"), tr("Arquivo muito grande"));
        return;
    }
    auto json = QJsonDocument::fromJson(file.readAll(), &parseError);
    QString error;
    if (parseError.error != QJsonParseError::NoError ||
        !boardFromJson(*board_, json.object(), &error)) {
        QMessageBox::warning(this, tr("Falha ao abrir"),
                             error.isEmpty() ? parseError.errorString() : error);
        return;
    }
    record();
    fitBoard();
}

void BoardEditor::refresh() {
    refreshing_ = true;
    presenceItems_.clear();
    preview_ = nullptr;
    scene_.clear();
    scene_.addRect(0, 0, double(board_->width) / 100000, double(board_->height) / 100000,
                   QPen(QColor("#e3c17c"), 2), QBrush(QColor(text(board_->solderMask))));
    if (board_->showAirwires)
        for (const auto& airwire : board_->airwires()) {
            auto* item = scene_.addLine(QLineF(scenePoint(airwire.from), scenePoint(airwire.to)),
                                        QPen(QColor("#d0d48b"), 0, Qt::DashLine));
            item->setAcceptedMouseButtons(Qt::NoButton);
        }
    for (const auto& track : board_->tracks) {
        auto layer = std::find_if(board_->layers.begin(), board_->layers.end(),
                                  [&](const auto& layer) { return layer.index == track.layer; });
        if (layer != board_->layers.end() && !layer->visible)
            continue;
        if (!netFilter_.isEmpty() && QString::fromStdString(track.net) != netFilter_)
            continue;
        auto* item = scene_.addLine(
            QLineF(scenePoint(track.from), scenePoint(track.to)),
            QPen(layer == board_->layers.end() ? QColor("#ff766e")
                                               : QColor(QString::fromStdString(layer->color)),
                 double(track.width) / 100000, Qt::SolidLine, Qt::RoundCap));
        item->setData(0, QVariant::fromValue<qulonglong>(track.id));
        item->setData(1, "track");
        item->setFlag(QGraphicsItem::ItemIsSelectable);
    }
    for (const auto& stroke : board_->outline)
        for (std::size_t i = 1; i < stroke.size(); ++i)
            scene_.addLine(QLineF(scenePoint(stroke[i - 1]), scenePoint(stroke[i])),
                           QPen(QColor("#e3c17c"), 2));
    for (const auto& footprint : board_->footprints) {
        auto* group = new QGraphicsItemGroup;
        scene_.addItem(group);
        group->addToGroup(
            scene_.addRect(-double(footprint.width) / 200000, -double(footprint.length) / 200000,
                           double(footprint.width) / 100000, double(footprint.length) / 100000,
                           QPen(QColor("#d9e5ee"), 1)));
        for (const auto& pad : footprint.pads) {
            auto p = scenePoint(pad.offset);
            const double radius = double(pad.diameter) / 200000;
            if (pad.shape == "rect" || pad.shape == "roundrect") {
                const auto w = double(pad.sizeX ? pad.sizeX : pad.diameter) / 100000,
                           h = double(pad.sizeY ? pad.sizeY : pad.diameter) / 100000;
                auto* copper = scene_.addRect(-w / 2, -h / 2, w, h, QPen(QColor("#edc879")),
                                              QBrush(QColor(text(board_->finishColor))));
                copper->setPos(p);
                copper->setRotation(pad.rotation);
                group->addToGroup(copper);
            } else
                group->addToGroup(scene_.addEllipse(p.x() - radius, p.y() - radius, 2 * radius,
                                                    2 * radius, QPen(QColor("#edc879")),
                                                    QBrush(QColor(text(board_->finishColor)))));
            const double hole = double(pad.drill) / 200000;
            group->addToGroup(scene_.addEllipse(p.x() - hole, p.y() - hole, 2 * hole, 2 * hole,
                                                QPen(Qt::NoPen), QBrush(QColor("#111b25"))));
            auto* number = scene_.addSimpleText(QString::number(pad.number));
            number->setVisible(board_->showPadNumbers);
            number->setBrush(QColor(text(board_->silkColor)));
            number->setScale(0.6);
            number->setPos(p + QPointF(7, -12));
            group->addToGroup(number);
        }
        auto* reference = scene_.addSimpleText(text(footprint.reference));
        reference->setVisible(board_->showReferences);
        reference->setBrush(QColor(text(board_->silkColor)));
        reference->setPos(-30, -double(footprint.length) / 200000 - 22);
        group->addToGroup(reference);
        group->setPos(scenePoint(footprint.position));
        group->setRotation(footprint.rotation);
        group->setData(0, QVariant::fromValue<qulonglong>(footprint.symbolId));
        group->setData(1, "footprint");
        group->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    }
    for (const auto& via : board_->vias) {
        auto p = scenePoint(via.position);
        const auto radius = double(via.diameter) / 200000;
        auto* item = scene_.addEllipse(p.x() - radius, p.y() - radius, 2 * radius, 2 * radius,
                                       QPen(QColor("#86dcdb")), QBrush(QColor("#4aa9b0")));
        item->setData(0, QVariant::fromValue<qulonglong>(via.id));
        item->setData(1, "via");
        item->setFlag(QGraphicsItem::ItemIsSelectable);
    }
    refreshing_ = false;
    redrawPresence();
}
} // namespace levelcad::app

namespace levelcad::app {
void BoardEditor::showPresence(const QJsonObject& presence) {
    presences_[presence["actor"].toString()] = presence;
    redrawPresence();
}

void BoardEditor::clearPresence() {
    presences_.clear();
    redrawPresence();
}

void BoardEditor::redrawPresence() {
    for (auto* item : presenceItems_)
        delete item;
    presenceItems_.clear();
    for (auto presence : presences_) {
        if (presence["workspace"].toString() != "pcb")
            continue;
        const auto color =
            QColor::fromHsv((presence["actor"].toString().toULongLong() * 83) % 360, 155, 245);
        for (auto id : presence["ids"].toArray()) {
            QRectF bounds;
            for (auto* object : scene_.items())
                if (object->data(0).toULongLong() == id.toString().toULongLong() &&
                    object->data(0).toULongLong())
                    bounds = bounds.united(object->sceneBoundingRect());
            if (bounds.isEmpty())
                continue;
            auto* outline =
                scene_.addRect(bounds.adjusted(-4, -4, 4, 4), QPen(color, 1.5, Qt::DashLine));
            outline->setZValue(100);
            outline->setAcceptedMouseButtons(Qt::NoButton);
            presenceItems_.push_back(outline);
            auto* label = scene_.addSimpleText(presence["name"].toString());
            label->setBrush(color);
            label->setPos(bounds.topLeft() - QPointF(0, 20));
            label->setZValue(101);
            label->setAcceptedMouseButtons(Qt::NoButton);
            presenceItems_.push_back(label);
        }
    }
}
} // namespace levelcad::app

namespace levelcad::app {
void BoardEditor::commitExternalChange() {
    record();
}

void BoardEditor::resetHistory() {
    history_ = {*board_};
    index_ = 0;
    routeStart_.reset();
}

void BoardEditor::assignFootprint(const core::Document& document) {
    if (!isEnabled())
        return;
    auto selection = scene_.selectedItems();
    if (selection.empty()) {
        QMessageBox::information(this, tr("Encapsulamento"), tr("Selecione um componente na PCB."));
        return;
    }
    bool ok = false;
    QStringList packages;
    for (const auto& package : pcb::footprintCatalog())
        packages.append(QString::fromStdString(package));
    auto root = officialLibraryRoot();
    if (!root.isEmpty()) {
        QDir library(QDir(root).filePath("kicad-footprints"));
        for (const auto& folder : library.entryList({"*.pretty"}, QDir::Dirs, QDir::Name)) {
            QDir category(library.filePath(folder));
            for (const auto& file : category.entryList({"*.kicad_mod"}, QDir::Files, QDir::Name))
                packages.append(QFileInfo(folder).completeBaseName() + ":" +
                                QFileInfo(file).completeBaseName());
        }
    }
    auto package = QInputDialog::getItem(this, tr("Encapsulamento"), tr("Pacote fisico"), packages,
                                         0, true, &ok);
    if (!ok)
        return;
    auto candidate = *board_;
    for (auto* item : selection) {
        if (item->data(1).toString() != "footprint")
            continue;
        auto id = item->data(0).toULongLong();
        auto symbol = std::find_if(document.symbols().begin(), document.symbols().end(),
                                   [&](const auto& s) { return s.id == id; });
        if (symbol == document.symbols().end())
            continue;
        auto prototype = *symbol;
        prototype.footprint = package.toStdString();
        auto mapped = officialFootprint(prototype);
        if (!mapped) {
            QMessageBox::warning(
                this, tr("Encapsulamento incompativel"),
                tr("Os numeros dos pads nao correspondem aos pinos deste simbolo."));
            return;
        }
        for (auto& existing : candidate.footprints)
            if (existing.symbolId == id) {
                for (auto& pad : mapped->pads)
                    for (const auto& old : existing.pads)
                        if (old.number == pad.number)
                            pad.net = old.net;
                existing.pads = mapped->pads;
                existing.source = mapped->source;
                existing.width = mapped->width;
                existing.length = mapped->length;
                existing.height = mapped->height;
                existing.mesh.clear();
                existing.texturePng.clear();
                assignAutomaticModel(existing);
            }
    }
    *board_ = std::move(candidate);
    record();
}

void BoardEditor::layerProperties() {
    if (!isEnabled())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Camadas de cobre"));
    dialog.resize(600, 480);
    QVBoxLayout layout(&dialog);
    QTableWidget table(static_cast<int>(board_->layers.size()), 4);
    table.setHorizontalHeaderLabels({tr("Indice"), tr("Nome"), tr("Cor"), tr("Visivel")});
    table.horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int i = 0; i < table.rowCount(); ++i) {
        const auto& layer = board_->layers[i];
        auto* index = new QTableWidgetItem(QString::number(layer.index));
        index->setFlags(index->flags() & ~Qt::ItemIsEditable);
        table.setItem(i, 0, index);
        table.setItem(i, 1, new QTableWidgetItem(text(layer.name)));
        table.setItem(i, 2, new QTableWidgetItem(text(layer.color)));
        auto* visible = new QTableWidgetItem;
        visible->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
        table.setItem(i, 3, visible);
    }
    connect(&table, &QTableWidget::cellDoubleClicked, &dialog, [&](int row, int column) {
        if (column == 2) {
            auto color = QColorDialog::getColor(QColor(table.item(row, 2)->text()), &dialog,
                                                tr("Cor da camada"));
            if (color.isValid())
                table.item(row, 2)->setText(color.name());
        }
    });
    layout.addWidget(&table);
    QLabel hint(tr("Edite os nomes. Duplo clique na cor abre a paleta. Indices preservam o "
                   "roteamento existente."));
    hint.setWordWrap(true);
    layout.addWidget(&hint);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto layers = board_->layers;
    for (int i = 0; i < table.rowCount(); ++i) {
        auto name = table.item(i, 1)->text().trimmed();
        QColor color(table.item(i, 2)->text());
        if (name.isEmpty() || !color.isValid()) {
            QMessageBox::warning(this, tr("Camada invalida"), tr("Informe nomes e cores validos."));
            return;
        }
        layers[i].name = name.toStdString();
        layers[i].color = color.name().toStdString();
        layers[i].visible = table.item(i, 3)->checkState() == Qt::Checked;
    }
    board_->layers = std::move(layers);
    record();
}
} // namespace levelcad::app