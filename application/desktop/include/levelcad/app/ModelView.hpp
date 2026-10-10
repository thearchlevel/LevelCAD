#pragma once
#include <QWidget>
#include <functional>
#include <levelcad/core/Document.hpp>
#include <levelcad/pcb/Board.hpp>

namespace levelcad::app {
class ModelView final : public QWidget {
  public:
    enum class Mode { Board, Schematic };
    explicit ModelView(Mode mode, pcb::Board* board, core::Document* document,
                       QWidget* parent = nullptr);
    void resetCamera();
    void editHeight();
    void exportModel();
    std::function<void()> onChanged;

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

  private:
    Mode mode_;
    pcb::Board* board_;
    core::Document* document_;
    double yaw_{-0.55}, pitch_{0.9}, zoom_{5};
    QPoint last_, pressed_;
    QPointF pan_;
    std::uint64_t selected_{};
    std::vector<std::pair<std::uint64_t, QPointF>> projectedComponents_;
};
} // namespace levelcad::app
