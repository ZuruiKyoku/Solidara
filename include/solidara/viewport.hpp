#pragma once

#include <QWidget>
#include <TopoDS_Shape.hxx>

#include <functional>
#include <memory>

namespace solidara {

class Viewport final : public QWidget {
public:
    enum class SelectionFilter { Body, Face, Edge, Vertex };

    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;

    void setShape(const TopoDS_Shape& shape);
    void fitAll();
    void setStandardView(const QString& name);
    void setWireframe(bool enabled);
    void setPerspective(bool enabled);
    void setSelectionFilter(SelectionFilter filter);
    [[nodiscard]] QString initializationError() const;
    [[nodiscard]] TopoDS_Shape selectedShape() const;

    std::function<void(const QString&)> selectionChanged;

protected:
    QPaintEngine* paintEngine() const override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct State;
    std::unique_ptr<State> state_;
    void initialize();
    void displayShape();
    void updateSelectionFilter();
    QPoint pixelPosition(const QPointF& position) const;
};

} // namespace solidara
