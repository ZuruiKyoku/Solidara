#include <solidara/model.hpp>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Trsf.hxx>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <stdexcept>

namespace solidara {
namespace {

[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toStdString()); }

double value(const Feature& feature, const Document& document, const QString& key, bool positive = false) {
    if (!feature.values.contains(key)) fail("Missing dimension: " + key);
    const double result = evaluateLength(feature.values.value(key), document.parameters);
    if (!std::isfinite(result) || std::abs(result) > 1e6) fail(key + " must be finite and within ±1,000,000 mm.");
    if (positive && result < 1e-6) fail(key + " must be at least 0.000001 mm.");
    return result;
}

bool profileKind(FeatureKind kind) { return kind == FeatureKind::Rectangle || kind == FeatureKind::Circle; }

void bounds(const TopoDS_Shape& shape, double& zMin, double& zMax) {
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    if (box.IsVoid()) fail("The reference body has no bounding box.");
    double x1, y1, x2, y2;
    box.Get(x1, y1, zMin, x2, y2, zMax);
}

TopoDS_Shape buildFeature(const Feature& feature, const Document& document,
                         const QMap<QString, TopoDS_Shape>& shapes,
                         const QMap<QString, FeatureKind>& kinds) {
    auto input = [&](int index, bool requireProfile) {
        if (index >= feature.inputs.size() || !shapes.contains(feature.inputs[index]))
            fail("A required input is missing or suppressed.");
        const QString id = feature.inputs[index];
        if (profileKind(kinds.value(id)) != requireProfile)
            fail(requireProfile ? "Select a closed sketch profile." : "Select a solid body feature.");
        // Modeling runs on a worker while AIS can triangulate the displayed body.
        // A deep copy prevents kernel builders from mutating either shared topology.
        return BRepBuilderAPI_Copy(shapes.value(id), true, false).Shape();
    };
    auto inputCount = [&](int expected) {
        if (feature.inputs.size() != expected) fail(QString("This operation requires %1 input(s).").arg(expected));
    };
    auto dimension = [&](const QString& key, bool positive = false) {
        return value(feature, document, key, positive);
    };
    auto origin = [&]() { return gp_Pnt(dimension("x"), dimension("y"), dimension("z")); };

    switch (feature.kind) {
    case FeatureKind::Rectangle: {
        inputCount(0);
        const gp_Pnt point = origin();
        const double width = dimension("width", true);
        const double depth = dimension("depth", true);
        BRepBuilderAPI_MakePolygon wire;
        wire.Add(point);
        wire.Add(gp_Pnt(point.X() + width, point.Y(), point.Z()));
        wire.Add(gp_Pnt(point.X() + width, point.Y() + depth, point.Z()));
        wire.Add(gp_Pnt(point.X(), point.Y() + depth, point.Z()));
        wire.Close();
        if (!wire.IsDone()) fail("Could not construct the rectangle profile.");
        BRepBuilderAPI_MakeFace face(wire.Wire());
        if (!face.IsDone()) fail("Could not close the rectangle profile.");
        return face.Face();
    }
    case FeatureKind::Circle: {
        if (feature.inputs.size() > 1) fail("A circle accepts at most one upper-plane reference.");
        gp_Pnt point = origin();
        if (!feature.inputs.isEmpty()) {
            double zMin, zMax;
            bounds(input(0, false), zMin, zMax);
            point.SetZ(zMax + dimension("z"));
        }
        const gp_Circ circle(gp_Ax2(point, gp_Dir(0, 0, 1)), dimension("radius", true));
        BRepBuilderAPI_MakeEdge edge(circle);
        BRepBuilderAPI_MakeWire wire(edge.Edge());
        BRepBuilderAPI_MakeFace face(wire.Wire());
        if (!face.IsDone()) fail("Could not close the circle profile.");
        return face.Face();
    }
    case FeatureKind::Extrude: {
        inputCount(1);
        BRepPrimAPI_MakePrism prism(input(0, true), gp_Vec(0, 0, dimension("depth", true)));
        prism.Build();
        if (!prism.IsDone()) fail("Extrusion failed; check the profile and depth.");
        return prism.Shape();
    }
    case FeatureKind::Cut: {
        inputCount(2);
        const TopoDS_Shape body = input(0, false);
        const TopoDS_Shape profile = input(1, true);
        double zMin, zMax, sketchMin, sketchMax;
        bounds(body, zMin, zMax);
        bounds(profile, sketchMin, sketchMax);
        gp_Trsf placement;
        placement.SetTranslation(gp_Vec(0, 0, zMax + 1.0 - sketchMax));
        const TopoDS_Shape relocated = BRepBuilderAPI_Transform(profile, placement, true).Shape();
        const TopoDS_Shape tool = BRepPrimAPI_MakePrism(relocated, gp_Vec(0, 0, -(zMax - zMin + 2.0))).Shape();
        BRepAlgoAPI_Cut cut(body, tool);
        cut.Build();
        if (!cut.IsDone()) fail("The cut failed; check whether the sketch intersects the body.");
        const TopoDS_Shape result = cut.Shape();
        if (!isValidSolid(result)) fail("The cut removed the entire body or produced invalid geometry.");
        if (measure(result).volume >= measure(body).volume - 1e-8)
            fail("The cutting profile does not remove material from the selected body.");
        return result;
    }
    case FeatureKind::Fillet: {
        inputCount(1);
        const TopoDS_Shape body = input(0, false);
        const double radius = dimension("radius", true);
        BRepFilletAPI_MakeFillet fillet(body);
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(body, TopAbs_EDGE, edges);
        for (int index = 1; index <= edges.Extent(); ++index) fillet.Add(radius, TopoDS::Edge(edges(index)));
        fillet.Build();
        if (!fillet.IsDone()) fail("All-edge fillet failed. Try a smaller radius or a simpler body.");
        return fillet.Shape();
    }
    case FeatureKind::Box: {
        inputCount(0);
        return BRepPrimAPI_MakeBox(origin(), dimension("width", true), dimension("depth", true), dimension("height", true)).Shape();
    }
    case FeatureKind::Cylinder: {
        inputCount(0);
        return BRepPrimAPI_MakeCylinder(gp_Ax2(origin(), gp_Dir(0, 0, 1)), dimension("radius", true), dimension("height", true)).Shape();
    }
    }
    fail("Unsupported feature type.");
}

bool sameGeometryDefinition(const Feature& left, const Feature& right) {
    return left.kind == right.kind && left.values == right.values && left.inputs == right.inputs && left.suppressed == right.suppressed;
}

} // namespace

QString featureKindName(FeatureKind kind) {
    switch (kind) {
    case FeatureKind::Rectangle: return "Rectangle";
    case FeatureKind::Circle: return "Circle";
    case FeatureKind::Extrude: return "Extrude";
    case FeatureKind::Cut: return "Through cut";
    case FeatureKind::Fillet: return "All-edge fillet";
    case FeatureKind::Box: return "Box";
    case FeatureKind::Cylinder: return "Cylinder";
    }
    fail("Unsupported feature type.");
}

Feature makeFeature(FeatureKind kind, const QString& name) {
    Feature feature;
    feature.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    feature.kind = kind;
    feature.name = name.isEmpty() ? featureKindName(kind) : name;
    switch (kind) {
    case FeatureKind::Rectangle:
        feature.values = {{"width", "100"}, {"depth", "60"}, {"x", "0"}, {"y", "0"}, {"z", "0"}};
        break;
    case FeatureKind::Circle:
        feature.values = {{"radius", "6"}, {"x", "50"}, {"y", "30"}, {"z", "0"}};
        break;
    case FeatureKind::Extrude: feature.values = {{"depth", "20"}}; break;
    case FeatureKind::Cut: break;
    case FeatureKind::Fillet: feature.values = {{"radius", "1"}}; break;
    case FeatureKind::Box:
        feature.values = {{"width", "100"}, {"depth", "60"}, {"height", "20"}, {"x", "0"}, {"y", "0"}, {"z", "0"}};
        break;
    case FeatureKind::Cylinder:
        feature.values = {{"radius", "10"}, {"height", "20"}, {"x", "0"}, {"y", "0"}, {"z", "0"}};
        break;
    }
    return feature;
}

TopoDS_Shape Model::shapeFor(const QString& id) const { return shapes_.value(id); }
TopoDS_Shape Model::resultShape() const { return shapeFor(resultId_); }

void Model::regenerate(const Document& document) {
    if (document.features.size() > 500) fail("This version supports up to 500 features per document.");
    if (document.parameters.size() > 256) fail("This version supports up to 256 named parameters.");
    static const QRegularExpression validName("^[A-Za-z_][A-Za-z_0-9]*$");
    for (auto it = document.parameters.cbegin(); it != document.parameters.cend(); ++it) {
        if (!validName.match(it.key()).hasMatch() || QStringList{"mm", "cm", "m", "in"}.contains(it.key()))
            fail("Invalid or reserved parameter name: " + it.key());
        evaluateLength(it.value(), document.parameters);
    }
    QMap<QString, TopoDS_Shape> candidateShapes;
    QMap<QString, FeatureKind> candidateKinds;
    QMap<QString, Feature> oldFeatures;
    for (const Feature& feature : document_.features) oldFeatures.insert(feature.id, feature);
    QSet<QString> changed;
    QString candidateResult;
    for (const Feature& feature : document.features) {
        try {
            if (QUuid(feature.id).isNull()) fail("A feature has an invalid stable identifier.");
            if (candidateKinds.contains(feature.id)) fail("Duplicate feature identifier.");
            for (const QString& id : feature.inputs) {
                if (!candidateKinds.contains(id)) fail("Dependencies must refer to an earlier feature; cyclic or missing reference.");
            }
            const Feature defaults = makeFeature(feature.kind);
            if (feature.values.keys() != defaults.values.keys()) fail("Unexpected or missing dimensions for this feature type.");
            candidateKinds.insert(feature.id, feature.kind);
            bool rebuild = document.parameters != document_.parameters || !oldFeatures.contains(feature.id)
                || !sameGeometryDefinition(feature, oldFeatures.value(feature.id));
            for (const QString& id : feature.inputs) rebuild = rebuild || changed.contains(id);
            if (rebuild) changed.insert(feature.id);
            if (feature.suppressed) continue;
            TopoDS_Shape shape;
            if (!rebuild && shapes_.contains(feature.id)) shape = shapes_.value(feature.id);
            else shape = buildFeature(feature, document, candidateShapes, candidateKinds);
            if (shape.IsNull()) fail("The operation produced no geometry.");
            if (!profileKind(feature.kind) && !isValidSolid(shape)) fail("The operation produced an invalid solid.");
            candidateShapes.insert(feature.id, shape);
            if (!profileKind(feature.kind)) candidateResult = feature.id;
        } catch (const Standard_Failure& error) {
            fail(feature.name + ": " + QString::fromUtf8(error.GetMessageString()));
        } catch (const std::exception& error) {
            fail(feature.name + ": " + QString::fromUtf8(error.what()));
        }
    }
    document_ = document;
    shapes_ = std::move(candidateShapes);
    resultId_ = candidateResult;
}

void Model::apply(const Document& document) {
    const Document previous = document_;
    regenerate(document);
    undo_.append(previous);
    if (undo_.size() > 100) undo_.removeFirst();
    redo_.clear();
}

void Model::undo() {
    if (undo_.isEmpty()) return;
    const Document previous = document_;
    regenerate(undo_.last());
    undo_.removeLast();
    redo_.append(previous);
}

void Model::redo() {
    if (redo_.isEmpty()) return;
    const Document previous = document_;
    regenerate(redo_.last());
    redo_.removeLast();
    undo_.append(previous);
}

void Model::clear() {
    document_ = Document{};
    shapes_.clear();
    resultId_.clear();
    undo_.clear();
    redo_.clear();
}

} // namespace solidara
