#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsItemGroup>
#include <QGraphicsSimpleTextItem>
#include <QInputDialog>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPushButton>
#include <QSettings>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <levelcad/app/SchematicView.hpp>
#include <levelcad/connectivity/Connectivity.hpp>

namespace levelcad::app {
namespace {
constexpr double grid = 10.0, sceneUnitsPerMm = 10.0;

QPointF toScene(core::Point p) {
    return {double(p.x) / core::nmPerMm * sceneUnitsPerMm,
            double(p.y) / core::nmPerMm * sceneUnitsPerMm};
}
} // namespace

SchematicView::SchematicView(QWidget* p) : QGraphicsView(p) {
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
    setViewportUpdateMode(FullViewportUpdate);
    setTransformationAnchor(AnchorUnderMouse);
    scene_.setSceneRect(-10000, -10000, 20000, 20000);
    setBackgroundBrush(QColor("#141b28"));
    setMinimumSize(500, 350);
    setMouseTracking(true);
    setTool(Tool::Select);
}

void SchematicView::setTool(Tool t) {
    tool_ = t;
    wireStart_.reset();
    if (preview_) {
        scene_.removeItem(preview_);
        delete preview_;
        preview_ = nullptr;
    }
    setDragMode(t == Tool::Pan ? ScrollHandDrag : t == Tool::Select ? RubberBandDrag : NoDrag);
    setCursor(t == Tool::Pan      ? Qt::OpenHandCursor
              : t == Tool::Select ? Qt::ArrowCursor
                                  : Qt::CrossCursor);
    if (onToolChanged)
        onToolChanged(t);
}

void SchematicView::setDocument(core::Document* d) {
    document_ = d;
    refresh();
}

void SchematicView::setLibrarySymbol(const core::Symbol& prototype) {
    librarySymbol_ = prototype;
    setTool(Tool::Library);
}

QPointF SchematicView::snap(QPointF position) const {
    // Pinos da biblioteca usam passos como 2,54 mm. O fio precisa encaixar no pino, nÃ£o sÃ³ na
    // grade.
    if (tool_ == Tool::Wire && document_) {
        const auto tolerance = 12.0 / transform().m11();
        double closest = tolerance;
        std::optional<QPointF> pinPosition;
        for (const auto& symbol : document_->symbols()) {
            for (const auto& pin : core::symbolPins(symbol)) {
                const auto candidate = toScene(pin.position);
                const auto distance = QLineF(position, candidate).length();
                if (distance < closest) {
                    closest = distance;
                    pinPosition = candidate;
                }
            }
        }
        for (const auto& wire : document_->wires()) {
            for (const auto& endpoint : {wire.from, wire.to}) {
                const auto candidate = toScene(endpoint);
                const auto distance = QLineF(position, candidate).length();
                if (distance < closest) {
                    closest = distance;
                    pinPosition = candidate;
                }
            }
        }
        if (!pinPosition)
            for (const auto& wire : document_->wires()) {
                auto a = toScene(wire.from), b = toScene(wire.to);
                QPointF candidate;
                if (wire.from.x == wire.to.x)
                    candidate = {a.x(), std::clamp(position.y(), std::min(a.y(), b.y()),
                                                   std::max(a.y(), b.y()))};
                else if (wire.from.y == wire.to.y)
                    candidate = {
                        std::clamp(position.x(), std::min(a.x(), b.x()), std::max(a.x(), b.x())),
                        a.y()};
                else
                    continue;
                auto length = QLineF(position, candidate).length();
                if (length < closest) {
                    closest = length;
                    pinPosition = candidate;
                }
            }
        if (pinPosition)
            return *pinPosition;
    }
    return snapEnabled_ ? QPointF(std::round(position.x() / grid) * grid,
                                  std::round(position.y() / grid) * grid)
                        : position;
}

core::Point SchematicView::toCore(QPointF p) {
    return {static_cast<core::Coord>(std::llround(p.x() / sceneUnitsPerMm * core::nmPerMm)),
            static_cast<core::Coord>(std::llround(p.y() / sceneUnitsPerMm * core::nmPerMm))};
}

void SchematicView::drawBackground(QPainter* p, const QRectF& r) {
    p->fillRect(r, lightTheme_ ? QColor("#fbf8ee") : QColor("#141b28"));
    if (!gridVisible_)
        return;
    p->save();
    p->setPen(QPen(lightTheme_ ? QColor("#c8c9c0") : QColor("#303c50"), 0));
    const auto step = transform().m11() < 0.4 ? grid * 5 : grid;
    for (double x = std::floor(r.left() / step) * step; x <= r.right(); x += step)
        for (double y = std::floor(r.top() / step) * step; y <= r.bottom(); y += step)
            p->drawPoint(QPointF(x, y));
    p->restore();
}

void SchematicView::wheelEvent(QWheelEvent* e) {
    if (!e->angleDelta().y()) {
        e->accept();
        return;
    }
    auto f = e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
    if (transform().m11() * f >= 0.15 && transform().m11() * f <= 12)
        scale(f, f);
    e->accept();
}

void SchematicView::changed() {
    refresh();
    if (onChanged)
        onChanged();
}

void SchematicView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) {
        setTool(Tool::Select);
        e->accept();
        return;
    }
    if (e->button() != Qt::LeftButton || !document_ || tool_ == Tool::Select ||
        tool_ == Tool::Pan) {
        if (document_ && tool_ == Tool::Select && e->button() == Qt::LeftButton) {
            auto* hit = itemAt(e->pos());
            while (hit && !hit->data(0).toULongLong())
                hit = hit->parentItem();
            if (hit) {
                const auto connected = document_->connectionWires(hit->data(0).toULongLong());
                if (!connected.empty()) {
                    if (!hit->isSelected() && !(e->modifiers() & Qt::ControlModifier))
                        scene_.clearSelection();
                    for (auto* item : scene_.items())
                        if (std::find(connected.begin(), connected.end(),
                                      item->data(0).toULongLong()) != connected.end())
                            item->setSelected(true);
                }
            }
        }
        QGraphicsView::mousePressEvent(e);
        return;
    }
    auto p = snap(mapToScene(e->pos()));
    if (tool_ == Tool::Area) {
        auto id = document_->addNote(
            toCore(p), "Novo bloco",
            QSettings().value("profile/name", "Usuario").toString().toStdString());
        auto note = document_->notes().back();
        note.background = true;
        document_->updateNote(note);
        changed();
        selectObject(id);
        editSelected();
        setTool(Tool::Select);
    } else if (tool_ == Tool::Comment || tool_ == Tool::NetLabel) {
        bool ok = false;
        auto text = QInputDialog::getMultiLineText(this, tr("Comentario"),
                                                   tr("Texto do comentario"), {}, &ok);
        if (ok && !text.trimmed().isEmpty()) {
            document_->addNote(
                toCore(p), text.toStdString(),
                QSettings().value("profile/name", "Usuario").toString().toStdString(),
                tool_ == Tool::NetLabel);
            changed();
        }
    } else if (tool_ == Tool::Wire) {
        if (!wireStart_)
            wireStart_ = p;
        else if (*wireStart_ != p) {
            auto bend = QPointF(p.x(), wireStart_->y());
            if (*wireStart_ != bend)
                document_->addWire(toCore(*wireStart_), toCore(bend));
            if (bend != p)
                document_->addWire(toCore(bend), toCore(p));
            wireStart_ = p;
            changed();
        }
    } else {
        if (tool_ == Tool::Library)
            document_->addLibrarySymbol(librarySymbol_, toCore(p));
        else
            document_->addSymbol(static_cast<core::SymbolKind>(static_cast<int>(tool_) - 1),
                                 toCore(p));
        changed();
    }
    e->accept();
}

void SchematicView::mouseMoveEvent(QMouseEvent* e) {
    if (tool_ == Tool::Wire && wireStart_) {
        auto p = snap(mapToScene(e->pos()));
        QPainterPath path(*wireStart_);
        path.lineTo(p.x(), wireStart_->y());
        path.lineTo(p);
        if (!preview_) {
            preview_ = scene_.addPath(path, QPen(QColor("#76e5bd"), 1, Qt::DashLine));
            preview_->setZValue(10);
        } else
            preview_->setPath(path);
    }
    QGraphicsView::mouseMoveEvent(e);
}

// Commit snapped positions after the drag, keeping the document independent of Qt graphics items.
void SchematicView::mouseReleaseEvent(QMouseEvent* event) {
    QGraphicsView::mouseReleaseEvent(event);
    if (!document_ || tool_ != Tool::Select)
        return;

    struct Motion {
        std::uint64_t id;
        core::Point delta;
        bool symbol;
    };

    std::vector<Motion> motions;
    for (auto* item : scene_.selectedItems()) {
        const auto id = item->data(0).toULongLong();
        if (!id)
            continue;
        QPointF expected;
        bool symbol = false;
        for (const auto& component : document_->symbols())
            if (component.id == id) {
                expected = toScene(component.origin);
                symbol = true;
            }
        for (const auto& note : document_->notes())
            if (note.id == id) {
                expected = toScene(note.position);
                symbol = true;
            }
        const auto delta = snap(item->pos()) - expected;
        if (!delta.isNull())
            motions.push_back({id, toCore(delta), symbol});
    }
    std::vector<std::uint64_t> movedWires;
    for (const auto& motion : motions) {
        if (motion.symbol) {
            document_->moveObject(motion.id, motion.delta);
            continue;
        }
        if (std::find(movedWires.begin(), movedWires.end(), motion.id) != movedWires.end())
            continue;
        const auto connected = document_->connectionWires(motion.id);
        movedWires.insert(movedWires.end(), connected.begin(), connected.end());
        document_->moveConnection(motion.id, motion.delta);
    }
    if (!motions.empty())
        changed();
}

void SchematicView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (tool_ == Tool::Select) {
        QGraphicsView::mousePressEvent(e);
        editSelected();
    } else
        QGraphicsView::mouseDoubleClickEvent(e);
}

void SchematicView::contextMenuEvent(QContextMenuEvent* event) {
    auto* item = itemAt(event->pos());
    while (item && !item->data(0).toULongLong())
        item = item->parentItem();
    if (item && !item->isSelected()) {
        scene_.clearSelection();
        item->setSelected(true);
    }
    QMenu menu(this);
    if (!selectedIds().empty()) {
        menu.addAction(tr("Excluir selecao"), this, &SchematicView::deleteSelected);
        menu.addAction(tr("Propriedades..."), this, &SchematicView::editSelected);
        menu.addAction(tr("Girar 90 deg"), this, &SchematicView::rotateSelected);
        menu.addAction(tr("Duplicar selecao"), this, &SchematicView::duplicateSelected);
        menu.addSeparator();
    }
    menu.addAction(tr("Selecionar"), this, [this] { setTool(Tool::Select); });
    menu.addAction(tr("Ligacao"), this, [this] { setTool(Tool::Wire); });
    menu.addAction(tr("Area de fundo"), this, [this] { setTool(Tool::Area); });
    menu.addAction(tr("Ajustar desenho"), this, &SchematicView::fitDrawing);
    menu.exec(event->globalPos());
}

void SchematicView::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        setTool(Tool::Select);
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

std::vector<std::uint64_t> SchematicView::selectedIds() const {
    std::vector<std::uint64_t> ids;
    for (auto* item : scene_.selectedItems())
        if (auto id = item->data(0).toULongLong())
            ids.push_back(id);
    return ids;
}

void SchematicView::deleteSelected() {
    if (!isEnabled())
        return;
    if (!document_)
        return;
    auto ids = selectedIds();
    for (auto id : ids)
        document_->removeObject(id);
    if (!ids.empty())
        changed();
}

void SchematicView::rotateSelected() {
    if (!isEnabled())
        return;
    if (!document_)
        return;
    auto ids = selectedIds();
    bool edit = false;
    auto symbols = document_->symbols();
    for (auto s : symbols)
        for (auto id : ids)
            if (s.id == id) {
                s.rotation = (s.rotation + 90) % 360;
                document_->updateSymbol(s);
                edit = true;
            }
    if (edit)
        changed();
    for (auto id : ids)
        selectObject(id);
}

void SchematicView::duplicateSelected() {
    if (!isEnabled())
        return;
    if (!document_)
        return;
    auto ids = selectedIds();
    auto symbols = document_->symbols();
    auto wires = document_->wires();
    bool edited = false;
    for (auto id : ids) {
        for (auto s : symbols)
            if (s.id == id) {
                auto n = s.kind == core::SymbolKind::Library
                             ? document_->addLibrarySymbol(
                                   s, {s.origin.x + 2000000, s.origin.y + 2000000})
                             : document_->addSymbol(s.kind,
                                                    {s.origin.x + 2000000, s.origin.y + 2000000});
                auto copy = document_->symbols().back();
                copy.value = s.value;
                copy.rotation = s.rotation;
                document_->updateSymbol(copy);
                (void)n;
                edited = true;
            }
        for (auto w : wires)
            if (w.id == id) {
                document_->addWire({w.from.x + 2000000, w.from.y + 2000000},
                                   {w.to.x + 2000000, w.to.y + 2000000});
                edited = true;
            }
    }
    if (edited)
        changed();
}

void SchematicView::editSelected() {
    if (!isEnabled())
        return;
    if (!document_)
        return;
    auto ids = selectedIds();
    if (ids.size() != 1)
        return;
    for (auto note : document_->notes())
        if (note.id == ids[0]) {
            if (note.background) {
                QDialog dialog(this);
                dialog.setWindowTitle(tr("Area de fundo"));
                QFormLayout layout(&dialog);
                QLineEdit title(QString::fromStdString(note.text));
                QDoubleSpinBox width, height;
                for (auto* size : {&width, &height}) {
                    size->setRange(1, 1000);
                    size->setSuffix(" mm");
                }
                width.setValue(note.widthMm);
                height.setValue(note.heightMm);
                QColor color(QString::fromStdString(note.color));
                QPushButton choose(tr("Escolher cor"));
                connect(&choose, &QPushButton::clicked, &dialog, [&] {
                    auto selected = QColorDialog::getColor(color, &dialog, tr("Cor da area"));
                    if (selected.isValid())
                        color = selected;
                });
                layout.addRow(tr("Titulo"), &title);
                layout.addRow(tr("Largura"), &width);
                layout.addRow(tr("Altura"), &height);
                layout.addRow(tr("Cor"), &choose);
                QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
                layout.addRow(&buttons);
                connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
                connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                if (dialog.exec() == QDialog::Accepted && !title.text().trimmed().isEmpty()) {
                    note.text = title.text().trimmed().toStdString();
                    note.widthMm = width.value();
                    note.heightMm = height.value();
                    note.color = color.name().toStdString();
                    document_->updateNote(note);
                    changed();
                }
                return;
            }
            bool ok = false;
            auto text =
                QInputDialog::getMultiLineText(this, tr("Comentario"), tr("Texto do comentario"),
                                               QString::fromStdString(note.text), &ok);
            if (ok && !text.trimmed().isEmpty()) {
                note.text = text.toStdString();
                document_->updateNote(note);
                changed();
            }
            return;
        }
    for (auto s : document_->symbols())
        if (s.id == ids[0]) {
            QDialog dialog(this);
            dialog.setWindowTitle(tr("Propriedades do componente"));
            QFormLayout layout(&dialog);
            QLineEdit ref(QString::fromStdString(s.reference)),
                value(QString::fromStdString(s.value));
            QComboBox rotation;
            rotation.addItems({"0A deg", "90 deg", "180A deg", "270A deg"});
            rotation.setCurrentIndex((s.rotation % 360 + 360) % 360 / 90);
            if (!s.libraryId.empty()) {
                layout.addRow(tr("Biblioteca"),
                              new QLabel(QString::fromStdString(s.libraryId), &dialog));
                layout.addRow(tr("Footprint sugerido"),
                              new QLabel(QString::fromStdString(s.footprint), &dialog));
            }
            layout.addRow(tr("Referencia"), &ref);
            layout.addRow(tr("Valor / modelo"), &value);
            layout.addRow(tr("Rotacao"), &rotation);
            QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            layout.addRow(&buttons);
            connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted)
                return;
            s.reference = ref.text().trimmed().toStdString();
            s.value = value.text().trimmed().toStdString();
            s.rotation = rotation.currentIndex() * 90;
            try {
                document_->updateSymbol(s);
                changed();
                selectObject(s.id);
            } catch (const std::exception& ex) {
                QMessageBox::warning(this, tr("Propriedades invalidas"),
                                     QString::fromUtf8(ex.what()));
            }
            return;
        }
}

void SchematicView::selectObject(std::uint64_t id) {
    for (auto* item : scene_.items())
        if (item->data(0).toULongLong() == id) {
            item->setSelected(true);
            ensureVisible(item);
            return;
        }
}

void SchematicView::fitDrawing() {
    if (scene_.items().isEmpty()) {
        resetTransform();
        centerOn(0, 0);
    } else {
        fitInView(scene_.itemsBoundingRect().adjusted(-60, -60, 60, 60), Qt::KeepAspectRatio);
        if (transform().m11() > 4) {
            resetTransform();
            scale(4, 4);
            centerOn(scene_.itemsBoundingRect().center());
        }
    }
}

void SchematicView::exportPdf(const QString& path) {
    QPdfWriter pdf(path);
    pdf.setTitle("LevelCAD - Esquematico");
    pdf.setCreator("Wenderson Dias | The Archlevel Foundation");
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setResolution(144);
    QPainter painter(&pdf);
    painter.fillRect(QRectF(0, 0, pdf.width(), pdf.height()), QColor("#141b28"));
    scene_.clearSelection();
    scene_.render(&painter, QRectF(0, 0, pdf.width(), pdf.height()),
                  scene_.itemsBoundingRect().adjusted(-40, -40, 40, 40));
}

void SchematicView::refresh() {
    refreshing_ = true;
    presenceItems_.clear();
    preview_ = nullptr;
    scene_.clear();
    if (!document_) {
        refreshing_ = false;
        return;
    }
    const QPen stroke(lightTheme_ ? QColor("#9d2b32") : QColor("#f2d184"), 2),
        wirePen(lightTheme_ ? QColor("#218a47") : QColor("#72deba"), 2);
    for (const auto& w : document_->wires()) {
        auto* item = scene_.addLine(QLineF(toScene(w.from), toScene(w.to)), wirePen);
        item->setData(0, QVariant::fromValue<qulonglong>(w.id));
        item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        item->setZValue(-1);
    }
    for (const auto& note : document_->notes()) {
        if (note.background) {
            // Keep the block behind the circuit so selecting a component still works normally.
            QColor border(QString::fromStdString(note.color)), fill = border;
            fill.setAlpha(18);
            auto* area = scene_.addRect(0, 0, note.widthMm * sceneUnitsPerMm,
                                        note.heightMm * sceneUnitsPerMm,
                                        QPen(border, 1, Qt::DashLine), QBrush(fill));
            area->setPos(toScene(note.position));
            area->setZValue(-20);
            area->setData(0, QVariant::fromValue<qulonglong>(note.id));
            area->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
            auto* title = new QGraphicsSimpleTextItem(QString::fromStdString(note.text), area);
            title->setBrush(border);
            title->setPos(8, 6);
            title->setAcceptedMouseButtons(Qt::NoButton);
            continue;
        }
        auto* item = scene_.addText(
            QString::fromStdString(note.electrical ? "[" + note.text + "]" : note.text));
        item->setDefaultTextColor(lightTheme_ ? QColor("#344777") : QColor("#b8caff"));
        item->setPos(toScene(note.position));
        item->setTextWidth(250);
        item->setToolTip(tr("Autor: ") + QString::fromStdString(note.author));
        item->setData(0, QVariant::fromValue<qulonglong>(note.id));
        item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    }
    for (const auto& s : document_->symbols()) {
        auto* group = new QGraphicsItemGroup;
        scene_.addItem(group);
        auto line = [&](qreal a, qreal b, qreal c, qreal d) {
            group->addToGroup(scene_.addLine(a, b, c, d, stroke));
        };
        auto rect = [&](qreal x, qreal y, qreal w, qreal h) {
            group->addToGroup(scene_.addRect(x, y, w, h, stroke));
        };
        auto circle = [&](qreal x, qreal y, qreal w, qreal h) {
            group->addToGroup(scene_.addEllipse(x, y, w, h, stroke));
        };
        switch (s.kind) {
        case core::SymbolKind::Library:
            for (const auto& strokePoints : s.strokes) {
                if (strokePoints.size() < 2)
                    continue;
                QPainterPath path(toScene(strokePoints[0]));
                for (std::size_t point = 1; point < strokePoints.size(); ++point)
                    path.lineTo(toScene(strokePoints[point]));
                group->addToGroup(scene_.addPath(path, stroke));
            }
            for (const auto& pin : s.pinDefinitions) {
                auto* text = scene_.addSimpleText(QString::number(pin.number) + " " +
                                                  QString::fromStdString(pin.name));
                text->setBrush(lightTheme_ ? QColor("#175969") : QColor("#a8bdd8"));
                text->setScale(0.65);
                text->setPos(toScene(pin.position) + QPointF(4, -12));
                group->addToGroup(text);
            }
            break;
        case core::SymbolKind::Resistor:
            line(-30, 0, -20, 0);
            line(20, 0, 30, 0);
            rect(-20, -9, 40, 18);
            break;
        case core::SymbolKind::Capacitor:
            line(-30, 0, -6, 0);
            line(6, 0, 30, 0);
            line(-6, -16, -6, 16);
            line(6, -16, 6, 16);
            break;
        case core::SymbolKind::Ground:
            line(0, -30, 0, 0);
            line(-18, 0, 18, 0);
            line(-12, 6, 12, 6);
            line(-6, 12, 6, 12);
            break;
        case core::SymbolKind::Inductor:
            line(-30, 0, -20, 0);
            line(20, 0, 30, 0);
            for (int i = 0; i < 4; ++i) {
                QPainterPath p;
                p.moveTo(-20 + i * 10, 0);
                p.cubicTo(-20 + i * 10, -18, -10 + i * 10, -18, -10 + i * 10, 0);
                group->addToGroup(scene_.addPath(p, stroke));
            }
            break;
        case core::SymbolKind::Diode:
        case core::SymbolKind::LED:
            line(-30, 0, -12, 0);
            line(12, 0, 30, 0);
            line(-12, -12, -12, 12);
            line(-12, -12, 12, 0);
            line(-12, 12, 12, 0);
            line(12, -12, 12, 12);
            if (s.kind == core::SymbolKind::LED) {
                line(0, -18, 12, -30);
                line(12, -30, 6, -29);
                line(12, -30, 11, -24);
                line(10, -15, 22, -27);
            }
            break;
        case core::SymbolKind::VoltageSource:
        case core::SymbolKind::CurrentSource:
            line(-30, 0, -18, 0);
            line(18, 0, 30, 0);
            circle(-18, -18, 36, 36);
            if (s.kind == core::SymbolKind::VoltageSource) {
                line(-12, 0, -4, 0);
                line(-8, -4, -8, 4);
                line(4, 0, 12, 0);
            } else {
                line(-10, 0, 10, 0);
                line(10, 0, 4, -5);
                line(10, 0, 4, 5);
            }
            break;
        case core::SymbolKind::Switch:
            line(-30, 0, -12, 0);
            line(12, 0, 30, 0);
            circle(-14, -2, 4, 4);
            circle(10, -2, 4, 4);
            line(-12, 0, 10, -12);
            break;
        case core::SymbolKind::Connector:
            rect(-15, -14, 30, 28);
            line(-30, 0, -15, 0);
            line(15, 0, 30, 0);
            circle(-9, -3, 6, 6);
            circle(3, -3, 6, 6);
            break;
        }
        auto* label = scene_.addSimpleText(QString::fromStdString(s.reference) + "  " +
                                           QString::fromStdString(s.value));
        label->setBrush(lightTheme_ ? QColor("#223143") : QColor("#e5edfa"));
        label->setPos(-30,
                      s.kind == core::SymbolKind::Library ? group->boundingRect().top() - 28 : -48);
        group->addToGroup(label);
        group->setPos(toScene(s.origin));
        group->setRotation(s.rotation);
        group->setData(0, QVariant::fromValue<qulonglong>(s.id));
        group->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        for (auto pin : core::symbolPins(s)) {
            auto p = toScene(pin.position);
            auto* dot = scene_.addEllipse(p.x() - 2, p.y() - 2, 4, 4, QPen(QColor("#72deba")),
                                          QColor("#141b28"));
            dot->setAcceptedMouseButtons(Qt::NoButton);
        }
    }
    // Only a wire endpoint creates a junction; a simple crossing stays visually separate.
    std::vector<core::Point> junctions;
    for (const auto& wire : document_->wires()) {
        for (const auto& point : {wire.from, wire.to}) {
            int branches = 0;
            for (const auto& other : document_->wires()) {
                if (other.from == point || other.to == point)
                    ++branches;
                else if (connectivity::pointOnWire(point, other))
                    branches += 2;
            }
            if (branches >= 3 &&
                std::find(junctions.begin(), junctions.end(), point) == junctions.end()) {
                junctions.push_back(point);
                const auto position = toScene(point);
                auto* dot = scene_.addEllipse(position.x() - 3, position.y() - 3, 6, 6, wirePen,
                                              QColor("#72deba"));
                dot->setAcceptedMouseButtons(Qt::NoButton);
            }
        }
    }
    refreshing_ = false;
    redrawPresence();
}
} // namespace levelcad::app

namespace levelcad::app {
void SchematicView::showPresence(const QJsonObject& presence) {
    presences_[presence["actor"].toString()] = presence;
    redrawPresence();
}

void SchematicView::clearPresence() {
    presences_.clear();
    redrawPresence();
}

void SchematicView::redrawPresence() {
    for (auto* item : presenceItems_)
        delete item;
    presenceItems_.clear();
    for (auto presence : presences_) {
        if (presence["workspace"].toString() != "schematic")
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
