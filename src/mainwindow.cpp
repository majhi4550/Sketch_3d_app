#include "mainwindow.h"
#include <cmath>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QTimer>
#include <QFrame>
#include "uikit.h"
#include "units.h"
#include "nesting.h"
#include <QMap>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QClipboard>
#include <QTextStream>
#include <QFile>
#include <QLineEdit>
#include <QToolButton>
#include <QPainter>
#include <QPageLayout>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QFontComboBox>
#include <QWidgetAction>
#include <QVBoxLayout>

MainWindow::MainWindow()
{
    m_canvas = new Canvas(this);
    setCentralWidget(m_canvas);
    setDockNestingEnabled(true);

    buildMenus();
    buildToolBars();
    buildDocks();

    m_hintLabel = new QLabel("Ready", this);
    m_coordLabel = new QLabel("X 0.0   Y 0.0   Z 0.0 mm", this);
    m_viewLabel = new QLabel(this);
    m_snapLabel = new QLabel(this);
    m_coordLabel->setMinimumWidth(210);
    m_viewLabel->setStyleSheet("color:#35e0c8; font-weight:bold;");
    m_snapLabel->setStyleSheet("color:#ffd166;");
    statusBar()->addWidget(m_hintLabel, 1);
    statusBar()->addPermanentWidget(m_snapLabel);
    statusBar()->addPermanentWidget(m_viewLabel);
    statusBar()->addPermanentWidget(m_coordLabel);

    connect(m_canvas, &Canvas::cursorMoved, this, [this](const QVector3D &p) { updateCoordLabel(p); });
    connect(m_canvas, &Canvas::message, this, [this](const QString &s) { statusBar()->showMessage(s, 4000); });
    connect(m_canvas, &Canvas::hint, this, [this](const QString &s) { m_hintLabel->setText(s); });
    connect(m_canvas, &Canvas::snapStateChanged, this, [this](const QString &s) { m_snapLabel->setText(s); });
    connect(m_canvas, &Canvas::viewChanged, this, [this] { updateStatusLabels(); syncControls(); });
    connect(m_canvas, &Canvas::planeChanged, this, [this] { updateStatusLabels(); syncControls(); });
    connect(m_canvas, &Canvas::settingsChanged, this, [this] { syncControls(); });
    connect(m_canvas, &Canvas::selectionChanged, this, [this] { syncSelection(); });
    connect(m_canvas, &Canvas::contentChanged, this, [this] { refreshObjects(); syncSelection(); });
    connect(m_canvas->undoStack(), &QUndoStack::indexChanged, this, &MainWindow::updateTitle);

    resize(1440, 880);
    loadSettings();
    m_canvas->setView(Canvas::View::Top);
    syncControls();
    updateTitle();
    updateStatusLabels();
    m_canvas->setTool(Canvas::Tool::Select);
    m_canvas->zoomFit();
    m_canvas->setFocus();
}

// ---------------------------------------------------------------------------
// menus
// ---------------------------------------------------------------------------
void MainWindow::mutate(const std::function<void(ViewSettings &)> &fn)
{
    if (m_syncing)
        return;
    ViewSettings s = m_canvas->settings();
    fn(s);
    m_canvas->setSettings(s);
}

void MainWindow::updateCoordLabel(const QVector3D &p)
{
    m_lastCursor = p;
    if (!m_coordLabel)
        return;
    const QString u = Units::isImperial() ? QString() : " " + Units::unitLabel();
    m_coordLabel->setText(QString("X %1   Y %2   Z %3%4").arg(Units::formatDim(p.x()), Units::formatDim(p.y()),
                                                               Units::formatDim(p.z()), u));
}

// length prompt that reads any unit (4'-6", 1.2m, 120cm ...) and answers in mm
static double askLength(QWidget *parent, const QString &title, const QString &label, double val, double mn,
                        double mx, bool &ok)
{
    QString txt = Units::format(val);
    for (;;) {
        txt = QInputDialog::getText(parent, title, label + "\n(" + Units::inputHint() + ")", QLineEdit::Normal, txt, &ok);
        if (!ok)
            return val;
        double mm = 0;
        if (Units::parse(txt, mm) && mm >= mn && mm <= mx)
            return mm;
        QMessageBox::warning(parent, title, QString("\"%1\" is not a length between %2 and %3.")
                                                .arg(txt, Units::format(mn), Units::format(mx)));
    }
}

// spin box that shows and reads the current units (value in mm)
static LengthSpin *lenSpin(double mn, double mx, double val, double step = 1.0)
{
    auto *s = new LengthSpin;
    s->setRange(mn, mx);
    s->setMetricStep(step);
    s->setValue(val);
    return s;
}

void MainWindow::buildMenus()
{
    m_alignToPage = QSettings().value("ui/alignPage", false).toBool();
    QMenu *file = menuBar()->addMenu("&File");
    file->addAction("&New Canvas...", QKeySequence::New, this, &MainWindow::newFile);
    m_newWinAct = file->addAction("New &Window", QKeySequence("Ctrl+Shift+N"), this, [this] { newWindow(); });
    m_newWinAct->setToolTip("Open another drawing window (its own file, undo and view)");
    file->addAction("&Open...", QKeySequence::Open, this, &MainWindow::openFile);
    file->addAction("Open in New W&indow...", this, &MainWindow::openInNewWindow);
    m_recentMenu = file->addMenu("Open &Recent");
    file->addAction("&Save", QKeySequence::Save, this, &MainWindow::saveFile);
    file->addAction("Save &As...", QKeySequence::SaveAs, this, &MainWindow::saveFileAs);
    file->addSeparator();
    QMenu *ex = file->addMenu("&Export");
    ex->addAction("PNG image...", this, &MainWindow::exportPng);
    ex->addAction("JPG image...", this, &MainWindow::exportJpg);
    ex->addAction("PDF...", this, &MainWindow::exportPdf);
    ex->addAction("DXF (3D lines, for CAD/CAM)...", this, &MainWindow::exportDxf);
    file->addAction("&Insert Image (JPG/PNG)...", QKeySequence("Ctrl+I"), this, &MainWindow::insertImage);
    file->addSeparator();
    m_closeWinAct = file->addAction("&Close Window", QKeySequence("Ctrl+W"), this, &QWidget::close);
    file->addAction("E&xit (all windows)", QKeySequence::Quit, qApp, &QApplication::closeAllWindows);

    QMenu *edit = menuBar()->addMenu("&Edit");
    edit->addAction(uiIcon("select"), "Command palette...", QKeySequence("Ctrl+K"), this, [this] { showCommandPalette(this); });
    edit->addSeparator();
    QAction *aUndo = m_canvas->undoStack()->createUndoAction(this, "&Undo");
    aUndo->setShortcut(QKeySequence::Undo);
    edit->addAction(aUndo);
    QAction *aRedo = m_canvas->undoStack()->createRedoAction(this, "&Redo");
    aRedo->setShortcut(QKeySequence::Redo);
    edit->addAction(aRedo);
    edit->addSeparator();
    edit->addAction("&Copy", QKeySequence::Copy, this, [this] { m_canvas->copySelection(); });
    edit->addAction("&Paste", QKeySequence::Paste, this, [this] { m_canvas->pasteClipboard(); });
    edit->addAction("&Duplicate", QKeySequence("Ctrl+D"), this, [this] { m_canvas->duplicateSelection(); });
    edit->addAction("Select &All", QKeySequence::SelectAll, this, [this] { m_canvas->selectAll(); });
    edit->addMenu(buildAlignMenu(edit));
    edit->addAction(uiIcon("flip-h"), "Flip &horizontal", QKeySequence("Alt+Shift+H"), this, [this] { m_canvas->flipSelection(true); });
    edit->addAction(uiIcon("flip-v"), "Flip &vertical", QKeySequence("Alt+Shift+V"), this, [this] { m_canvas->flipSelection(false); });
    edit->addAction(uiIcon("fill"), "Fill colour...", this, [this] { pickFill(); });
    edit->addAction(uiIcon("hatch"), "Hatch / fill inside a boundary\tH", this, [this] { m_canvas->setTool(Canvas::Tool::Hatch); });
    edit->addAction("Apply hatch style to selection", this, [this] { pushHatchStyle(); m_canvas->applyHatchToSelection(); });
    edit->addAction(uiIcon("front"), "Bring to front", QKeySequence("Ctrl+]"), this, [this] { m_canvas->arrangeSelection(true); });
    edit->addAction(uiIcon("back"), "Send to back", QKeySequence("Ctrl+["), this, [this] { m_canvas->arrangeSelection(false); });
    edit->addAction(uiIcon("explode"), "E&xplode (break rectangles / polylines at joints)", QKeySequence("Ctrl+Shift+X"), this,
                    [this] { m_canvas->explodeSelection(); });
    edit->addAction("Flatten to sketch &plane (fix slanted lines)", QKeySequence("Ctrl+Shift+F"), this,
                    [this] { m_canvas->flattenToPlane(); });
    edit->addAction(uiIcon("group"), "&Group", QKeySequence("Ctrl+G"), this, [this] { m_canvas->groupSelection(); });
    edit->addAction(uiIcon("ungroup"), "&Ungroup", QKeySequence("Ctrl+Shift+G"), this, [this] { m_canvas->ungroupSelection(); });
    edit->addAction("Edit &text...", QKeySequence("F2"), this, [this] { m_canvas->editSelectedText(); });
    edit->addAction("Delete Selection", this, [this] { m_canvas->deleteSelection(); });
    edit->addSeparator();
    QMenu *mir = edit->addMenu("&Mirror selection");
    mir->addAction("Flip left <-> right (about centre, in sketch plane)", this,
                   [this] { m_canvas->mirrorSelection(true, m_keepMirror ? m_keepMirror->isChecked() : true); });
    mir->addAction("Flip up <-> down (about centre, in sketch plane)", this,
                   [this] { m_canvas->mirrorSelection(false, m_keepMirror ? m_keepMirror->isChecked() : true); });
    edit->addAction("&Extrude wireframe...", this, [this] {
        bool ok;
        const double d = askLength(this, "Extrude", "Distance along the shape's plane normal:",
                                   m_extrudeSpin ? m_extrudeSpin->value() : 50, -100000, 100000, ok);
        if (ok) m_canvas->extrudeSelection(d);
    });
    edit->addAction("&Pattern (rectangular / polar)...", QKeySequence("Shift+A"), this, &MainWindow::openPattern);
    edit->addAction("&Array selection...", this, [this] {
        bool ok;
        const int n = QInputDialog::getInt(this, "Array", "Total count (including the original):", 3, 2, 500, 1, &ok);
        if (ok) m_canvas->arraySelection(n, QVector3D(m_vx->value(), m_vy->value(), m_vz->value()));
    });

    QMenu *view = menuBar()->addMenu("&View");
    QMenu *vm = view->addMenu("&Viewport");
    const struct { const char *n; Canvas::View v; const char *k; } vs[] = {
        {"Top", Canvas::View::Top, "Ctrl+1"}, {"Front", Canvas::View::Front, "Ctrl+2"},
        {"Left", Canvas::View::Side, "Ctrl+3"}, {"ISO 3D", Canvas::View::Iso, "Ctrl+4"},
        {"Bottom", Canvas::View::Bottom, "Ctrl+5"}, {"Back", Canvas::View::Back, "Ctrl+6"},
        {"Right", Canvas::View::Right, "Ctrl+7"}};
    auto *vg = new QActionGroup(this);
    for (const auto &v : vs) {
        QAction *a = vm->addAction(QString("%1\t%2").arg(v.n, v.k));
        a->setShortcut(QKeySequence(v.k));
        a->setCheckable(true);
        vg->addAction(a);
        const Canvas::View vv = v.v;
        connect(a, &QAction::triggered, this, [this, vv] { m_canvas->setView(vv); });
        m_viewActs[vv] = a;
    }
    QAction *persp = view->addAction("&Perspective camera (ISO view)");
    persp->setShortcut(QKeySequence("Ctrl+P"));
    persp->setCheckable(true);
    connect(persp, &QAction::toggled, this, [this](bool on) { mutate([on](ViewSettings &s) { s.perspective = on; }); });
    m_syncers.push_back([persp](const ViewSettings &s) { QSignalBlocker b(persp); persp->setChecked(s.perspective); });
    view->addAction("Reset ISO view (Home)", QKeySequence("Ctrl+R"), this, [this] { m_canvas->resetIsoView(); });
    view->addAction("Look at sketch plane", QKeySequence("Ctrl+Shift+L"), this, [this] { m_canvas->lookAtPlane(); });
    QAction *freeOrb = view->addAction("&Free orbit (any axis, 360\xC2\xB0)");
    freeOrb->setShortcut(QKeySequence("Ctrl+Shift+O"));
    freeOrb->setCheckable(true);
    connect(freeOrb, &QAction::toggled, this, [this](bool on) { mutate([on](ViewSettings &s) { s.freeOrbit = on; }); });
    m_syncers.push_back([freeOrb](const ViewSettings &s) { QSignalBlocker b(freeOrb); freeOrb->setChecked(s.freeOrbit); });
    QMenu *orient = view->addMenu("View &orientation");
    QMenu *setAs = orient->addMenu("Set current view as");
    const struct { const char *n; Canvas::View v; } asv[] = {
        {"Top", Canvas::View::Top}, {"Front", Canvas::View::Front}, {"Left", Canvas::View::Side},
        {"Right", Canvas::View::Right}, {"Back", Canvas::View::Back}, {"Bottom", Canvas::View::Bottom}};
    for (const auto &v : asv) {
        const Canvas::View vv = v.v;
        setAs->addAction(v.n, this, [this, vv] { m_canvas->setCurrentViewAs(vv); });
    }
    orient->addAction("Reset view orientation (world axes)", this, [this] { m_canvas->resetViewOrientation(); });
    orient->addSeparator();
    orient->addAction("Set current view as Home", this, [this] { m_canvas->setCurrentViewAsHome(); });
    orient->addAction("Reset Home", this, [this] { m_canvas->resetHome(); });
    orient->addSeparator();
    orient->addAction("Roll view 90\xC2\xB0 left", QKeySequence("Ctrl+Shift+Left"), this, [this] { m_canvas->rollView(90); });
    orient->addAction("Roll view 90\xC2\xB0 right", QKeySequence("Ctrl+Shift+Right"), this, [this] { m_canvas->rollView(-90); });

    QMenu *pm = view->addMenu("Sketch &plane");
    auto *pg = new QActionGroup(this);
    const struct { const char *n; Canvas::Plane p; } pls[] = {
        {"XY  (Top)", Canvas::Plane::TopXY}, {"XZ  (Front)", Canvas::Plane::XZ},
        {"ZY  (Side)", Canvas::Plane::ZY}, {"YX", Canvas::Plane::YX}};
    for (const auto &p : pls) {
        QAction *a = pm->addAction(p.n);
        a->setCheckable(true);
        pg->addAction(a);
        const Canvas::Plane pp = p.p;
        connect(a, &QAction::triggered, this, [this, pp] { m_canvas->setPlane(pp); });
        m_planeActs[pp] = a;
    }
    pm->addSeparator();
    pm->addAction("Next plane\tTab", this, [this] { m_canvas->cyclePlane(); });
    pm->addAction("Reset plane offset to 0", this, [this] { m_canvas->setPlaneOffset(0); });

    view->addSeparator();
    auto toggle = [this, view](const QString &text, bool ViewSettings::*field, const QString &key = QString()) {
        QAction *a = view->addAction(text);
        if (!key.isEmpty()) a->setShortcut(QKeySequence(key));
        a->setCheckable(true);
        connect(a, &QAction::toggled, this, [this, field](bool on) { mutate([field, on](ViewSettings &s) { s.*field = on; }); });
        m_syncers.push_back([a, field](const ViewSettings &s) { QSignalBlocker b(a); a->setChecked(s.*field); });
    };
    QAction *lab = view->addAction("Show tool &labels");
    lab->setCheckable(true);
    connect(lab, &QAction::toggled, this, [this](bool on) {
        if (auto *t = findChild<QToolBar *>("toolsBar"))
            t->setToolButtonStyle(on ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonIconOnly);
        QSettings().setValue("ui/labels", on);
    });
    lab->setChecked(QSettings().value("ui/labels", false).toBool());
    view->addSeparator();
    toggle("Show &grid", &ViewSettings::showGrid, "G");
    toggle("Show &axes", &ViewSettings::showAxes);
    toggle("Show &paper sheet", &ViewSettings::showPaper);
    toggle("Show sketch-plane sheet", &ViewSettings::showPlaneSheet);
    toggle("Show orientation gizmo", &ViewSettings::showGizmo);
    toggle("Show on-screen info", &ViewSettings::showHud);
    view->addSeparator();
    toggle("Snap to &grid", &ViewSettings::snapGrid, "F9");
    toggle("Snap to points &off the plane (3D snap)", &ViewSettings::snap3D);
    toggle("&Limit drawing to the sheet (stops stray shapes)", &ViewSettings::limitSheet);
    toggle("Snap to &objects (end / mid / centre)", &ViewSettings::snapObject, "F3");
    view->addAction("Object snap &settings (which points snap)...", QKeySequence("Ctrl+F3"), this, &MainWindow::osnapSettings);
    toggle("&Ortho drafting", &ViewSettings::ortho, "F8");
    toggle("&Polar tracking (angle snapping)", &ViewSettings::polar, "F10");
    toggle("&Dynamic input (size fields at the cursor)", &ViewSettings::dynInput, "F12");
    toggle("Sync sketch plane with viewport", &ViewSettings::syncPlane);

    QMenu *um = view->addMenu("&Units");
    auto *ug = new QActionGroup(this);
    for (int i = 0; i < Units::SystemCount; ++i) {
        QAction *a = um->addAction(Units::systemName(i));
        a->setCheckable(true);
        ug->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] { mutate([i](ViewSettings &s) { s.units = i; }); });
        m_syncers.push_back([a, i](const ViewSettings &s) { QSignalBlocker b(a); a->setChecked(s.units == i); });
    }
    QMenu *th = view->addMenu("&Theme");
    const char *themes[] = {"Dark", "Graphite", "Blueprint", "Light"};
    for (int i = 0; i < 4; ++i)
        th->addAction(themes[i], this, [this, i] { mutate([i](ViewSettings &s) { s.theme = i; }); });
    view->addSeparator();
    view->addAction("Zoom &In", QKeySequence::ZoomIn, this, [this] { m_canvas->zoomIn(); });
    view->addAction("Zoom &Out", QKeySequence::ZoomOut, this, [this] { m_canvas->zoomOut(); });
    view->addAction("Zoom &Fit all", QKeySequence("Ctrl+0"), this, [this] { m_canvas->zoomFit(); });
    view->addAction("Zoom to selection\tF", this, [this] { m_canvas->zoomFitSelection(); });
    view->addSeparator();
    m_viewMenu = view;

    // ---- Model: solid bodies, constraints, corners, visibility ----
    QMenu *model = menuBar()->addMenu("&Model");
    auto askNum = [this](const QString &title, const QString &label, double val, double mn, double mx, bool &ok) {
        return askLength(this, title, label, val, mn, mx, ok);
    };
    QAction *aExt = model->addAction(uiIcon("extrude"), "&Extrude solid...", QKeySequence("Ctrl+E"), this, [this, askNum] {
        bool ok;
        const double d = askNum("Extrude solid", "Thickness along the shape's plane normal (negative = other side):",
                                m_extrudeSpin ? m_extrudeSpin->value() : 18, -100000, 100000, ok);
        if (ok) m_canvas->extrudeSolid(d);
    });
    model->addAction("Remove extrude (back to flat shape)", this, [this] { m_canvas->removeExtrude(); });
    model->addSeparator();
    model->addAction(uiIcon("box"), "&Box (3D board)\tShift+B", this, [this] { m_canvas->setTool(Canvas::Tool::Box); });
    model->addAction(uiIcon("presspull"), "&Press / Pull a face\tShift+P", this, [this] { m_canvas->setTool(Canvas::Tool::PressPull); });
    model->addAction("Board &thickness for Box...", this, [this, askNum] {
        bool ok;
        const double t = askNum("Board thickness", "Default height of the Box tool (Enter at the height step):",
                                m_canvas->boxHeight(), 0.1, 10000, ok);
        if (ok) {
            m_canvas->setBoxHeight(t);
            if (m_boardSpin) { QSignalBlocker b(m_boardSpin); m_boardSpin->setValue(t); }
        }
    });
    model->addAction(uiIcon("cutlist"), "&Cut list (boards)...", QKeySequence("Ctrl+Shift+B"), this, &MainWindow::openCutList);
    model->addSeparator();
    QAction *aUnion = model->addAction(uiIcon("union"), "&Union (join boards into one)", QKeySequence("Ctrl+Alt+U"), this,
                                       [this] { runBoolean(Canvas::BoolOp::Union); });
    QAction *aSub = model->addAction(uiIcon("subtract"), "&Subtract (first selected minus the others)", QKeySequence("Ctrl+Alt+S"), this,
                                     [this] { runBoolean(Canvas::BoolOp::Subtract); });
    model->addAction(uiIcon("subtract"), "Subtract, &keep the cutting boards (joints, grooves)", this,
                     [this] { m_canvas->booleanSelection(Canvas::BoolOp::Subtract, true); });
    QAction *aInt = model->addAction(uiIcon("intersect"), "&Intersect (keep only the overlap)", QKeySequence("Ctrl+Alt+I"), this,
                                     [this] { runBoolean(Canvas::BoolOp::Intersect); });
    aUnion->setToolTip("Select two or more boards: they become one board (pieces stay one part in the cut list)");
    aSub->setToolTip("Select the board to cut FIRST, then Ctrl+click the boards that cut it (they are removed)");
    aInt->setToolTip("Select two or more boards: only what they share is left");
    model->addAction(uiIcon("subtract"), "C&ombine... (choose target and tool bodies)", QKeySequence("Ctrl+Alt+C"), this,
                     &MainWindow::openCombine);
    QAction *aJoint = model->addAction(uiIcon("joint"), "Cut &joints (selected boards cut housings into the others)", QKeySequence("Ctrl+Shift+J"),
                                       this, [this] { m_canvas->cutJointsSelection(); });
    aJoint->setToolTip("Sink a shelf / rail / tenon into the boards it joins, select it, Cut joints: every board it goes into "
                       "gets the housing (dado, groove, mortise); the selected board stays whole");
    {
        QMenu *em = model->addMenu(uiIcon("presspull"), "&Extrude a shape (Press/Pull) does");
        auto *eg = new QActionGroup(this);
        for (int k = 0; k < 5; ++k) {
            QAction *a = em->addAction(Canvas::extrudeOpName(k) + (k == 0 ? "  (cut when it goes into a board, else a new body)" : ""));
            a->setCheckable(true);
            a->setChecked(k == 0);
            eg->addAction(a);
            connect(a, &QAction::triggered, this, [this, k] { m_canvas->setExtrudeOp(k); });
            connect(m_canvas, &Canvas::toolSizesChanged, a, [this, a, k] { QSignalBlocker b(a); a->setChecked(m_canvas->extrudeOp() == k); });
        }
        em->addSeparator();
        em->addAction("While pulling a shape: N new, J join, C cut, I intersect, A auto")->setEnabled(false);
    }
    model->addSeparator();
    model->addAction(uiIcon("fillet"), "&Fillet tool (round corners and edges)\tShift+F", this, [this] { m_canvas->setTool(Canvas::Tool::Fillet); });
    model->addAction(uiIcon("chamfer"), "Chamfer tool (bevel corners and edges)\tShift+H", this, [this] { m_canvas->setTool(Canvas::Tool::Chamfer); });
    model->addAction(uiIcon("shell"), "Shell tool (click the face to leave open)\tShift+S", this, [this] { m_canvas->setTool(Canvas::Tool::Shell); });
    model->addAction(uiIcon("revolve"), "Re&volve tool (profile, axis, angle)\tShift+O", this, [this] { m_canvas->setTool(Canvas::Tool::Revolve); });
    model->addAction(uiIcon("revolve"), "Revolve the selected shape about the selected line...", this, [this, askNum] {
        bool ok;
        const double a = QInputDialog::getDouble(this, "Revolve", "Angle (degrees, 360 = all round):", 360, 0.1, 360, 2, &ok);
        if (ok) m_canvas->revolveSelection(a);
    });
    model->addAction(uiIcon("pipe"), "&Pipe tool (click a centre line)\tShift+I", this, [this] { m_canvas->setTool(Canvas::Tool::Pipe); });
    model->addAction(uiIcon("pipe"), "Pipe settings (diameter, wall, bend radius)...", this, &MainWindow::pipeSettings);
    model->addAction(uiIcon("pipe"), "Pipe along the selected lines", this, [this] { m_canvas->pipeSelection(); });
    {
        QMenu *tm = model->addMenu(uiIcon("rotate"), "&Rotate");
        tm->addAction(uiIcon("rotate"), "Rotate tool (pivot point, angle)\tShift+R", this, [this] { m_canvas->setTool(Canvas::Tool::Rotate); });
        tm->addSeparator();
        const char *an[] = {"X", "Y", "Z"};
        for (int k = 0; k < 3; ++k) {
            tm->addAction(QString("Turn 90° about %1").arg(an[k]), this, [this, k] { m_canvas->rotateSelection3D(90, k); });
            tm->addAction(QString("Turn -90° about %1").arg(an[k]), this, [this, k] { m_canvas->rotateSelection3D(-90, k); });
        }
        tm->addAction("Turn 180° about Z", this, [this] { m_canvas->rotateSelection3D(180, 2); });
        QMenu *gm = model->addMenu(uiIcon("gizmo"), "&Gizmo on the selection");
        auto *gg = new QActionGroup(this);
        const char *gn[] = {"Off", "Move gizmo (arrows + planes)", "Rotate gizmo (rings)"};
        for (int k = 0; k < 3; ++k) {
            QAction *a = gm->addAction(gn[k]);
            a->setCheckable(true);
            gg->addAction(a);
            connect(a, &QAction::triggered, this, [this, k] { m_canvas->setGizmoMode(Canvas::GizmoMode(k)); });
            connect(m_canvas, &Canvas::gizmoModeChanged, a, [a, k](int m) { QSignalBlocker b(a); a->setChecked(m == k); });
            a->setChecked(int(m_canvas->gizmoMode()) == k);
        }
    }
    model->addSeparator();
    model->addAction("Sketch on &top face of body", this, [this] { m_canvas->sketchOnBodyFace(true); });
    model->addAction("Sketch on &bottom face of body", this, [this] { m_canvas->sketchOnBodyFace(false); });
    model->addAction("&Split body at sketch plane", this, [this] { m_canvas->splitBodyAtPlane(); });
    model->addAction("S&hell the selected boards (open at the far end)...", this, [this, askNum] {
        bool ok;
        const double w = askNum("Shell", "Wall thickness:", m_canvas->shellThickness(), 0.1, 10000, ok);
        if (ok) m_canvas->shellBody(w);
    });
    model->addAction(uiIcon("fill"), "Body / fill &appearance colour...", this, [this] { pickFill(); });
    model->addSeparator();
    QMenu *cons = model->addMenu("&Constraints (selected lines)");
    const char *cn[] = {"Horizontal", "Vertical", "Parallel to first", "Perpendicular to first", "Equal length to first", "Coincident ends"};
    for (int k = 0; k < 6; ++k)
        cons->addAction(cn[k], this, [this, k] { m_canvas->constrainSelection(k); });
    cons->addSeparator();
    cons->addAction("&Fix (lock) selection", QKeySequence("Ctrl+L"), this, [this] { m_canvas->fixSelection(); });
    cons->addAction("Unfix all", this, [this] { m_canvas->unfixAll(); });
    model->addAction("Fillet two selected lines...", this, [this, askNum] {
        bool ok;
        const double r = askNum("Fillet", "Radius (0 = sharp corner):", 10, 0, 100000, ok);
        if (ok) m_canvas->filletLines(r, false);
    });
    model->addAction("Chamfer two selected lines...", this, [this, askNum] {
        bool ok;
        const double r = askNum("Chamfer", "Distance:", 10, 0, 100000, ok);
        if (ok) m_canvas->filletLines(r, true);
    });
    model->addSeparator();
    QAction *aHide = model->addAction(uiIcon("hide"), "&Hide selection", QKeySequence("Ctrl+H"), this, [this] { m_canvas->hideSelection(); });
    model->addAction("&Isolate selection", QKeySequence("Ctrl+Shift+I"), this, [this] { m_canvas->isolateSelection(); });
    QAction *aShow = model->addAction(uiIcon("show"), "Show &all", QKeySequence("Ctrl+Shift+H"), this, [this] { m_canvas->showAll(); });
    {
        QToolBar *mb = addToolBar("Model");
        mb->setObjectName("modelBar");
        mb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        mb->addAction(aExt);
        mb->addAction(aUnion);
        mb->addAction(aSub);
        mb->addAction(aInt);
        mb->addAction(aHide);
        mb->addAction(aShow);
    }

    m_windowMenu = menuBar()->addMenu("&Window");
    connect(m_windowMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildWindowMenu);
    rebuildWindowMenu();

    QMenu *help = menuBar()->addMenu("&Help");
    help->addAction("Quick guide && shortcuts", QKeySequence::HelpContents, this, &MainWindow::showHelp);
    help->addAction("&About Sketch3D", this, &MainWindow::about);
}

// ---------------------------------------------------------------------------
// toolbars
// ---------------------------------------------------------------------------
void MainWindow::buildToolBars()
{
    // ---- view + plane + drafting ----
    QToolBar *vb = addToolBar("Viewport");
    vb->setObjectName("viewportBar");
    vb->setMovable(false);
    vb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    vb->addWidget(new QLabel(" View "));
    auto *vg = new QActionGroup(this);
    const struct { const char *n; Canvas::View v; const char *tip; } vs[] = {
        {"Top", Canvas::View::Top, "Top view (Ctrl+1)"}, {"Front", Canvas::View::Front, "Front view (Ctrl+2)"},
        {"Left", Canvas::View::Side, "Left view (Ctrl+3)"}, {"Right", Canvas::View::Right, "Right view (Ctrl+7)"},
        {"Back", Canvas::View::Back, "Back view (Ctrl+6)"}, {"Bottom", Canvas::View::Bottom, "Bottom view (Ctrl+5)"},
        {"ISO 3D", Canvas::View::Iso, "Free 3D view - right-drag to orbit (Ctrl+4)"}};
    for (const auto &v : vs) {
        QAction *a = vb->addAction(v.n);
        a->setCheckable(true);
        a->setToolTip(v.tip);
        vg->addAction(a);
        const Canvas::View vv = v.v;
        connect(a, &QAction::triggered, this, [this, vv] { m_canvas->setView(vv); });
        QAction *old = m_viewActs.value(vv);
        m_viewActs[vv] = a;
        if (old) {                                   // keep menu + toolbar checks in sync
            connect(a, &QAction::triggered, old, [old] { old->setChecked(true); });
        }
    }
    QAction *persp = vb->addAction("Persp");
    persp->setCheckable(true);
    persp->setToolTip("Perspective camera for the ISO 3D view (Ctrl+P)");
    connect(persp, &QAction::toggled, this, [this](bool on) { mutate([on](ViewSettings &s) { s.perspective = on; }); });
    m_syncers.push_back([persp](const ViewSettings &s) { QSignalBlocker b(persp); persp->setChecked(s.perspective); });
    QAction *fit = vb->addAction("Fit");
    fit->setToolTip("Zoom to fit everything (Ctrl+0) - or press F to fit the selection");
    connect(fit, &QAction::triggered, this, [this] { m_canvas->zoomFit(); });

    vb->addSeparator();
    vb->addWidget(new QLabel(" Sketch plane "));
    auto *pg = new QActionGroup(this);
    const struct { const char *n; Canvas::Plane p; } pls[] = {
        {"XY", Canvas::Plane::TopXY}, {"XZ", Canvas::Plane::XZ}, {"ZY", Canvas::Plane::ZY}, {"YX", Canvas::Plane::YX}};
    for (const auto &p : pls) {
        QAction *a = vb->addAction(p.n);
        a->setCheckable(true);
        a->setToolTip("Draw on this plane (Tab cycles planes)");
        pg->addAction(a);
        const Canvas::Plane pp = p.p;
        connect(a, &QAction::triggered, this, [this, pp] { m_canvas->setPlane(pp); });
        QAction *old = m_planeActs.value(pp);
        m_planeActs[pp] = a;
        if (old)
            connect(a, &QAction::triggered, old, [old] { old->setChecked(true); });
    }
    QAction *look = vb->addAction("Look at");
    look->setToolTip("Turn the camera to look straight at the sketch plane (Ctrl+Shift+L)");
    connect(look, &QAction::triggered, this, [this] { m_canvas->lookAtPlane(); });

    vb->addSeparator();
    auto addToggle = [this, vb](const QString &text, const QString &tip, bool ViewSettings::*field) {
        QAction *a = vb->addAction(text);
        a->setCheckable(true);
        a->setToolTip(tip);
        connect(a, &QAction::toggled, this, [this, field](bool on) { mutate([field, on](ViewSettings &s) { s.*field = on; }); });
        m_syncers.push_back([a, field](const ViewSettings &s) { QSignalBlocker b(a); a->setChecked(s.*field); });
    };
    addToggle("Grid snap", "Snap to the grid (F9)", &ViewSettings::snapGrid);
    addToggle("Object snap", "Snap to endpoints, midpoints and centres (F3)", &ViewSettings::snapObject);
    addToggle("Ortho", "Constrain to the plane's horizontal / vertical axes (F8)", &ViewSettings::ortho);
    addToggle("Polar", "Polar tracking: snap the direction to angle steps (F10). Hold Shift = temporary Ortho", &ViewSettings::polar);
    m_polarCombo = new QComboBox;
    for (double a : {5.0, 10.0, 15.0, 22.5, 30.0, 45.0, 90.0})
        m_polarCombo->addItem(QString::number(a) + "\u00B0", a);
    m_polarCombo->setCurrentIndex(2);
    m_polarCombo->setToolTip("Polar tracking angle step");
    connect(m_polarCombo, &QComboBox::activated, this, [this](int i) {
        const double a = m_polarCombo->itemData(i).toDouble();
        mutate([a](ViewSettings &s) { s.polarStep = a; });
    });
    m_syncers.push_back([this](const ViewSettings &s) {
        QSignalBlocker b(m_polarCombo);
        const int i = m_polarCombo->findData(s.polarStep);
        if (i >= 0) m_polarCombo->setCurrentIndex(i);
    });
    vb->addWidget(m_polarCombo);
    addToggle("Dyn", "Dynamic input: type sizes in fields at the cursor, Tab = next field (F12)", &ViewSettings::dynInput);
    vb->addSeparator();
    vb->addWidget(new QLabel(" Grid "));
    m_gridCombo = new QComboBox;
    m_gridCombo->setMinimumContentsLength(7);
    rebuildGridCombo();
    connect(m_gridCombo, &QComboBox::activated, this, [this](int i) {
        m_canvas->setGridSize(m_gridCombo->itemData(i).toDouble());
    });
    vb->addWidget(m_gridCombo);

    addToolBarBreak();

    // ---- tools ----
    QToolBar *tb = addToolBar("Tools");
    tb->setObjectName("toolsBar");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->setIconSize(QSize(22, 22));
    auto *tg = new QActionGroup(this);
    auto addTool = [this, tb, tg](const QString &name, Canvas::Tool t, const QString &key, const QString &tip, bool onBar = true) {
        // tools that live only in the 3D Tools panel still get their shortcut through the window
        QAction *a = onBar ? tb->addAction(name) : new QAction(name, this);
        if (!onBar)
            addAction(a);
        a->setCheckable(true);
        a->setShortcut(QKeySequence(key));
        a->setToolTip(QString("<b>%1</b>  [%2]<br>%3").arg(name, key, tip));
        QString ik = name.toLower();
        if (ik == "dynmove") ik = "move";
        a->setIcon(uiIcon(ik));
        tg->addAction(a);
        connect(a, &QAction::triggered, this, [this, t] { m_canvas->setTool(t); });
        m_toolActs[t] = a;
    };
    addTool("Select", Canvas::Tool::Select, "S", "Select, drag to move, box-select");
    tb->addSeparator();
    addTool("Line", Canvas::Tool::Line, "L", "Straight line");
    addTool("Polyline", Canvas::Tool::Polyline, "P", "Connected line segments (Enter / double-click to finish)");
    addTool("Rect", Canvas::Tool::Rect, "R", "Rectangle from two corners");
    addTool("Circle", Canvas::Tool::Circle, "C", "Circle: centre then rim");
    addTool("Arc", Canvas::Tool::Arc, "A", "3-point arc");
    addTool("Curve", Canvas::Tool::Curve3, "U", "Smooth curve through 3 points");
    addTool("Dim", Canvas::Tool::Dimension, "D", "Dimension line");
    addTool("Hatch", Canvas::Tool::Hatch, "H", "Hatch / colour fill: click inside any closed boundary - lines, arcs, circles, "
                                               "polylines that meet or cross. Islands inside stay open. Pattern, spacing and "
                                               "colour: Properties panel");
    tb->addSeparator();
    addTool("Box", Canvas::Tool::Box, "Shift+B", "3D box / board: first corner, opposite corner, height. Start on a board's face "
                                                  "and the new box grows out of that face. Type sizes; Enter = board thickness");
    addTool("PressPull", Canvas::Tool::PressPull, "Shift+P", "Press/Pull: click a face of a board and pull or push it - snap to a "
                                                             "corner of another board, or type the distance. Inside a closed "
                                                             "shape it makes a board - or cuts / joins (N J C I while pulling).");
    // the newer modelling tools sit in the 3D Tools panel (buildToolsDock), not on this toolbar
    addTool("Fillet", Canvas::Tool::Fillet, "Shift+F", "Fillet: point at a corner of a shape or an edge of a board and click; "
                                                       "type the radius. Board edges along the thickness, any edge of a box.", false);
    addTool("Chamfer", Canvas::Tool::Chamfer, "Shift+H", "Chamfer: like Fillet, a straight bevel; type the distance", false);
    addTool("Shell", Canvas::Tool::Shell, "Shift+S", "Shell: click the face to leave open; a box becomes a carcass of separate "
                                                     "boards (sides, top, bottom, back). Type the wall thickness.", false);
    addTool("Revolve", Canvas::Tool::Revolve, "Shift+O", "Revolve: click a closed shape, then two points of the axis (in its plane); "
                                                         "type the angle first (Enter = 360). Legs, knobs, bowls.", false);
    addTool("Pipe", Canvas::Tool::Pipe, "Shift+I", "Pipe: click a line / polyline / arc - a rod or tube along it with true bends. "
                                                   "Type the diameter; wall and bend radius: Model > Pipe settings", false);
    tb->addSeparator();
    addTool("Text", Canvas::Tool::Text, "T", "Text label");
    addTool("Image", Canvas::Tool::InsertImage, "I", "Place the picked image (File > Insert Image)");
    tb->addSeparator();
    addTool("Trim", Canvas::Tool::Trim, "Q", "Trim: click the part to remove (hold Shift to Extend)");
    addTool("Join", Canvas::Tool::Join, "J", "Join two lines into a corner");
    addTool("Break", Canvas::Tool::Break, "B", "Split a line in two");
    addTool("Offset", Canvas::Tool::Offset, "W", "Parallel copy: click the object, then the side. Type a distance + Enter, T = through point");
    addTool("Extend", Canvas::Tool::Extend, "Shift+X", "Run a line to the next edge (hold Shift to Trim)");
    addTool("Cutter", Canvas::Tool::Cutter, "Shift+C", "Cutter blade: slice every shape the blade crosses, in any plane (Cut mode below)");
    m_cutCombo = new QComboBox;
    m_cutCombo->addItems({"Cut: View", "Cut: True 3D"});
    m_cutCombo->setToolTip("View = cut what visibly crosses on screen (any plane mix). True 3D = only real 3D contacts.");
    connect(m_cutCombo, &QComboBox::activated, this, [this](int i) { m_canvas->setCutMode(i); });
    connect(m_canvas, &Canvas::cutModeChanged, this, [this](int m) { QSignalBlocker b(m_cutCombo); m_cutCombo->setCurrentIndex(m); });
    tb->addWidget(m_cutCombo);
    m_bladeCombo = new QComboBox;
    m_bladeCombo->addItems({"Blade: Split", "Blade: Cut away side"});
    m_bladeCombo->setToolTip("Split = cut into pieces. Cut away = remove everything on one side of an endless blade.");
    connect(m_bladeCombo, &QComboBox::activated, this, [this](int i) { m_canvas->setBladeAction(i); });
    connect(m_canvas, &Canvas::bladeActionChanged, this, [this](int m) { QSignalBlocker b(m_bladeCombo); m_bladeCombo->setCurrentIndex(m); });
    tb->addWidget(m_bladeCombo);
    addTool("Rotate", Canvas::Tool::Rotate, "Shift+R", "Rotate: pick the objects, a pivot point, then the angle (mouse or typed). "
                                                       "X / Y / Z turn about that axis, K leaves a copy, Shift = 15° steps");
    addTool("Mirror", Canvas::Tool::Mirror, "Shift+M", "Mirror objects about a line you pick (K = keep original)");
    QAction *patt = tb->addAction(uiIcon("pattern"), "Pattern");
    patt->setShortcut(QKeySequence("Shift+A"));
    patt->setToolTip("Rectangular / polar pattern of the selection  [Shift+A]");
    connect(patt, &QAction::triggered, this, &MainWindow::openPattern);
    tb->addSeparator();
    addTool("Edit", Canvas::Tool::Edit, "E", "Drag vertex handles");
    addTool("Move", Canvas::Tool::Move, "M", "Precise move: pick objects, base point, then target or typed distance + angle (K = copy)");
    m_copyAct = tb->addAction(uiIcon("copy"), "Copy");
    m_copyAct->setCheckable(true);
    m_copyAct->setToolTip("Move leaves a copy behind (K while moving)");
    connect(m_copyAct, &QAction::toggled, this, [this](bool on) { m_canvas->setMoveCopy(on); });
    connect(m_canvas, &Canvas::moveCopyChanged, this, [this](bool on) { QSignalBlocker b(m_copyAct); m_copyAct->setChecked(on); });
    addTool("Drag", Canvas::Tool::FreeMove, "Shift+V", "Free drag-move (vertices snap onto other geometry)");
    addTool("DynMove", Canvas::Tool::DynMove, "N", "Axis-locked move with distance readout");
    {
        auto *gz = new QComboBox;
        gz->addItem(uiIcon("gizmo"), "Gizmo: Off");
        gz->addItem(uiIcon("gizmo"), "Gizmo: Move");
        gz->addItem(uiIcon("gizmo"), "Gizmo: Rotate");
        gz->setCurrentIndex(int(m_canvas->gizmoMode()));
        gz->setToolTip("Gizmo on the selection (Select tool): arrows move along X / Y / Z, squares move in a plane, "
                       "rings turn about an axis. Click a handle, move, click - or type a distance / angle + Enter.");
        connect(gz, &QComboBox::activated, this, [this](int i) { m_canvas->setGizmoMode(Canvas::GizmoMode(i)); });
        connect(m_canvas, &Canvas::gizmoModeChanged, gz, [gz](int m) { QSignalBlocker b(gz); gz->setCurrentIndex(m); });
        tb->addWidget(gz);
    }
    tb->addSeparator();
    tb->addWidget(new QLabel(" Dim "));
    m_dimCombo = new QComboBox;
    m_dimCombo->addItems({"Aligned", "Horizontal", "Vertical"});
    m_dimCombo->setToolTip("Dimension measuring mode (keys while dimensioning: K aligned, H horizontal, V vertical)");
    connect(m_dimCombo, &QComboBox::activated, this, [this](int i) { m_canvas->setDimMode(i); });
    connect(m_canvas, &Canvas::dimModeChanged, this, [this](int m) { QSignalBlocker b(m_dimCombo); m_dimCombo->setCurrentIndex(m); });
    tb->addWidget(m_dimCombo);
    m_toolActs[Canvas::Tool::Select]->setChecked(true);
    connect(m_canvas, &Canvas::message, this, [this](const QString &s) {
        Q_UNUSED(s);
        if (QAction *a = m_toolActs.value(m_canvas->tool()))
            a->setChecked(true);
    });
}

// ---------------------------------------------------------------------------
// docks
// ---------------------------------------------------------------------------
static QDoubleSpinBox *spin(double mn, double mx, double val, const QString &suffix, double step = 1.0,
                            int dec = 2)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(mn, mx);
    s->setDecimals(dec);
    s->setSingleStep(step);
    s->setValue(val);
    if (!suffix.isEmpty())
        s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    return s;
}

static QIcon lineStyleIcon(int s, const QColor &c = QColor(210, 220, 235))
{
    QPixmap pm(64, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(c, 2.0, Qt::SolidLine, s == 2 ? Qt::RoundCap : Qt::FlatCap);
    static const QVector<qreal> pat[] = {{}, {5, 2.5}, {0.01, 2.5}, {6, 2, 0.01, 2}, {9, 2, 2, 2}, {3, 2.5}};
    if (s > 0)
        pen.setDashPattern(pat[std::clamp(s, 0, 5)]);
    p.setPen(pen);
    p.drawLine(2, 7, 62, 7);
    return QIcon(pm);
}

static const char *kStyleNames[] = {"Solid", "Dash", "Dot", "Dash-dot", "Centre", "Hidden"};
static const double kWeights[] = {0.09, 0.13, 0.18, 0.25, 0.35, 0.5, 0.7, 1.0, 1.4, 2.0, 3.0};

QWidget *MainWindow::buildPropertiesPanel()
{
    auto *panel = new QWidget;
    auto *lay = new QVBoxLayout(panel);

    m_selInfo = new QLabel("Nothing selected");
    m_selInfo->setWordWrap(true);
    m_selInfo->setStyleSheet("background:#0d1117; border:1px solid #232c3a; border-radius:6px; padding:8px; "
                             "color:#9fe8dc; font-family:Consolas,monospace;");
    lay->addWidget(m_selInfo);
    {
        m_nameEdit = new QLineEdit;
        m_nameEdit->setPlaceholderText("Part name (for the cut list) - Enter to apply");
        m_nameEdit->setToolTip("Name the selected board(s), e.g. Side, Top, Shelf. The cut list counts identical boards "
                               "with the same name together.");
        connect(m_nameEdit, &QLineEdit::returnPressed, this, [this] { m_canvas->setSelectedName(m_nameEdit->text()); });
        auto *nl = new QHBoxLayout;
        nl->addWidget(new QLabel("Name"));
        nl->addWidget(m_nameEdit, 1);
        lay->addLayout(nl);
        m_matCombo = new QComboBox;
        m_matCombo->setEditable(true);
        m_matCombo->setInsertPolicy(QComboBox::NoInsert);
        m_matCombo->setToolTip("Board material: the cut list and the sheet nesting are grouped by it. Pick one or type your own + Enter");
        m_matCombo->lineEdit()->setPlaceholderText("Material, e.g. 18 mm ply");
        auto applyMat = [this] {
            if (!m_syncing && m_canvas->hasSelection())
                m_canvas->setSelectedMaterial(m_matCombo->currentText());
        };
        connect(m_matCombo, &QComboBox::activated, this, applyMat);
        connect(m_matCombo->lineEdit(), &QLineEdit::returnPressed, this, applyMat);
        auto *ml = new QHBoxLayout;
        ml->addWidget(new QLabel("Material"));
        ml->addWidget(m_matCombo, 1);
        lay->addLayout(ml);
    }

    auto *style = new QGroupBox("Pen");
    auto *f1 = new QFormLayout(style);
    m_colorBtn = new QPushButton("Choose...");
    connect(m_colorBtn, &QPushButton::clicked, this, &MainWindow::applyColor);
    f1->addRow("Colour", m_colorBtn);
    m_widthSpin = spin(0.05, 10.0, m_canvas->defaultWidth(), " mm", 0.05);
    connect(m_widthSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_canvas->setSelectedWidth(v); });
    f1->addRow("Line width", m_widthSpin);
    m_lineStyleCombo = new QComboBox;
    for (int i = 0; i < 6; ++i)
        m_lineStyleCombo->addItem(lineStyleIcon(i), kStyleNames[i]);
    connect(m_lineStyleCombo, &QComboBox::activated, this, [this](int i) { m_canvas->setSelectedLineStyle(i); if (m_styleCombo) { QSignalBlocker b(m_styleCombo); m_styleCombo->setCurrentIndex(i); } });
    f1->addRow("Line type", m_lineStyleCombo);
    {
        auto *fillBtn = new QPushButton("Choose...");
        fillBtn->setToolTip("Fill closed shapes (rectangle, circle, closed polyline). Use the alpha value for transparency.");
        connect(fillBtn, &QPushButton::clicked, this, [this] { pickFill(); });
        auto *noFill = new QPushButton("None");
        connect(noFill, &QPushButton::clicked, this, [this] { m_canvas->setSelectedFill(QColor()); });
        auto *fl = new QHBoxLayout;
        fl->addWidget(fillBtn, 1);
        fl->addWidget(noFill);
        f1->addRow("Fill", fl);
    }
    m_textSpin = lenSpin(0.5, 2000.0, m_canvas->defaultTextSize(), 0.5);
    connect(m_textSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_canvas->setSelectedTextSize(v); });
    f1->addRow("Text height", m_textSpin);
    m_offsetSpin = lenSpin(0.001, 100000.0, 5.0, 1.0);
    connect(m_offsetSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_canvas->setOffsetDistance(v); });
    connect(m_canvas, &Canvas::offsetDistanceChanged, this, [this](double v) {
        if (v > m_offsetSpin->maximum())
            m_offsetSpin->setMaximum(v);
        QSignalBlocker b(m_offsetSpin);
        m_offsetSpin->setValue(v);
    });
    f1->addRow("Offset distance", m_offsetSpin);
    lay->addWidget(style);

    // ---- hatch / colour fill inside closed boundaries ----
    auto *hg = new QGroupBox("Hatch / colour fill");
    auto *fh = new QFormLayout(hg);
    auto *hatchTool = new QPushButton(uiIcon("hatch"), "Hatch tool  (H)  -  click inside a boundary");
    hatchTool->setToolTip("Click inside any area closed by lines, arcs, circles, rectangles or polylines on the sketch plane.\n"
                          "Boundaries may cross each other; shapes inside the area (islands) stay open.");
    connect(hatchTool, &QPushButton::clicked, this, [this] { m_canvas->setTool(Canvas::Tool::Hatch); });
    fh->addRow(hatchTool);
    m_hatchCombo = new QComboBox;
    m_hatchCombo->setIconSize(QSize(22, 22));
    const QStringList pn = Canvas::hatchPatternNames();
    for (int i = 0; i < pn.size(); ++i)
        m_hatchCombo->addItem(QIcon(Canvas::hatchSwatch(i, 22, QColor(216, 226, 240))), pn[i]);
    m_hatchCombo->setCurrentIndex(1);
    connect(m_hatchCombo, &QComboBox::activated, this, [this] { pushHatchStyle(); });
    fh->addRow("Pattern", m_hatchCombo);
    m_hatchScale = lenSpin(0, 100000, 0, 1);
    m_hatchScale->setSpecialValueText("Auto");
    m_hatchScale->setToolTip("Distance between the hatch lines. Auto = about 10 pixels at the zoom you hatch at,\n"
                             "rounded to 1, 2 or 5 - hatches made together match. The spacing is real size, so it\n"
                             "zooms with the drawing.");
    connect(m_hatchScale, &QDoubleSpinBox::valueChanged, this, [this] { pushHatchStyle(); });
    fh->addRow("Spacing", m_hatchScale);
    m_hatchAngle = spin(-360, 360, 0, " deg", 15, 1);
    connect(m_hatchAngle, &QDoubleSpinBox::valueChanged, this, [this] { pushHatchStyle(); });
    fh->addRow("Angle", m_hatchAngle);
    m_hatchFillBtn = new QPushButton;
    m_hatchFillBtn->setToolTip("Colour of a solid fill, or the background under a pattern (alpha = transparency).\n"
                               "Solid fill without a colour uses the pen colour.");
    connect(m_hatchFillBtn, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_hatchFill.isValid() ? m_hatchFill : QColor(53, 224, 200, 140), this,
                                                "Hatch fill colour (alpha = transparency)", QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            m_hatchFill = c;
            pushHatchStyle();
        }
    });
    auto *hatchNoFill = new QPushButton("None");
    connect(hatchNoFill, &QPushButton::clicked, this, [this] { m_hatchFill = QColor(); pushHatchStyle(); });
    auto *hfl = new QHBoxLayout;
    hfl->addWidget(m_hatchFillBtn, 1);
    hfl->addWidget(hatchNoFill);
    fh->addRow("Fill colour", hfl);
    auto *hatchApply = new QPushButton("Apply to selected hatches / closed shapes");
    hatchApply->setToolTip("Selected hatches, rectangles, circles and closed polylines take this pattern, spacing, angle and fill.\n"
                           "The hatch lines use the pen colour.");
    connect(hatchApply, &QPushButton::clicked, this, [this] { pushHatchStyle(); m_canvas->applyHatchToSelection(); });
    fh->addRow(hatchApply);
    lay->addWidget(hg);

    auto *sz = new QGroupBox("Size (transform)");
    auto *fs = new QFormLayout(sz);
    m_sizeW = lenSpin(0.01, 100000, 100, 5);
    m_sizeH = lenSpin(0.01, 100000, 100, 5);
    m_sizeLock = new QCheckBox("Keep proportions");
    m_sizeLock->setChecked(true);
    m_sizeAnchor = new QComboBox;
    m_sizeAnchor->addItems({"Centre", "Bottom-left corner", "Bottom-right corner", "Top-left corner", "Top-right corner"});
    connect(m_sizeW, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_syncing || !m_sizeLock->isChecked() || m_ratio <= 0) return;
        QSignalBlocker b(m_sizeH); m_sizeH->setValue(v / m_ratio);
    });
    connect(m_sizeH, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_syncing || !m_sizeLock->isChecked() || m_ratio <= 0) return;
        QSignalBlocker b(m_sizeW); m_sizeW->setValue(v * m_ratio);
    });
    auto *applySize = new QPushButton("Apply size");
    applySize->setToolTip("Resize the selection to exactly this width and height (in the sketch plane). "
                          "You can also drag the 8 grips around the selection; Shift = free corner scaling.");
    connect(applySize, &QPushButton::clicked, this, [this] {
        m_canvas->resizeSelection(m_sizeW->value(), m_sizeH->value(), m_sizeAnchor->currentIndex());
    });
    fs->addRow("Width", m_sizeW);
    fs->addRow("Height", m_sizeH);
    fs->addRow(m_sizeLock);
    fs->addRow("Keep fixed", m_sizeAnchor);
    fs->addRow(applySize);
    lay->addWidget(sz);

    auto *tg = new QGroupBox("Text");
    auto *ft = new QFormLayout(tg);
    m_textEdit = new QPlainTextEdit;
    m_textEdit->setPlaceholderText("Select a text object, edit here, then Apply");
    m_textEdit->setMaximumHeight(70);
    auto *applyT = new QPushButton("Apply text");
    connect(applyT, &QPushButton::clicked, this, [this] { m_canvas->setSelectedTextContent(m_textEdit->toPlainText()); });
    ft->addRow(m_textEdit);
    ft->addRow(applyT);
    m_fontCombo = new QFontComboBox;
    m_boldChk = new QCheckBox("Bold");
    m_italicChk = new QCheckBox("Italic");
    auto applyFont = [this] {
        if (!m_syncing)
            m_canvas->setSelectedFont(static_cast<QFontComboBox *>(m_fontCombo)->currentFont().family(),
                                      m_boldChk->isChecked(), m_italicChk->isChecked());
    };
    connect(static_cast<QFontComboBox *>(m_fontCombo), &QFontComboBox::currentFontChanged, this, applyFont);
    connect(m_boldChk, &QCheckBox::toggled, this, applyFont);
    connect(m_italicChk, &QCheckBox::toggled, this, applyFont);
    ft->addRow("Font", m_fontCombo);
    auto *bi = new QHBoxLayout; bi->addWidget(m_boldChk); bi->addWidget(m_italicChk); bi->addStretch(1);
    ft->addRow(bi);
    m_textAngle = spin(-360, 360, 0, " deg", 15);
    connect(m_textAngle, &QDoubleSpinBox::valueChanged, this, [this](double v) { if (!m_syncing) m_canvas->setSelectedTextAngle(v); });
    ft->addRow("Rotation", m_textAngle);
    auto *editT = new QPushButton("Edit on canvas  (F2 / double-click)");
    connect(editT, &QPushButton::clicked, this, [this] { m_canvas->editSelectedText(); });
    ft->addRow(editT);
    lay->addWidget(tg);

    auto *alg = new QGroupBox("Align && distribute");
    auto *agl = new QGridLayout(alg);
    using AM = Canvas::AlignMode;
    const struct { const char *icon, *tip; int mode; } abtn[] = {
        {"align-left", "Align left", int(AM::Left)}, {"align-hcenter", "Align centre", int(AM::HCenter)},
        {"align-right", "Align right", int(AM::Right)}, {"align-top", "Align top", int(AM::Top)},
        {"align-vcenter", "Align middle", int(AM::VCenter)}, {"align-bottom", "Align bottom", int(AM::Bottom)}};
    int ai = 0;
    for (const auto &a : abtn) {
        auto *b = new QToolButton;
        b->setIcon(uiIcon(a.icon));
        b->setToolTip(a.tip);
        const int mode = a.mode;
        connect(b, &QToolButton::clicked, this, [this, mode] { m_canvas->alignSelection(AM(mode), m_alignToPage); });
        agl->addWidget(b, ai / 3, ai % 3);
        ++ai;
    }
    auto *dh = new QToolButton; dh->setIcon(uiIcon("dist-h")); dh->setToolTip("Distribute horizontally (equal gaps)");
    auto *dv = new QToolButton; dv->setIcon(uiIcon("dist-v")); dv->setToolTip("Distribute vertically (equal gaps)");
    auto *cp = new QToolButton; cp->setIcon(uiIcon("center-page")); cp->setToolTip("Centre on page");
    connect(dh, &QToolButton::clicked, this, [this] { m_canvas->distributeSelection(true); });
    connect(dv, &QToolButton::clicked, this, [this] { m_canvas->distributeSelection(false); });
    connect(cp, &QToolButton::clicked, this, [this] {
        m_canvas->alignSelection(AM::HCenter, true);
        m_canvas->alignSelection(AM::VCenter, true);
    });
    agl->addWidget(dh, 2, 0); agl->addWidget(dv, 2, 1); agl->addWidget(cp, 2, 2);
    auto *relChk = new QCheckBox("Relative to page");
    relChk->setChecked(m_alignToPage);
    connect(relChk, &QCheckBox::toggled, this, [this](bool on) { m_alignToPage = on; QSettings().setValue("ui/alignPage", on); });
    agl->addWidget(relChk, 3, 0, 1, 3);
    lay->addWidget(alg);

    auto *tr = new QGroupBox("Transform selection");
    auto *f2 = new QFormLayout(tr);
    m_angleSpin = spin(-360, 360, 15, " deg", 5);
    auto *rotAxis = new QComboBox;
    rotAxis->addItems({"Plane", "X", "Y", "Z"});
    rotAxis->setToolTip("Turn about the sketch plane's normal, or about the world X / Y / Z axis (through the selection centre). "
                        "Boards and circles turn onto any plane.");
    auto *rot = new QPushButton(uiIcon("rotate"), "Rotate");
    rot->setToolTip("Rotate the selection by this angle about the chosen axis. For a pivot point: the Rotate tool (Shift+R)");
    connect(rot, &QPushButton::clicked, this, [this, rotAxis] {
        const int i = rotAxis->currentIndex();
        m_canvas->rotateSelection3D(m_angleSpin->value(), i == 0 ? 3 : i - 1);
    });
    auto *r1 = new QHBoxLayout; r1->addWidget(m_angleSpin, 1); r1->addWidget(rotAxis); r1->addWidget(rot);
    f2->addRow("Rotate", r1);
    {
        auto *r90 = new QHBoxLayout;
        const char *an[] = {"X", "Y", "Z"};
        for (int k = 0; k < 3; ++k) {
            auto *b = new QPushButton(QString("%1 90°").arg(an[k]));
            b->setToolTip(QString("Turn the selection 90° about %1 (Shift+click: -90°) - e.g. stand a board up").arg(an[k]));
            connect(b, &QPushButton::clicked, this, [this, k] {
                const bool back = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
                m_canvas->rotateSelection3D(back ? -90 : 90, k);
            });
            r90->addWidget(b);
        }
        f2->addRow("Turn", r90);
    }
    {
        auto *fh = new QPushButton(uiIcon("flip-h"), "Flip H");
        auto *fv = new QPushButton(uiIcon("flip-v"), "Flip V");
        connect(fh, &QPushButton::clicked, this, [this] { m_canvas->flipSelection(true); });
        connect(fv, &QPushButton::clicked, this, [this] { m_canvas->flipSelection(false); });
        auto *r5 = new QHBoxLayout; r5->addWidget(fh); r5->addWidget(fv);
        f2->addRow("Flip", r5);
        auto *bf = new QPushButton(uiIcon("front"), "Front");
        auto *bb = new QPushButton(uiIcon("back"), "Back");
        connect(bf, &QPushButton::clicked, this, [this] { m_canvas->arrangeSelection(true); });
        connect(bb, &QPushButton::clicked, this, [this] { m_canvas->arrangeSelection(false); });
        auto *r6 = new QHBoxLayout; r6->addWidget(bf); r6->addWidget(bb);
        f2->addRow("Order", r6);
    }
    m_scaleSpin = spin(0.01, 100, 2.0, "x", 0.1);
    auto *scl = new QPushButton("Scale");
    connect(scl, &QPushButton::clicked, this, [this] { m_canvas->scaleSelection(m_scaleSpin->value()); });
    auto *r2 = new QHBoxLayout; r2->addWidget(m_scaleSpin, 1); r2->addWidget(scl);
    f2->addRow("Scale", r2);

    m_vx = lenSpin(-100000, 100000, 50, 5);
    m_vy = lenSpin(-100000, 100000, 0, 5);
    m_vz = lenSpin(-100000, 100000, 0, 5);
    auto *vr = new QHBoxLayout;
    for (auto *s : {m_vx, m_vy, m_vz}) { s->setMinimumWidth(60); vr->addWidget(s); }
    f2->addRow("Vector X Y Z", vr);
    auto *mv = new QPushButton("Move by vector");
    connect(mv, &QPushButton::clicked, this, [this] {
        m_canvas->moveSelectionBy(QVector3D(m_vx->value(), m_vy->value(), m_vz->value()));
    });
    f2->addRow(mv);
    m_arrayCount = new QSpinBox;
    m_arrayCount->setRange(2, 500);
    m_arrayCount->setValue(3);
    auto *arr = new QPushButton("Array copies");
    arr->setToolTip("Copies the selection N-1 more times, each shifted by the vector");
    connect(arr, &QPushButton::clicked, this, [this] {
        m_canvas->arraySelection(m_arrayCount->value(), QVector3D(m_vx->value(), m_vy->value(), m_vz->value()));
    });
    auto *r3 = new QHBoxLayout; r3->addWidget(m_arrayCount, 1); r3->addWidget(arr);
    f2->addRow("Array count", r3);

    auto *mh = new QPushButton("Mirror L/R");
    auto *mvb = new QPushButton("Mirror U/D");
    m_keepMirror = new QCheckBox("Keep original");
    m_keepMirror->setChecked(true);
    connect(m_keepMirror, &QCheckBox::toggled, this, [this](bool on) { if (!m_syncing) m_canvas->setMirrorKeep(on); });
    connect(m_canvas, &Canvas::mirrorKeepChanged, this, [this](bool on) { QSignalBlocker b(m_keepMirror); m_keepMirror->setChecked(on); });
    connect(mh, &QPushButton::clicked, this, [this] { m_canvas->mirrorSelection(true, m_keepMirror->isChecked()); });
    connect(mvb, &QPushButton::clicked, this, [this] { m_canvas->mirrorSelection(false, m_keepMirror->isChecked()); });
    auto *r4 = new QHBoxLayout; r4->addWidget(mh); r4->addWidget(mvb);
    f2->addRow(r4);
    f2->addRow(m_keepMirror);

    lay->addWidget(tr);

    auto *mg = new QGroupBox("Model (solids, constraints)");
    auto *fm = new QFormLayout(mg);
    {
        m_boardSpin = lenSpin(0.1, 10000, m_canvas->boxHeight(), 1);
        m_boardSpin->setToolTip("Box tool: Enter at the height step makes a board this thick");
        connect(m_boardSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_canvas->setBoxHeight(v); });
        auto *boxB = new QPushButton(uiIcon("box"), "Box");
        boxB->setToolTip("Box tool (Shift+B)");
        connect(boxB, &QPushButton::clicked, this, [this] { m_canvas->setTool(Canvas::Tool::Box); });
        auto *ppB = new QPushButton(uiIcon("presspull"), "Pull");
        ppB->setToolTip("Press/Pull tool (Shift+P)");
        connect(ppB, &QPushButton::clicked, this, [this] { m_canvas->setTool(Canvas::Tool::PressPull); });
        auto *rb = new QHBoxLayout; rb->addWidget(m_boardSpin, 1); rb->addWidget(boxB); rb->addWidget(ppB);
        fm->addRow("Board", rb);
        auto *cl = new QPushButton(uiIcon("cutlist"), "Cut list of the boards...");
        cl->setToolTip("Every board with its length, width, thickness and count - copy, save as CSV for Excel, "
                       "or place it on the drawing (Ctrl+Shift+B)");
        connect(cl, &QPushButton::clicked, this, &MainWindow::openCutList);
        fm->addRow(cl);
        auto *bu = new QPushButton(uiIcon("union"), "Union");
        auto *bs = new QPushButton(uiIcon("subtract"), "Subtract");
        auto *bi = new QPushButton(uiIcon("intersect"), "Intersect");
        bu->setToolTip("Selected boards become one board (Ctrl+Alt+U)");
        bs->setToolTip("The FIRST selected board minus the others (Ctrl+Alt+S). Model menu: a version that keeps the cutters");
        bi->setToolTip("Only the overlap of the selected boards stays (Ctrl+Alt+I)");
        connect(bu, &QPushButton::clicked, this, [this] { runBoolean(Canvas::BoolOp::Union); });
        connect(bs, &QPushButton::clicked, this, [this] { runBoolean(Canvas::BoolOp::Subtract); });
        connect(bi, &QPushButton::clicked, this, [this] { runBoolean(Canvas::BoolOp::Intersect); });
        auto *rbool = new QHBoxLayout; rbool->addWidget(bu); rbool->addWidget(bs); rbool->addWidget(bi);
        fm->addRow("Boolean", rbool);
    }
    m_extrudeSpin = lenSpin(-100000, 100000, 18, 1);
    auto *ext = new QPushButton(uiIcon("extrude"), "Solid");
    ext->setToolTip("Closed shapes become solid bodies of this thickness (Ctrl+E). Bodies are never cut by Trim / Cutter.");
    connect(ext, &QPushButton::clicked, this, [this] { m_canvas->extrudeSolid(m_extrudeSpin->value()); });
    auto *extW = new QPushButton("Wire");
    extW->setToolTip("Old style: sweep the shapes into separate wireframe lines");
    connect(extW, &QPushButton::clicked, this, [this] { m_canvas->extrudeSelection(m_extrudeSpin->value()); });
    auto *r5 = new QHBoxLayout; r5->addWidget(m_extrudeSpin, 1); r5->addWidget(ext); r5->addWidget(extW);
    fm->addRow("Extrude", r5);
    auto *shellSpin = lenSpin(0.1, 10000, 18, 1);
    auto *shellBtn = new QPushButton("Shell");
    shellBtn->setToolTip("Rectangular body -> open box: base plate + 4 walls of this thickness, grouped");
    connect(shellBtn, &QPushButton::clicked, this, [this, shellSpin] { m_canvas->shellBody(shellSpin->value()); });
    auto *r7 = new QHBoxLayout; r7->addWidget(shellSpin, 1); r7->addWidget(shellBtn);
    fm->addRow("Shell", r7);
    auto *faceT = new QPushButton("Top face");
    auto *faceB = new QPushButton("Bottom face");
    auto *split = new QPushButton("Split");
    faceT->setToolTip("Put the sketch plane on the top face of the selected body");
    split->setToolTip("Split the selected bodies where the current sketch plane crosses them");
    connect(faceT, &QPushButton::clicked, this, [this] { m_canvas->sketchOnBodyFace(true); });
    connect(faceB, &QPushButton::clicked, this, [this] { m_canvas->sketchOnBodyFace(false); });
    connect(split, &QPushButton::clicked, this, [this] { m_canvas->splitBodyAtPlane(); });
    auto *r8 = new QHBoxLayout; r8->addWidget(faceT); r8->addWidget(faceB); r8->addWidget(split);
    fm->addRow("Body", r8);
    auto *filSpin = lenSpin(0, 100000, 10, 1);
    auto *fil = new QPushButton("Fillet");
    auto *cha = new QPushButton("Chamfer");
    fil->setToolTip("Select two lines: round their corner with this radius (0 = sharp corner)");
    cha->setToolTip("Select two lines: bevel their corner by this distance");
    connect(fil, &QPushButton::clicked, this, [this, filSpin] { m_canvas->filletLines(filSpin->value(), false); });
    connect(cha, &QPushButton::clicked, this, [this, filSpin] { m_canvas->filletLines(filSpin->value(), true); });
    auto *r9 = new QHBoxLayout; r9->addWidget(filSpin, 1); r9->addWidget(fil); r9->addWidget(cha);
    fm->addRow("Corner", r9);
    auto *cg = new QGridLayout;
    const char *cl[] = {"Horiz", "Vert", "Parallel", "Perp", "Equal", "Join ends"};
    for (int k = 0; k < 6; ++k) {
        auto *b = new QPushButton(cl[k]);
        b->setToolTip("Selected lines; the FIRST selected line is the reference");
        connect(b, &QPushButton::clicked, this, [this, k] { m_canvas->constrainSelection(k); });
        cg->addWidget(b, k / 3, k % 3);
    }
    auto *fixB = new QPushButton("Fix");
    fixB->setToolTip("Lock the selection: it still snaps but cannot be selected, moved or cut (Ctrl+L)");
    connect(fixB, &QPushButton::clicked, this, [this] { m_canvas->fixSelection(); });
    auto *unfixB = new QPushButton("Unfix all");
    connect(unfixB, &QPushButton::clicked, this, [this] { m_canvas->unfixAll(); });
    cg->addWidget(fixB, 2, 0);
    cg->addWidget(unfixB, 2, 1, 1, 2);
    fm->addRow("Constrain", cg);
    auto *hideB = new QPushButton(uiIcon("hide"), "Hide");
    auto *isoB = new QPushButton("Isolate");
    auto *showB = new QPushButton(uiIcon("show"), "Show all");
    connect(hideB, &QPushButton::clicked, this, [this] { m_canvas->hideSelection(); });
    connect(isoB, &QPushButton::clicked, this, [this] { m_canvas->isolateSelection(); });
    connect(showB, &QPushButton::clicked, this, [this] { m_canvas->showAll(); });
    auto *r10 = new QHBoxLayout; r10->addWidget(hideB); r10->addWidget(isoB); r10->addWidget(showB);
    fm->addRow("Visibility", r10);
    lay->addWidget(mg);
    lay->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(panel);
    updateSwatch();
    return scroll;
}

QWidget *MainWindow::buildViewportPanel()
{
    auto *panel = new QWidget;
    auto *lay = new QVBoxLayout(panel);

    // --- quick views ---
    auto *gv = new QGroupBox("Views");
    auto *grid = new QGridLayout(gv);
    const struct { const char *n; Canvas::View v; } vs[] = {
        {"Top", Canvas::View::Top}, {"Front", Canvas::View::Front}, {"Left", Canvas::View::Side},
        {"Bottom", Canvas::View::Bottom}, {"Back", Canvas::View::Back}, {"Right", Canvas::View::Right}};
    int i = 0;
    for (const auto &v : vs) {
        auto *b = new QPushButton(v.n);
        const Canvas::View vv = v.v;
        connect(b, &QPushButton::clicked, this, [this, vv] { m_canvas->setView(vv); });
        grid->addWidget(b, i / 3, i % 3);
        ++i;
    }
    auto *iso = new QPushButton("ISO 3D");
    connect(iso, &QPushButton::clicked, this, [this] { m_canvas->setView(Canvas::View::Iso); });
    auto *rst = new QPushButton("Reset ISO");
    connect(rst, &QPushButton::clicked, this, [this] { m_canvas->resetIsoView(); });
    auto *fit = new QPushButton("Fit all");
    connect(fit, &QPushButton::clicked, this, [this] { m_canvas->zoomFit(); });
    grid->addWidget(iso, 2, 0);
    grid->addWidget(rst, 2, 1);
    grid->addWidget(fit, 2, 2);
    lay->addWidget(gv);

    // --- camera ---
    auto *gc = new QGroupBox("Camera");
    auto *fc = new QFormLayout(gc);
    auto *persp = new QCheckBox("Perspective (ISO view)");
    connect(persp, &QCheckBox::toggled, this, [this](bool on) { mutate([on](ViewSettings &s) { s.perspective = on; }); });
    m_syncers.push_back([persp](const ViewSettings &s) { QSignalBlocker b(persp); persp->setChecked(s.perspective); });
    fc->addRow(persp);
    auto *fov = spin(10, 120, 35, " deg", 5, 0);
    connect(fov, &QDoubleSpinBox::valueChanged, this, [this](double v) { mutate([v](ViewSettings &s) { s.fov = v; }); });
    m_syncers.push_back([fov](const ViewSettings &s) { QSignalBlocker b(fov); fov->setValue(s.fov); });
    fc->addRow("Field of view", fov);
    auto *orb = spin(0.1, 2.0, 0.4, " deg/px", 0.1);
    connect(orb, &QDoubleSpinBox::valueChanged, this, [this](double v) { mutate([v](ViewSettings &s) { s.orbitSpeed = v; }); });
    m_syncers.push_back([orb](const ViewSettings &s) { QSignalBlocker b(orb); orb->setValue(s.orbitSpeed); });
    fc->addRow("Orbit speed", orb);
    auto *freeOrbit = new QCheckBox("Free orbit (any axis, 360\xC2\xB0)");
    freeOrbit->setToolTip("Right-drag turns the model about any axis, over the top and upside down (Ctrl = roll).\n"
                          "Off = turntable: the Top direction stays up.");
    connect(freeOrbit, &QCheckBox::toggled, this, [this](bool on) { mutate([on](ViewSettings &s) { s.freeOrbit = on; }); });
    m_syncers.push_back([freeOrbit](const ViewSettings &s) { QSignalBlocker b(freeOrbit); freeOrbit->setChecked(s.freeOrbit); });
    fc->addRow(freeOrbit);
    auto *orientRow = new QHBoxLayout;
    auto *setTop = new QPushButton("View = Top");
    setTop->setToolTip("Make what you see now the Top view (the view cube follows). Right-click the cube for Front, Left ...");
    connect(setTop, &QPushButton::clicked, this, [this] { m_canvas->setCurrentViewAs(Canvas::View::Top); });
    auto *setFront = new QPushButton("View = Front");
    setFront->setToolTip("Make what you see now the Front view");
    connect(setFront, &QPushButton::clicked, this, [this] { m_canvas->setCurrentViewAs(Canvas::View::Front); });
    auto *resetOri = new QPushButton("Reset");
    resetOri->setToolTip("Reset view orientation: Top / Front are the world axes again");
    connect(resetOri, &QPushButton::clicked, this, [this] { m_canvas->resetViewOrientation(); });
    orientRow->addWidget(setTop);
    orientRow->addWidget(setFront);
    orientRow->addWidget(resetOri);
    fc->addRow("Orientation", orientRow);
    auto *ztc = new QCheckBox("Zoom towards cursor");
    connect(ztc, &QCheckBox::toggled, this, [this](bool on) { mutate([on](ViewSettings &s) { s.zoomToCursor = on; }); });
    m_syncers.push_back([ztc](const ViewSettings &s) { QSignalBlocker b(ztc); ztc->setChecked(s.zoomToCursor); });
    fc->addRow(ztc);
    auto *inv = new QCheckBox("Invert mouse wheel");
    connect(inv, &QCheckBox::toggled, this, [this](bool on) { mutate([on](ViewSettings &s) { s.invertWheel = on; }); });
    m_syncers.push_back([inv](const ViewSettings &s) { QSignalBlocker b(inv); inv->setChecked(s.invertWheel); });
    fc->addRow(inv);
    lay->addWidget(gc);

    // --- sketch plane ---
    auto *gp = new QGroupBox("Sketch plane");
    auto *fp = new QFormLayout(gp);
    m_planeCombo = new QComboBox;
    m_planeCombo->addItems({"XY  (Top)", "XZ  (Front)", "ZY  (Side)", "YX"});
    connect(m_planeCombo, &QComboBox::activated, this, [this](int i) { m_canvas->setPlane(Canvas::Plane(i)); });
    fp->addRow("Plane", m_planeCombo);
    m_planeOffsetSpin = lenSpin(-100000, 100000, 0, 5);
    connect(m_planeOffsetSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!m_syncing) m_canvas->setPlaneOffset(v);
    });
    m_planeOffsetSpin->setToolTip("Slide the plane along its normal. Hover a point and press O to put the plane through it.");
    fp->addRow("Offset", m_planeOffsetSpin);
    auto *sync = new QCheckBox("Follow viewport (Top -> XY ...)");
    connect(sync, &QCheckBox::toggled, this, [this](bool on) { mutate([on](ViewSettings &s) { s.syncPlane = on; }); });
    m_syncers.push_back([sync](const ViewSettings &s) { QSignalBlocker b(sync); sync->setChecked(s.syncPlane); });
    fp->addRow(sync);
    lay->addWidget(gp);

    // --- units ---
    auto *gu = new QGroupBox("Units");
    auto *fu = new QFormLayout(gu);
    auto *sys = new QComboBox;
    for (int k = 0; k < Units::SystemCount; ++k)
        sys->addItem(Units::systemName(k));
    sys->setToolTip("How lengths are shown and typed. The drawing itself is always kept exact, so you can switch at any time.");
    connect(sys, &QComboBox::activated, this, [this](int k) { mutate([k](ViewSettings &s) { s.units = k; }); });
    fu->addRow("Lengths", sys);
    auto *dec = new QSpinBox;
    dec->setRange(0, 6);
    dec->setToolTip("Decimal places (mm, cm, m, decimal inches / feet, engineering inches)");
    connect(dec, &QSpinBox::valueChanged, this, [this](int v) { mutate([v](ViewSettings &s) { s.unitDecimals = v; }); });
    fu->addRow("Decimals", dec);
    auto *frac = new QComboBox;
    for (int d : {2, 4, 8, 16, 32, 64})
        frac->addItem(QString("1/%1\"").arg(d), d);
    frac->setToolTip("Architectural feet-inches are rounded to this fraction of an inch");
    connect(frac, &QComboBox::activated, this, [this, frac](int k) {
        const int d = frac->itemData(k).toInt();
        mutate([d](ViewSettings &s) { s.unitFrac = d; });
    });
    fu->addRow("Fractions", frac);
    auto *hint = new QLabel;
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#8fa3bf;");
    fu->addRow(hint);
    m_syncers.push_back([sys, dec, frac, hint](const ViewSettings &s) {
        QSignalBlocker b1(sys), b2(dec), b3(frac);
        sys->setCurrentIndex(s.units);
        dec->setValue(s.unitDecimals);
        dec->setEnabled(s.units != Units::Architectural);
        const int fi = frac->findData(s.unitFrac);
        frac->setCurrentIndex(fi >= 0 ? fi : 3);
        frac->setEnabled(s.units == Units::Architectural);
        hint->setText("Type: " + Units::inputHint());
    });
    lay->addWidget(gu);

    // --- display ---
    auto *gd = new QGroupBox("Display");
    auto *fd = new QFormLayout(gd);
    auto check = [this, fd](const QString &t, bool ViewSettings::*field) {
        auto *c = new QCheckBox(t);
        connect(c, &QCheckBox::toggled, this, [this, field](bool on) { mutate([field, on](ViewSettings &s) { s.*field = on; }); });
        m_syncers.push_back([c, field](const ViewSettings &s) { QSignalBlocker b(c); c->setChecked(s.*field); });
        fd->addRow(c);
    };
    check("Grid", &ViewSettings::showGrid);
    check("Axes", &ViewSettings::showAxes);
    check("Paper sheet", &ViewSettings::showPaper);
    check("Sketch-plane sheet", &ViewSettings::showPlaneSheet);
    check("Orientation gizmo", &ViewSettings::showGizmo);
    check("On-screen info", &ViewSettings::showHud);
    auto *ext = lenSpin(50, 50000, 400, 50);
    ext->setToolTip("Half size of the grid and of the sketch sheet (up to 50 m = 164 ft each way)");
    connect(ext, &QDoubleSpinBox::valueChanged, this, [this](double v) { mutate([v](ViewSettings &s) { s.gridExtent = v; }); });
    m_syncers.push_back([ext](const ViewSettings &s) { QSignalBlocker b(ext); ext->setValue(s.gridExtent); });
    fd->addRow("Grid half-size", ext);
    auto *lw = spin(0.5, 5.0, 1.0, "x", 0.25);
    connect(lw, &QDoubleSpinBox::valueChanged, this, [this](double v) { mutate([v](ViewSettings &s) { s.lineScale = v; }); });
    m_syncers.push_back([lw](const ViewSettings &s) { QSignalBlocker b(lw); lw->setValue(s.lineScale); });
    fd->addRow("Line weight", lw);
    auto *theme = new QComboBox;
    theme->addItems({"Dark", "Graphite", "Blueprint", "Light"});
    connect(theme, &QComboBox::activated, this, [this](int i) { mutate([i](ViewSettings &s) { s.theme = i; }); });
    m_syncers.push_back([theme](const ViewSettings &s) { QSignalBlocker b(theme); theme->setCurrentIndex(s.theme); });
    fd->addRow("Theme", theme);
    lay->addWidget(gd);
    lay->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(panel);
    return scroll;
}

QWidget *MainWindow::buildObjectsPanel()
{
    auto *panel = new QWidget;
    auto *lay = new QVBoxLayout(panel);
    m_objects = new QListWidget;
    m_objects->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_objects, &QListWidget::itemSelectionChanged, this, [this] {
        if (m_syncing)
            return;
        QList<int> idx;
        for (const QModelIndex &mi : m_objects->selectionModel()->selectedRows())
            idx << mi.row();
        m_syncing = true;
        m_canvas->selectIndices(idx);
        m_syncing = false;
        syncSelection();
    });
    connect(m_objects, &QListWidget::itemDoubleClicked, this, [this] { m_canvas->zoomFitSelection(); });
    lay->addWidget(m_objects, 1);
    auto *del = new QPushButton("Delete selected");
    connect(del, &QPushButton::clicked, this, [this] { m_canvas->deleteSelection(); });
    lay->addWidget(del);
    return panel;
}

void MainWindow::buildDocks()
{
    auto make = [this](const QString &title, const QString &name, QWidget *w) {
        auto *d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                       QDockWidget::DockWidgetClosable);
        addDockWidget(Qt::RightDockWidgetArea, d);
        d->setMinimumWidth(290);
        return d;
    };
    QDockWidget *vp = make("Viewport", "dockViewport", buildViewportPanel());
    QDockWidget *pr = make("Properties", "dockProps", buildPropertiesPanel());
    QDockWidget *ob = make("Objects", "dockObjects", buildObjectsPanel());
    tabifyDockWidget(vp, pr);
    tabifyDockWidget(pr, ob);
    vp->raise();
    if (m_viewMenu) {
        m_viewMenu->addAction(vp->toggleViewAction());
        m_viewMenu->addAction(pr->toggleViewAction());
        m_viewMenu->addAction(ob->toggleViewAction());
    }
    buildToolsDock();
    buildStyleToolBar();
    m_alignToPage = QSettings().value("ui/alignPage", false).toBool();
    buildSelBar();
}

// ---------------------------------------------------------------------------
// syncing UI with canvas state
// ---------------------------------------------------------------------------
void MainWindow::syncControls()
{
    const bool was = m_syncing;
    m_syncing = true;
    const ViewSettings &s = m_canvas->settings();
    for (const auto &f : m_syncers)
        f(s);
    if (QAction *a = m_viewActs.value(m_canvas->view()))
        a->setChecked(true);
    if (QAction *a = m_planeActs.value(m_canvas->plane()))
        a->setChecked(true);
    if (m_planeCombo) {
        QSignalBlocker b(m_planeCombo);
        m_planeCombo->setCurrentIndex(int(m_canvas->plane()));
    }
    if (m_planeOffsetSpin) {
        QSignalBlocker b(m_planeOffsetSpin);
        m_planeOffsetSpin->setValue(m_canvas->planeOffset());
    }
    if (m_gridCombo) {
        QSignalBlocker b(m_gridCombo);
        int best = 0;
        double bd = 1e18;
        for (int i = 0; i < m_gridCombo->count(); ++i) {
            const double d = std::fabs(m_gridCombo->itemData(i).toDouble() - m_canvas->gridSize());
            if (d < bd) { bd = d; best = i; }
        }
        m_gridCombo->setCurrentIndex(best);
    }
    unitsChanged();
    m_syncing = was;
}

// a new unit system re-labels every length field, the grid list and the cursor read-out
void MainWindow::unitsChanged()
{
    const Units::Options o = Units::options();
    const int key = o.system * 10000 + o.decimals * 100 + o.fracDen;
    if (key == m_unitsKey)
        return;
    const bool familyChanged = m_unitsKey >= 0 && Units::isImperial(m_unitsKey / 10000) != Units::isImperial();
    m_unitsKey = key;
    if (familyChanged) {                                    // the grid follows: 5 mm <-> 1/4"
        const double g = m_canvas->gridSize();
        double best = g, bd = 1e18;
        for (double c : Units::gridPresets()) {
            const double d = std::fabs(std::log(c / g));
            if (d < bd) { bd = d; best = c; }
        }
        m_canvas->setGridSize(best);
    }
    rebuildGridCombo();
    LengthSpin::refreshAll();
    updateCoordLabel(m_lastCursor);
    if (m_selInfo)
        m_selInfo->setText(m_canvas->selectionInfo());
    m_canvas->update();
    // units are one setting for the whole app: the other drawing windows follow
    for (MainWindow *w : windows()) {
        ViewSettings s = w->m_canvas->settings();
        if (w == this || (s.units == o.system && s.unitDecimals == o.decimals && s.unitFrac == o.fracDen))
            continue;
        s.units = o.system;
        s.unitDecimals = o.decimals;
        s.unitFrac = o.fracDen;
        w->m_canvas->setSettings(s);
    }
}

void MainWindow::rebuildGridCombo()
{
    if (!m_gridCombo)
        return;
    QSignalBlocker b(m_gridCombo);
    m_gridCombo->clear();
    const QList<double> pre = Units::gridPresets();
    int best = 0;
    double bd = 1e18;
    for (int i = 0; i < pre.size(); ++i) {
        m_gridCombo->addItem(Units::format(pre[i]), pre[i]);
        const double d = std::fabs(pre[i] - m_canvas->gridSize());
        if (d < bd) { bd = d; best = i; }
    }
    m_gridCombo->setCurrentIndex(best);
}

void MainWindow::pushHatchStyle()
{
    if (m_syncing || !m_hatchCombo)
        return;
    HatchStyle s;
    s.pattern = m_hatchCombo->currentIndex();
    s.scale = m_hatchScale->value();
    s.angle = m_hatchAngle->value();
    s.fill = m_hatchFill;
    m_canvas->setHatchStyle(s);
    updateHatchFillBtn();
}

void MainWindow::updateHatchFillBtn()
{
    if (!m_hatchFillBtn)
        return;
    if (m_hatchFill.isValid()) {
        const QColor c = m_hatchFill;
        m_hatchFillBtn->setStyleSheet(QString("background-color:rgba(%1,%2,%3,%4); color:%5; border-radius:5px; padding:4px;")
                                          .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha())
                                          .arg(c.lightness() > 128 && c.alpha() > 110 ? "black" : "white"));
        m_hatchFillBtn->setText(c.name(QColor::HexArgb));
    } else {
        m_hatchFillBtn->setStyleSheet(QString());
        m_hatchFillBtn->setText("None  (solid = pen colour)");
    }
}

void MainWindow::updateStatusLabels()
{
    m_viewLabel->setText(QString("%1  |  PLANE %2").arg(Canvas::viewName(m_canvas->view()),
                                                         Canvas::planeName(m_canvas->plane())));
}

void MainWindow::syncSelection()
{
    if (m_selInfo)
        m_selInfo->setText(m_canvas->selectionInfo());
    if (m_nameEdit && !m_nameEdit->hasFocus()) {
        m_nameEdit->setText(m_canvas->selectedName());
        m_nameEdit->setEnabled(m_canvas->hasSelection());
    }
    if (m_matCombo && !m_matCombo->hasFocus()) {
        QSignalBlocker mb(m_matCombo);
        QStringList mats{"", "18 mm ply", "12 mm ply", "9 mm ply", "6 mm ply", "18 mm MDF", "16 mm particle board",
                         "25 mm solid wood"};
        for (const QString &m : m_canvas->materialsInUse())
            if (!mats.contains(m))
                mats << m;
        m_matCombo->clear();
        m_matCombo->addItems(mats);
        m_matCombo->setCurrentText(m_canvas->selectedMaterial());
        m_matCombo->setEnabled(m_canvas->hasSelection());
    }
    if (!m_objects || m_syncing)
        return;
    m_syncing = true;
    if (!m_canvas->items().isEmpty()) {
        const SketchItem *first = nullptr;
        for (const SketchItem *it : m_canvas->items())
            if (it->selected) { first = it; break; }
        if (first) {
            QSignalBlocker b1(m_widthSpin), b2(m_widthCombo), b3(m_styleCombo), b4(m_lineStyleCombo);
            m_widthSpin->setValue(first->width);
            m_widthCombo->setCurrentText(QString::number(first->width, 'g', 3) + " mm");
            m_styleCombo->setCurrentIndex(first->lineStyle);
            m_lineStyleCombo->setCurrentIndex(first->lineStyle);
            if (m_palBtn) {
                QPixmap pm(18, 18);
                pm.fill(first->color);
                QPainter pp(&pm);
                pp.setPen(QColor(120, 130, 150));
                pp.drawRect(0, 0, 17, 17);
                m_palBtn->setIcon(QIcon(pm));
            }
            if (first->type() == SketchItem::Hatch && m_hatchCombo) {   // the hatch panel shows the picked hatch
                QSignalBlocker h1(m_hatchCombo), h2(m_hatchScale), h3(m_hatchAngle);
                m_hatchCombo->setCurrentIndex(first->hatch);
                m_hatchScale->setValue(first->hatchScale);
                m_hatchAngle->setValue(first->hatchAngle);
                m_hatchFill = first->fill;
                updateHatchFillBtn();
                m_canvas->setHatchStyle({first->hatch, first->hatchScale, first->hatchAngle, first->fill});
            }
        }
    }
    {
        QSizeF sz;
        const bool has = m_canvas->selectionSizeUV(sz);
        if (m_sizeW) {
            QSignalBlocker b1(m_sizeW), b2(m_sizeH);
            if (has) {
                m_sizeW->setValue(std::max(0.01, sz.width()));
                m_sizeH->setValue(std::max(0.01, sz.height()));
                m_ratio = sz.height() > 1e-9 ? sz.width() / sz.height() : 1.0;
            }
            m_sizeW->setEnabled(has);
            m_sizeH->setEnabled(has);
        }
    }
    if (const SketchItem *t = m_canvas->selectedText()) {
        QSignalBlocker b1(m_textEdit), b2(m_fontCombo), b3(m_boldChk), b4(m_italicChk), b5(m_textAngle), b6(m_textSpin);
        m_textEdit->setPlainText(t->text);
        static_cast<QFontComboBox *>(m_fontCombo)->setCurrentFont(QFont(t->fontFamily.isEmpty() ? QApplication::font().family() : t->fontFamily));
        m_boldChk->setChecked(t->bold);
        m_italicChk->setChecked(t->italic);
        m_textAngle->setValue(t->textAngle);
        m_textSpin->setValue(t->textSize);
    }
    QSignalBlocker b(m_objects);
    m_objects->clearSelection();
    const auto &items = m_canvas->items();
    for (int i = 0; i < items.size() && i < m_objects->count(); ++i)
        if (items[i]->selected)
            m_objects->item(i)->setSelected(true);
    m_syncing = false;
}

void MainWindow::refreshObjects()
{
    if (!m_objects)
        return;
    QSignalBlocker b(m_objects);
    m_objects->clear();
    int n = 0;
    for (const SketchItem *it : m_canvas->items()) {
        auto *li = new QListWidgetItem(QString("%1.  %2").arg(++n, 3).arg(Canvas::itemLabel(it)));
        QPixmap sw(12, 12);
        sw.fill(it->color);
        li->setIcon(QIcon(sw));
        m_objects->addItem(li);
    }
}

// ---------------------------------------------------------------------------
// colour
// ---------------------------------------------------------------------------
QMenu *MainWindow::buildAlignMenu(QWidget *parent)
{
    auto *am = new QMenu("&Align && distribute", parent);
    am->setIcon(uiIcon("align"));
    using AM = Canvas::AlignMode;
    const struct { const char *t, *icon, *key; AM mode; } al[] = {
        {"Align left", "align-left", "Ctrl+Alt+Left", AM::Left},
        {"Align centre (horizontal)", "align-hcenter", "Ctrl+Alt+H", AM::HCenter},
        {"Align right", "align-right", "Ctrl+Alt+Right", AM::Right},
        {"Align top", "align-top", "Ctrl+Alt+Up", AM::Top},
        {"Align middle (vertical)", "align-vcenter", "Ctrl+Alt+V", AM::VCenter},
        {"Align bottom", "align-bottom", "Ctrl+Alt+Down", AM::Bottom}};
    for (const auto &a : al) {
        const AM mode = a.mode;
        am->addAction(uiIcon(a.icon), a.t, QKeySequence(a.key), this, [this, mode] { m_canvas->alignSelection(mode, m_alignToPage); });
    }
    am->addSeparator();
    am->addAction(uiIcon("dist-h"), "Distribute horizontally (equal gaps)", QKeySequence("Ctrl+Alt+Shift+H"), this, [this] { m_canvas->distributeSelection(true); });
    am->addAction(uiIcon("dist-v"), "Distribute vertically (equal gaps)", QKeySequence("Ctrl+Alt+Shift+V"), this, [this] { m_canvas->distributeSelection(false); });
    am->addSeparator();
    am->addAction(uiIcon("center-page"), "Centre on page", this, [this] {
        m_canvas->alignSelection(Canvas::AlignMode::HCenter, true);
        m_canvas->alignSelection(Canvas::AlignMode::VCenter, true);
    });
    am->addAction(uiIcon("center-origin"), "Centre on origin", this, [this] { m_canvas->centerSelectionOn(QVector3D()); });
    QAction *rel = am->addAction("Align relative to the page");
    rel->setCheckable(true);
    rel->setChecked(m_alignToPage);
    connect(rel, &QAction::toggled, this, [this](bool on) { m_alignToPage = on; QSettings().setValue("ui/alignPage", on); });
    return am;
}

void MainWindow::buildSelBar()
{
    m_selBar = new QFrame(m_canvas);
    m_selBar->setObjectName("selBar");
    m_selBar->setFocusPolicy(Qt::NoFocus);
    m_selBar->setStyleSheet("#selBar{background:rgba(14,20,30,238);border:1px solid #35e0c8;border-radius:11px;}"
                            "QToolButton{background:transparent;border:none;padding:5px;border-radius:7px;}"
                            "QToolButton:hover{background:#17384a;}");
    auto *lay = new QHBoxLayout(m_selBar);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(2);
    auto btn = [this, lay](const QString &icon, const QString &tip, std::function<void()> fn) {
        auto *b = new QToolButton;
        b->setIcon(uiIcon(icon));
        b->setIconSize(QSize(20, 20));
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, [fn] { fn(); });
        lay->addWidget(b);
        return b;
    };
    btn("move", "Move precisely  [M]", [this] { m_canvas->setMoveCopy(false); m_canvas->setTool(Canvas::Tool::Move); });
    btn("copy", "Move a copy  [M then K]", [this] { m_canvas->setMoveCopy(true); m_canvas->setTool(Canvas::Tool::Move); });
    m_sbGroup = btn("group", "Group  [Ctrl+G]", [this] { m_canvas->groupSelection(); });
    m_sbUngroup = btn("ungroup", "Ungroup  [Ctrl+Shift+G]", [this] { m_canvas->ungroupSelection(); });
    auto *al = new QToolButton;
    al->setIcon(uiIcon("align"));
    al->setIconSize(QSize(20, 20));
    al->setToolTip("Align & distribute");
    al->setPopupMode(QToolButton::InstantPopup);
    al->setFocusPolicy(Qt::NoFocus);
    al->setMenu(buildAlignMenu(al));
    lay->addWidget(al);
    if (m_palBtn && m_palBtn->menu()) {
        auto *pc = new QToolButton;
        pc->setIcon(uiIcon("palette"));
        pc->setIconSize(QSize(20, 20));
        pc->setToolTip("Colour");
        pc->setPopupMode(QToolButton::InstantPopup);
        pc->setFocusPolicy(Qt::NoFocus);
        pc->setMenu(m_palBtn->menu());
        lay->addWidget(pc);
    }
    btn("flip-h", "Flip horizontal  [Alt+Shift+H]", [this] { m_canvas->flipSelection(true); });
    btn("flip-v", "Flip vertical  [Alt+Shift+V]", [this] { m_canvas->flipSelection(false); });
    btn("fill", "Fill colour of closed shapes", [this] { pickFill(); });
    btn("front", "Bring to front  [Ctrl+]]", [this] { m_canvas->arrangeSelection(true); });
    btn("extrude", "Extrude solid  [Ctrl+E]", [this] { m_canvas->extrudeSolid(m_extrudeSpin ? m_extrudeSpin->value() : 18); });
    btn("hide", "Hide  [Ctrl+H]", [this] { m_canvas->hideSelection(); });
    btn("explode", "Explode at joints  [Ctrl+Shift+X]", [this] { m_canvas->explodeSelection(); });
    {
        auto *wb = new QToolButton;
        wb->setIcon(uiIcon("weight"));
        wb->setIconSize(QSize(20, 20));
        wb->setToolTip("Line thickness");
        wb->setPopupMode(QToolButton::InstantPopup);
        wb->setFocusPolicy(Qt::NoFocus);
        auto *wm = new QMenu(wb);
        for (double w : {0.13, 0.18, 0.25, 0.35, 0.5, 0.7, 1.0, 1.4, 2.0})
            wm->addAction(QString::number(w, 'g', 3) + " mm", this, [this, w] { m_canvas->setSelectedWidth(w); syncSelection(); });
        wb->setMenu(wm);
        lay->addWidget(wb);
        auto *tb = new QToolButton;
        tb->setIcon(uiIcon("linetype"));
        tb->setIconSize(QSize(20, 20));
        tb->setToolTip("Line type");
        tb->setPopupMode(QToolButton::InstantPopup);
        tb->setFocusPolicy(Qt::NoFocus);
        auto *tm = new QMenu(tb);
        for (int k = 0; k < 6; ++k)
            tm->addAction(lineStyleIcon(k), kStyleNames[k], this, [this, k] { m_canvas->setSelectedLineStyle(k); syncSelection(); });
        tb->setMenu(tm);
        lay->addWidget(tb);
    }
    btn("delete", "Delete  [Del]", [this] { m_canvas->deleteSelection(); });
    m_selBar->hide();
    auto *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, [this] { updateSelBar(); });
    t->start(60);
}

void MainWindow::updateSelBar()
{
    if (!m_selBar)
        return;
    const QRectF r = m_canvas->hasSelection() && m_canvas->selectionBarAllowed() ? m_canvas->selectionScreenRect() : QRectF();
    if (r.isNull() && r.width() == 0 && r.height() == 0) {
        m_selBar->hide();
        return;
    }
    m_sbGroup->setVisible(m_canvas->selectedCount() > 1);
    m_sbUngroup->setVisible(m_canvas->selectionHasGroup());
    m_selBar->adjustSize();
    int x = int(r.center().x() - m_selBar->width() / 2.0);
    int y = int(r.bottom() + 24);                       // below the box: the grips and size tag stay free
    if (y + m_selBar->height() > m_canvas->height() - 8)
        y = int(r.top() - m_selBar->height() - 40);
    x = std::clamp(x, 8, std::max(8, m_canvas->width() - m_selBar->width() - 8));
    y = std::clamp(y, 8, std::max(8, m_canvas->height() - m_selBar->height() - 8));
    m_selBar->move(x, y);
    m_selBar->show();
    m_selBar->raise();
}

void MainWindow::buildStyleToolBar()
{
    addToolBarBreak();
    QToolBar *st = addToolBar("Style");
    st->setObjectName("styleBar");
    st->addWidget(new QLabel(" Colour "));
    m_palBtn = new QToolButton;
    m_palBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_palBtn->setPopupMode(QToolButton::InstantPopup);
    m_palBtn->setToolTip("Colour of the selection / new objects");
    auto *menu = new QMenu(m_palBtn);
    const char *cols[] = {"#1e1e1e", "#6b7280", "#ffffff", "#e53935", "#fb8c00", "#fdd835", "#43a047", "#00acc1",
                          "#1e88e5", "#3949ab", "#8e24aa", "#d81b60", "#795548", "#00e5ff", "#76ff03", "#ff4081"};
    for (const char *cn : cols) {
        QPixmap pm(16, 16);
        pm.fill(QColor(cn));
        QAction *a = menu->addAction(QIcon(pm), QString(cn));
        connect(a, &QAction::triggered, this, [this, cn] { m_canvas->setSelectedColor(QColor(cn)); updateSwatch(); });
    }
    menu->addSeparator();
    menu->addAction("More colours...", this, &MainWindow::applyColor);
    m_palBtn->setMenu(menu);
    st->addWidget(m_palBtn);

    st->addWidget(new QLabel("  Weight "));
    m_widthCombo = new QComboBox;
    m_widthCombo->setEditable(true);
    for (double w : kWeights)
        m_widthCombo->addItem(QString::number(w, 'g', 3) + " mm", w);
    m_widthCombo->setCurrentText(QString::number(m_canvas->defaultWidth(), 'g', 3) + " mm");
    m_widthCombo->setMinimumWidth(92);
    m_widthCombo->setToolTip("Line thickness in mm (type your own value)");
    auto applyW = [this](const QString &t) {
        bool ok = false;
        const double v = QString(t).remove("mm").trimmed().toDouble(&ok);
        if (ok && v > 0.01 && v <= 20) {
            m_canvas->setSelectedWidth(v);
            if (m_widthSpin) { QSignalBlocker b(m_widthSpin); m_widthSpin->setValue(v); }
        }
    };
    connect(m_widthCombo, &QComboBox::textActivated, this, applyW);
    st->addWidget(m_widthCombo);

    st->addWidget(new QLabel("  Type "));
    m_styleCombo = new QComboBox;
    for (int i = 0; i < 6; ++i)
        m_styleCombo->addItem(lineStyleIcon(i), kStyleNames[i]);
    m_styleCombo->setToolTip("Line type of the selection / new objects");
    connect(m_styleCombo, &QComboBox::activated, this, [this](int i) {
        m_canvas->setSelectedLineStyle(i);
        if (m_lineStyleCombo) { QSignalBlocker b(m_lineStyleCombo); m_lineStyleCombo->setCurrentIndex(i); }
    });
    st->addWidget(m_styleCombo);
    st->addSeparator();
    QAction *g = st->addAction(uiIcon("group"), "Group");
    g->setToolTip("Group the selected objects  [Ctrl+G]");
    connect(g, &QAction::triggered, this, [this] { m_canvas->groupSelection(); });
    QAction *ug = st->addAction(uiIcon("ungroup"), "Ungroup");
    ug->setToolTip("Dissolve the selected groups  [Ctrl+Shift+G]");
    connect(ug, &QAction::triggered, this, [this] { m_canvas->ungroupSelection(); });
    updateSwatch();
}

void MainWindow::updateSwatch()
{
    const QColor c = m_canvas->defaultColor();
    m_colorBtn->setStyleSheet(QString("background-color:%1; color:%2; border-radius:5px; padding:4px;")
                                  .arg(c.name(), c.lightness() > 128 ? "black" : "white"));
    m_colorBtn->setText(c.name());
    if (m_palBtn) {
        QPixmap pm(18, 18);
        pm.fill(c);
        QPainter pp(&pm);
        pp.setPen(QColor(120, 130, 150));
        pp.drawRect(0, 0, 17, 17);
        m_palBtn->setIcon(QIcon(pm));
    }
}

void MainWindow::applyColor()
{
    const QColor c = QColorDialog::getColor(m_canvas->defaultColor(), this, "Pen colour");
    if (!c.isValid())
        return;
    m_canvas->setSelectedColor(c);
    updateSwatch();
}

// ---------------------------------------------------------------------------
// files
// ---------------------------------------------------------------------------
void MainWindow::newFile()
{
    if (!maybeSave())
        return;
    QDialog dlg(this);
    dlg.setWindowTitle("New Canvas");
    auto *lay = new QVBoxLayout(&dlg);
    auto *a4 = new QRadioButton("A4  (210 x 297 mm)", &dlg);
    auto *a3 = new QRadioButton("A3  (297 x 420 mm)", &dlg);
    auto *letter = new QRadioButton("Letter  (8 1/2 x 11 in)", &dlg);
    auto *tabloid = new QRadioButton("Tabloid  (11 x 17 in)", &dlg);
    auto *custom = new QRadioButton("Custom size", &dlg);
    (Units::isImperial() ? letter : a4)->setChecked(true);
    lay->addWidget(a4);
    lay->addWidget(a3);
    lay->addWidget(letter);
    lay->addWidget(tabloid);
    lay->addWidget(custom);
    auto *cb = new QWidget(&dlg);
    auto *cl = new QHBoxLayout(cb);
    auto *wS = lenSpin(20, 50000, 210, 10);
    auto *hS = lenSpin(20, 50000, 297, 10);
    cl->addWidget(new QLabel("Width"));
    cl->addWidget(wS);
    cl->addWidget(new QLabel("Height"));
    cl->addWidget(hS);
    cb->setEnabled(false);
    lay->addWidget(cb);
    connect(custom, &QRadioButton::toggled, cb, &QWidget::setEnabled);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QSizeF size(210, 297);
    if (a3->isChecked()) size = QSizeF(297, 420);
    else if (letter->isChecked()) size = QSizeF(215.9, 279.4);
    else if (tabloid->isChecked()) size = QSizeF(279.4, 431.8);
    else if (custom->isChecked()) size = QSizeF(wS->value(), hS->value());
    m_canvas->setPageSizeMm(size);
    if (m_canvas->customOrientation())
        m_canvas->resetViewOrientation();
    m_canvas->resetHome();
    m_canvas->clearAll();
    m_canvas->undoStack()->clear();
    m_canvas->undoStack()->setClean();
    m_canvas->zoomFit();
    m_filePath.clear();
    updateTitle();
}

bool MainWindow::openPath(const QString &path)
{
    if (!m_canvas->loadFile(path)) {
        QMessageBox::warning(this, "Sketch3D", "Could not open file:\n" + path);
        return false;
    }
    m_filePath = path;
    m_canvas->undoStack()->clear();
    m_canvas->undoStack()->setClean();
    m_canvas->zoomFit();
    addRecent(path);
    updateTitle();
    return true;
}

bool MainWindow::openFile()
{
    const QString path = QFileDialog::getOpenFileName(this, "Open Sketch", QString(), "Sketch3D Files (*.sk3)");
    return !path.isEmpty() && openInThisOrOtherWindow(path);
}

// a file that is open already just comes to the front; otherwise it replaces this window's drawing
bool MainWindow::openInThisOrOtherWindow(const QString &path)
{
    if (MainWindow *w = windowFor(path)) {
        if (w->isMinimized())
            w->showNormal();
        w->raise();
        w->activateWindow();
        return true;
    }
    return maybeSave() && openPath(path);
}

void MainWindow::openInNewWindow()
{
    const QString path = QFileDialog::getOpenFileName(this, "Open Sketch in a New Window", QString(), "Sketch3D Files (*.sk3)");
    if (path.isEmpty())
        return;
    if (MainWindow *w = windowFor(path)) {
        w->raise();
        w->activateWindow();
        return;
    }
    MainWindow *w = newWindow();
    if (!w->openPath(path))
        w->close();
}

// ---------------------------------------------------------------------------
// separate drawing windows
// ---------------------------------------------------------------------------
QList<MainWindow *> MainWindow::windows()
{
    QList<MainWindow *> out;
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *mw = qobject_cast<MainWindow *>(w); mw && mw->isVisible())
            out << mw;
    return out;
}

MainWindow *MainWindow::windowFor(const QString &path)
{
    const QString want = QFileInfo(path).canonicalFilePath();
    if (want.isEmpty())
        return nullptr;
    for (MainWindow *w : windows())
        if (!w->m_filePath.isEmpty() && QFileInfo(w->m_filePath).canonicalFilePath().compare(want, Qt::CaseInsensitive) == 0)
            return w;
    return nullptr;
}

// a new empty drawing in its own window, with this window's current settings
MainWindow *MainWindow::newWindow()
{
    saveSettings();                                   // the new window starts from what this one shows now
    auto *w = new MainWindow;
    w->setAttribute(Qt::WA_DeleteOnClose);
    if (!isMaximized() && !isFullScreen())
        w->move(pos() + QPoint(36, 36));              // cascade so both are visible
    w->show();
    w->raise();
    w->activateWindow();
    return w;
}

QString MainWindow::documentName() const
{
    return m_filePath.isEmpty() ? QString("Untitled.sk3") : QFileInfo(m_filePath).fileName();
}

void MainWindow::rebuildWindowMenu()
{
    if (!m_windowMenu)
        return;
    m_windowMenu->clear();
    m_windowMenu->addAction(m_newWinAct);
    m_windowMenu->addAction("Open in New Window...", this, &MainWindow::openInNewWindow);
    m_windowMenu->addAction("&Tile windows side by side", this, &MainWindow::tileWindows);
    m_windowMenu->addAction(m_closeWinAct);
    m_windowMenu->addSeparator();
    int n = 0;
    for (MainWindow *w : windows()) {
        ++n;
        const QString name = w->documentName() + (w->isWindowModified() ? " *" : "");
        QAction *a = m_windowMenu->addAction(QString("&%1  %2").arg(n).arg(name), this, [w] {
            if (w->isMinimized())
                w->showNormal();
            w->raise();
            w->activateWindow();
        });
        a->setCheckable(true);
        a->setChecked(w == this);
    }
}

// every drawing window side by side on this screen, so two drawings can be compared and copied between
void MainWindow::tileWindows()
{
    const QList<MainWindow *> ws = windows();
    if (ws.isEmpty())
        return;
    const QRect area = screen()->availableGeometry();
    const int n = int(ws.size());
    const int cols = int(std::ceil(std::sqrt(double(n)))), rows = (n + cols - 1) / cols;
    for (int i = 0; i < n; ++i) {
        MainWindow *w = ws[i];
        w->showNormal();
        const QRect cell(area.x() + area.width() * (i % cols) / cols, area.y() + area.height() * (i / cols) / rows,
                         area.width() / cols, area.height() / rows);
        // geometry excludes the title bar and frame: keep the whole window inside its cell
        const QRect frame = w->frameGeometry(), inner = w->geometry();
        w->setGeometry(cell.adjusted(inner.x() - frame.x(), inner.y() - frame.y(),
                                     -(frame.right() - inner.right()), -(frame.bottom() - inner.bottom())));
        w->raise();
    }
    activateWindow();
}

bool MainWindow::saveFile()
{
    if (m_filePath.isEmpty())
        return saveFileAs();
    if (!m_canvas->saveFile(m_filePath)) {
        QMessageBox::warning(this, "Sketch3D", "Could not save file.");
        return false;
    }
    m_canvas->undoStack()->setClean();
    addRecent(m_filePath);
    updateTitle();
    return true;
}

bool MainWindow::saveFileAs()
{
    QString path = QFileDialog::getSaveFileName(this, "Save Sketch As", QString(), "Sketch3D Files (*.sk3)");
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".sk3", Qt::CaseInsensitive))
        path += ".sk3";
    m_filePath = path;
    return saveFile();
}

void MainWindow::exportPng()
{
    const QString p = QFileDialog::getSaveFileName(this, "Export PNG", "sketch.png", "PNG Images (*.png)");
    if (!p.isEmpty() && m_canvas->exportImage(p, "PNG")) statusBar()->showMessage("Exported " + p, 4000);
}

void MainWindow::exportJpg()
{
    const QString p = QFileDialog::getSaveFileName(this, "Export JPG", "sketch.jpg", "JPEG Images (*.jpg *.jpeg)");
    if (!p.isEmpty() && m_canvas->exportImage(p, "JPG")) statusBar()->showMessage("Exported " + p, 4000);
}

void MainWindow::exportPdf()
{
    // interim: vector drawing fitted to A4 (the print dialog with scale / paper / title block is still to come)
    const QString p = QFileDialog::getSaveFileName(this, "Export PDF", "sketch.pdf", "PDF Documents (*.pdf)");
    if (p.isEmpty())
        return;
    QPdfWriter w(p);
    w.setCreator("Sketch3D 3.2");
    w.setPageLayout(QPageLayout(QPageSize(QPageSize::A4),
                                m_canvas->width() > m_canvas->height() ? QPageLayout::Landscape : QPageLayout::Portrait,
                                QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter));
    QPainter painter(&w);
    if (!painter.isActive())
        return;
    painter.scale(w.logicalDpiX() / 25.4, w.logicalDpiY() / 25.4);
    const QSizeF page = w.pageLayout().fullRect(QPageLayout::Millimeter).size();
    m_canvas->paintPlot(painter, QRectF(10, 10, page.width() - 20, page.height() - 20), Canvas::PlotView());
    statusBar()->showMessage("Exported " + p, 4000);
}

void MainWindow::exportDxf()
{
    const QString p = QFileDialog::getSaveFileName(this, "Export DXF", "sketch.dxf", "DXF Drawings (*.dxf)");
    if (!p.isEmpty() && m_canvas->exportDxf(p)) statusBar()->showMessage("Exported " + p, 4000);
}

void MainWindow::insertImage()
{
    const QString path = QFileDialog::getOpenFileName(this, "Insert Image", QString(),
                                                      "Images (*.png *.jpg *.jpeg);;PNG (*.png);;JPEG (*.jpg *.jpeg)");
    if (path.isEmpty())
        return;
    const QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, "Sketch3D", "Could not load image.");
        return;
    }
    m_canvas->setPendingImage(img);
    m_canvas->setTool(Canvas::Tool::InsertImage);
    statusBar()->showMessage("Click on the canvas to place the image (it lies on the active sketch plane)", 6000);
}

void MainWindow::addRecent(const QString &path)
{
    QSettings st;
    QStringList r = st.value("recent").toStringList();
    r.removeAll(path);
    r.prepend(path);
    while (r.size() > 8)
        r.removeLast();
    st.setValue("recent", r);
    for (MainWindow *w : windows())                   // every window's Open Recent list
        if (w != this)
            w->rebuildRecentMenu();
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList r = QSettings().value("recent").toStringList();
    for (const QString &p : r) {
        if (!QFileInfo::exists(p))
            continue;
        m_recentMenu->addAction(QFileInfo(p).fileName() + "   -   " + p, this, [this, p] { openInThisOrOtherWindow(p); });
    }
    m_recentMenu->setEnabled(!m_recentMenu->isEmpty());
}

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------
void MainWindow::loadSettings()
{
    QSettings st;
    ViewSettings s;
    s.perspective = st.value("v/persp", s.perspective).toBool();
    s.fov = st.value("v/fov", s.fov).toDouble();
    s.showGrid = st.value("v/grid", s.showGrid).toBool();
    s.showAxes = st.value("v/axes", s.showAxes).toBool();
    s.showPaper = st.value("v/paper", s.showPaper).toBool();
    s.showPlaneSheet = st.value("v/sheet", s.showPlaneSheet).toBool();
    s.showGizmo = st.value("v/gizmo", s.showGizmo).toBool();
    s.showHud = st.value("v/hud", s.showHud).toBool();
    s.gridExtent = st.value("v/extent", s.gridExtent).toDouble();
    s.theme = st.value("v/theme", s.theme).toInt();
    s.lineScale = st.value("v/lineScale", s.lineScale).toDouble();
    s.orbitSpeed = st.value("v/orbit", s.orbitSpeed).toDouble();
    s.zoomToCursor = st.value("v/ztc", s.zoomToCursor).toBool();
    s.invertWheel = st.value("v/invWheel", s.invertWheel).toBool();
    s.syncPlane = st.value("v/syncPlane", s.syncPlane).toBool();
    s.snapGrid = st.value("v/snapGrid", s.snapGrid).toBool();
    s.snapObject = st.value("v/snapObj", s.snapObject).toBool();
    s.ortho = st.value("v/ortho", s.ortho).toBool();
    s.polar = st.value("v/polar", s.polar).toBool();
    s.limitSheet = st.value("v/limitSheet", s.limitSheet).toBool();
    s.snap3D = st.value("v/snap3D", s.snap3D).toBool();
    s.osnap = st.value("v/osnapModes", s.osnap).toInt();
    s.polarStep = st.value("v/polarStep", s.polarStep).toDouble();
    s.dynInput = st.value("v/dyn", s.dynInput).toBool();
    s.freeOrbit = st.value("v/freeOrbit", s.freeOrbit).toBool();
    s.units = std::clamp(st.value("v/units", s.units).toInt(), 0, int(Units::SystemCount) - 1);
    m_canvas->setBoxHeight(st.value("model/board", 18.0).toDouble());
    m_canvas->setGizmoMode(Canvas::GizmoMode(std::clamp(st.value("model/gizmo", 1).toInt(), 0, 2)));
    m_canvas->setPipeParams(st.value("pipe/d", 25.0).toDouble(), st.value("pipe/wall", 0.0).toDouble(),
                            st.value("pipe/bend", 0.0).toDouble());
    m_canvas->setShellThickness(st.value("model/shell", 18.0).toDouble());
    m_canvas->setFilletSize(st.value("model/fillet", 6.0).toDouble(), false);
    m_canvas->setFilletSize(st.value("model/chamfer", 3.0).toDouble(), true);
    if (m_boardSpin) { QSignalBlocker b(m_boardSpin); m_boardSpin->setValue(m_canvas->boxHeight()); }
    s.unitDecimals = st.value("v/unitDec", s.unitDecimals).toInt();
    s.unitFrac = st.value("v/unitFrac", s.unitFrac).toInt();
    m_canvas->setCutMode(st.value("v/cutMode", 0).toInt());
    m_canvas->setBladeAction(st.value("v/bladeAct", 0).toInt());
    m_canvas->setGridSize(st.value("v/gridSize", 5.0).toDouble());
    m_unitsKey = -1;
    m_canvas->setSettings(s);
    if (m_hatchCombo) {
        QSignalBlocker h1(m_hatchCombo), h2(m_hatchScale), h3(m_hatchAngle);
        m_hatchCombo->setCurrentIndex(std::clamp(st.value("hatch/pattern", 1).toInt(), 0, m_hatchCombo->count() - 1));
        m_hatchScale->setValue(st.value("hatch/scale", 0.0).toDouble());
        m_hatchAngle->setValue(st.value("hatch/angle", 0.0).toDouble());
        const QString hf = st.value("hatch/fill").toString();
        m_hatchFill = hf.isEmpty() ? QColor() : QColor(hf);
    }
    pushHatchStyle();
    restoreGeometry(st.value("geometry").toByteArray());
    restoreState(st.value("windowState").toByteArray());
    rebuildRecentMenu();
}

void MainWindow::saveSettings()
{
    QSettings st;
    const ViewSettings &s = m_canvas->settings();
    st.setValue("v/persp", s.perspective);
    st.setValue("v/fov", s.fov);
    st.setValue("v/grid", s.showGrid);
    st.setValue("v/axes", s.showAxes);
    st.setValue("v/paper", s.showPaper);
    st.setValue("v/sheet", s.showPlaneSheet);
    st.setValue("v/gizmo", s.showGizmo);
    st.setValue("v/hud", s.showHud);
    st.setValue("v/extent", s.gridExtent);
    st.setValue("v/theme", s.theme);
    st.setValue("v/lineScale", s.lineScale);
    st.setValue("v/orbit", s.orbitSpeed);
    st.setValue("v/ztc", s.zoomToCursor);
    st.setValue("v/invWheel", s.invertWheel);
    st.setValue("v/syncPlane", s.syncPlane);
    st.setValue("v/snapGrid", s.snapGrid);
    st.setValue("v/snapObj", s.snapObject);
    st.setValue("v/ortho", s.ortho);
    st.setValue("v/polar", s.polar);
    st.setValue("v/limitSheet", s.limitSheet);
    st.setValue("v/snap3D", s.snap3D);
    st.setValue("v/osnapModes", s.osnap);
    st.setValue("v/polarStep", s.polarStep);
    st.setValue("v/dyn", s.dynInput);
    st.setValue("v/freeOrbit", s.freeOrbit);
    st.setValue("v/units", s.units);
    st.setValue("model/board", m_canvas->boxHeight());
    st.setValue("model/gizmo", int(m_canvas->gizmoMode()));
    st.setValue("model/shell", m_canvas->shellThickness());
    st.setValue("model/fillet", m_canvas->filletSize(false));
    st.setValue("model/chamfer", m_canvas->filletSize(true));
    st.setValue("v/unitDec", s.unitDecimals);
    st.setValue("v/unitFrac", s.unitFrac);
    st.setValue("hatch/pattern", m_canvas->hatchStyle().pattern);
    st.setValue("hatch/scale", m_canvas->hatchStyle().scale);
    st.setValue("hatch/angle", m_canvas->hatchStyle().angle);
    st.setValue("hatch/fill", m_hatchFill.isValid() ? m_hatchFill.name(QColor::HexArgb) : QString());
    st.setValue("v/cutMode", m_canvas->cutMode());
    st.setValue("v/bladeAct", m_canvas->bladeAction());
    st.setValue("v/gridSize", m_canvas->gridSize());
    st.setValue("geometry", saveGeometry());
    st.setValue("windowState", saveState());
}

// ---------------------------------------------------------------------------
// help / misc
// ---------------------------------------------------------------------------
void MainWindow::showHelp()
{
    QMessageBox::information(this, "Sketch3D - quick guide",
        "<h3>Drawing in the 3D view</h3>"
        "<p>Every click is ray-cast onto the <b>sketch plane</b> (XY / XZ / ZY / YX), so you can draw "
        "directly in ISO 3D. <b>Tab</b> cycles the plane (when you are not in the middle of drawing). Hover a point and press <b>O</b> to slide the "
        "plane through it.</p>"
        "<p>After the first point of a line/polyline/arc press <b>X</b>, <b>Y</b> or <b>Z</b> to lock the "
        "cursor to that world axis (draw vertical lines!). Press again to unlock.</p>"
        "<p><b>Dynamic input (F12):</b> after the first point, size fields appear at the cursor. Just type a "
        "number, press <b>Tab</b> (or <b>,</b> / <b>&lt;</b>) to jump to the next field (length &rarr; angle, width &rarr; height), "
        "then <b>Enter</b>. Fields you typed stay locked while the other follows the mouse. <b>Esc</b> clears them.</p>"
        "<h3>Move, groups, line types, text (v2.5)</h3>"
        "<p><b>Move (M):</b> select objects (or pick them, Enter), click a <b>base point</b>, then click the target <i>or type a distance, Tab, an angle, Enter</i>. "
        "Ortho (F8 / hold Shift), polar (F10) and all object snaps apply; the live preview shows \u0394X \u0394Y \u0394Z. <b>K</b> or the Copy button leaves a copy behind (repeat to make many). "
        "<b>Drag</b> (Shift+V) and the Select tool snap any vertex of the moved objects onto other geometry.<br>"
        "<b>Group / Ungroup:</b> Ctrl+G / Ctrl+Shift+G - a group selects, moves, copies and trims as one.<br>"
        "<b>Style toolbar:</b> colour palette, true line weight in mm (type your own) and line type (solid, dash, dot, dash-dot, centre, hidden).<br>"
        "<b>Text:</b> double-click or F2 to edit; the Properties panel has multi-line content, font, bold/italic, height and rotation. Type \\n in the inline editor for a new line.</p>"
        "<h3>Cutter &amp; virtual trim (v2.2)</h3>"
        "<p>Trim (Q) and the Cutter (Shift+C) work on lines, polylines, rectangles, circles, arcs and curves in any plane. "
        "<b>Cut: View</b> cuts what visibly crosses on screen (shapes in different planes included) so the final look is right; "
        "<b>Cut: True 3D</b> cuts only real 3D intersections. Trim: click a part to remove it; drag a fence line with Trim to trim many at once. "
        "Cutter: click two blade points; <b>Split</b> slices every crossed shape, <b>Cut away side</b> uses an endless blade and a third click on the side to remove. "
        "Select objects first to limit the cut to them. Esc cancels.</p>"
        "<h3>Trim / Extend / Mirror / Pattern</h3>"
        "<p><b>Trim (Q):</b> every other entity in the plane is a cutting edge. Click the part to remove - a red preview "
        "shows what goes. Lines, polylines, rectangles and circles (circle becomes an arc). Hold <b>Shift</b> to Extend.<br>"
        "<b>Extend (Shift+X):</b> click near an end of a line/polyline to run it to the next edge (green preview). Shift = Trim.<br>"
        "<b>Mirror (Shift+M):</b> pick objects, Enter, click two points of the mirror line (live preview, F8 ortho). "
        "K toggles keep-original.<br>"
        "<b>Pattern (Shift+A):</b> rectangular (rows x columns) or polar (count, angle, centre) copies of the selection.</p>"
        "<h3>Units: metric, feet-inches (v2.9)</h3>"
        "<p><b>View &gt; Units</b> or the Viewport panel: mm, cm, m, decimal inches, decimal feet, "
        "<b>Architectural</b> 5'-6 1/2\" (fractions to 1/2 ... 1/64\") or <b>Engineering</b> 5'-6.50\". "
        "Every size field, the dimension text, the cursor read-out and the grid list follow. "
        "Type lengths in any unit, in any field or while drawing: <b>4'-6 1/2\"</b>, <b>4' 6\"</b>, <b>54\"</b>, <b>3/8\"</b>, "
        "<b>1.2m</b>, <b>120cm</b>. A bare number is in the current unit (inches for feet-inches). "
        "The grid and sketch sheet go up to 50 m (164 ft) each way.</p>"
        "<h3>Hatch and colour fill (v2.9)</h3>"
        "<p><b>H</b> = Hatch tool: hover inside any area closed by lines, arcs, circles, rectangles or polylines on the sketch "
        "plane (they may cross each other) and click. Shapes inside the area are islands and stay open. "
        "Pattern (solid, ANSI31, cross-hatch, steel, brick, dots, earth ...), spacing (Auto or a length), angle and fill colour "
        "are in the Properties panel; the lines use the pen colour. Select hatches or closed shapes and press "
        "<b>Apply to selected</b> to restyle them. Hatches are exported to PNG / PDF and, as lines, to DXF.</p>"
        "<h3>Drawing windows</h3>"
        "<p><b>Ctrl+Shift+N</b> (File &gt; New Window) opens another drawing in its own window, with its own file, undo and view; "
        "<b>File &gt; Open in New Window</b> keeps the current drawing open. <b>Window</b> lists every drawing and "
        "<b>Tile windows side by side</b> arranges them on the screen. Copy (Ctrl+C) in one window and paste (Ctrl+V) "
        "in another - the first paste lands in the same place. Opening a file that is already open brings its window "
        "to the front. Units are shared by all windows. <b>Ctrl+W</b> closes one window; File &gt; Exit closes them all.</p>"
        "<h3>3D boards: Box, Press/Pull, cut list (v3.0)</h3>"
        "<p><b>Box (Shift+B)</b>: click a corner, the opposite corner, then the height - or type them (Tab between "
        "length and width, Enter). Start on the face of a board and the box grows straight out of that face (it lights up). "
        "<b>Enter</b> at the height step makes a board of the set thickness (Properties &gt; Model &gt; Board). "
        "Corners and midpoints of every board snap.<br>"
        "<b>Press/Pull (Shift+P)</b>: click a face of a board and move: the face slides along its normal and the board "
        "grows or shrinks; snap onto a corner of another board, or type the distance + Enter. Inside a closed shape it "
        "turns the shape into a board. Esc puts it back.<br>"
        "Click a board's face to select it; boards hide each other correctly in every view. Mirror (Shift+M) and patterns "
        "keep a board's thickness on the right side.<br>"
        "<b>Name</b> boards in Properties, then <b>Model &gt; Cut list</b> (Ctrl+Shift+B): copy it, save it as CSV for "
        "Excel, or place it on the drawing.</p>"
        "<h3>Gizmo, Union / Subtract / Intersect (v3.1)</h3>"
        "<p><b>Gizmo</b> (toolbar box, Model menu): with the Select tool, the selection shows <b>arrows</b> (move along X / Y / Z) "
        "and <b>squares</b> (move in that plane) - or <b>rings</b> to turn it about X / Y / Z. Click a handle, move, click "
        "(or press - drag - release). Type a distance or an angle + Enter. Moving along an arrow over a corner of another "
        "board stops the selection's near or far side exactly there; Shift turns in 15&deg; steps.<br>"
        "<b>Union</b> (Ctrl+Alt+U): selected boards become one. <b>Subtract</b> (Ctrl+Alt+S): select the board to cut first, "
        "then Ctrl+click the cutters - notches, grooves, holes (Model menu: a version that keeps the cutters, for joints). "
        "<b>Intersect</b> (Ctrl+Alt+I): only the overlap stays. Boards must be square to each other; a result made of several "
        "pieces is grouped and counts as one part in the cut list.</p>"
        "<h3>Rotate, materials, sheets (v3.2)</h3>"
        "<p><b>Rotate (Shift+R)</b>: select, click the base point (pivot), then move and click or type the angle + Enter. "
        "<b>X / Y / Z</b> turn about that axis (again = the plane normal); Shift = 15&deg;, F8 = 90&deg;; "
        "<b>K</b> = copy (keeps making turned copies until Esc).<br>"
        "<b>Quick turns</b>: Model &gt; Rotate, or Properties &gt; Turn X / Y / Z 90&deg; (Shift+click = -90&deg;) - stand a "
        "board up in one click.<br>"
        "<b>Material</b> (Properties): the cut list gets a Material column and never mixes materials.<br>"
        "<b>Cut list &gt; Sheets</b>: sheet size, kerf, trim and turning; every material + thickness is packed onto stock "
        "sheets (fewest sheets, biggest offcut) and drawn to scale - save the layouts as PNG.</p>"
        "<h3>Fillet, Chamfer, Shell, Revolve, Pipe, Extrude cut / join (v3.3)</h3>"
        "<p><b>Sums in every typed value</b>: 800-2*18, (2400-3*18)/4, 1.2m-18, 360/7 - the field shows the result.<br>"
        "<b>Sketch on a face</b>: Rectangle / Circle / Polyline started on a board&#39;s face lie in that face. "
        "<b>Press/Pull</b> the shape: into a board it <b>cuts</b> (pocket, groove, hole), out of it a new body; "
        "while pulling N new, J join, C cut, I intersect, A auto.<br>"
        "<b>Cut joints</b> (Ctrl+Shift+J): the selected boards cut housings into every board they go into and stay whole.<br>"
        "<b>Fillet</b> (Shift+F) / <b>Chamfer</b> (Shift+H): type the size, click corners of shapes, board edges along the "
        "thickness, or any edge of a box.<br>"
        "<b>Shell</b> (Shift+S): type the wall, click the face to leave open - a box becomes sides, top, bottom and back boards.<br>"
        "<b>Revolve</b> (Shift+O): click a closed shape, then two axis points (type the angle first; Enter = 360).<br>"
        "<b>Pipe</b> (Shift+I): click a line / polyline / arc - a rod or tube with true bends; diameter typed, wall and "
        "bend radius in Model &gt; Pipe settings. The cut list gives the developed length.</p>"
        "<h3>3D Tools panel, several faces, up to a face, Combine (v3.4)</h3>"
        "<p><b>3D Tools</b> (left panel, View &gt; 3D Tools): the modelling tools, Combine commands and their sizes.<br>"
        "<b>Shift+click</b> picks several lines / bodies (Select) or faces (Press/Pull) - again takes one off.<br>"
        "<b>Press/Pull</b> the Shift+clicked faces together; move over a <b>parallel face</b> (green) to pull "
        "<b>up to</b> it - every face lands on its plane.<br>"
        "Boards selected before extruding a shape are the only ones it cuts / joins. N / J / C / I apply to that pull only.<br>"
        "<b>Combine</b> (Ctrl+Alt+C): Cut / Join / Intersect with a Targets list and a Tools list, keep the tools or not.<br>"
        "The <b>Box</b> grows with the mouse after the first corner even with Ortho / Polar on; in the Top view move up for the height.</p>"
        "<h3>Object snap, Mirror, Move (v3.5)</h3>"
        "<p><b>Object snap</b> (F3) finds every corner and edge middle of a body, the <b>centre of each face</b> and the "
        "<b>body centre</b>, and in Move / Mirror / Rotate the point <b>on the face</b> under the cursor. "
        "View &gt; Object snap settings (Ctrl+F3) switches each kind.<br>"
        "<b>Mirror</b>: point at a snap (e.g. another body&#39;s centre) and press X / Y / Z - the plane through it square to that axis.<br>"
        "<b>Move</b>: base and target snap in 3D (point to point); with the base on a face, a target over a parallel face "
        "moves it <b>up to that face</b> (green).<br>The grid hides while you orbit and comes back when you let go.<br>"
        "<b>Union / Subtract / Intersect</b> ask whether to keep the tool bodies (the first selected body is the target); "
        "3D Tools panel: Ask to keep tool bodies.</p>"
        "<h3>Dimensions</h3>"
        "<p>Click two points, then click where the dimension line goes. It is measured and offset <b>inside the active sketch "
        "plane</b>. H = horizontal, V = vertical, K = aligned (or use the Dim box in the toolbar).</p>"
        "<h3>Navigation</h3>"
        "<table cellspacing=6>"
        "<tr><td><b>Right-drag</b> / Alt+drag</td><td>Orbit (switches to ISO 3D)</td></tr>"
        "<tr><td><b>Middle-drag</b> / Shift+drag / Space+drag</td><td>Pan</td></tr>"
        "<tr><td><b>Wheel</b></td><td>Zoom to cursor</td></tr>"
        "<tr><td><b>F</b> / Ctrl+0</td><td>Fit selection / fit all</td></tr>"
        "<tr><td>View cube (top-right)</td><td>Click a face, edge or corner to look from there; house = Home; "
        "round arrows = roll 90&deg;; <b>right-click</b> = Set current view as Top / Front ..., Home, free orbit</td></tr>"
        "<tr><td>Ctrl+Shift+O</td><td>Free orbit (any axis, 360&deg;, Ctrl+right-drag = roll) / turntable</td></tr>"
        "<tr><td>Ctrl+Shift+Left / Right</td><td>Roll the view 90&deg;</td></tr>"
        "<tr><td>Ctrl+1..7</td><td>Top, Front, Left, ISO, Bottom, Back, Right</td></tr>"
        "<tr><td>Ctrl+P</td><td>Perspective / orthographic camera</td></tr>"
        "</table>"
        "<h3>Tools</h3>"
        "<p>S select &nbsp; L line &nbsp; P polyline &nbsp; R rect &nbsp; C circle &nbsp; A arc &nbsp; U curve &nbsp; "
        "D dimension &nbsp; H hatch / fill &nbsp; T text &nbsp; Q trim &nbsp; Shift+X extend &nbsp; Shift+M mirror &nbsp; Shift+A pattern &nbsp; J join &nbsp; B break &nbsp; W offset &nbsp; E edit &nbsp; "
        "M move &nbsp; N dynamic move</p>"
        "<p>F9 grid snap &nbsp; F3 object snap &nbsp; F8 ortho (hold <b>Shift</b> = temporary ortho) &nbsp; F10 polar tracking &nbsp; G grid &nbsp; "
        "Arrow keys nudge the selection (Shift = x10, PgUp/PgDn = along the plane normal).</p>"
        "<p>Right-click (without dragging) opens a context menu.</p>");
}

void MainWindow::about()
{
    QMessageBox::about(this, "About Sketch3D",
        "Sketch3D 3.5\n\nA CAD-style 3D sketch application built with Qt and CMake.\n"
        "True 3D camera (ortho / perspective), drawing directly in 3D on any sketch plane, "
        "object snaps, axis locks, numeric input, mirror / array / extrude, DXF export.");
}

bool MainWindow::maybeSave()
{
    if (m_canvas->undoStack()->isClean())
        return true;
    if (isMinimized())                                // with several windows, show which drawing is asking
        showNormal();
    raise();
    activateWindow();
    const auto r = QMessageBox::warning(this, "Sketch3D", QString("\"%1\" has been modified.\nSave your changes?").arg(documentName()),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                        QMessageBox::Save);
    if (r == QMessageBox::Save)
        return saveFile();
    return r == QMessageBox::Discard;
}

void MainWindow::updateTitle()
{
    const QString name = m_filePath.isEmpty() ? "Untitled.sk3" : QFileInfo(m_filePath).fileName();
    setWindowTitle(QString("Sketch3D - %1[*]").arg(name));
    setWindowModified(!m_canvas->undoStack()->isClean());
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (maybeSave()) {
        saveSettings();
        e->accept();
    } else {
        e->ignore();
    }
}

// ---------------------------------------------------------------------------
// pattern dialog (stays open so you can tweak and Apply repeatedly)
// ---------------------------------------------------------------------------
void MainWindow::openPattern()
{
    if (!m_canvas->hasSelection())
        statusBar()->showMessage("Pattern: select the objects first, then press Shift+A", 5000);
    if (m_patternDlg) {
        m_patternDlg->show();
        m_patternDlg->raise();
        return;
    }
    auto *dlg = new QDialog(this);
    m_patternDlg = dlg;
    dlg->setWindowTitle("Pattern");
    dlg->setAttribute(Qt::WA_DeleteOnClose, false);
    auto *lay = new QVBoxLayout(dlg);
    auto *type = new QComboBox;
    type->addItems({"Rectangular (rows x columns)", "Polar (around a centre)"});
    lay->addWidget(type);

    // rectangular page
    auto *rect = new QWidget;
    auto *fr = new QFormLayout(rect);
    auto *rows = new QSpinBox; rows->setRange(1, 200); rows->setValue(2);
    auto *cols = new QSpinBox; cols->setRange(1, 200); cols->setValue(3);
    auto *dU = lenSpin(-100000, 100000, 50, 5);
    auto *dV = lenSpin(-100000, 100000, 50, 5);
    fr->addRow("Rows", rows);
    fr->addRow("Columns", cols);
    fr->addRow("Column spacing (along U)", dU);
    fr->addRow("Row spacing (along V)", dV);
    lay->addWidget(rect);

    // polar page
    auto *polar = new QWidget;
    auto *fp = new QFormLayout(polar);
    auto *cnt = new QSpinBox; cnt->setRange(2, 500); cnt->setValue(6);
    auto *ang = spin(-360, 360, 360, " deg", 15, 1);
    auto *cx = lenSpin(-100000, 100000, 0, 5), *cy = lenSpin(-100000, 100000, 0, 5), *cz = lenSpin(-100000, 100000, 0, 5);
    auto *cr = new QHBoxLayout;
    for (auto *w : {cx, cy, cz}) cr->addWidget(w);
    auto *selC = new QPushButton("Selection centre");
    auto *pick = new QPushButton("Pick in view");
    auto *org = new QPushButton("Origin");
    auto *br = new QHBoxLayout;
    br->addWidget(selC); br->addWidget(pick); br->addWidget(org);
    auto *rot = new QCheckBox("Rotate the items");
    rot->setChecked(true);
    fp->addRow("Count (incl. original)", cnt);
    fp->addRow("Total angle", ang);
    fp->addRow("Centre X Y Z", cr);
    fp->addRow(br);
    fp->addRow(rot);
    lay->addWidget(polar);
    polar->hide();
    connect(type, &QComboBox::activated, dlg, [rect, polar](int i) { rect->setVisible(i == 0); polar->setVisible(i == 1); });

    auto setCentre = [=](const QVector3D &c) { cx->setValue(c.x()); cy->setValue(c.y()); cz->setValue(c.z()); };
    connect(selC, &QPushButton::clicked, dlg, [=] { setCentre(m_canvas->selectionCentre()); });
    connect(org, &QPushButton::clicked, dlg, [=] { setCentre(QVector3D()); });
    connect(pick, &QPushButton::clicked, dlg, [=] {
        m_canvas->pickPoint([=](const QVector3D &p) { setCentre(p); dlg->show(); }, "Pattern: click the centre point");
        m_canvas->setFocus();
    });

    auto *bb = new QDialogButtonBox;
    auto *apply = bb->addButton("Apply", QDialogButtonBox::ApplyRole);
    bb->addButton(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::hide);
    connect(apply, &QPushButton::clicked, dlg, [=] {
        if (type->currentIndex() == 0)
            m_canvas->patternRect(rows->value(), cols->value(), dU->value(), dV->value());
        else
            m_canvas->patternPolar(cnt->value(), ang->value(), QVector3D(cx->value(), cy->value(), cz->value()), rot->isChecked());
    });
    lay->addWidget(bb);
    dlg->setModal(false);
    dlg->show();
}

void MainWindow::pickFill()
{
    QColor start(53, 224, 200, 90);
    for (const SketchItem *it : m_canvas->items())
        if (it->selected && it->fill.isValid()) { start = it->fill; break; }
    const QColor c = QColorDialog::getColor(start, this, "Fill colour (alpha = transparency)",
                                            QColorDialog::ShowAlphaChannel);
    if (c.isValid())
        m_canvas->setSelectedFill(c);
}

// ---------------------------------------------------------------------------
// cut list of the solid boards: table, copy, CSV for Excel, place on the drawing
// ---------------------------------------------------------------------------
// Union / Subtract / Intersect from the menu, toolbar, panel or a shortcut: the first selected body is the
// target, the others are the tools - and it asks whether the tools stay (unless told not to ask)
void MainWindow::runBoolean(Canvas::BoolOp op)
{
    const QList<int> sel = m_canvas->selectedSolidIndices();
    if (sel.size() < 2) {
        m_canvas->booleanSelection(op);                    // explains what to select
        return;
    }
    QSettings st;
    bool keep = st.value("bool/keepTools", false).toBool();
    if (st.value("bool/ask", true).toBool()) {
        const QString name = op == Canvas::BoolOp::Union ? "Join" : op == Canvas::BoolOp::Subtract ? "Cut" : "Intersect";
        const SketchItem *target = m_canvas->items().value(sel.first());
        QMessageBox box(this);
        box.setWindowTitle(name);
        box.setIcon(QMessageBox::Question);
        box.setText(QString("<b>%1</b>: keep the tool bodies?").arg(name));
        box.setInformativeText(QString("Target: %1 (selected first).\nTools: the other %2 selected bod%3.")
                                   .arg(target ? Canvas::itemLabel(target) : QString("the first body"))
                                   .arg(sel.size() - 1).arg(sel.size() == 2 ? "y" : "ies"));
        QPushButton *keepB = box.addButton("Keep the tools", QMessageBox::AcceptRole);
        QPushButton *dropB = box.addButton("Remove the tools", QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(keep ? keepB : dropB);
        auto *dontAsk = new QCheckBox("Don't ask again (3D Tools panel: Ask to keep tools)");
        box.setCheckBox(dontAsk);
        box.exec();
        if (box.clickedButton() != keepB && box.clickedButton() != dropB)
            return;
        keep = box.clickedButton() == keepB;
        st.setValue("bool/keepTools", keep);
        if (dontAsk->isChecked()) {
            st.setValue("bool/ask", false);
            if (m_askKeepChk) { QSignalBlocker b(m_askKeepChk); m_askKeepChk->setChecked(false); }
        }
    }
    m_canvas->combineBodies({sel.first()}, sel.mid(1), op, keep);
}
// Object snap settings: which kinds of points snap (like AutoCAD's OSNAP tab); F3 switches all of them
void MainWindow::osnapSettings()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Object snap");
    auto *lay = new QVBoxLayout(&dlg);
    auto *on = new QCheckBox("Object snap on (F3)");
    on->setChecked(m_canvas->settings().snapObject);
    lay->addWidget(on);
    auto *box = new QGroupBox("Snap to");
    auto *v = new QVBoxLayout(box);
    QList<QPair<int, QCheckBox *>> checks;
    for (int bit = Canvas::OsEnd; bit <= Canvas::OsOnFace; bit <<= 1) {
        auto *c = new QCheckBox(Canvas::osnapName(bit));
        c->setChecked(m_canvas->settings().osnap & bit);
        v->addWidget(c);
        checks.append({bit, c});
    }
    lay->addWidget(box);
    auto *row = new QHBoxLayout;
    auto *all = new QPushButton("Select all");
    auto *none = new QPushButton("Clear all");
    row->addWidget(all);
    row->addWidget(none);
    row->addStretch(1);
    lay->addLayout(row);
    connect(all, &QPushButton::clicked, &dlg, [checks] { for (const auto &c : checks) c.second->setChecked(true); });
    connect(none, &QPushButton::clicked, &dlg, [checks] { for (const auto &c : checks) c.second->setChecked(false); });
    auto *note = new QLabel("Several points on top of each other on screen: the one nearer to you wins "
                            "(turn the view to reach a body centre behind a face centre).");
    note->setWordWrap(true);
    note->setStyleSheet("color:#6f819e;");
    lay->addWidget(note);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted)
        return;
    int mask = 0;
    for (const auto &c : checks)
        if (c.second->isChecked())
            mask |= c.first;
    const bool objOn = on->isChecked();
    mutate([mask, objOn](ViewSettings &s) {
        s.osnap = mask;
        s.snapObject = objOn;
    });
}

// diameter, wall (0 = solid rod) and bend radius (0 = 1.5 x diameter) for the Pipe tool; remembered
void MainWindow::pipeSettings()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Pipe settings");
    auto *form = new QFormLayout(&dlg);
    auto mk = [](double mn, double v) {
        auto *s = new LengthSpin;
        s->setRange(mn, 100000);
        s->setMetricStep(1);
        s->setValue(v);
        return s;
    };
    auto *d = mk(0.5, m_canvas->pipeDiameter());
    auto *w = mk(0, m_canvas->pipeWallThickness());
    auto *b = mk(0, m_canvas->pipeBendRadius());
    w->setSpecialValueText("solid rod");
    b->setSpecialValueText("1.5 x diameter");
    form->addRow("Outside diameter", d);
    form->addRow("Wall thickness", w);
    form->addRow("Bend radius (centre line)", b);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_canvas->setPipeParams(d->value(), w->value(), b->value());
    QSettings st;
    st.setValue("pipe/d", d->value());
    st.setValue("pipe/wall", w->value());
    st.setValue("pipe/bend", b->value());
    statusBar()->showMessage(QString("Pipe: %1, bends R %2").arg(Canvas::pipeLabel(d->value(), w->value()),
                                                               Units::format(b->value() > 0 ? b->value() : 1.5 * d->value())), 6000);
}

void MainWindow::openCutList()
{
    if (m_canvas->cutList(false).isEmpty()) {
        QMessageBox::information(this, "Cut list", "There are no solid boards yet.\n\nMake them with Box (Shift+B), "
                                                   "Press/Pull (Shift+P) or Model > Extrude solid.");
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle("Cut list");
    dlg.resize(820, 620);
    auto *lay = new QVBoxLayout(&dlg);
    auto *scope = new QCheckBox("Only the selected boards");
    scope->setChecked(m_canvas->hasSelection() && !m_canvas->cutList(true).isEmpty());
    scope->setEnabled(m_canvas->hasSelection());
    lay->addWidget(scope);
    auto *tabs = new QTabWidget;
    lay->addWidget(tabs, 1);

    // ---- parts
    auto *partsPage = new QWidget;
    auto *pl = new QVBoxLayout(partsPage);
    auto *table = new QTableWidget(0, 7);
    table->setHorizontalHeaderLabels({"Part", "Material", "Length", "Width", "Thickness", "Qty", "Area"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pl->addWidget(table, 1);
    auto *total = new QLabel;
    pl->addWidget(total);
    tabs->addTab(partsPage, uiIcon("cutlist"), "Parts");

    // ---- sheets: nesting per material and thickness
    auto *sheetsPage = new QWidget;
    auto *sl = new QVBoxLayout(sheetsPage);
    QSettings st;
    auto *sL = lenSpin(100, 10000, st.value("nest/L", 2440.0).toDouble(), 10);
    auto *sW = lenSpin(100, 10000, st.value("nest/W", 1220.0).toDouble(), 10);
    auto *kerf = lenSpin(0, 20, st.value("nest/kerf", 4.0).toDouble(), 0.5);
    auto *trim = lenSpin(0, 100, st.value("nest/trim", 10.0).toDouble(), 1);
    auto *turn = new QCheckBox("Parts may turn (no grain)");
    turn->setChecked(st.value("nest/turn", true).toBool());
    turn->setToolTip("Off: every part keeps its length along the sheet's length (grain direction)");
    auto *srow = new QHBoxLayout;
    srow->addWidget(new QLabel("Sheet"));
    srow->addWidget(sL);
    srow->addWidget(new QLabel("x"));
    srow->addWidget(sW);
    srow->addWidget(new QLabel("  Saw kerf"));
    srow->addWidget(kerf);
    srow->addWidget(new QLabel("  Edge trim"));
    srow->addWidget(trim);
    srow->addWidget(turn);
    srow->addStretch(1);
    sl->addLayout(srow);
    auto *summary = new QLabel;
    summary->setWordWrap(true);
    summary->setTextFormat(Qt::RichText);
    sl->addWidget(summary);
    auto *view = new QLabel;
    view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto *scroll = new QScrollArea;
    scroll->setWidget(view);
    scroll->setWidgetResizable(true);
    sl->addWidget(scroll, 1);
    tabs->addTab(sheetsPage, "Sheets");

    QList<Canvas::CutRow> rows;
    QImage layoutImg;
    QString summaryPlain;
    auto nest = [&] {
        QList<NestGroup> groups;
        QMap<QString, int> at;
        for (const auto &c : std::as_const(rows)) {
            if (c.linear)
                continue;                                  // pipes and turned parts are bought by length
            const QString key = (c.material.isEmpty() ? QString("(no material)") : c.material) + "  -  " +
                                Units::format(c.t) + " thick";
            if (!at.contains(key)) {
                at.insert(key, int(groups.size()));
                groups.append(NestGroup{key, {}, {}});
            }
            for (int q = 0; q < c.qty; ++q)
                groups[at[key]].parts.append({c.l, c.w, c.name});
        }
        QStringList html, plain;
        int sheetsTotal = 0;
        for (NestGroup &g : groups) {
            g.res = nestParts(g.parts, sL->value(), sW->value(), kerf->value(), trim->value(), turn->isChecked());
            sheetsTotal += int(g.res.sheets.size());
            const QString line = QString("%1: %2 sheet(s) of %3 x %4 (%5 at the very least), %6% of the board used, %7 part(s)")
                                     .arg(g.title).arg(g.res.sheets.size()).arg(Units::format(sL->value()), Units::format(sW->value()))
                                     .arg(g.res.lowerBound).arg(int(std::round(100 * g.res.utilisation()))).arg(g.parts.size());
            plain << line + (g.res.tooBig.isEmpty() ? QString() : QString(", %1 larger than the sheet").arg(g.res.tooBig.size()));
            html << "<b>" + g.title.toHtmlEscaped() + "</b>: " + line.mid(g.title.size() + 2).toHtmlEscaped() +
                        (g.res.tooBig.isEmpty() ? QString()
                                                : QString(", <span style='color:#c0392b'>%1 larger than the sheet</span>").arg(g.res.tooBig.size()));
        }
        summaryPlain = plain.join('\n') + QString("\nTotal: %1 sheet(s)").arg(sheetsTotal);
        summary->setText(html.join("<br>") + QString("<br><b>Total: %1 sheet(s)</b>").arg(sheetsTotal));
        layoutImg = nestingImage(groups, 760);
        view->setPixmap(QPixmap::fromImage(layoutImg));
        QSettings s2;
        s2.setValue("nest/L", sL->value());
        s2.setValue("nest/W", sW->value());
        s2.setValue("nest/kerf", kerf->value());
        s2.setValue("nest/trim", trim->value());
        s2.setValue("nest/turn", turn->isChecked());
    };
    auto fill = [&] {
        rows = m_canvas->cutList(scope->isChecked());
        table->setRowCount(int(rows.size()));
        double area = 0;
        int parts = 0;
        for (int r = 0; r < rows.size(); ++r) {
            const auto &c = rows[r];
            const QStringList cells = {c.name, c.material, Units::format(c.l), Units::format(c.w), Units::format(c.t),
                                       QString::number(c.qty), Units::formatBoardArea(c.l * c.w * c.qty)};
            for (int k = 0; k < cells.size(); ++k) {
                auto *cell = new QTableWidgetItem(cells[k]);
                if (k > 1)
                    cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                table->setItem(r, k, cell);
            }
            area += c.l * c.w * c.qty;
            parts += c.qty;
        }
        total->setText(QString("<b>%1 parts</b>, %2 rows, board area %3").arg(parts).arg(rows.size()).arg(Units::formatBoardArea(area)));
        nest();
    };
    fill();
    connect(scope, &QCheckBox::toggled, &dlg, [&] { fill(); });
    for (QDoubleSpinBox *s : {sL, sW, kerf, trim})
        connect(s, &QDoubleSpinBox::valueChanged, &dlg, [&] { nest(); });
    connect(turn, &QCheckBox::toggled, &dlg, [&] { nest(); });

    auto *bb = new QDialogButtonBox;
    auto *copy = bb->addButton("Copy", QDialogButtonBox::ActionRole);
    auto *csv = bb->addButton("Save CSV (Excel)...", QDialogButtonBox::ActionRole);
    auto *png = bb->addButton("Save sheet layouts...", QDialogButtonBox::ActionRole);
    auto *place = bb->addButton("Place on drawing", QDialogButtonBox::ActionRole);
    bb->addButton(QDialogButtonBox::Close);
    copy->setToolTip("Parts tab: tab-separated rows that paste into Excel. Sheets tab: the sheet summary");
    connect(copy, &QPushButton::clicked, &dlg, [&] {
        if (tabs->currentIndex() == 1) {
            QApplication::clipboard()->setText(summaryPlain);
            statusBar()->showMessage("Sheet summary copied", 4000);
            return;
        }
        QStringList out{"Part\tMaterial\tLength\tWidth\tThickness\tQty"};
        for (const auto &c : std::as_const(rows))
            out << QString("%1\t%2\t%3\t%4\t%5\t%6").arg(c.name, c.material, Units::format(c.l), Units::format(c.w),
                                                        Units::format(c.t)).arg(c.qty);
        QApplication::clipboard()->setText(out.join('\n'));
        statusBar()->showMessage("Cut list copied - paste it into Excel", 4000);
    });
    connect(csv, &QPushButton::clicked, &dlg, [&] {
        const QString base = m_filePath.isEmpty() ? QString("cut list") : QFileInfo(m_filePath).completeBaseName() + " cut list";
        QString path = QFileDialog::getSaveFileName(&dlg, "Save cut list", base + ".csv", "CSV for Excel (*.csv)");
        if (path.isEmpty())
            return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(&dlg, "Cut list", "Could not write " + path);
            return;
        }
        auto q = [](QString s) {
            if (s.contains(',') || s.contains('"'))
                s = '"' + s.replace("\"", "\"\"") + '"';
            return s;
        };
        QTextStream o(&f);
        o << "Part,Material,Length (mm),Width (mm),Thickness (mm),Qty,Area (m2)\n";
        for (const auto &c : std::as_const(rows))
            o << q(c.name) << ',' << q(c.material) << ',' << c.l << ',' << c.w << ',' << c.t << ',' << c.qty << ','
              << QString::number(c.l * c.w * c.qty / 1e6, 'f', 4) << '\n';
        statusBar()->showMessage("Cut list saved: " + path, 5000);
    });
    connect(png, &QPushButton::clicked, &dlg, [&] {
        const QString base = m_filePath.isEmpty() ? QString("sheets") : QFileInfo(m_filePath).completeBaseName() + " sheets";
        const QString path = QFileDialog::getSaveFileName(&dlg, "Save sheet layouts", base + ".png", "PNG image (*.png)");
        if (!path.isEmpty() && layoutImg.save(path))
            statusBar()->showMessage("Sheet layouts saved: " + path, 5000);
    });
    bool placeIt = false;
    connect(place, &QPushButton::clicked, &dlg, [&] { placeIt = true; dlg.accept(); });
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    dlg.exec();
    if (placeIt) {
        const bool only = scope->isChecked();
        m_canvas->pickPoint([this, only](const QVector3D &p) { m_canvas->placeCutList(p, only); },                            "Cut list: click where the table goes (top-left corner)");
        m_canvas->setFocus();
    }
}

// ---------------------------------------------------------------------------
// Combine: which bodies are cut / joined (targets) and which do it (tools), like Fusion's Combine
// ---------------------------------------------------------------------------
void MainWindow::openCombine()
{
    if (m_combineDlg) {
        m_combineDlg->show();
        m_combineDlg->raise();
        m_combineDlg->activateWindow();
        return;
    }
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("Combine bodies");
    m_combineDlg = dlg;
    connect(dlg, &QObject::destroyed, this, [this] { m_combineDlg = nullptr; });
    auto *lay = new QVBoxLayout(dlg);
    auto *op = new QComboBox;
    op->addItems({"Cut - the targets lose what the tools fill", "Join - targets and tools become one body",
                  "Intersect - only what targets and tools share stays"});
    lay->addWidget(new QLabel("Operation"));
    lay->addWidget(op);
    auto *note = new QLabel("Select bodies in the drawing (Shift+click for several), then Add selected.");
    note->setWordWrap(true);
    auto *count = new int(int(m_canvas->items().size()));   // the drawing these rows point into
    QListWidget *lists[2] = {new QListWidget, new QListWidget};   // made first: the lambdas below copy these
    auto label = [this](int i) {
        const SketchItem *it = m_canvas->items().value(i);
        return it ? QString("#%1  %2").arg(i + 1).arg(Canvas::itemLabel(it)) : QString("#%1").arg(i + 1);
    };
    auto addRows = [this, lists, label, count](int which, const QList<int> &idx) {
        for (int i : idx) {
            for (QListWidget *l : lists)                   // a body is a target or a tool, not both
                for (int r = l->count() - 1; r >= 0; --r)
                    if (l->item(r)->data(Qt::UserRole).toInt() == i)
                        delete l->takeItem(r);
            auto *row = new QListWidgetItem(label(i));
            row->setData(Qt::UserRole, i);
            lists[which]->addItem(row);
        }
        *count = int(m_canvas->items().size());
    };
    const char *titles[] = {"Targets (kept: cut, joined or intersected)", "Tools (do the cutting / joining)"};
    for (int w = 0; w < 2; ++w) {
        auto *box = new QGroupBox(titles[w]);
        auto *v = new QVBoxLayout(box);
        QListWidget *list = lists[w];
        list->setMinimumHeight(86);
        v->addWidget(list);
        auto *row = new QHBoxLayout;
        auto *addB = new QPushButton("Add selected");
        auto *remB = new QPushButton("Remove");
        auto *clrB = new QPushButton("Clear");
        row->addWidget(addB);
        row->addWidget(remB);
        row->addWidget(clrB);
        v->addLayout(row);
        lay->addWidget(box);
        connect(addB, &QPushButton::clicked, dlg, [this, addRows, w] { addRows(w, m_canvas->selectedSolidIndices()); });
        connect(remB, &QPushButton::clicked, dlg, [list] { delete list->takeItem(list->currentRow()); });
        connect(clrB, &QPushButton::clicked, list, &QListWidget::clear);
        connect(list, &QListWidget::itemClicked, dlg, [this](QListWidgetItem *r) {
            m_canvas->selectIndices({r->data(Qt::UserRole).toInt()});   // show which body it is
        });
    }
    auto *keep = new QCheckBox("Keep the tool bodies");
    keep->setChecked(QSettings().value("bool/keepTools", false).toBool());   // the last answer
    connect(keep, &QCheckBox::toggled, dlg, [](bool on) { QSettings().setValue("bool/keepTools", on); });
    lay->addWidget(keep);
    lay->addWidget(note);
    auto *bb = new QHBoxLayout;
    auto *swap = new QPushButton("Swap targets / tools");
    auto *apply = new QPushButton("Apply");
    auto *close = new QPushButton("Close");
    apply->setDefault(true);
    bb->addWidget(swap);
    bb->addStretch(1);
    bb->addWidget(apply);
    bb->addWidget(close);
    lay->addLayout(bb);
    connect(swap, &QPushButton::clicked, dlg, [lists] {
        QList<QListWidgetItem *> a, b;
        while (lists[0]->count()) a << lists[0]->takeItem(0);
        while (lists[1]->count()) b << lists[1]->takeItem(0);
        for (auto *r : b) lists[0]->addItem(r);
        for (auto *r : a) lists[1]->addItem(r);
    });
    connect(close, &QPushButton::clicked, dlg, &QDialog::close);
    connect(apply, &QPushButton::clicked, dlg, [this, lists, op, keep, note] {
        QList<int> t, u;
        for (int r = 0; r < lists[0]->count(); ++r) t << lists[0]->item(r)->data(Qt::UserRole).toInt();
        for (int r = 0; r < lists[1]->count(); ++r) u << lists[1]->item(r)->data(Qt::UserRole).toInt();
        const Canvas::BoolOp o = op->currentIndex() == 0 ? Canvas::BoolOp::Subtract
                               : op->currentIndex() == 1 ? Canvas::BoolOp::Union : Canvas::BoolOp::Intersect;
        if (m_canvas->combineBodies(t, u, o, keep->isChecked())) {
            lists[0]->clear();
            lists[1]->clear();
            note->setText("Done (Ctrl+Z undoes it). Select the next bodies and Add selected.");
        }
    });
    // rows are positions in the drawing: when bodies come or go they no longer fit
    connect(m_canvas, &Canvas::contentChanged, dlg, [this, lists, note, count] {
        if (int(m_canvas->items().size()) == *count || (!lists[0]->count() && !lists[1]->count()))
            return;
        lists[0]->clear();
        lists[1]->clear();
        note->setText("The drawing changed - add the bodies again.");
    });
    connect(dlg, &QObject::destroyed, dlg, [count] { delete count; });
    // start from the selection: the first selected body is the target, the others the tools
    const QList<int> sel = m_canvas->selectedSolidIndices();
    if (!sel.isEmpty()) {
        addRows(0, {sel.first()});
        addRows(1, sel.mid(1));
    }
    dlg->resize(430, 470);
    dlg->show();
}

// ---------------------------------------------------------------------------
// 3D Tools: a side panel with the modelling tools the upgrades added, their sizes and the
// Combine / joint commands, so the main toolbar stays for drawing
// ---------------------------------------------------------------------------
void MainWindow::buildToolsDock()
{
    auto *panel = new QWidget;
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);
    auto section = [lay](const QString &title) {
        auto *l = new QLabel(title.toUpper());
        l->setStyleSheet("color:#6f819e;font-size:10px;font-weight:600;letter-spacing:1px;padding-top:6px;");
        lay->addWidget(l);
    };
    auto grid = [lay]() {
        auto *g = new QGridLayout;
        g->setSpacing(4);
        lay->addLayout(g);
        return g;
    };
    auto button = [](const QString &icon, const QString &text, const QString &tip) {
        auto *b = new QToolButton;
        b->setIcon(uiIcon(icon));
        b->setIconSize(QSize(24, 24));
        b->setText(text);
        b->setToolTip(tip);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setAutoRaise(true);
        b->setFixedSize(72, 54);
        return b;
    };
    auto toolBtn = [this, button](Canvas::Tool t, const QString &icon, const QString &text) {
        QAction *a = m_toolActs.value(t);
        QToolButton *b = button(icon, text, a ? a->toolTip() : QString());
        if (a) {
            a->setIconText(text);                          // the label under the icon (the toolbar shows icons only)
            b->setDefaultAction(a);
            b->setIcon(uiIcon(icon));
        }
        return b;
    };
    auto put = [](QGridLayout *g, const QList<QToolButton *> &bs) {
        for (int i = 0; i < bs.size(); ++i)
            g->addWidget(bs[i], i / 3, i % 3);
    };

    section("Create");
    put(grid(), {toolBtn(Canvas::Tool::Box, "box", "Box"), toolBtn(Canvas::Tool::PressPull, "presspull", "Press/Pull"),
                 toolBtn(Canvas::Tool::Revolve, "revolve", "Revolve"), toolBtn(Canvas::Tool::Pipe, "pipe", "Pipe")});
    section("Modify");
    put(grid(), {toolBtn(Canvas::Tool::Fillet, "fillet", "Fillet"), toolBtn(Canvas::Tool::Chamfer, "chamfer", "Chamfer"),
                 toolBtn(Canvas::Tool::Shell, "shell", "Shell"), toolBtn(Canvas::Tool::Rotate, "rotate", "Rotate")});
    section("Combine");
    QToolButton *comb = button("subtract", "Combine...", "Choose target and tool bodies: Cut, Join or Intersect  [Ctrl+Alt+C]");
    QToolButton *uni = button("union", "Union", "The selected boards become one  [Ctrl+Alt+U]");
    QToolButton *sub = button("subtract", "Subtract", "First selected minus the others  [Ctrl+Alt+S]");
    QToolButton *its = button("intersect", "Intersect", "Only the overlap stays  [Ctrl+Alt+I]");
    QToolButton *joint = button("joint", "Cut joints", "The selected boards cut housings into every board they go into  [Ctrl+Shift+J]");
    QToolButton *cut = button("cutlist", "Cut list", "Parts, sheets and nesting  [Ctrl+Shift+B]");
    connect(comb, &QToolButton::clicked, this, &MainWindow::openCombine);
    connect(uni, &QToolButton::clicked, this, [this] { runBoolean(Canvas::BoolOp::Union); });
    connect(sub, &QToolButton::clicked, this, [this] { runBoolean(Canvas::BoolOp::Subtract); });
    connect(its, &QToolButton::clicked, this, [this] { runBoolean(Canvas::BoolOp::Intersect); });
    connect(joint, &QToolButton::clicked, this, [this] { m_canvas->cutJointsSelection(); });
    connect(cut, &QToolButton::clicked, this, &MainWindow::openCutList);
    put(grid(), {comb, uni, sub, its, joint, cut});
    m_askKeepChk = new QCheckBox("Ask to keep tool bodies");
    m_askKeepChk->setToolTip("Union / Subtract / Intersect ask whether the tool bodies stay. Off: the last answer is used.");
    m_askKeepChk->setChecked(QSettings().value("bool/ask", true).toBool());
    connect(m_askKeepChk, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("bool/ask", on); });
    lay->addWidget(m_askKeepChk);

    section("Extrude a shape does");
    auto *ext = new QComboBox;
    for (int k = 0; k < 5; ++k)
        ext->addItem(Canvas::extrudeOpName(k) + (k == 0 ? "  (cut into a board, else new)" : ""));
    ext->setToolTip("What Press/Pull does with a closed shape it turns into a body. While pulling: N new, J join, C cut, "
                    "I intersect, A auto. Bodies selected before the pull are the only ones it cuts / joins.");
    lay->addWidget(ext);
    connect(ext, &QComboBox::activated, this, [this](int k) { m_canvas->setExtrudeOp(k); });

    section("Sizes");
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    auto spin = [](double mn, double v, const QString &special = QString()) {
        auto *s = new LengthSpin;
        s->setRange(mn, 100000);
        s->setMetricStep(1);
        s->setValue(v);
        if (!special.isEmpty())
            s->setSpecialValueText(special);
        return s;
    };
    auto *board = spin(0.1, m_canvas->boxHeight());
    auto *fil = spin(0.1, m_canvas->filletSize(false));
    auto *cha = spin(0.1, m_canvas->filletSize(true));
    auto *shell = spin(0.1, m_canvas->shellThickness());
    auto *pd = spin(0.5, m_canvas->pipeDiameter());
    auto *pw = spin(0, m_canvas->pipeWallThickness(), "solid rod");
    board->setToolTip("Board thickness: Box tool Enter at the height step");
    form->addRow("Board", board);
    form->addRow("Fillet R", fil);
    form->addRow("Chamfer", cha);
    form->addRow("Shell wall", shell);
    form->addRow("Pipe " + QString::fromUtf8("\xC3\x98"), pd);
    form->addRow("Pipe wall", pw);
    lay->addLayout(form);
    auto *pipeMore = new QPushButton(uiIcon("pipe"), "Pipe bend radius...");
    connect(pipeMore, &QPushButton::clicked, this, &MainWindow::pipeSettings);
    lay->addWidget(pipeMore);
    auto *snapBtn = new QPushButton("Object snap settings...");
    snapBtn->setToolTip("Which points snap: corners, midpoints, centres, face and body centres, points on faces  [Ctrl+F3]");
    connect(snapBtn, &QPushButton::clicked, this, &MainWindow::osnapSettings);
    lay->addWidget(snapBtn);
    connect(board, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_canvas->setBoxHeight(v);
        if (m_boardSpin) { QSignalBlocker b(m_boardSpin); m_boardSpin->setValue(v); }
    });
    connect(fil, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_canvas->setFilletSize(v, false); });
    connect(cha, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_canvas->setFilletSize(v, true); });
    connect(shell, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_canvas->setShellThickness(v); });
    connect(pd, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_canvas->setPipeParams(v, m_canvas->pipeWallThickness(), m_canvas->pipeBendRadius());
    });
    connect(pw, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        m_canvas->setPipeParams(m_canvas->pipeDiameter(), v, m_canvas->pipeBendRadius());
    });
    // typed in a tool (or set from a menu): the panel follows
    auto refresh = [=] {
        const QSignalBlocker b1(board), b2(fil), b3(cha), b4(shell), b5(pd), b6(pw), b7(ext);
        board->setValue(m_canvas->boxHeight());
        fil->setValue(m_canvas->filletSize(false));
        cha->setValue(m_canvas->filletSize(true));
        shell->setValue(m_canvas->shellThickness());
        pd->setValue(m_canvas->pipeDiameter());
        pw->setValue(m_canvas->pipeWallThickness());
        ext->setCurrentIndex(m_canvas->extrudeOp());
    };
    connect(m_canvas, &Canvas::toolSizesChanged, panel, refresh);
    QTimer::singleShot(0, panel, refresh);                  // after the saved settings are loaded

    auto *tip = new QLabel("Shift+click picks several lines, bodies (Select) or faces (Press/Pull). "
                           "Press/Pull over a parallel face = up to that face.");
    tip->setWordWrap(true);
    tip->setStyleSheet("color:#6f819e;font-size:11px;padding-top:8px;");
    lay->addWidget(tip);
    lay->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *d = new QDockWidget("3D Tools", this);
    d->setObjectName("dock3DTools");
    d->setWidget(scroll);
    d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    d->setMinimumWidth(262);
    addDockWidget(Qt::LeftDockWidgetArea, d);
    if (m_viewMenu)
        m_viewMenu->addAction(d->toggleViewAction());
}