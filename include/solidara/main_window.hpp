#pragma once

#include <solidara/model.hpp>
#include <QMainWindow>
#include <functional>

class QAction;
class QCloseEvent;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace solidara {
class Viewport;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    const Model& model() const { return model_; }
    bool busy() const { return busy_; }
    Viewport* viewport() const { return viewport_; }
    void editDocument(const Document& document);
    void runSmokeTest(const QString& outputDirectory);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void buildUi();
    void refresh();
    void selectFeature(const QString& id);
    void updateInspector();
    void applyInspector();
    void createFeature(FeatureKind kind);
    void createHole();
    void editParameters();
    void deleteSelected();
    void toggleSuppression();
    void runModelTask(std::function<void(Model&)> operation, std::function<void()> success = {},
                      const QString& successMessage = QStringLiteral("Model updated · dimensions in mm"));
    void newDocument();
    void openDocument();
    bool saveDocument(bool saveAs = false);
    bool confirmDiscard();
    void exportModel(bool step);
    void showHelp();
    const Feature* selectedFeature() const;
    Model model_;
    Viewport* viewport_ = nullptr;
    QTreeWidget* browser_ = nullptr;
    QListWidget* timeline_ = nullptr;
    QWidget* inspector_ = nullptr;
    QFormLayout* form_ = nullptr;
    QLineEdit* featureName_ = nullptr;
    QMap<QString,QLineEdit*> fields_;
    QLabel* statistics_ = nullptr;
    QLabel* feedback_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QList<QAction*> mutationActions_;
    QString selectedId_;
    QString path_;
    bool dirty_ = false;
    bool busy_ = false;
    bool refreshing_ = false;
};
}
