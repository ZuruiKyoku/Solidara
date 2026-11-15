#include "solidara/viewport.hpp"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <QApplication>
#include <QImage>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QScreen>
#include <QTest>
#include <QWheelEvent>

#include <memory>

using solidara::Viewport;

class ViewportTests : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<Viewport> viewport_;
    TopoDS_Shape sourceShape_;

    QImage image() const
    {
        return viewport_->screen()->grabWindow(viewport_->winId()).toImage().convertToFormat(QImage::Format_RGB32);
    }

    static int changedPixels(const QImage& before, const QImage& after)
    {
        if (before.size() != after.size() || before.isNull()) return 0;
        int changed = 0;
        for (int y = 0; y < before.height(); ++y) {
            const auto* first = reinterpret_cast<const QRgb*>(before.constScanLine(y));
            const auto* second = reinterpret_cast<const QRgb*>(after.constScanLine(y));
            for (int x = 0; x < before.width(); ++x) {
                if (first[x] != second[x]) ++changed;
            }
        }
        return changed;
    }

    void drag(Qt::MouseButton button, const QPoint& from, const QPoint& to)
    {
        QTest::mousePress(viewport_.get(), button, Qt::NoModifier, from);
        QMouseEvent move(QEvent::MouseMove, to, viewport_->mapToGlobal(to),
            Qt::NoButton, button, Qt::NoModifier);
        QApplication::sendEvent(viewport_.get(), &move);
        QTest::mouseRelease(viewport_.get(), button, Qt::NoModifier, to);
    }

private slots:
    void init()
    {
        viewport_ = std::make_unique<Viewport>();
        viewport_->resize(800, 600);
        sourceShape_ = BRepPrimAPI_MakeBox(100, 60, 20).Shape();
        viewport_->setShape(sourceShape_);
        viewport_->show();
        QVERIFY(QTest::qWaitForWindowExposed(viewport_.get()));
        QVERIFY2(viewport_->initializationError().isEmpty(), qPrintable(viewport_->initializationError()));
        QVERIFY(!image().isNull());
    }

    void cleanup()
    {
        viewport_.reset();
    }

    void rendersSolidAndChangesDisplayModes()
    {
        const auto shaded = image();
        const auto center = shaded.pixelColor(shaded.width() / 2, shaded.height() / 2);
        QVERIFY2(center.red() > center.blue(), "The center should contain the copper solid, not the background");
        for (TopExp_Explorer faces(sourceShape_, TopAbs_FACE); faces.More(); faces.Next()) {
            TopLoc_Location location;
            QVERIFY2(BRep_Tool::Triangulation(TopoDS::Face(faces.Current()), location).IsNull(),
                "Rendering must not mutate the model's triangulation from the GUI thread");
        }

        viewport_->setWireframe(true);
        QTRY_VERIFY(changedPixels(shaded, image()) > 10000);
        viewport_->setWireframe(false);
        const auto orthographic = image();
        viewport_->setPerspective(true);
        QTRY_VERIFY(changedPixels(orthographic, image()) > 1000);
        viewport_->setPerspective(false);

        for (const auto* name : {"Top", "Bottom", "Front", "Back", "Left", "Right", "Isometric"}) {
            viewport_->setStandardView(QString::fromLatin1(name));
            QVERIFY(!image().isNull());
            QVERIFY(viewport_->initializationError().isEmpty());
        }
        const auto isometric = image();
        viewport_->setStandardView(QStringLiteral("Top"));
        QTRY_VERIFY(changedPixels(isometric, image()) > 10000);
    }

    void selectsRealBodyAndFaceAndClearsSelection()
    {
        QString feedback;
        viewport_->selectionChanged = [&feedback](const QString& message) { feedback = message; };
        QTest::mouseClick(viewport_.get(), Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
        QVERIFY(!viewport_->selectedShape().IsNull());
        QCOMPARE(viewport_->selectedShape().ShapeType(), TopAbs_SOLID);
        QVERIFY(feedback.contains(QStringLiteral("Body")));

        viewport_->setSelectionFilter(Viewport::SelectionFilter::Face);
        QVERIFY(viewport_->selectedShape().IsNull());
        QTest::mouseClick(viewport_.get(), Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
        QVERIFY(!viewport_->selectedShape().IsNull());
        QCOMPARE(viewport_->selectedShape().ShapeType(), TopAbs_FACE);
        QVERIFY(feedback.contains(QStringLiteral("Face")));

        QTest::keyClick(viewport_.get(), Qt::Key_Escape);
        QVERIFY(viewport_->selectedShape().IsNull());
        QVERIFY(feedback.contains(QStringLiteral("cleared")));
    }

    void orbitPanAndZoomChangeRenderedCamera()
    {
        const auto initial = image();
        drag(Qt::RightButton, {400, 300}, {490, 360});
        QTRY_VERIFY(changedPixels(initial, image()) > 10000);
        QVERIFY2(viewport_->selectedShape().IsNull(), "Orbit must not select geometry");

        const auto rotated = image();
        drag(Qt::MiddleButton, {400, 300}, {460, 330});
        QTRY_VERIFY(changedPixels(rotated, image()) > 10000);
        QVERIFY2(viewport_->selectedShape().IsNull(), "Pan must not select geometry");

        const auto panned = image();
        const QPoint center(400, 300);
        QWheelEvent wheel(center, viewport_->mapToGlobal(center), {}, QPoint(0, 120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(viewport_.get(), &wheel);
        QTRY_VERIFY(changedPixels(panned, image()) > 10000);

        const auto zoomed = image();
        QTest::keyClick(viewport_.get(), Qt::Key_F);
        QTRY_VERIFY(changedPixels(zoomed, image()) > 1000);
    }

    void orientationCubeChangesViewWithoutSelectingBody()
    {
        const auto initial = image();
        // The cube is anchored 72 physical pixels from the upper-right corner.
        const double scale = viewport_->devicePixelRatioF();
        const QPoint topFace(viewport_->width() - qRound(72 / scale), qRound(50 / scale));
        QTest::mouseClick(viewport_.get(), Qt::LeftButton, Qt::NoModifier, topFace);
        QTRY_VERIFY(changedPixels(initial, image()) > 10000);
        QVERIFY(viewport_->selectedShape().IsNull());
    }

    void dockedViewportFillsNativeWindowAfterResize()
    {
        viewport_->hide();
        QMainWindow host;
        auto* embedded = new Viewport(&host);
        host.setCentralWidget(embedded);
        auto* browser = new QDockWidget(QStringLiteral("Browser"), &host);
        auto* content = new QLabel(QStringLiteral("Model browser"), browser);
        content->setMinimumWidth(180);
        browser->setWidget(content);
        host.addDockWidget(Qt::LeftDockWidgetArea, browser);
        embedded->setShape(sourceShape_);
        host.resize(600, 400);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QVERIFY2(embedded->initializationError().isEmpty(), qPrintable(embedded->initializationError()));

        const auto filledExtent = [embedded] {
            const auto capture = embedded->screen()->grabWindow(embedded->winId()).toImage();
            if (capture.isNull()) return false;
            int blackPixels = 0;
            for (int y = 0; y < capture.height(); ++y)
                for (int x = 0; x < capture.width(); ++x) {
                    const auto color = capture.pixelColor(x, y);
                    if (color.red() < 3 && color.green() < 3 && color.blue() < 3) ++blackPixels;
                }
            // A stale OCCT framebuffer leaves large black regions in the resized native child.
            return blackPixels < capture.width() * capture.height() / 100;
        };
        QTRY_VERIFY(filledExtent());
        host.resize(1100, 700);
        QTRY_VERIFY(embedded->width() > 800);
        QTRY_VERIFY(filledExtent());
        browser->hide();
        QTRY_VERIFY(embedded->width() > 1000);
        QTRY_VERIFY(filledExtent());
        browser->show();
        host.resize(750, 500);
        QTRY_VERIFY(embedded->width() < 650);
        QTRY_VERIFY(filledExtent());
    }
};

QTEST_MAIN(ViewportTests)
#include "viewport_tests.moc"
