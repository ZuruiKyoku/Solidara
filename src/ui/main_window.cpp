#include <solidara/main_window.hpp>
#include <solidara/viewport.hpp>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <Standard_Failure.hxx>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace solidara {
namespace {
QString displayNumber(double value) { return QString::number(value, 'f', 2); }
QString labelFor(const QString& key) {
    if (key == "x") return "Origin X";
    if (key == "y") return "Origin Y";
    if (key == "z") return "Plane offset Z";
    auto label = key;
    if (!label.isEmpty()) label[0] = label[0].toUpper();
    return label;
}
struct TaskResult { std::shared_ptr<Model> model; QString error; };
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName("SolidaraMainWindow");
    resize(1440, 900);
    setMinimumSize(1000, 640);
    buildUi();
    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    restoreState(settings.value("window/layout").toByteArray());
    refresh();
}

void MainWindow::buildUi() {
    viewport_ = new Viewport(this);
    setCentralWidget(viewport_);
    viewport_->selectionChanged = [this](const QString& description) {
        statusBar()->showMessage(description, 5000);
    };
    auto* fileMenu = menuBar()->addMenu("&File");
    auto* editMenu = menuBar()->addMenu("&Edit");
    auto* viewMenu = menuBar()->addMenu("&View");
    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* appBar = addToolBar("Document");
    appBar->setObjectName("documentToolbar");
    appBar->setMovable(false);
    auto* brand = new QLabel("SOLIDARA");
    brand->setObjectName("brand");
    appBar->addWidget(brand);
    auto addAction = [this](QMenu* menu, QToolBar* bar, const QString& text,
                            const QString& tip, const QKeySequence& shortcut, auto callback) {
        auto* action = new QAction(text, this);
        action->setToolTip(tip);
        action->setStatusTip(tip);
        if (!shortcut.isEmpty()) action->setShortcut(shortcut);
        if (menu) menu->addAction(action);
        if (bar) bar->addAction(action);
        connect(action, &QAction::triggered, this, callback);
        mutationActions_.append(action);
        return action;
    };
    addAction(fileMenu, appBar, "New", "Create an empty local project. Ctrl+N", QKeySequence::New, [this] { newDocument(); });
    addAction(fileMenu, appBar, "Open", "Open an editable .solidara project from this computer. Ctrl+O", QKeySequence::Open, [this] { openDocument(); });
    addAction(fileMenu, appBar, "Save", "Atomically save feature history and dimensions locally. Ctrl+S", QKeySequence::Save, [this] { saveDocument(); });
    addAction(fileMenu, nullptr, "Save as…", "Save a copy using a new filename.", QKeySequence::SaveAs, [this] { saveDocument(true); });
    fileMenu->addSeparator();
    addAction(fileMenu, nullptr, "Export STL…", "Export the final solid as a binary STL in millimeters for 3D printing.", {}, [this] { exportModel(false); });
    addAction(fileMenu, nullptr, "Export STEP…", "Export the exact final B-rep solid as STEP for CAD interchange.", {}, [this] { exportModel(true); });
    appBar->addSeparator();
    undoAction_ = addAction(editMenu, appBar, "Undo", "Undo the last successful model change. Ctrl+Z", QKeySequence::Undo,
        [this] { runModelTask([](Model& model) { model.undo(); }); });
    redoAction_ = addAction(editMenu, appBar, "Redo", "Restore an undone model change. Ctrl+Shift+Z", QKeySequence::Redo,
        [this] { runModelTask([](Model& model) { model.redo(); }); });
    addAction(editMenu, nullptr, "Named parameters…", "Define local length parameters such as width = 100 mm and expressions such as width / 2.", {}, [this] { editParameters(); });
    addToolBarBreak();
    auto* tools = addToolBar("Modeling");
    tools->setObjectName("modelingToolbar");
    tools->setMovable(false);
    for (const auto& pair : {std::pair{FeatureKind::Rectangle, "Rectangle"}, {FeatureKind::Circle, "Circle"}})
        addAction(nullptr, tools, pair.second, "Create a dimensioned closed profile on XY or a parallel offset plane. Enter dimensions or parameter expressions.", {}, [this, kind = pair.first] { createFeature(kind); });
    tools->addSeparator();
    addAction(nullptr, tools, "Extrude", "Extrude the selected rectangle or circle in +Z to create a real CAD solid.", {}, [this] { createFeature(FeatureKind::Extrude); });
    addAction(nullptr, tools, "Hole", "Cut a circular through-hole in the final body. The profile follows the body's upper XY plane.", {}, [this] { createHole(); });
    addAction(nullptr, tools, "Fillet all edges", "Round every eligible edge on the final body. A radius that cannot be built leaves the model unchanged.", {}, [this] { createFeature(FeatureKind::Fillet); });
    tools->addSeparator();
    addAction(nullptr, tools, "Box", "Create a box with exact width, depth and height in millimeters.", {}, [this] { createFeature(FeatureKind::Box); });
    addAction(nullptr, tools, "Cylinder", "Create a cylinder with an editable radius and height.", {}, [this] { createFeature(FeatureKind::Cylinder); });
    tools->addSeparator();
    addAction(nullptr, tools, "Parameters", "Edit named length parameters; changes regenerate the feature history.", {}, [this] { editParameters(); });
    addAction(nullptr, tools, "Export STL", "Export the current final body for 3D printing.", {}, [this] { exportModel(false); });

    auto* nav = addToolBar("View");
    nav->setObjectName("viewToolbar");
    nav->setMovable(false);
    auto* fit = nav->addAction("Fit");
    fit->setToolTip("Fit the displayed geometry in the viewport. F");
    fit->setShortcut(QKeySequence("F"));
    connect(fit, &QAction::triggered, viewport_, &Viewport::fitAll);
    viewMenu->addAction(fit);
    for (const QString name : {"Isometric", "Top", "Bottom", "Front", "Back", "Left", "Right"}) {
        auto* action = viewMenu->addAction(name);
        action->setToolTip("Orient the camera to the " + name.toLower() + " view.");
        connect(action, &QAction::triggered, this, [this, name] { viewport_->setStandardView(name); });
        if (name == "Isometric" || name == "Top") nav->addAction(action);
    }
    auto* selection = new QComboBox;
    selection->addItems({"Select bodies", "Select faces", "Select edges", "Select vertices"});
    selection->setToolTip("Choose which geometric entities respond to left-click selection. Shift-click adds to the selection.");
    nav->addWidget(selection);
    connect(selection, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewport_->setSelectionFilter(static_cast<Viewport::SelectionFilter>(index));
    });
    auto* wire = viewMenu->addAction("Wireframe");
    wire->setCheckable(true);
    wire->setToolTip("Show B-rep edges instead of shaded faces.");
    nav->addAction(wire);
    connect(wire, &QAction::toggled, viewport_, &Viewport::setWireframe);
    auto* perspective = viewMenu->addAction("Perspective");
    perspective->setCheckable(true);
    perspective->setToolTip("Switch between perspective and orthographic projection.");
    connect(perspective, &QAction::toggled, viewport_, &Viewport::setPerspective);

    auto* browserDock = new QDockWidget("MODEL BROWSER", this);
    browserDock->setObjectName("browserDock");
    browserDock->setMinimumWidth(210);
    browser_ = new QTreeWidget;
    browser_->setObjectName("modelBrowser");
    browser_->setHeaderHidden(true);
    browser_->setToolTip("Select a feature to inspect and edit its dimensions. The viewport shows its regenerated geometry.");
    browserDock->setWidget(browser_);
    addDockWidget(Qt::LeftDockWidgetArea, browserDock);
    connect(browser_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!refreshing_ && item) selectFeature(item->data(0, Qt::UserRole).toString());
    });
    browser_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(browser_, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        if (busy_ || !selectedFeature()) return;
        QMenu menu;
        menu.addAction("Suppress / unsuppress", this, &MainWindow::toggleSuppression);
        menu.addAction("Delete feature", this, &MainWindow::deleteSelected);
        menu.exec(browser_->viewport()->mapToGlobal(point));
    });

    auto* inspectorDock = new QDockWidget("FEATURE INSPECTOR", this);
    inspectorDock->setObjectName("inspectorDock");
    inspectorDock->setMinimumWidth(260);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    inspector_ = new QWidget;
    form_ = new QFormLayout(inspector_);
    form_->setContentsMargins(16, 12, 16, 12);
    form_->setSpacing(8);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    scroll->setWidget(inspector_);
    inspectorDock->setWidget(scroll);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    auto* historyDock = new QDockWidget("FEATURE HISTORY", this);
    historyDock->setObjectName("historyDock");
    timeline_ = new QListWidget;
    timeline_->setObjectName("featureHistory");
    timeline_->setFlow(QListView::LeftToRight);
    timeline_->setMaximumHeight(90);
    timeline_->setMinimumHeight(65);
    timeline_->setToolTip("Select an earlier feature to edit its dimensions and regenerate downstream features.");
    historyDock->setWidget(timeline_);
    addDockWidget(Qt::BottomDockWidgetArea, historyDock);
    connect(timeline_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (!refreshing_ && item) selectFeature(item->data(Qt::UserRole).toString());
    });
    for (auto* dock : {browserDock, inspectorDock, historyDock}) viewMenu->addAction(dock->toggleViewAction());
    auto* reset = viewMenu->addAction("Reset workspace layout");
    connect(reset, &QAction::triggered, this, [this, browserDock, inspectorDock, historyDock] {
        for (auto* dock : {browserDock, inspectorDock, historyDock}) { dock->setFloating(false); dock->show(); }
        addDockWidget(Qt::LeftDockWidgetArea, browserDock);
        addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
        addDockWidget(Qt::BottomDockWidgetArea, historyDock);
        resizeDocks({browserDock, inspectorDock}, {240, 290}, Qt::Horizontal);
    });
    feedback_ = new QLabel("Ready · Create a rectangle to begin");
    feedback_->setObjectName("muted");
    statusBar()->addWidget(feedback_, 1);
    statusBar()->addPermanentWidget(new QLabel("mm   |   LOCAL PROJECT"));
    auto* help = helpMenu->addAction("Getting started and tool guide");
    help->setShortcut(QKeySequence::HelpContents);
    help->setToolTip("Read the bundled offline modeling and navigation guide. F1");
    connect(help, &QAction::triggered, this, &MainWindow::showHelp);
    auto* about = helpMenu->addAction("About Solidara");
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(this, "Solidara", "<h2>Solidara 0.1</h2><p>Offline parametric CAD for making real things.</p><p>C++20 · Qt 6 · Open CASCADE</p><p>Early development release. See the offline guide for supported operations.</p>");
    });
}

const Feature* MainWindow::selectedFeature() const {
    for (const auto& feature : model_.document().features) if (feature.id == selectedId_) return &feature;
    return nullptr;
}

void MainWindow::refresh() {
    refreshing_ = true;
    browser_->clear();
    timeline_->clear();
    auto* root = new QTreeWidgetItem(browser_, {path_.isEmpty() ? "Untitled project" : QFileInfo(path_).fileName()});
    root->setExpanded(true);
    auto* origin = new QTreeWidgetItem(root, {"Origin · XY plane · millimeters"});
    origin->setToolTip(0, "Profiles are parallel to XY. X/Y set the origin; Z sets an offset plane.");
    auto* parameters = new QTreeWidgetItem(root, {QString("Named parameters (%1)").arg(model_.document().parameters.size())});
    for (auto it = model_.document().parameters.cbegin(); it != model_.document().parameters.cend(); ++it)
        new QTreeWidgetItem(parameters, {it.key() + " = " + it.value()});
    auto* features = new QTreeWidgetItem(root, {"Features"});
    features->setExpanded(true);
    if (!selectedFeature()) selectedId_ = model_.resultId();
    int index = 0;
    for (const auto& feature : model_.document().features) {
        const auto text = QString("%1  %2%3").arg(++index, 2, 10, QChar('0')).arg(feature.name, feature.suppressed ? " [suppressed]" : "");
        auto* item = new QTreeWidgetItem(features, {text});
        item->setData(0, Qt::UserRole, feature.id);
        item->setToolTip(0, featureKindName(feature.kind));
        auto* tile = new QListWidgetItem(text, timeline_);
        tile->setData(Qt::UserRole, feature.id);
        tile->setToolTip(featureKindName(feature.kind) + " · select to edit");
        if (feature.id == selectedId_) { browser_->setCurrentItem(item); timeline_->setCurrentItem(tile); }
    }
    refreshing_ = false;
    undoAction_->setEnabled(!busy_ && model_.canUndo());
    redoAction_->setEnabled(!busy_ && model_.canRedo());
    setWindowTitle(QString("%1%2 — Solidara").arg(path_.isEmpty() ? "Untitled" : QFileInfo(path_).fileName(), dirty_ ? " *" : ""));
    auto shape = model_.shapeFor(selectedId_);
    if (shape.IsNull()) shape = model_.resultShape();
    viewport_->setShape(shape);
    updateInspector();
}

void MainWindow::selectFeature(const QString& id) {
    if (busy_) return;
    selectedId_ = id;
    refreshing_ = true;
    for (int i = 0; i < timeline_->count(); ++i)
        if (timeline_->item(i)->data(Qt::UserRole).toString() == id) timeline_->setCurrentRow(i);
    QTreeWidgetItemIterator iterator(browser_);
    while (*iterator) {
        if ((*iterator)->data(0, Qt::UserRole).toString() == id) browser_->setCurrentItem(*iterator);
        ++iterator;
    }
    refreshing_ = false;
    auto shape = model_.shapeFor(id);
    if (shape.IsNull()) shape = model_.resultShape();
    viewport_->setShape(shape);
    updateInspector();
}

void MainWindow::updateInspector() {
    while (form_->rowCount()) form_->removeRow(0);
    fields_.clear();
    featureName_ = nullptr;
    // Rebuilding the form removes child widgets; invalidate the entire backing store.
    inspector_->update();
    const auto* feature = selectedFeature();
    if (!feature) {
        auto* welcome = new QLabel("BUILD SOMETHING REAL");
        welcome->setObjectName("heading");
        form_->addRow(welcome);
        auto* guide = new QLabel("1. Create a rectangle or circle\n2. Enter dimensions in millimeters\n3. Extrude the profile\n4. Add a hole or round the edges\n5. Save and export for printing\n\nRight drag: orbit\nMiddle drag: pan\nWheel: zoom\nF: fit to view\nF1: offline guide");
        guide->setWordWrap(true);
        guide->setObjectName("muted");
        form_->addRow(guide);
        return;
    }
    auto* heading = new QLabel(featureKindName(feature->kind).toUpper());
    heading->setObjectName("heading");
    form_->addRow(heading);
    featureName_ = new QLineEdit(feature->name);
    featureName_->setToolTip("A readable feature name. The stable internal identifier is preserved when renamed.");
    form_->addRow("Name", featureName_);
    for (auto it = feature->values.cbegin(); it != feature->values.cend(); ++it) {
        auto* input = new QLineEdit(it.value());
        input->setObjectName("dimension_" + it.key());
        input->setToolTip("Enter a length or expression, for example 120 mm, 2 in, or width / 2. Apply to regenerate dependent features.");
        fields_[it.key()] = input;
        form_->addRow(labelFor(it.key()) + " (mm)", input);
        connect(input, &QLineEdit::returnPressed, this, &MainWindow::applyInspector);
    }
    auto* apply = new QPushButton("Apply dimensions");
    apply->setObjectName("primary");
    apply->setToolTip("Regenerate this feature and all dependent geometry. Invalid changes preserve the last valid model.");
    connect(apply, &QPushButton::clicked, this, &MainWindow::applyInspector);
    form_->addRow(apply);
    if (!feature->inputs.isEmpty()) {
        QStringList names;
        for (const auto& id : feature->inputs)
            for (const auto& candidate : model_.document().features) if (candidate.id == id) names << candidate.name;
        auto* dependencies = new QLabel("Depends on: " + names.join(", "));
        dependencies->setWordWrap(true);
        dependencies->setObjectName("muted");
        form_->addRow(dependencies);
    }
    auto* suppress = new QPushButton(feature->suppressed ? "Unsuppress feature" : "Suppress feature");
    suppress->setToolTip("Skip this feature during regeneration. Suppression fails when active dependent features still need it.");
    connect(suppress, &QPushButton::clicked, this, &MainWindow::toggleSuppression);
    form_->addRow(suppress);
    const auto shape = model_.shapeFor(feature->id);
    if (!shape.IsNull()) {
        const auto data = measure(shape);
        auto* label = new QLabel("GEOMETRY");
        label->setObjectName("heading");
        form_->addRow(label);
        statistics_ = new QLabel(QString("%1 × %2 × %3 mm\nVolume: %4 mm³\nSurface area: %5 mm²")
            .arg(displayNumber(data.width), displayNumber(data.depth), displayNumber(data.height), displayNumber(data.volume), displayNumber(data.area)));
        statistics_->setWordWrap(true);
        statistics_->setObjectName("muted");
        form_->addRow(statistics_);
        if (isValidSolid(shape)) {
            auto* printer = new QLabel(data.width <= 220 && data.depth <= 220 && data.height <= 250
                ? "Fits within 220 × 220 × 250 mm dimensions" : "Exceeds 220 × 220 × 250 mm dimensions");
            printer->setWordWrap(true);
            printer->setToolTip("Axis-aligned dimensions only, allowing repositioning. Does not check orientation, bed placement, wall thickness or printability.");
            form_->addRow(printer);
        }
    }
}

void MainWindow::runModelTask(std::function<void(Model&)> operation, std::function<void()> success, const QString& successMessage) {
    if (busy_) return;
    busy_ = true;
    for (auto* action : mutationActions_) action->setEnabled(false);
    inspector_->setEnabled(false);
    browser_->setEnabled(false);
    timeline_->setEnabled(false);
    feedback_->setText("Regenerating geometry…");
    auto* watcher = new QFutureWatcher<TaskResult>(this);
    connect(watcher, &QFutureWatcher<TaskResult>::finished, this, [this, watcher, success, successMessage] {
        const auto result = watcher->result();
        watcher->deleteLater();
        busy_ = false;
        for (auto* action : mutationActions_) action->setEnabled(true);
        inspector_->setEnabled(true);
        browser_->setEnabled(true);
        timeline_->setEnabled(true);
        if (result.error.isEmpty()) {
            model_ = *result.model;
            dirty_ = true;
            if (success) success();
            feedback_->setText(successMessage);
        } else {
            feedback_->setText("Change rejected · previous model preserved");
            QMessageBox::warning(this, "Unable to update model", result.error);
        }
        refresh();
    });
    const auto candidate = std::make_shared<Model>(model_);
    watcher->setFuture(QtConcurrent::run([candidate, operation] {
        try { operation(*candidate); return TaskResult{candidate, {}}; }
        catch (const Standard_Failure& error) { return TaskResult{{}, QString::fromUtf8(error.GetMessageString())}; }
        catch (const std::exception& error) { return TaskResult{{}, QString::fromUtf8(error.what())}; }
    }));
}

void MainWindow::editDocument(const Document& document) {
    runModelTask([document](Model& model) { model.apply(document); }, [this, document] {
        if (!document.features.isEmpty()) selectedId_ = document.features.last().id;
        QTimer::singleShot(0, viewport_, &Viewport::fitAll);
    });
}

void MainWindow::applyInspector() {
    if (!selectedFeature() || busy_) return;
    auto document = model_.document();
    for (auto& feature : document.features) if (feature.id == selectedId_) {
        feature.name = featureName_->text().trimmed();
        for (auto it = fields_.cbegin(); it != fields_.cend(); ++it) feature.values[it.key()] = it.value()->text();
    }
    runModelTask([document](Model& model) { model.apply(document); });
}

void MainWindow::createFeature(FeatureKind kind) {
    auto feature = makeFeature(kind);
    if (kind == FeatureKind::Extrude) {
        const auto* profile = selectedFeature();
        if (!profile || (profile->kind != FeatureKind::Rectangle && profile->kind != FeatureKind::Circle)) {
            QMessageBox::information(this, "Select a profile", "Create or select a rectangle or circle in the model browser, then choose Extrude.");
            return;
        }
        feature.inputs = {profile->id};
    }
    if (kind == FeatureKind::Fillet) {
        if (!isValidSolid(model_.resultShape())) { QMessageBox::information(this, "Create a solid", "Extrude a profile or create a primitive first."); return; }
        feature.inputs = {model_.resultId()};
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Create " + featureKindName(kind));
    dialog.setMinimumWidth(360);
    auto* form = new QFormLayout(&dialog);
    auto* notice = new QLabel((kind == FeatureKind::Rectangle || kind == FeatureKind::Circle)
        ? "Dimensioned closed profile · XY plane\nValues accept mm, cm, in and named parameters."
        : "Exact CAD geometry · dimensions in millimeters");
    notice->setWordWrap(true);
    form->addRow(notice);
    QMap<QString,QLineEdit*> inputs;
    for (auto it = feature.values.cbegin(); it != feature.values.cend(); ++it) {
        auto* input = new QLineEdit(it.value());
        input->setToolTip("Length expression in millimeters; for example 20 mm or width / 2.");
        inputs[it.key()] = input;
        form->addRow(labelFor(it.key()), input);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText((kind == FeatureKind::Rectangle || kind == FeatureKind::Circle) ? "Finish profile" : "Create");
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    for (auto it = inputs.cbegin(); it != inputs.cend(); ++it) feature.values[it.key()] = it.value()->text();
    auto document = model_.document();
    document.features.append(feature);
    editDocument(document);
}

void MainWindow::createHole() {
    if (!isValidSolid(model_.resultShape())) { QMessageBox::information(this, "Create a solid", "Create an extrusion or primitive before adding a through-hole."); return; }
    auto profile = makeFeature(FeatureKind::Circle, "Hole profile");
    profile.inputs = {model_.resultId()};
    profile.values["radius"] = "5";
    const auto dimensions = measure(model_.resultShape());
    profile.values["x"] = QString::number(dimensions.xMin + dimensions.width / 2);
    profile.values["y"] = QString::number(dimensions.yMin + dimensions.depth / 2);
    QDialog dialog(this);
    dialog.setWindowTitle("Circular through-hole");
    auto* form = new QFormLayout(&dialog);
    auto* note = new QLabel("Cut down through the final body.\nThe profile follows its upper XY plane.\nCenter coordinates are absolute world X/Y.");
    form->addRow(note);
    QMap<QString,QLineEdit*> inputs;
    for (const QString key : {"radius", "x", "y"}) {
        auto* input = new QLineEdit(profile.values[key]);
        input->setToolTip("Length or named parameter expression in millimeters.");
        inputs[key] = input;
        form->addRow(labelFor(key), input);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    for (auto it = inputs.cbegin(); it != inputs.cend(); ++it) profile.values[it.key()] = it.value()->text();
    auto cut = makeFeature(FeatureKind::Cut, "Through hole");
    cut.inputs = {model_.resultId(), profile.id};
    auto document = model_.document();
    document.features << profile << cut;
    editDocument(document);
}

void MainWindow::editParameters() {
    QDialog dialog(this);
    dialog.setWindowTitle("Named length parameters");
    dialog.resize(520, 380);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Name and expression · for example width = 120 mm\nUse names in dimensions: width / 2. Blank rows are ignored."));
    auto* table = new QTableWidget(model_.document().parameters.size() + 5, 2);
    table->setHorizontalHeaderLabels({"Name", "Length expression"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    int row = 0;
    for (auto it = model_.document().parameters.cbegin(); it != model_.document().parameters.cend(); ++it, ++row) {
        table->setItem(row, 0, new QTableWidgetItem(it.key()));
        table->setItem(row, 1, new QTableWidgetItem(it.value()));
    }
    layout->addWidget(table);
    auto* add = new QPushButton("Add parameter row");
    connect(add, &QPushButton::clicked, table, [table] { table->insertRow(table->rowCount()); });
    layout->addWidget(add);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    auto document = model_.document();
    document.parameters.clear();
    for (int i = 0; i < table->rowCount(); ++i) {
        const auto name = table->item(i, 0) ? table->item(i, 0)->text().trimmed() : QString();
        const auto expression = table->item(i, 1) ? table->item(i, 1)->text().trimmed() : QString();
        if (name.isEmpty() && expression.isEmpty()) continue;
        if (document.parameters.contains(name)) { QMessageBox::warning(this, "Duplicate parameter", "Each parameter name must be unique."); return; }
        document.parameters[name] = expression;
    }
    runModelTask([document](Model& model) { model.apply(document); });
}

void MainWindow::deleteSelected() {
    const auto* feature = selectedFeature();
    if (!feature) return;
    auto document = model_.document();
    for (qsizetype i = 0; i < document.features.size(); ++i) if (document.features[i].id == selectedId_) { document.features.removeAt(i); break; }
    runModelTask([document](Model& model) { model.apply(document); });
}

void MainWindow::toggleSuppression() {
    auto document = model_.document();
    for (auto& feature : document.features) if (feature.id == selectedId_) feature.suppressed = !feature.suppressed;
    runModelTask([document](Model& model) { model.apply(document); });
}

bool MainWindow::confirmDiscard() {
    if (!dirty_) return true;
    const auto answer = QMessageBox::question(this, "Unsaved project", "Save changes before continuing?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel) return false;
    return answer != QMessageBox::Save || saveDocument();
}

void MainWindow::newDocument() {
    if (!confirmDiscard()) return;
    model_.clear(); path_.clear(); selectedId_.clear(); dirty_ = false;
    refresh();
    feedback_->setText("New project · create a rectangle to begin");
}

void MainWindow::openDocument() {
    if (!confirmDiscard()) return;
    const auto path = QFileDialog::getOpenFileName(this, "Open project", {}, "Solidara projects (*.solidara)");
    if (path.isEmpty()) return;
    runModelTask([path](Model& model) { model.load(path); }, [this, path] { path_ = path; dirty_ = false; selectedId_.clear(); QTimer::singleShot(0, viewport_, &Viewport::fitAll); });
}

bool MainWindow::saveDocument(bool saveAs) {
    auto destination = path_;
    if (saveAs || destination.isEmpty()) destination = QFileDialog::getSaveFileName(this, "Save project", destination, "Solidara projects (*.solidara)");
    if (destination.isEmpty()) return false;
    if (!destination.endsWith(".solidara", Qt::CaseInsensitive)) destination += ".solidara";
    try { model_.save(destination); path_ = destination; dirty_ = false; refresh(); feedback_->setText("Project saved locally"); return true; }
    catch (const std::exception& error) { QMessageBox::warning(this, "Save failed", QString::fromUtf8(error.what())); return false; }
}

void MainWindow::exportModel(bool step) {
    if (!isValidSolid(model_.resultShape())) { QMessageBox::information(this, "Create a solid", "Export needs a valid solid body."); return; }
    double deflection = 0.1;
    if (!step) {
        QDialog dialog(this);
        dialog.setWindowTitle("STL mesh resolution");
        auto* layout = new QFormLayout(&dialog);
        auto* resolution = new QDoubleSpinBox;
        resolution->setDecimals(3); resolution->setRange(0.005, 5); resolution->setValue(0.1); resolution->setSuffix(" mm");
        resolution->setToolTip("Maximum chord deviation. Smaller values create smoother, larger meshes. STL coordinates use millimeters.");
        layout->addRow("Chord deviation", resolution);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addRow(buttons);
        if (dialog.exec() != QDialog::Accepted) return;
        deflection = resolution->value();
    }
    auto destination = QFileDialog::getSaveFileName(this, step ? "Export STEP" : "Export STL", {}, step ? "STEP (*.step)" : "Binary STL (*.stl)");
    if (destination.isEmpty()) return;
    const auto suffix = step ? ".step" : ".stl";
    if (!destination.endsWith(suffix, Qt::CaseInsensitive)) destination += suffix;
    const bool wasDirty = dirty_;
    runModelTask([step, destination, deflection](Model& model) {
        if (step) exportStep(model.resultShape(), destination);
        else exportStl(model.resultShape(), destination, deflection);
    }, [this, wasDirty] { dirty_ = wasDirty; }, "Exported " + QFileInfo(destination).fileName());
}

void MainWindow::showHelp() {
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Solidara · Offline guide");
    dialog->resize(740, 640);
    auto* layout = new QVBoxLayout(dialog);
    auto* browser = new QTextBrowser;
    browser->setOpenExternalLinks(false);
    QFile file(":/help/getting-started.html");
    if (file.open(QIODevice::ReadOnly)) browser->setHtml(QString::fromUtf8(file.readAll()));
    layout->addWidget(browser);
    dialog->show();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (busy_) { event->ignore(); statusBar()->showMessage("Wait for geometry regeneration to finish.", 3000); return; }
    if (!confirmDiscard()) { event->ignore(); return; }
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/layout", saveState());
    event->accept();
}

void MainWindow::runSmokeTest(const QString& outputDirectory) {
    // Exercise the same asynchronous transaction and inspector paths as interactive edits.
    auto stage = std::make_shared<int>(0);
    auto* timer = new QTimer(this);
    timer->setInterval(100);
    auto ticks = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [this, timer, stage, ticks, outputDirectory] {
        if (++*ticks > 600) { qCritical("GUI smoke test timed out"); QApplication::exit(2); return; }
        if (busy_) return;
        try {
            if (!viewport_->initializationError().isEmpty()) throw std::runtime_error(viewport_->initializationError().toStdString());
            if (*stage == 0) {
                auto rectangle = makeFeature(FeatureKind::Rectangle);
                rectangle.values["width"] = "100"; rectangle.values["depth"] = "60";
                auto extrusion = makeFeature(FeatureKind::Extrude);
                extrusion.inputs = {rectangle.id}; extrusion.values["depth"] = "20";
                auto circle = makeFeature(FeatureKind::Circle);
                circle.inputs = {extrusion.id}; circle.values["radius"] = "5"; circle.values["x"] = "50"; circle.values["y"] = "30";
                auto cut = makeFeature(FeatureKind::Cut);
                cut.inputs = {extrusion.id, circle.id};
                auto fillet = makeFeature(FeatureKind::Fillet);
                fillet.inputs = {cut.id};
                Document document; document.features = {rectangle, extrusion, circle, cut, fillet};
                editDocument(document); ++*stage;
            } else if (*stage == 1) {
                selectFeature(model_.document().features.first().id);
                fields_["width"]->setText("120");
                applyInspector(); ++*stage;
            } else if (*stage == 2) {
                if (std::abs(measure(model_.resultShape()).width - 120) > 0.001) throw std::runtime_error("Inspector regeneration failed");
                runModelTask([](Model& model) { model.undo(); }); ++*stage;
            } else if (*stage == 3) {
                if (std::abs(measure(model_.resultShape()).width - 100) > 0.001) throw std::runtime_error("Undo failed");
                runModelTask([](Model& model) { model.redo(); }); ++*stage;
            } else if (*stage == 4) {
                QDir().mkpath(outputDirectory);
                model_.save(outputDirectory + "/smoke.solidara");
                Model loaded; loaded.load(outputDirectory + "/smoke.solidara");
                if (std::abs(measure(loaded.resultShape()).width - 120) > 0.001) throw std::runtime_error("Reopen failed");
                exportStl(loaded.resultShape(), outputDirectory + "/smoke.stl");
                exportStep(loaded.resultShape(), outputDirectory + "/smoke.step");
                selectFeature(model_.resultId()); viewport_->fitAll();
                ++*stage;
            } else {
                repaint();
                inspector_->repaint();
                viewport_->repaint();
                QApplication::sync();
                const auto rendered = viewport_->screen()->grabWindow(viewport_->winId()).toImage();
                int geometryPixels = 0;
                for (int y = 0; y < rendered.height(); ++y)
                    for (int x = 0; x < rendered.width(); ++x) {
                        const auto color = rendered.pixelColor(x, y);
                        if (color.red() > color.blue() + 30 && color.red() > color.green() + 15) ++geometryPixels;
                    }
                if (geometryPixels < 1000) throw std::runtime_error("Viewport did not render the copper solid");
                if (!screen()->grabWindow(winId()).save(outputDirectory + "/solidara.png")) throw std::runtime_error("Screenshot save failed");
                qInfo("GUI smoke PASS: native viewport, extrusion/cut/fillet, asynchronous geometry, inspector edit, undo/redo, save/reopen, STL/STEP");
                timer->stop(); dirty_ = false; QApplication::exit(0);
            }
        } catch (const Standard_Failure& error) { qCritical("GUI smoke failure: %s", error.GetMessageString()); QApplication::exit(1); }
          catch (const std::exception& error) { qCritical("GUI smoke failure: %s", error.what()); QApplication::exit(1); }
    });
    timer->start();
}
}
