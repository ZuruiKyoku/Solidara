#include <solidara/model.hpp>

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cmath>
#include <stdexcept>

namespace solidara {
namespace {

[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toStdString()); }

void publishFile(const QString& temporaryPath, const QString& destination) {
    QFile input(temporaryPath);
    if (!input.open(QIODevice::ReadOnly) || input.size() == 0) fail("The geometry writer did not produce a file.");
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) fail("Cannot open export destination: " + output.errorString());
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(64 * 1024);
        if (chunk.isEmpty() && input.error() != QFileDevice::NoError) fail("Cannot read temporary export: " + input.errorString());
        if (output.write(chunk) != chunk.size()) fail("Cannot write export: " + output.errorString());
    }
    if (!output.commit()) fail("Cannot finish export: " + output.errorString());
}

} // namespace

bool isValidSolid(const TopoDS_Shape& shape) {
    if (shape.IsNull()) return false;
    return TopExp_Explorer(shape, TopAbs_SOLID).More() && BRepCheck_Analyzer(shape).IsValid();
}

Measurements measure(const TopoDS_Shape& shape) {
    if (shape.IsNull()) fail("There is no geometry to measure.");
    try {
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(shape, bounds, false, false);
        if (bounds.IsVoid()) fail("The shape has no measurable bounds.");
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
        GProp_GProps volume, surface;
        if (TopExp_Explorer(shape, TopAbs_SOLID).More()) BRepGProp::VolumeProperties(shape, volume);
        BRepGProp::SurfaceProperties(shape, surface);
        return {xMax - xMin, yMax - yMin, zMax - zMin, std::abs(volume.Mass()), surface.Mass(), xMin, yMin, zMin};
    } catch (const Standard_Failure& error) {
        fail(QString("Measurement failed: ") + error.GetMessageString());
    }
}

void exportStl(const TopoDS_Shape& shape, const QString& path, double deflection, bool binary) {
    if (!isValidSolid(shape)) fail("STL export requires a valid solid.");
    if (!std::isfinite(deflection) || deflection < 1e-6 || deflection > 10)
        fail("STL chord tolerance must be between 0.000001 and 10 mm.");
    try {
        QTemporaryDir temporary;
        if (!temporary.isValid()) fail("Cannot create temporary export directory.");
        const QString temporaryPath = temporary.filePath("model.stl");
        // Mesh a private copy: export tolerances must not mutate the displayed B-rep.
        const TopoDS_Shape copy = BRepBuilderAPI_Copy(shape, true, false).Shape();
        BRepMesh_IncrementalMesh mesh(copy, deflection, false, 0.3, false);
        mesh.Perform();
        if (!mesh.IsDone()) fail("STL tessellation failed.");
        StlAPI_Writer writer;
        writer.ASCIIMode() = !binary;
        if (!writer.Write(copy, temporaryPath.toUtf8().constData())) fail("STL writer failed.");
        publishFile(temporaryPath, path);
    } catch (const Standard_Failure& error) {
        fail(QString("STL export failed: ") + error.GetMessageString());
    }
}

void exportStep(const TopoDS_Shape& shape, const QString& path) {
    if (!isValidSolid(shape)) fail("STEP export requires a valid solid.");
    try {
        QTemporaryDir temporary;
        if (!temporary.isValid()) fail("Cannot create temporary export directory.");
        const QString temporaryPath = temporary.filePath("model.step");
        STEPControl_Writer writer;
        if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) fail("STEP transfer failed.");
        if (writer.Write(temporaryPath.toUtf8().constData()) != IFSelect_RetDone) fail("STEP writer failed.");
        publishFile(temporaryPath, path);
    } catch (const Standard_Failure& error) {
        fail(QString("STEP export failed: ") + error.GetMessageString());
    }
}

TopoDS_Shape readStep(const QString& path) {
    try {
        STEPControl_Reader reader;
        if (reader.ReadFile(path.toUtf8().constData()) != IFSelect_RetDone) fail("Cannot read the STEP file.");
        if (reader.TransferRoots() <= 0) fail("The STEP file contains no transferable geometry.");
        const TopoDS_Shape result = reader.OneShape();
        if (!isValidSolid(result)) fail("The STEP file does not contain a valid solid model.");
        return result;
    } catch (const Standard_Failure& error) {
        fail(QString("STEP import failed: ") + error.GetMessageString());
    }
}

} // namespace solidara
