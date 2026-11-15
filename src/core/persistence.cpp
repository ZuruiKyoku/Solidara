#include <solidara/model.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <stdexcept>

namespace solidara {
namespace {

[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toStdString()); }

QString typeKey(FeatureKind kind) {
    switch (kind) {
    case FeatureKind::Rectangle: return "rectangle";
    case FeatureKind::Circle: return "circle";
    case FeatureKind::Extrude: return "extrude";
    case FeatureKind::Cut: return "cut";
    case FeatureKind::Fillet: return "fillet";
    case FeatureKind::Box: return "box";
    case FeatureKind::Cylinder: return "cylinder";
    }
    fail("Unsupported feature type.");
}

FeatureKind parseType(const QString& key) {
    for (FeatureKind kind : {FeatureKind::Rectangle, FeatureKind::Circle, FeatureKind::Extrude, FeatureKind::Cut,
                             FeatureKind::Fillet, FeatureKind::Box, FeatureKind::Cylinder}) {
        if (typeKey(kind) == key) return kind;
    }
    fail("Unsupported feature type: " + key);
}

QJsonObject stringsToJson(const QMap<QString, QString>& values) {
    QJsonObject result;
    for (auto it = values.cbegin(); it != values.cend(); ++it) result.insert(it.key(), it.value());
    return result;
}

QMap<QString, QString> stringsFromJson(const QJsonValue& value, const QString& label) {
    if (!value.isObject()) fail(label + " must be an object.");
    QMap<QString, QString> result;
    const auto object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!it.value().isString()) fail(label + " values must be expression strings.");
        result.insert(it.key(), it.value().toString());
    }
    return result;
}

QString requiredString(const QJsonObject& object, const QString& key) {
    if (!object.value(key).isString()) fail("Missing or invalid string field: " + key);
    return object.value(key).toString();
}

} // namespace

void Model::save(const QString& path) const {
    QJsonArray features;
    for (const Feature& feature : document_.features) {
        QJsonArray inputs;
        for (const QString& input : feature.inputs) inputs.append(input);
        features.append(QJsonObject{{"id", feature.id}, {"name", feature.name}, {"type", typeKey(feature.kind)},
                                    {"values", stringsToJson(feature.values)}, {"inputs", inputs}, {"suppressed", feature.suppressed}});
    }
    const QJsonObject root{{"format", "solidara"}, {"version", 1}, {"units", "mm"}, {"name", document_.name},
                           {"parameters", stringsToJson(document_.parameters)}, {"features", features}};
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) fail("Cannot open project for writing: " + file.errorString());
    if (file.write(bytes) != bytes.size()) fail("Cannot write project: " + file.errorString());
    if (!file.commit()) fail("Cannot finish atomic project save: " + file.errorString());
}

void Model::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail("Cannot open project: " + file.errorString());
    if (file.size() > 10 * 1024 * 1024) fail("Project exceeds this version's 10 MB size limit.");
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) fail("Invalid project JSON: " + error.errorString());
    const QJsonObject root = json.object();
    if (root.value("format") != "solidara" || root.value("version") != 1)
        fail("Unsupported Solidara project format or version.");
    if (root.value("units") != "mm") fail("This version supports millimeter documents only.");
    Document candidate;
    candidate.name = requiredString(root, "name");
    candidate.parameters = stringsFromJson(root.value("parameters"), "Parameters");
    if (!root.value("features").isArray()) fail("Features must be an array.");
    const QJsonArray features = root.value("features").toArray();
    if (features.size() > 500) fail("This version supports up to 500 features per document.");
    for (const QJsonValue& entry : features) {
        if (!entry.isObject()) fail("Each feature must be an object.");
        const QJsonObject object = entry.toObject();
        Feature feature;
        feature.id = requiredString(object, "id");
        feature.name = requiredString(object, "name");
        feature.kind = parseType(requiredString(object, "type"));
        feature.values = stringsFromJson(object.value("values"), "Feature dimensions");
        if (!object.value("inputs").isArray()) fail("Feature inputs must be an array.");
        for (const auto& input : object.value("inputs").toArray()) {
            if (!input.isString()) fail("Feature input identifiers must be strings.");
            feature.inputs.append(input.toString());
        }
        if (!object.value("suppressed").isBool()) fail("Feature suppression must be a Boolean.");
        feature.suppressed = object.value("suppressed").toBool();
        candidate.features.append(feature);
    }
    regenerate(candidate);
    undo_.clear();
    redo_.clear();
}

} // namespace solidara
