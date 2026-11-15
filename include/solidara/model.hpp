#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <TopoDS_Shape.hxx>

namespace solidara {

enum class FeatureKind { Rectangle, Circle, Extrude, Cut, Fillet, Box, Cylinder };

struct Feature {
    QString id;
    QString name;
    FeatureKind kind = FeatureKind::Rectangle;
    QMap<QString, QString> values;
    QStringList inputs;
    bool suppressed = false;
};

struct Document {
    QString name = QStringLiteral("Untitled");
    QMap<QString, QString> parameters;
    QVector<Feature> features;
};

struct Measurements {
    double width = 0;
    double depth = 0;
    double height = 0;
    double volume = 0;
    double area = 0;
    double xMin = 0;
    double yMin = 0;
    double zMin = 0;
};

QString featureKindName(FeatureKind kind);
Feature makeFeature(FeatureKind kind, const QString& name = {});
double evaluateLength(const QString& expression, const QMap<QString, QString>& parameters = {});
Measurements measure(const TopoDS_Shape& shape);
bool isValidSolid(const TopoDS_Shape& shape);
void exportStl(const TopoDS_Shape& shape, const QString& path, double deflection = 0.1, bool binary = true);
void exportStep(const TopoDS_Shape& shape, const QString& path);
TopoDS_Shape readStep(const QString& path);

// All edits regenerate a candidate before replacing the live document. Failures throw
// std::runtime_error and leave the current document and undo history untouched.
class Model {
public:
    const Document& document() const { return document_; }
    TopoDS_Shape shapeFor(const QString& id) const;
    TopoDS_Shape resultShape() const;
    QString resultId() const { return resultId_; }
    void apply(const Document& document);
    bool canUndo() const { return !undo_.isEmpty(); }
    bool canRedo() const { return !redo_.isEmpty(); }
    void undo();
    void redo();
    void clear();
    void save(const QString& path) const;
    void load(const QString& path);

private:
    void regenerate(const Document& document);
    Document document_;
    QMap<QString, TopoDS_Shape> shapes_;
    QString resultId_;
    QVector<Document> undo_;
    QVector<Document> redo_;
};

} // namespace solidara
