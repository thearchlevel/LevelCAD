#pragma once
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonObject>
#include <QMap>
#include <functional>
#include <levelcad/pcb/Board.hpp>
#include <optional>

namespace levelcad::app {
class BoardEditor final : public QGraphicsView {
  public:
    explicit BoardEditor(pcb::Board* board, QWidget* parent = nullptr);
    void refresh();
    void commitExternalChange();
    void resetHistory();
    void layerProperties();
    void assignFootprint(const core::Document& document);
    void fitBoard();
    void setRouting(bool enabled);
    void setLayer(int layer);

    void setNetFilter(const QString& net) {
        netFilter_ = net;
        refresh();
    }

    void setTraceWidth(core::Coord width);
    void deleteSelected();
    void rotateSelected();
    void editSelected();
    void boardProperties();
    void saveBoard();
    void loadBoard();
    void exportModel();
    void checkRules();
    void undo();
    void redo();
    std::function<void()> onChanged;
    std::function<void(const std::vector<std::uint64_t>&)> onSelectionChanged;
    void showPresence(const QJsonObject& presence);
    void clearPresence();

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
    core::Point snap(QPointF position) const;
    std::optional<std::pair<core::Point, std::string>> nearestPad(QPointF position) const;
    void record();
    pcb::Board* board_;
    QGraphicsScene scene_;
    bool routing_{};
    QString netFilter_;
    int layer_{};
    core::Coord traceWidth_{350000};
    std::optional<core::Point> routeStart_;
    std::string routeNet_;
    QGraphicsPathItem* preview_{};
    std::vector<pcb::Board> history_;
    int index_{};
};
} // namespace levelcad::app
