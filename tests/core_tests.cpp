#include <solidara/model.hpp>

#include <BRepCheck_Analyzer.hxx>
#include <TopExp_Explorer.hxx>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

using namespace solidara;

namespace {

Document plateDocument(bool hole = false, bool fillet = false) {
    Document document;
    document.name = "Maker plate";
    document.parameters = {{"plate_width", "100 mm"}, {"plate_depth", "60 mm"}, {"thickness", "20 mm"}};
    Feature rectangle = makeFeature(FeatureKind::Rectangle, "Plate profile");
    rectangle.values["width"] = "plate_width";
    rectangle.values["depth"] = "plate_depth";
    document.features.append(rectangle);
    Feature extrusion = makeFeature(FeatureKind::Extrude, "Plate extrusion");
    extrusion.inputs = {rectangle.id};
    extrusion.values["depth"] = "thickness";
    document.features.append(extrusion);
    if (hole) {
        Feature circle = makeFeature(FeatureKind::Circle, "Hole profile");
        circle.inputs = {extrusion.id};
        circle.values["x"] = "plate_width / 2";
        circle.values["y"] = "plate_depth / 2";
        document.features.append(circle);
        Feature cut = makeFeature(FeatureKind::Cut, "Mounting hole");
        cut.inputs = {extrusion.id, circle.id};
        document.features.append(cut);
    }
    if (fillet) {
        Feature round = makeFeature(FeatureKind::Fillet);
        round.inputs = {document.features.last().id};
        document.features.append(round);
    }
    return document;
}

void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) throw std::runtime_error("Test fixture write failed.");
}

} // namespace

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void expressions_data() {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<double>("expected");
        QTest::newRow("millimeters") << "2.4 mm" << 2.4;
        QTest::newRow("centimeters") << "12 cm" << 120.0;
        QTest::newRow("meters") << "0.12 m" << 120.0;
        QTest::newRow("inches") << "2 in" << 50.8;
        QTest::newRow("precedence") << "2 + 3 * 4" << 14.0;
        QTest::newRow("grouping") << "(2 + 3) * 4" << 20.0;
        QTest::newRow("unary") << "-(-5)" << 5.0;
        QTest::newRow("scientific") << "2e-3 m" << 2.0;
        QTest::newRow("mixed-units") << "2 cm + 5 mm" << 25.0;
        QTest::newRow("dimensional-division") << "(2 cm * 5 mm) / 1 mm" << 100.0;
    }

    void expressions() {
        QFETCH(QString, expression);
        QFETCH(double, expected);
        QVERIFY(std::abs(evaluateLength(expression) - expected) < 1e-9);
    }

    void invalidExpressions_data() {
        QTest::addColumn<QString>("expression");
        for (const QString& expression : {QString(), QString("1/0"), QString("1 mm + 1"), QString("1 mm * 2 mm"),
                                          QString("(1+2"), QString("3 foo"), QString("nan"), QString("2 ** 3"),
                                          QString("1..2"), QString("1e999"), QString("unknown")})
            QTest::newRow(expression.isEmpty() ? "empty" : expression.toUtf8().constData()) << expression;
    }

    void invalidExpressions() {
        QFETCH(QString, expression);
        QVERIFY_EXCEPTION_THROWN(evaluateLength(expression), std::runtime_error);
    }

    void namedParametersAndCycles() {
        const QMap<QString, QString> parameters{{"width", "100 mm"}, {"spacing", "width / 2"}};
        QCOMPARE(evaluateLength("spacing + 2 mm", parameters), 52.0);
        const QMap<QString, QString> circular{{"a", "b + 1 mm"}, {"b", "a"}};
        QVERIFY_EXCEPTION_THROWN(evaluateLength("a", circular), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(evaluateLength(QString(100, '(') + "1" + QString(100, ')')), std::runtime_error);
    }

    void repeatedParameterReferencesUseCachedValues() {
        QMap<QString, QString> parameters{{"p0", "1 mm"}};
        for (int index = 1; index <= 35; ++index)
            parameters.insert(QString("p%1").arg(index), QString("p%1 + p%1").arg(index - 1));
        QCOMPARE(evaluateLength("p35", parameters), std::pow(2.0, 35));
    }

    void rectangleAndCircleAreClosedProfiles() {
        Model model;
        Document document;
        Feature rectangle = makeFeature(FeatureKind::Rectangle);
        Feature circle = makeFeature(FeatureKind::Circle);
        document.features = {rectangle, circle};
        model.apply(document);
        QVERIFY(BRepCheck_Analyzer(model.shapeFor(rectangle.id)).IsValid());
        QCOMPARE(measure(model.shapeFor(rectangle.id)).area, 6000.0);
        QVERIFY(std::abs(measure(model.shapeFor(circle.id)).area - std::numbers::pi * 36) < 1e-7);
        QCOMPARE(measure(model.shapeFor(circle.id)).volume, 0.0);
        int edges = 0;
        for (TopExp_Explorer explorer(model.shapeFor(rectangle.id), TopAbs_EDGE); explorer.More(); explorer.Next()) ++edges;
        QCOMPARE(edges, 4);
        QVERIFY(model.resultShape().IsNull());
    }

    void primitives() {
        Model model;
        Document document;
        document.features.append(makeFeature(FeatureKind::Box));
        model.apply(document);
        QVERIFY(isValidSolid(model.resultShape()));
        QVERIFY(std::abs(measure(model.resultShape()).volume - 120000) < 1e-6);
        document.features.append(makeFeature(FeatureKind::Cylinder));
        model.apply(document);
        QVERIFY(isValidSolid(model.resultShape()));
        QVERIFY(std::abs(measure(model.resultShape()).volume - std::numbers::pi * 100 * 20) < 1e-6);
    }

    void measurementsRespectTranslatedOrigins() {
        Model model;
        Document document;
        auto box = makeFeature(FeatureKind::Box);
        box.values["x"] = "-35";
        box.values["y"] = "80";
        box.values["z"] = "12";
        document.features.append(box);
        model.apply(document);
        const Measurements dimensions = measure(model.resultShape());
        QVERIFY(std::abs(dimensions.xMin + 35) < 1e-6);
        QVERIFY(std::abs(dimensions.yMin - 80) < 1e-6);
        QVERIFY(std::abs(dimensions.zMin - 12) < 1e-6);
        QVERIFY(std::abs(dimensions.width - 100) < 1e-6);
    }

    void extrusionAndThroughCut() {
        Model model;
        model.apply(plateDocument());
        const auto plate = measure(model.resultShape());
        QVERIFY(std::abs(plate.volume - 120000) < 1e-6);
        QVERIFY(std::abs(plate.width - 100) < 1e-6);
        QVERIFY(std::abs(plate.depth - 60) < 1e-6);
        QVERIFY(std::abs(plate.height - 20) < 1e-6);
        model.apply(plateDocument(true));
        const auto hole = measure(model.resultShape());
        QVERIFY(isValidSolid(model.resultShape()));
        QVERIFY(std::abs(hole.volume - (120000 - std::numbers::pi * 36 * 20)) < 1e-6);
    }

    void filletAndRegeneration() {
        Model model;
        Document document = plateDocument(true, true);
        model.apply(document);
        const double initialVolume = measure(model.resultShape()).volume;
        QVERIFY(isValidSolid(model.resultShape()));
        QVERIFY(initialVolume < 120000 - std::numbers::pi * 36 * 20);
        document.parameters["plate_width"] = "120 mm";
        model.apply(document);
        const auto changed = measure(model.resultShape());
        QVERIFY(std::abs(changed.width - 120) < 1e-5);
        QVERIFY(changed.volume > initialVolume);
        const auto circle = measure(model.shapeFor(document.features[2].id));
        QVERIFY(std::abs(circle.width - 12) < 1e-6);
    }

    void editsAndUndoRedoAreTransactional() {
        Model model;
        Document document = plateDocument(true);
        model.apply(document);
        const double initialVolume = measure(model.resultShape()).volume;
        document.parameters["plate_width"] = "120 mm";
        model.apply(document);
        const double expandedVolume = measure(model.resultShape()).volume;
        QVERIFY(expandedVolume > initialVolume);
        QVERIFY(model.canUndo());
        model.undo();
        QVERIFY(std::abs(measure(model.resultShape()).volume - initialVolume) < 1e-6);
        QVERIFY(model.canRedo());
        model.redo();
        QVERIFY(std::abs(measure(model.resultShape()).volume - expandedVolume) < 1e-6);
        const TopoDS_Shape previous = model.resultShape();
        document.parameters["plate_width"] = "-1 mm";
        QVERIFY_EXCEPTION_THROWN(model.apply(document), std::runtime_error);
        QVERIFY(previous.IsSame(model.resultShape()));
        QCOMPARE(model.document().parameters.value("plate_width"), QString("120 mm"));
        model.undo();
        document = model.document();
        document.name = "Renamed";
        model.apply(document);
        QVERIFY(!model.canRedo());
    }

    void failedGeometryPreservesDocument() {
        Model model;
        Document document = plateDocument();
        model.apply(document);
        const TopoDS_Shape previous = model.resultShape();
        Feature circle = makeFeature(FeatureKind::Circle);
        circle.values["x"] = "1000";
        document.features.append(circle);
        Feature cut = makeFeature(FeatureKind::Cut);
        cut.inputs = {document.features[1].id, circle.id};
        document.features.append(cut);
        QVERIFY_EXCEPTION_THROWN(model.apply(document), std::runtime_error);
        QVERIFY(previous.IsSame(model.resultShape()));
        QCOMPARE(model.document().features.size(), 2);
        document = model.document();
        Feature fillet = makeFeature(FeatureKind::Fillet);
        fillet.inputs = {document.features.last().id};
        fillet.values["radius"] = "10000";
        document.features.append(fillet);
        QVERIFY_EXCEPTION_THROWN(model.apply(document), std::runtime_error);
        QVERIFY(previous.IsSame(model.resultShape()));
    }

    void graphRejectsMissingCircularAndWrongInputs() {
        Model model;
        Document document = plateDocument();
        document.features[1].inputs = {document.features[1].id};
        QVERIFY_EXCEPTION_THROWN(model.apply(document), std::runtime_error);
        document = plateDocument();
        document.features[1].inputs = {"missing"};
        QVERIFY_EXCEPTION_THROWN(model.apply(document), std::runtime_error);
        document = plateDocument();
        document.features[0] = makeFeature(FeatureKind::Box);
        document.features[1].inputs = {document.features[0].id};
        QVERIFY_EXCEPTION_THROWN(model.apply(document), std::runtime_error);
        document = plateDocument();
        document.features[1].id = document.features[0].id;
        QVERIFY_EXCEPTION_THROWN(model.apply(document), std::runtime_error);
        QVERIFY(model.document().features.isEmpty());
    }

    void suppressionAndCache() {
        Model model;
        Document document = plateDocument(true);
        model.apply(document);
        const TopoDS_Shape originalProfile = model.shapeFor(document.features[0].id);
        document.features.last().suppressed = true;
        model.apply(document);
        QCOMPARE(model.resultId(), document.features[1].id);
        QVERIFY(originalProfile.IsSame(model.shapeFor(document.features[0].id)));
        document.features[0].suppressed = true;
        QVERIFY_EXCEPTION_THROWN(model.apply(document), std::runtime_error);
        QVERIFY(!model.document().features[0].suppressed);
        document = model.document();
        document.features[0].name = "Renamed profile";
        model.apply(document);
        QVERIFY(originalProfile.IsSame(model.shapeFor(document.features[0].id)));
    }

    void projectRoundTripRemainsEditable() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath("maker.solidara");
        Model model;
        model.apply(plateDocument(true, true));
        model.save(path);
        Model loaded;
        loaded.load(path);
        QCOMPARE(loaded.document().name, model.document().name);
        QCOMPARE(loaded.document().features.size(), 5);
        QCOMPARE(loaded.document().features[2].id, model.document().features[2].id);
        QCOMPARE(loaded.document().features[2].inputs, model.document().features[2].inputs);
        QCOMPARE(loaded.document().features[2].values, model.document().features[2].values);
        QVERIFY(std::abs(measure(loaded.resultShape()).volume - measure(model.resultShape()).volume) < 1e-6);
        QVERIFY(!loaded.canUndo());
        Document document = loaded.document();
        document.parameters["plate_width"] = "120 mm";
        loaded.apply(document);
        QVERIFY(std::abs(measure(loaded.resultShape()).width - 120) < 1e-5);
        loaded.undo();
        QVERIFY(std::abs(measure(loaded.resultShape()).width - 100) < 1e-5);
    }

    void badProjectDoesNotReplaceCurrentState() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath("bad.solidara");
        Model model;
        model.apply(plateDocument());
        const TopoDS_Shape previous = model.resultShape();
        writeBytes(path, "{malformed");
        QVERIFY_EXCEPTION_THROWN(model.load(path), std::runtime_error);
        QVERIFY(previous.IsSame(model.resultShape()));
        model.save(path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        object["version"] = 999;
        writeBytes(path, QJsonDocument(object).toJson());
        QVERIFY_EXCEPTION_THROWN(model.load(path), std::runtime_error);
        QVERIFY(previous.IsSame(model.resultShape()));
        QVERIFY_EXCEPTION_THROWN(model.save(temporary.filePath("missing/file.solidara")), std::runtime_error);
        QVERIFY(model.canUndo());
    }

    void binaryStlDimensionsAndStepRoundTrip() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        Model model;
        Document document = plateDocument(true);
        document.parameters["plate_width"] = "120 mm";
        model.apply(document);
        const QString stlPath = temporary.filePath("plate.stl");
        exportStl(model.resultShape(), stlPath, 0.05);
        QFile file(stlPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.seek(80));
        QDataStream stream(&file);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        quint32 triangleCount;
        stream >> triangleCount;
        QVERIFY(triangleCount > 12);
        QCOMPARE(file.size(), qint64(84) + qint64(triangleCount) * 50);
        double minimum[3] = {1e100, 1e100, 1e100};
        double maximum[3] = {-1e100, -1e100, -1e100};
        for (quint32 triangle = 0; triangle < triangleCount; ++triangle) {
            float normal;
            stream >> normal >> normal >> normal;
            for (int vertex = 0; vertex < 3; ++vertex) {
                for (int axis = 0; axis < 3; ++axis) {
                    float coordinate;
                    stream >> coordinate;
                    minimum[axis] = std::min(minimum[axis], double(coordinate));
                    maximum[axis] = std::max(maximum[axis], double(coordinate));
                }
            }
            quint16 attributes;
            stream >> attributes;
        }
        QCOMPARE(stream.status(), QDataStream::Ok);
        QVERIFY(std::abs(maximum[0] - minimum[0] - 120) < 1e-5);
        QVERIFY(std::abs(maximum[1] - minimum[1] - 60) < 1e-5);
        QVERIFY(std::abs(maximum[2] - minimum[2] - 20) < 1e-5);
        const QString stepPath = temporary.filePath("plate.step");
        exportStep(model.resultShape(), stepPath);
        const TopoDS_Shape imported = readStep(stepPath);
        QVERIFY(isValidSolid(imported));
        QVERIFY(std::abs(measure(imported).volume - measure(model.resultShape()).volume) < 1e-5);
        QVERIFY(std::abs(measure(imported).width - 120) < 1e-5);
    }

    void asciiStlAndInvalidExports() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        Model model;
        model.apply(plateDocument());
        const QString path = temporary.filePath("ascii.stl");
        exportStl(model.resultShape(), path, 0.1, false);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("solid"));
        QVERIFY_EXCEPTION_THROWN(exportStl(model.resultShape(), path, 0), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(exportStl(TopoDS_Shape{}, path), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(exportStep(TopoDS_Shape{}, path), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(readStep(temporary.filePath("missing.step")), std::runtime_error);
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
