#pragma once
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonObject>
#include <QMap>
#include <functional>
#include <levelcad/core/Document.hpp>
#include <optional>

namespace levelcad::app {
class SchematicView final : public QGraphicsView {
  public:
    enum class Tool {
        Select,
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
        Wire,
        Pan,
        Library,
        Comment,
        NetLabel,
        Area
    };
    explicit SchematicView(QWidget* parent = nullptr);
    void setTool(Tool tool);
    void setLibrarySymbol(const core::Symbol& prototype);
    void setDocument(core::Document* document);
    void refresh();
    void fitDrawing();
    void deleteSelected();
    void rotateSelected();
    void duplicateSelected();
    void editSelected();
    void selectObject(std::uint64_t id);

    void setGridVisible(bool visible) {
        gridVisible_ = visible;
        viewport()->update();
    }

    void setSnapEnabled(bool enabled) {
        snapEnabled_ = enabled;
    }

    void exportPdf(const QString& path);

    void setLightTheme(bool enabled) {
        lightTheme_ = enabled;
        refresh();
        viewport()->update();
    }

    std::function<void()> onChanged;
    std::function<void(const std::vector<std::uint64_t>&)> onSelectionChanged;
    void showPresence(const QJsonObject& presence);
    void clearPresence();
    std::function<void(Tool)> onToolChanged;

    Tool tool() const {
        return tool_;
    }

  protected:
    void drawBackground(QPainter*, const QRectF&) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

  private:
    bool refreshing_{};
    QMap<QString, QJsonObject> presences_;
    std::vector<QGraphicsItem*> presenceItems_;
    void redrawPresence();
    QPointF snap(QPointF) const;
    static core::Point toCore(QPointF);
    void changed();
    std::vector<std::uint64_t> selectedIds() const;
    QGraphicsScene scene_;
    core::Document* document_{};
    Tool tool_{Tool::Select};
    core::Symbol librarySymbol_;
    std::optional<QPointF> wireStart_;
    QGraphicsPathItem* preview_{};
    bool gridVisible_{true}, snapEnabled_{true};
    bool lightTheme_{};
};
} // namespace levelcad::app
