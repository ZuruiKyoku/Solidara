#include "solidara/viewport.hpp"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#ifdef _WIN32
#include <WNT_Window.hxx>
#else
#include <Xw_Window.hxx>
#endif

#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QGuiApplication>

namespace solidara {
namespace {

Quantity_Color rgb(int red, int green, int blue)
{
    return {red / 255.0, green / 255.0, blue / 255.0, Quantity_TOC_sRGB};
}

QString shapeDescription(const TopoDS_Shape& shape)
{
    switch (shape.ShapeType()) {
    case TopAbs_VERTEX: return QStringLiteral("Vertex selected");
    case TopAbs_EDGE: return QStringLiteral("Edge selected");
    case TopAbs_WIRE: return QStringLiteral("Profile selected");
    case TopAbs_FACE: return QStringLiteral("Face selected");
    case TopAbs_SOLID: return QStringLiteral("Body selected");
    default: return QStringLiteral("Geometry selected");
    }
}

} // namespace

struct Viewport::State {
    Handle(Aspect_DisplayConnection) display;
    Handle(OpenGl_GraphicDriver) driver;
    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(AIS_Shape) presentation;
    Handle(AIS_ViewCube) cube;
    TopoDS_Shape shape;
    QPoint previousPosition;
    QPoint pressPosition;
    Qt::MouseButton draggingButton = Qt::NoButton;
    SelectionFilter selectionFilter = SelectionFilter::Body;
    QString error;
    bool initialized = false;
    bool wireframe = false;
    bool perspective = false;
    bool hasFittedShape = false;
};

Viewport::Viewport(QWidget* parent)
    : QWidget(parent), state_(std::make_unique<State>())
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setToolTip(tr("3D viewport\nRight drag: orbit · Middle drag: pan · Wheel: zoom\n"
                  "Left click: select · Shift+click: add to selection\nF: fit model · Escape: clear selection"));
}

Viewport::~Viewport()
{
    if (!state_->context.IsNull()) {
        state_->context->RemoveAll(false);
    }
    if (!state_->view.IsNull()) {
        state_->view->Remove();
    }
}

QPaintEngine* Viewport::paintEngine() const
{
    return nullptr;
}

void Viewport::initialize()
{
    if (state_->initialized || !state_->error.isEmpty()) {
        return;
    }
    try {
        state_->display = new Aspect_DisplayConnection();
        state_->driver = new OpenGl_GraphicDriver(state_->display);
        state_->viewer = new V3d_Viewer(state_->driver);
        state_->viewer->SetDefaultLights();
        state_->viewer->SetLightOn();
        state_->context = new AIS_InteractiveContext(state_->viewer);
        state_->view = state_->viewer->CreateView();
#ifdef _WIN32
        Handle(WNT_Window) window = new WNT_Window(reinterpret_cast<Aspect_Handle>(winId()));
#else
        Handle(Xw_Window) window = new Xw_Window(state_->display, static_cast<Aspect_Drawable>(winId()));
#endif
        state_->view->SetWindow(window);
        if (!window->IsMapped()) {
            window->Map();
        }
        state_->view->SetBackgroundColor(rgb(28, 35, 41));
        state_->view->ChangeRenderingParams().NbMsaaSamples = 4;
        state_->view->Camera()->SetProjectionType(state_->perspective
            ? Graphic3d_Camera::Projection_Perspective : Graphic3d_Camera::Projection_Orthographic);
        state_->view->SetProj(V3d_XposYnegZpos);
        state_->view->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, rgb(168, 179, 185), 0.08, V3d_ZBUFFER);
        state_->context->HighlightStyle()->SetColor(rgb(83, 191, 194));
        state_->context->SelectionStyle()->SetColor(rgb(69, 167, 160));

        state_->cube = new AIS_ViewCube();
        state_->cube->SetSize(48.0);
        state_->cube->SetFontHeight(11.0);
        state_->cube->SetTransformPersistence(new Graphic3d_TransformPers(
            Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER, Graphic3d_Vec2i(72, 72)));
        state_->cube->SetColor(rgb(168, 179, 185));
        state_->context->Display(state_->cube, false);
        state_->initialized = true;
        displayShape();
        state_->view->MustBeResized();
        state_->view->Redraw();
    } catch (const Standard_Failure& failure) {
        state_->error = QString::fromUtf8(failure.GetMessageString());
        if (state_->error.isEmpty()) {
            state_->error = tr("The OpenGL rendering context could not be created.");
        }
        auto* message = new QLabel(tr("3D viewport unavailable\n%1\n\n"
            "Check the graphics driver and OpenGL support.").arg(state_->error), this);
        message->setWordWrap(true);
        message->setAlignment(Qt::AlignCenter);
        message->setStyleSheet(QStringLiteral("background: #1C2329; color: #DB726B; padding: 24px;"));
        message->setGeometry(rect());
        message->show();
    }
}

QString Viewport::initializationError() const
{
    return state_->error;
}

void Viewport::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    initialize();
}

void Viewport::paintEvent(QPaintEvent*)
{
    if (state_->initialized) {
        state_->view->MustBeResized();
        state_->view->Redraw();
    }
}

void Viewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (state_->initialized) {
        // Qt may deliver a child resize before flushing its native window geometry.
        // Resize OCCT after that flush so it does not retain the old framebuffer extent.
        QTimer::singleShot(0, this, [this] {
            QGuiApplication::sync();
            state_->view->MustBeResized();
            state_->view->Redraw();
        });
    }
    for (auto* label : findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
        label->setGeometry(rect());
    }
}

void Viewport::setShape(const TopoDS_Shape& shape)
{
    // AIS may attach triangulation to a shape. Keep that state separate from worker-owned models.
    state_->shape = shape.IsNull() ? TopoDS_Shape{} : BRepBuilderAPI_Copy(shape, true, false).Shape();
    if (state_->initialized) {
        displayShape();
    }
}

void Viewport::displayShape()
{
    if (!state_->presentation.IsNull()) {
        state_->context->Remove(state_->presentation, false);
        state_->presentation.Nullify();
    }
    if (!state_->shape.IsNull()) {
        state_->presentation = new AIS_Shape(state_->shape);
        state_->presentation->SetColor(rgb(212, 141, 75));
        state_->presentation->Attributes()->SetFaceBoundaryDraw(true);
        state_->presentation->Attributes()->SetFaceBoundaryAspect(
            new Prs3d_LineAspect(rgb(43, 49, 54), Aspect_TOL_SOLID, 1.0));
        state_->context->Display(state_->presentation, state_->wireframe ? AIS_WireFrame : AIS_Shaded, 0, false);
        updateSelectionFilter();
        if (!state_->hasFittedShape) {
            fitAll();
            state_->hasFittedShape = true;
        }
    } else {
        state_->hasFittedShape = false;
    }
    state_->view->Redraw();
    if (selectionChanged) {
        selectionChanged(tr("Selection cleared"));
    }
}

void Viewport::fitAll()
{
    if (state_->initialized) {
        state_->view->FitAll(0.12, false);
        state_->view->ZFitAll();
        state_->view->Redraw();
    }
}

void Viewport::setStandardView(const QString& name)
{
    if (!state_->initialized) {
        return;
    }
    const auto lowerName = name.toLower();
    V3d_TypeOfOrientation orientation = V3d_XposYnegZpos;
    if (lowerName == QStringLiteral("top")) orientation = V3d_Zpos;
    else if (lowerName == QStringLiteral("bottom")) orientation = V3d_Zneg;
    else if (lowerName == QStringLiteral("front")) orientation = V3d_Yneg;
    else if (lowerName == QStringLiteral("back")) orientation = V3d_Ypos;
    else if (lowerName == QStringLiteral("left")) orientation = V3d_Xneg;
    else if (lowerName == QStringLiteral("right")) orientation = V3d_Xpos;
    state_->view->SetProj(orientation);
    fitAll();
}

void Viewport::setWireframe(bool enabled)
{
    state_->wireframe = enabled;
    if (!state_->presentation.IsNull()) {
        state_->context->SetDisplayMode(state_->presentation, enabled ? AIS_WireFrame : AIS_Shaded, true);
    }
}

void Viewport::setPerspective(bool enabled)
{
    state_->perspective = enabled;
    if (state_->initialized) {
        state_->view->Camera()->SetProjectionType(enabled
            ? Graphic3d_Camera::Projection_Perspective : Graphic3d_Camera::Projection_Orthographic);
        state_->view->Redraw();
    }
}

void Viewport::setSelectionFilter(SelectionFilter filter)
{
    state_->selectionFilter = filter;
    updateSelectionFilter();
}

void Viewport::updateSelectionFilter()
{
    if (state_->presentation.IsNull()) {
        return;
    }
    int mode = 0;
    switch (state_->selectionFilter) {
    case SelectionFilter::Body: mode = 0; break;
    case SelectionFilter::Face: mode = AIS_Shape::SelectionMode(TopAbs_FACE); break;
    case SelectionFilter::Edge: mode = AIS_Shape::SelectionMode(TopAbs_EDGE); break;
    case SelectionFilter::Vertex: mode = AIS_Shape::SelectionMode(TopAbs_VERTEX); break;
    }
    state_->context->ClearSelected(false);
    state_->context->Deactivate(state_->presentation);
    state_->context->Activate(state_->presentation, mode);
}

QPoint Viewport::pixelPosition(const QPointF& position) const
{
    // OCCT consumes physical window pixels; Qt input positions use device-independent pixels.
    return (position * devicePixelRatioF()).toPoint();
}

void Viewport::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    if (!state_->initialized) {
        return;
    }
    const QPoint position = pixelPosition(event->position());
    state_->previousPosition = position;
    state_->pressPosition = position;
    state_->draggingButton = event->button();
    if (event->button() == Qt::RightButton) {
        state_->view->StartRotation(position.x(), position.y());
        setCursor(Qt::ClosedHandCursor);
    } else if (event->button() == Qt::MiddleButton) {
        setCursor(Qt::SizeAllCursor);
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
    if (!state_->initialized) {
        return;
    }
    const QPoint position = pixelPosition(event->position());
    if (event->buttons().testFlag(Qt::RightButton)) {
        state_->view->Rotation(position.x(), position.y());
    } else if (event->buttons().testFlag(Qt::MiddleButton)) {
        const auto delta = position - state_->previousPosition;
        state_->view->Pan(delta.x(), -delta.y());
    } else if (event->buttons() == Qt::NoButton) {
        state_->context->MoveTo(position.x(), position.y(), state_->view, true);
    }
    state_->previousPosition = position;
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    unsetCursor();
    if (!state_->initialized) {
        return;
    }
    const QPoint position = pixelPosition(event->position());
    if (event->button() == Qt::LeftButton && state_->draggingButton == Qt::LeftButton
        && (position - state_->pressPosition).manhattanLength() < 5 * devicePixelRatioF()) {
        state_->context->MoveTo(position.x(), position.y(), state_->view, false);
        const Handle(AIS_ViewCubeOwner) cubeOwner =
            Handle(AIS_ViewCubeOwner)::DownCast(state_->context->DetectedOwner());
        if (!cubeOwner.IsNull()) {
            state_->view->SetProj(cubeOwner->MainOrientation());
            fitAll();
        } else {
            state_->context->SelectDetected(event->modifiers().testFlag(Qt::ShiftModifier)
                ? AIS_SelectionScheme_XOR : AIS_SelectionScheme_Replace);
            state_->view->Redraw();
            if (selectionChanged) {
                const auto selected = selectedShape();
                selectionChanged(selected.IsNull() ? tr("Selection cleared") : shapeDescription(selected));
            }
        }
    }
    state_->draggingButton = Qt::NoButton;
}

TopoDS_Shape Viewport::selectedShape() const
{
    if (!state_->initialized) {
        return {};
    }
    state_->context->InitSelected();
    if (state_->context->MoreSelected() && state_->context->HasSelectedShape()) {
        return state_->context->SelectedShape();
    }
    return {};
}

void Viewport::wheelEvent(QWheelEvent* event)
{
    if (!state_->initialized) {
        return;
    }
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        return;
    }
    const QPoint position = pixelPosition(event->position());
    state_->view->StartZoomAtPoint(position.x(), position.y());
    state_->view->ZoomAtPoint(0, 0, delta / 6, 0);
    state_->view->Redraw();
    event->accept();
}

void Viewport::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_F) {
        fitAll();
    } else if (event->key() == Qt::Key_Escape && state_->initialized) {
        state_->context->ClearSelected(true);
        if (selectionChanged) {
            selectionChanged(tr("Selection cleared"));
        }
    } else {
        QWidget::keyPressEvent(event);
    }
}

} // namespace solidara
