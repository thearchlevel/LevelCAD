#pragma once
#include <QMap>
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
    enum class BoardDisplay { Assembled, Exploded, CopperOnly, Transparent };

    void setBoardDisplay(BoardDisplay mode) {
        display_ = mode;
        update();
    }

    void setExplodeSpacing(double mm) {
        explodeSpacing_ = mm;
        update();
    }

    void setComponentScale(double value) {
        componentScale_ = value;
        update();
    }

    void resetCamera();

    void setComponentsVisible(bool value) {
        componentsVisible_ = value;
        update();
    }

    void setCopperVisible(bool value) {
        copperVisible_ = value;
        update();
    }

    void setCameraPreset(int preset) {
        resetCamera();
        if (preset == 1) {
            yaw_ = 0;
            pitch_ = 0.05;
        }
        if (preset == 2) {
            yaw_ = 0;
            pitch_ = 3.09;
        }
        update();
    }

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
    BoardDisplay display_{BoardDisplay::Assembled};
    double explodeSpacing_{4}, componentScale_{1.8};
    bool componentsVisible_{true}, copperVisible_{true};
    QMap<qulonglong, pcb::Footprint> schematicAssets_;
    pcb::Board* board_;
    core::Document* document_;
    double yaw_{-0.55}, pitch_{0.9}, zoom_{5};
    QPoint last_, pressed_;
    QPointF pan_;
    std::uint64_t selected_{};
    std::vector<std::pair<std::uint64_t, QPointF>> projectedComponents_;
};
} // namespace levelcad::app
