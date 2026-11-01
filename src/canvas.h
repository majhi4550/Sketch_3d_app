#pragma once

#include <functional>
#include <memory>

#include <QHash>
#include <QFont>
#include <QImage>
#include <QTransform>
#include <QLineF>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QQuaternion>
#include <QVariantAnimation>
#include <QSizeF>
#include <QUndoStack>
#include <QVector>
#include <QVector3D>
#include <QWidget>

#include "sketchitem.h"

class QLineEdit;
class QPainterPath;

// Persistent viewport / drafting options (edited from the Viewport dock).
struct ViewSettings
{
    bool perspective = false;   // only applies to the free 3D (ISO) view
    double fov = 35.0;          // vertical field of view, degrees
    bool showGrid = true;
    bool showAxes = true;
    bool showPaper = true;
    bool showPlaneSheet = true;
    bool showGizmo = true;
    bool showHud = true;
    double gridExtent = 400.0;  // half size of the drawn grid (mm)
    int theme = 0;              // 0 Dark | 1 Graphite | 2 Blueprint | 3 Light
    double lineScale = 1.0;     // display multiplier for line weights
    double orbitSpeed = 0.4;    // degrees per pixel
    bool zoomToCursor = true;
    bool invertWheel = false;
    bool syncPlane = true;      // Top/Front/Side... views also select the matching plane
    bool snapGrid = true;
    bool snapObject = true;     // endpoint / midpoint / centre snaps
    bool ortho = false;         // constrain drawing to the plane's U/V axes (Shift = temporary toggle)
    bool snap3D = false;        // let line tools snap to points that are OFF the sketch plane (else they are projected onto it)
    bool limitSheet = true;     // keep the cursor on the sketch sheet so shapes cannot land far away
    bool polar = false;         // polar tracking: snap direction to multiples of polarStep
    double polarStep = 15.0;    // degrees
    int osnap = 0x1FF;          // object snap kinds that are on (Canvas::OsEnd ... OsOnFace)
    bool dynInput = true;       // on-canvas size fields while drawing (Tab = next field)
    bool freeOrbit = true;      // orbit about any axis, 360 deg over the poles (else turntable around Top's up)
    int units = 0;              // Units::System: how lengths are shown and typed (stored in mm always)
    int unitDecimals = 2;       // decimal places of the decimal unit systems
    int unitFrac = 16;          // Architectural feet-inches: round to 1/unitFrac inch
};

// Style used by the Hatch tool for new hatches (and by "apply to selection").
struct HatchStyle
{
    int pattern = 1;            // Canvas::hatchPatternNames(); 0 = solid fill
    double scale = 0;           // pattern spacing in mm, 0 = automatic
    double angle = 0;           // degrees
    QColor fill;                // background / solid colour (invalid = pen colour for solid, none for patterns)
};

// Central drawing surface. World units are millimetres.
//
// * The viewport (Top/Front/Side/Back/Left/Bottom/ISO) is a real camera:
//   orthographic for the named views, orthographic or perspective for ISO.
// * The sketch plane (XY/XZ/ZY/YX + offset) is where new 2D input lands in 3D.
//   Every mouse position is ray-cast onto that plane, so you can draw directly
//   in the 3D view. X / Y / Z lock the cursor to a world axis (vertical lines!).
class Canvas : public QWidget
{
    Q_OBJECT
public:
    enum class Tool {
        Select, Line, Curve3, Arc, Circle, Polyline, Rect, Dimension,
        Text, InsertImage,
        Trim, Join, Break, Offset, Extend, Mirror, Cutter,
        Edit, FreeMove, DynMove, Move, Hatch, Box, PressPull, Rotate, Fillet, Chamfer, Shell, Revolve, Pipe
    };
    enum class Plane { TopXY = 0, XZ = 1, ZY = 2, YX = 3 };
    enum class View { Top = 0, Front = 1, Side = 2, Iso = 3, Bottom = 4, Back = 5, Right = 6 };

    explicit Canvas(QWidget *parent = nullptr);
    ~Canvas() override;

    // ---- tools / plane / view ----
    void setTool(Tool t);
    Tool tool() const { return m_tool; }
    static QString toolName(Tool t);

    void setPlane(Plane p);
    Plane plane() const { return m_plane; }
    static QString planeName(Plane p);
    void cyclePlane();
    void setPlaneOffset(double mm);
    double planeOffset() const { return m_planeOffset; }

    void setView(View v);
    View view() const { return m_view; }
    static QString viewName(View v);
    void resetIsoView();
    void lookAtPlane();           // view straight onto the active sketch plane
    // free 3D view looking from cubeDir (view-cube space) towards the target (cube corners / edges)
    void setViewDirection(const QVector3D &cubeDir, bool animate = true);
    // Fusion-style view orientation: the current camera becomes the named view v
    // (Top, Front ...); the view cube and every named view follow. Reset = world axes again.
    void setCurrentViewAs(View v);
    void resetViewOrientation();
    bool customOrientation() const;
    void goHome(bool animate = true);
    void setCurrentViewAsHome();
    void resetHome();
    void rollView(qreal degrees);     // turn the picture about the viewing axis

    const ViewSettings &settings() const { return m_set; }
    void setSettings(const ViewSettings &s);
    bool perspectiveActive() const { return m_set.perspective && m_view == View::Iso; }

    double gridSize() const { return m_gridSize; }
    void setGridSize(double mm);
    QSizeF pageSizeMm() const { return m_pageSize; }
    void setPageSizeMm(const QSizeF &s) { m_pageSize = s; update(); }

    // ---- defaults for new entities ----
    QColor defaultColor() const { return m_defColor; }
    void setDefaultColor(const QColor &c) { m_defColor = c; }
    double defaultWidth() const { return m_defWidth; }
    void setDefaultWidth(double w) { m_defWidth = w; }
    double defaultTextSize() const { return m_defTextSize; }
    void setDefaultTextSize(double mm) { m_defTextSize = mm; }
    double offsetDistance() const { return m_offsetDist; }
    void setOffsetDistance(double d);
    void setPendingImage(const QImage &img) { m_pendingImage = img; }

    // ---- document / selection ----
    const QList<SketchItem *> &items() const { return m_items; }
    int selectionCount() const { return m_selected.size(); }
    bool hasSelection() const { return !m_selected.isEmpty(); }
    QString selectionInfo() const;
    static QString itemLabel(const SketchItem *it);
    void selectIndices(const QList<int> &idx);
    void clearAll();
    void selectAll();
    void deleteSelection();

    // ---- transforms / editing ----
    void rotateSelection(double deg);
    void scaleSelection(double factor);
    void moveSelectionBy(const QVector3D &delta);
    void mirrorSelection(bool alongU, bool keepOriginal);
    void arraySelection(int count, const QVector3D &step);
    void extrudeSelection(double distance);
    void copySelection();
    void pasteClipboard();
    void duplicateSelection();
    // mirror about a picked axis (in the sketch plane) and patterns
    void mirrorAcross(const QVector3D &p1, const QVector3D &p2, bool keep);
    void patternRect(int rows, int cols, double dU, double dV);
    void patternPolar(int count, double totalDeg, const QVector3D &center, bool rotateItems);
    void pickPoint(std::function<void(const QVector3D &)> cb, const QString &prompt);
    QVector3D selectionCentre() const { return selectionCenter(); }
    bool mirrorKeep() const { return m_mirrorKeep; }
    void setMirrorKeep(bool k) { m_mirrorKeep = k; emit mirrorKeepChanged(k); update(); }
    // cutting engine: 0 = View (virtual, what you see), 1 = True 3D intersections
    void setCutMode(int m);
    int cutMode() const { return m_cutMode; }
    void setBladeAction(int a);                  // 0 = split along the blade, 1 = cut away one side
    int bladeAction() const { return m_bladeAction; }
    int dimMode() const { return m_dimMode; }
    void setDimMode(int m);
    bool hasClipboard() const;                   // Sketch3D objects on the system clipboard (any window)
    void setSelectedColor(const QColor &c);
    void setSelectedWidth(double w);
    void setSelectedLineStyle(int s);
    int defaultLineStyle() const { return m_defStyle; }
    void setDefaultLineStyle(int s) { m_defStyle = s; }
    void groupSelection();
    void ungroupSelection();
    bool selectionHasGroup() const;
    // text editing of the selected text object(s)
    SketchItem *selectedText() const;
    void setSelectedTextContent(const QString &t);
    void setSelectedFont(const QString &family, bool bold, bool italic);
    void setSelectedTextAngle(double deg);
    void editSelectedText();
    // align / distribute (a group counts as one object)
    enum class AlignMode { Left, HCenter, Right, Top, VCenter, Bottom };
    void alignSelection(AlignMode m, bool toPage = false);
    void flattenToPlane();
    void flipSelection(bool horizontal);          // mirror in place about the selection centre
    void setSelectedFill(const QColor &c);        // invalid colour = no fill
    void arrangeSelection(bool toFront);          // draw order: bring to front / send to back
    // ---- hatch / colour fill inside closed boundaries (canvas_hatch.cpp) ----
    static QStringList hatchPatternNames();
    static QPixmap hatchSwatch(int pattern, int size, const QColor &line);   // small preview for lists
    const HatchStyle &hatchStyle() const { return m_hatchStyle; }
    void setHatchStyle(const HatchStyle &s) { m_hatchStyle = s; updateHatchPreview(); update(); }
    void applyHatchToSelection();                 // selected hatches + closed shapes take the hatch style
    // ---- visibility / fix ----
    void hideSelection();
    void explodeSelection();                      // rectangles / polylines -> separate lines, groups dissolved
    void showAll();
    void isolateSelection();
    void fixSelection();                          // Fix constraint (lock)
    void unfixAll();
    // ---- solid bodies ----
    void extrudeSolid(double dist);               // closed shapes become solid prisms
    void removeExtrude();
    void sketchOnBodyFace(bool top);              // sketch plane onto the top / bottom face of the selected body
    void splitBodyAtPlane();                      // split selected bodies by the current sketch plane
    void shellBody(double wall);                  // hollow rectangular bodies (wall thickness)
    // ---- 3D boards: Box / Press-Pull tools, part names, cut list (canvas_solid.cpp) ----
    QPointF screenOf(const QVector3D &p) const { return project(p); }   // world -> widget pixels (self-test)
    double boxHeight() const { return m_boxHeight; }          // default board thickness (Enter in the Box tool)
    void setBoxHeight(double mm) { m_boxHeight = std::max(0.1, mm); emitHint(); }
    void setSelectedName(const QString &name);
    QString selectedName() const;
    struct CutRow { QString name; double l = 0, w = 0, t = 0; int qty = 0; QString material; bool linear = false; };   // linear: pipes, turned blanks (no sheet nesting)
    void setSelectedMaterial(const QString &material);
    QString selectedMaterial() const;
    QStringList materialsInUse() const;
    QList<CutRow> cutList(bool selectionOnly) const;          // every solid board, L >= W >= T, identical ones counted
    void placeCutList(const QVector3D &at, bool selectionOnly);
    static QString cutListText(const QList<CutRow> &rows);
    // ---- Union / Subtract / Intersect of boards (canvas_bool.cpp) ----
    enum class BoolOp { Union, Subtract, Intersect };
    void booleanSelection(BoolOp op, bool keepCutters = false);   // Subtract: first selected minus the others
    void cutJointsSelection();                    // selected boards cut housings into the boards they go into
    int pullSetSize() const { return int(m_ppSet.size()); }
    // object snap kinds (ViewSettings::osnap)
    enum OsnapBits { OsEnd = 1, OsMid = 2, OsCen = 4, OsInt = 8, OsPerp = 16, OsNear = 32, OsFaceCen = 64, OsBodyCen = 128,
                     OsOnFace = 256 };
    static QString osnapName(int bit);
    void mirrorPlane(const QVector3D &point, const QVector3D &normal, bool keep);   // across the plane through point
    QList<int> selectedSolidIndices() const;
    // Combine: targets are kept and cut (Cut / Intersect) or joined; tools do the cutting
    bool combineBodies(const QList<int> &targets, const QList<int> &tools, BoolOp op, bool keepTools);
    void setFilletSize(double r, bool chamfer) { (chamfer ? m_chamferD : m_filletR) = std::max(0.0, r); update(); emit toolSizesChanged(); }
    double filletSize(bool chamfer) const { return chamfer ? m_chamferD : m_filletR; }
    void setShellThickness(double t) { m_shellT = std::max(0.1, t); update(); emit toolSizesChanged(); }
    // solids: boards (a profile swept straight), revolved bodies and pipes
    static bool isSolid(const SketchItem *it)
    {
        return !it->extrude.isNull() || it->type() == SketchItem::Revolve || it->type() == SketchItem::Pipe;
    }
    void revolveSelection(double deg);            // the selected closed shape about the selected line
    void pipeSelection();                         // the selected centre lines become pipes
    void setPipeParams(double d, double wall, double bend)
    {
        m_pipeD = std::max(0.1, d); m_pipeWall = std::max(0.0, wall); m_pipeBend = std::max(0.0, bend); update();
        emit toolSizesChanged();
    }
    double pipeDiameter() const { return m_pipeD; }
    double pipeWallThickness() const { return m_pipeWall; }
    double pipeBendRadius() const { return m_pipeBend; }
    void setRevolveAngle(double a) { m_rvAngle = std::clamp(std::fabs(a), 0.1, 360.0); update(); }
    static QString pipeLabel(double d, double wall);
    double shellThickness() const { return m_shellT; }
    static QString extrudeOpName(int op);         // 0 Auto, 1 New body, 2 Join, 3 Cut, 4 Intersect
    int extrudeOp() const { return m_ppOp; }
    void setExtrudeOp(int op) { m_ppOp = m_ppOpNow = std::clamp(op, 0, 4); emitHint(); update(); emit toolSizesChanged(); }
    // ---- move / rotate gizmo on the selection (canvas_gizmo.cpp) ----
    enum class GizmoMode { Off = 0, Move = 1, Rotate = 2 };
    GizmoMode gizmoMode() const { return m_gizmoMode; }
    void setGizmoMode(GizmoMode m);
    // ---- Rotate tool + quick turns (canvas_rotate.cpp) ----
    void rotateSelection3D(double deg, int axis);             // 0 X, 1 Y, 2 Z, 3 sketch plane normal; about the selection centre
    QPointF gizmoGripScreen(int h) const;                     // screen point on a gizmo handle (self-test)
    // ---- constraints and corners (selected lines) ----
    void constrainSelection(int kind);            // 0 horizontal 1 vertical 2 parallel 3 perpendicular 4 equal 5 coincident
    void filletLines(double r, bool chamfer);     // two selected lines: round (fillet) or cut (chamfer) the corner
    void distributeSelection(bool horizontal);
    void centerSelectionOn(const QVector3D &target);
    QRectF selectionScreenRect() const;
    int selectedCount() const { return m_selected.size(); }
    bool selectionBarAllowed() const;
    int  lastTool() const { return int(m_lastTool); }
    // ---- transform size ----
    bool selectionSizeUV(QSizeF &size) const;                        // W x H in the sketch plane
    void resizeSelection(double w, double h, int anchor);            // anchor: 0 centre 1 BL 2 BR 3 TL 4 TR
    // precise move
    bool moveCopy() const { return m_moveCopy; }
    void setMoveCopy(bool c) { m_moveCopy = c; emit moveCopyChanged(c); emitHint(); update(); }
    void applyMove(const QVector3D &delta, bool copy);
    void setSelectedTextSize(double mm);

    // ---- files ----
    QByteArray serialize() const;
    bool deserialize(const QByteArray &data, bool restoreCamera = true);
    bool saveFile(const QString &path);
    bool loadFile(const QString &path);
    bool exportImage(const QString &path, const char *format);
    bool exportDxf(const QString &path);

    // ---- printing / vector PDF to scale (canvas_print.cpp, pages in printing.cpp) ----
    struct PlotView {
        double scale = 0;           // paper mm per model mm (0.1 = 1:10), 0 = fit the frame
        int area = 0;               // 0 drawing extents, 1 what the window shows
        int faces = 0;              // solids: 0 shaded faces, 1 hidden line (paper-white faces), 2 wireframe
        bool mono = false;          // all linework black
        bool edges = true;          // solid edges (off = faces only: the self-test's depth order check)
    };
    struct PlotResult {
        double scale = 0;           // paper mm per model mm used
        bool fit = false;
        bool clipped = false;       // the extents are bigger than the frame at this scale
        QSizeF modelSize;           // plotted extents in model mm (in the view plane)
        QTransform screenToPaper;   // window pixels (screenOf) -> paper mm
        int faces = 0, edgeRuns = 0, hiddenRuns = 0;   // vector faces / visible edge pieces / pieces removed
        double orderMs = 0, edgeMs = 0;                // time for the face order and the hidden-line pass
    };
    // the drawing as vector linework on a painter in paper mm, into frameMm (clipped to it)
    PlotResult paintPlot(QPainter &p, const QRectF &frameMm, const PlotView &v);
    PlotResult planPlot(const QRectF &frameMm, const PlotView &v) const;   // scale and fit only
    QImage zBufferImage();          // the screen's depth-buffered solids, window size (self-test)

    // ---- navigation ----
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void zoomFitSelection();

    QUndoStack *undoStack() { return &m_undo; }

signals:
    void cursorMoved(const QVector3D &p);
    void message(const QString &s);
    void hint(const QString &s);          // persistent prompt for the active tool
    void viewChanged();
    void planeChanged();
    void settingsChanged();
    void snapStateChanged(const QString &s);
    void selectionChanged();
    void contentChanged();
    void toolSizesChanged();                      // fillet / chamfer / shell / pipe sizes or the extrude choice changed
    void dimModeChanged(int mode);
    void offsetDistanceChanged(double mm);
    void cutModeChanged(int mode);
    void bladeActionChanged(int action);
    void mirrorKeepChanged(bool keep);
    void moveCopyChanged(bool copy);
    void gizmoModeChanged(int mode);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    bool event(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool focusNextPrevChild(bool) override { return false; }   // Tab belongs to the canvas

private:
    friend class SnapshotCommand;

    struct Frame { QVector3D U, V, N; };           // in-plane axes + positive normal
    struct Snap { QVector3D pt; int kind = 0; bool valid = false; QString note; };
    struct SnapCand { QVector3D p; int kind; };
    // object snap (canvas_snap.cpp): one list of points per object; kinds 2 end, 3 mid, 4 centre, 6 intersection,
    // 7 perpendicular, 9 nearest, 10 face centre, 11 body centre, 12 on a face, 13 Move up to a face
    void allSnapPoints(const SketchItem *it, QList<SnapCand> &out) const;
    void solidSnapPoints(const SketchItem *it, QList<SnapCand> &out) const;
    struct SnapCache { quint64 key = 0; QList<SnapCand> pts; };
    mutable QHash<const SketchItem *, SnapCache> m_snapCache;   // a body's snap points, kept while it is unchanged
    bool osnapOn(int kind) const;
    static int osnapBit(int kind);
    bool moveUpToFace(const QPoint &screen, Snap &s);
    void noteMoveBase(const QPoint &screen);
    bool m_moveBaseOn = false;                 // the Move base point was picked on a face of the moving body
    QVector3D m_moveBaseN, m_moveBasePt;
    struct Theme;

    // camera
    QVector3D m_target;
    qreal m_zoom = 3.0;             // pixels per mm at the target
    View m_view = View::Top;
    QQuaternion m_isoQ;             // free 3D camera: local X = right, Y = up, Z = towards the eye
    QQuaternion m_frame;            // user view orientation (view-cube space -> world), identity = standard
    QQuaternion m_homeQ;            // user Home camera (world), valid when m_hasHome
    bool m_hasHome = false;
    bool m_rmbOnCube = false;
    QVector3D m_br, m_bu, m_bf;     // cached camera basis (right, up, forward)
    QPointer<QVariantAnimation> m_viewAnim;
    static QQuaternion camFromDirUp(const QVector3D &eye, const QVector3D &up, const QQuaternion &fallback);
    QQuaternion currentCamQ() const { return QQuaternion::fromAxes(m_br, m_bu, -m_bf); }
    QQuaternion namedViewQ(View v, bool withFrame) const;
    void namedBasis(View v, QVector3D &r, QVector3D &u, bool withFrame) const;
    QVector3D frameUp() const { return m_frame.rotatedVector(QVector3D(0, 0, 1)); }
    void animateTo(const QQuaternion &target, const QString &msg, View endView = View::Iso);
    void showCubeMenu(const QPoint &global);

    // document
    QList<SketchItem *> m_items;
    QSizeF m_pageSize = QSizeF(210, 297);
    double m_gridSize = 5.0;
    Plane m_plane = Plane::TopXY;
    double m_planeOffset = 0.0;
    double m_offsets[4] = {0, 0, 0, 0};      // remembered offset per sketch plane
    qint64 m_edgeMsgAt = 0;
    void switchPlane(Plane p);
    ViewSettings m_set;

    // defaults
    QColor m_defColor = QColor(216, 226, 240);
    double m_defWidth = 0.35;
    double m_defTextSize = 3.5;
    double m_offsetDist = 5.0;
    SketchItem *m_offsetSrc = nullptr;   // Offset: picked entity, the cursor then chooses the side
    QString m_offsetTyped;               // Offset: distance being typed
    bool m_offsetThrough = false;        // Offset: distance follows the cursor (T)
    double m_offsetShown = 0;            // Offset: distance of the current preview
    SketchItem *m_lastOffsetMade = nullptr;  // Offset: Enter picks the last copy for the next step
    HatchStyle m_hatchStyle;
    struct HatchGraph;                   // planar map of the boundaries on the sketch plane (canvas_hatch.cpp)
    mutable std::shared_ptr<HatchGraph> m_hatchGraph;
    QList<QVector<QVector3D>> m_hatchPreview;  // region under the cursor while the Hatch tool is active
    double m_hatchPreviewScale = 0;            // spacing the clicked hatch will get

    // ---- solids: faces, depth-buffered drawing, Box and Press/Pull (canvas_solid.cpp) ----
    struct SolidFace {
        const SketchItem *it = nullptr;
        int kind = -1;                         // 0 base cap, 1 far cap, 2 + i = side i, -1 flat closed shape
        QVector<QVector3D> v;                  // corners (open loop)
        QVector3D n;                           // outward normal
        QList<int> loops;                      // end caps with holes: corner count of each loop in v
    };
    QList<SolidFace> solidFaces(const SketchItem *it) const;
    bool faceAt(const QPointF &s, SolidFace &face, QVector3D &hit, const SketchItem *skip = nullptr) const;
    bool flatShapeAt(const QPointF &s, SolidFace &face, QVector3D &hit) const;
    void renderSolids(QPainter &p);
    void drawSolidEdges(QPainter &p, const SketchItem *it, const QPen &pen) const;
    void drawFaceHighlight(QPainter &p, const QVector<QVector3D> &v, const QColor &c, const QList<int> &loops = {}) const;
    void drawSolidOverlay(QPainter &p) const;  // Box / Press-Pull feedback
    mutable QImage m_zImg;                     // depth-buffered solids, device pixels
    mutable QVector<float> m_zBuf;
    mutable QVector<int> m_zId;                // face drawn in each pixel (index into m_zPlanes), -1 = none
    mutable QVector<QVector3D> m_zPlanes;      // per face: depth = x * px + y * py + z (image pixels)
    mutable qreal m_zScale = 1;
    mutable size_t m_zKey = 0;
    mutable bool m_zValid = false;
    // Box tool: corner, corner, height (the base lies on the face you start on)
    double m_boxHeight = 18.0;
    double m_boxCurH = 0;
    QString m_boxNote;
    int m_boxSavedPlane = -1;
    double m_boxSavedOffset = 0;
    double m_boxOutSign = 1;
    SolidFace m_hoverFace;
    bool m_hoverFaceValid = false;
    double solidDistanceAt(const QPoint &s, const QVector3D &from, const QVector3D &dir, const SketchItem *skip,
                           QString *note) const;
    bool boxStartOnFace(const QPoint &s);
    void boxRestorePlane();
    void finishBox(double h);
    // Press/Pull: drag a face of a board (or a closed shape) along its normal
    SketchItem *m_ppItem = nullptr;
    SolidFace m_ppFace;
    QVector3D m_ppPoint;
    QList<QVector3D> m_ppOrigPts;
    QVector3D m_ppOrigExt;
    double m_ppDist = 0;
    QString m_ppNote;
    int m_ppOp = 0;                               // a pulled shape, the default: 0 Auto, 1 New, 2 Join, 3 Cut, 4 Intersect
    int m_ppOpNow = 0;                            // this pull (N / J / C / I change only it; each pull starts at the default)
    struct PullPick { int item = -1; SolidFace face; };
    QList<PullPick> m_ppSet;                      // Shift+clicked faces waiting to be pulled together
    struct PullOrig { SketchItem *it = nullptr; QList<QVector3D> pts; QVector3D ext; };
    QList<PullOrig> m_ppOrig;                     // every board being pulled, as it was
    QList<SolidFace> m_ppFaces;                   // the faces being pulled, the clicked one first
    QList<SketchItem *> m_ppScope;                // selected boards: an extruded shape cuts / joins only these
    bool m_ppToFace = false;                      // pulling up to m_ppTo: parallel faces land on its plane
    SolidFace m_ppTo;
    void pullSetToggle(const QPoint &s);
    bool faceAtIf(const QPointF &s, SolidFace &face, QVector3D &hit, const std::function<bool(const SketchItem *)> &skip) const;
    SolidFace m_moveTo;                           // Move up to a face: the face it goes to
    bool m_moveToValid = false;
    bool isPulling(const SketchItem *it) const;
    void updatePullTarget(const QPoint &s);
    double pullFace(SketchItem *it, const SolidFace &f, double d);
    bool booleanItems(const QList<SketchItem *> &ops, BoolOp op, bool keepCutters, QString &msg, QList<SketchItem *> &made);
    int solidContact(const SketchItem *a, const SketchItem *b) const;
    int extrudeAutoOp(const SketchItem *body) const;
    bool pickPullFace(const QPoint &s, SolidFace &f, QVector3D &hit) const;
    QString extrudeCombine(SketchItem *body, int op);
    bool pressPullStart(const QPoint &s);
    void applyPull(double d);
    void pressPullCommit();
    void pressPullCancel();
    void updateSolidHover(const QPoint &s);
    bool boolPrism(const SketchItem *it, const QVector3D &a, const QVector3D &u, const QVector3D &v, QPainterPath &path,
                   double &lo, double &hi) const;
    // gizmo (canvas_gizmo.cpp)
    GizmoMode m_gizmoMode = GizmoMode::Move;
    int m_gzHover = -1, m_gzActive = -1;       // handle under the mouse / being dragged
    QVector3D m_gzCenter, m_gzStart, m_gzDelta;
    double m_gzT0 = 0, m_gzA0 = 0, m_gzAngle = 0;
    QString m_gzNote;
    bool gizmoVisible() const;
    double gizmoWorldSize(const QVector3D &c) const;
    int gzHit(const QPoint &s) const;
    double gzRingAngle(const QPoint &s) const;
    void gzBegin(int h, const QPoint &s);
    void gzUpdate(const QPoint &s);
    void gzCommit();
    void gzCancel();
    std::function<QVector3D(const QVector3D &)> gzTransform() const;
    void applyXformSelection(const std::function<QVector3D(const QVector3D &)> &f, const QString &name,
                             const std::function<void(SketchItem *)> &post = nullptr);
    // Rotate tool (canvas_rotate.cpp)
    int m_r3Stage = 0;                         // 0 pick objects, 1 base point, 2 angle
    int m_r3Axis = 3;                          // 0 X, 1 Y, 2 Z, 3 the sketch plane's normal
    QVector3D m_r3Base;
    double m_r3Angle = 0;
    QVector3D r3AxisVec() const;
    double r3AngleAt(const QPoint &s) const;
    double r3SnapAngle(double a) const;
    std::function<void(SketchItem *)> textTurn(const QVector3D &axis, double deg) const;
    void r3Apply(double deg);
    void drawRotateTool(QPainter &p) const;
    void drawGizmo3D(QPainter &p) const;
    // Fillet / Chamfer tools (canvas_fillet.cpp)
    struct FilletHit {
        SketchItem *it = nullptr;
        int kind = -1;                         // 0 2D corner, 1 board edge along the thickness, 2 box edge across, 3 two lines, -1 not possible
        int loop = 0, idx = 0;                 // the corner in the profile
        QVector3D a, b;                        // the edge (a == b for a corner)
        SketchItem *other = nullptr;           // kind 3: the second line
        bool valid = false;
        QString why;                           // kind -1: the reason
    };
    FilletHit m_fh;
    double m_filletR = 6, m_chamferD = 3;
    QList<QList<QVector3D>> profileRings(const SketchItem *it, bool &closed) const;
    SketchItem *ringsToItem(const SketchItem *src, const QList<QList<QVector3D>> &rings, bool closed,
                            const QVector3D &extrude, int plane) const;
    void replaceItem(SketchItem *old, SketchItem *n);
    bool isBoxSolid(const SketchItem *it) const;
    void updateFilletHover(const QPoint &s);
    void filletClick();
    void drawFilletTool(QPainter &p) const;
    QList<QVector<QVector3D>> silhouetteEdges(const SketchItem *it) const;
    static bool smoothVertex(const QVector3D &prev, const QVector3D &p, const QVector3D &next);
    // Revolve / Pipe (canvas_sweep.cpp)
    struct SweepGrid {                         // rings swept along: faces join ring s to s + 1 (last to first if wrap)
        QList<QVector<QVector3D>> rings;
        QVector<char> sharp;                   // per ring point: a crease runs along it
        bool wrap = false, flip = false;
    };
    struct SweepCap { QVector<QVector3D> v; QList<int> loops; QVector3D n; };
    struct SweepData {
        QList<SweepGrid> grids;
        QList<SweepCap> caps;
        QList<QVector<QVector3D>> feature;     // creases, end outlines (and a pipe's centre line)
        QList<SolidFace> faces;                // made once, it = nullptr
        double length = 0, radius = 0, axial = 0;   // pipe: developed length; revolve: largest radius, length
    };
    mutable QHash<quint64, std::shared_ptr<SweepData>> m_sweepCache;
    std::shared_ptr<const SweepData> sweepData(const SketchItem *it) const;
    void buildRevolve(const SketchItem *it, SweepData &d) const;
    void buildPipe(const SketchItem *it, SweepData &d) const;
    QList<QVector<QVector3D>> sweepSilhouettes(const SketchItem *it) const;
    QList<QVector3D> profileRing(const SketchItem *it) const;
    bool makeRevolve(SketchItem *profile, const QVector3D &a, const QVector3D &b, double deg, QString &err);
    bool makePipe(SketchItem *path, QString &err);
    void revolveClick(const QPoint &s);
    void pipeClick(const QPoint &s);
    void drawSweepTool(QPainter &p) const;
    int m_rvStage = 0, m_rvProfile = -1;
    QVector3D m_rvA;
    double m_rvAngle = 360;
    double m_pipeD = 25, m_pipeWall = 0, m_pipeBend = 0;
    // Shell tool (canvas_shell.cpp)
    double m_shellT = 18;
    SketchItem *boxItem(const QVector3D &o, const QVector3D &a, const QVector3D &b, const QVector3D &c,
                        const SketchItem *like) const;
    bool shellSolid(SketchItem *it, int openKind, const QVector3D &openN, double t, QString &err);
    void shellClick(const QPoint &s);
    // printing (canvas_print.cpp): while m_plot is set the camera is scaled into plot pixels of 1/pxPerMm paper mm
    struct PlotState { qreal pxPerMm = 40; bool mono = false; };
    const PlotState *m_plot = nullptr;
    qreal m_focalMul = 1;                      // focalPx() factor while plotting (a perspective eye stays put)
    QRectF plotExtents() const;                // window pixels covered by the visible drawing
    void plotSolids(QPainter &p, const PlotView &v, PlotResult &r);
    qreal annotPx(qreal screenPx) const;       // fixed-size annotation: 1 window px = 0.25 paper mm when plotting
    qreal itemPenPx(const SketchItem *it, qreal sc) const;   // line weight: true mm on screen, paper mm on paper
    void applyLineStyle(QPen &pen, const SketchItem *it, qreal lw, qreal sc) const;

    // interaction
    Tool m_tool = Tool::Select;
    QList<QVector3D> m_pending;
    QImage m_pendingImage;
    Snap m_cur;                      // snapped 3D cursor
    int m_axisLock = 0;              // 0 none, 1 X, 2 Y, 3 Z
    QList<QPair<QVector3D, QVector3D>> m_guides;
    QPair<QVector3D, QVector3D> m_trackLine;   // ortho / polar tracking ray
    bool m_trackOn = false;
    bool m_dragging = false, m_dragMoved = false;
    QVector3D m_dragStart;
    QVector3D m_dynDelta;
    QHash<SketchItem *, QList<QVector3D>> m_dragData;
    QPoint m_pressScreen, m_lastScreen, m_lastMouse;
    bool m_panning = false, m_orbiting = false, m_rmbDown = false, m_spaceDown = false;
    bool m_overGizmo = false;
    int m_gizmoHover = -1;          // hovered view-cube hotspot (see gizmoHit)
    QRectF m_rubberBand;
    bool m_rubberActive = false;
    QList<SketchItem *> m_selected;
    SketchItem *m_hover = nullptr;
    SketchItem *m_editItem = nullptr;
    int m_editIndex = -1;
    SketchItem *m_joinFirst = nullptr;
    QLineEdit *m_editor = nullptr;
    SketchItem *m_editorItem = nullptr;
    QByteArray m_snapshot;
    QUndoStack m_undo;
    bool m_exporting = false;

    // dynamic input (size fields drawn at the cursor)
    struct DynField { QString text; };
    DynField m_dyn[2];
    int m_dynIdx = 0;
    struct SmartGuide { QVector3D a, b; QString label; };
    QList<SmartGuide> m_smart;
    QList<QRectF> m_staticBoxes;
    bool m_boxesValid = false;
    Tool m_lastTool = Tool::Select;
    bool uvBounds(const SketchItem *it, const Frame &fr, QRectF &r) const;
    struct AlignUnit { QList<SketchItem *> items; QRectF box; };
    QList<AlignUnit> alignUnits(const QList<SketchItem *> &sel, const Frame &fr) const;
    bool smartAlign(QVector3D &delta, const QVector3D &applied);
    void drawWelcome(QPainter &p) const;
    int m_defStyle = 0;
    bool m_resizing = false;
    int m_rsHandle = -1;
    QRectF m_rsBox;
    QHash<SketchItem *, QList<QVector3D>> m_rsOrig;
    QHash<SketchItem *, QPair<double, QSizeF>> m_rsExtra;     // text height / image size
    QVector3D m_rsPlanePt;
    qreal m_rsSu = 1, m_rsSv = 1;
    qint64 m_limitMsgAt = 0;
    bool selectionBoxUV(QRectF &r) const;
    QPointF boxHandleUV(const QRectF &r, int i) const;
    int handleAt(const QPoint &s) const;
    void beginResize(int handle);
    void doResize(bool freeScale);
    void applyScaleUV(const QPointF &anchor, qreal su, qreal sv);
    void drawTransformBox(QPainter &p) const;
    // free rotation with the round grip above the selection box
    bool m_rotating = false;
    bool m_snapshotHeld = false;
    bool m_rotHover = false;
    QVector3D m_rotCenter;
    qreal m_rotStart = 0, m_rotAngle = 0;
    QHash<SketchItem *, double> m_rotTxt;
    QHash<SketchItem *, QVector3D> m_rotExt;
    bool rotGripScreen(const QRectF &r, QPointF &out) const;
    bool rotAngleAt(const QPoint &s, qreal &deg) const;
    void beginRotate(const QPoint &s);
    void doRotate(const QPoint &s, bool snap15);
    void finishRotate();
    SketchItem *rectToPolyline(SketchItem *old, const QList<QVector3D> &origPts);
    bool closedShape(const SketchItem *it) const;
    int m_moveStage = 0;             // 0 = choosing objects, 1 = base point / target
    bool m_moveCopy = false;
    QVector3D m_magnet;              // vertex-to-vertex snap while dragging
    bool m_magnetOn = false;
    void regroup(const QList<SketchItem *> &made);
    void itemSnapPoints(const SketchItem *it, QList<SnapCand> &out) const;
    bool dragMagnet(QVector3D &delta);
    QFont textFont(const SketchItem *it) const;
    QTransform textXf(const SketchItem *it) const;
    int m_mirrorStage = 0;           // 0 = choosing objects, 1 = picking the axis
    bool m_mirrorKeep = true;
    int m_dimMode = 0;
    mutable bool m_fine = false;     // fine tessellation (used for exact trim / extend)
    std::function<void(const QVector3D &)> m_pickCb;
    QList<QVector<QVector3D>> m_ghosts;   // trim / extend / cutter preview strips
    QVector<QVector3D> m_marks;           // cut points
    bool m_ghostExtend = false;
    int m_cutMode = 0;
    int m_bladeAction = 0;
    bool m_fenceDown = false, m_fenceDrag = false;
    QPoint m_fenceA, m_fenceB;
    struct CutPath;

    struct Piece { SketchItem::Type type; QList<QVector3D> pts; };
    struct EditOp { bool ok = false; SketchItem *target = nullptr; QList<Piece> pieces;
                    QList<QVector<QVector3D>> ghost; QVector<QVector3D> marks; QString why; };
    struct DimGeom { bool ok = false; QVector3D d1, d2; qreal value = 0; };

    // ---- camera / projection ----
    void updateCamera();
    void viewBasis(QVector3D &r, QVector3D &u, QVector3D &f) const;
    qreal focalPx() const;
    QVector3D eyePos() const;
    qreal nearDist() const;
    bool projectD(const QVector3D &p, QPointF &out, qreal *depth = nullptr) const;
    QPointF project(const QVector3D &p) const;
    bool projectSeg(const QVector3D &a, const QVector3D &b, QLineF &out) const;
    qreal scaleAt(const QVector3D &p) const;                  // pixels per mm at p
    void screenRay(const QPointF &s, QVector3D &ro, QVector3D &rd) const;
    static bool rayPlane(const QVector3D &ro, const QVector3D &rd, const QVector3D &p0,
                         const QVector3D &n, QVector3D &out);
    QPointF viewCenter() const { return QPointF(width() / 2.0, height() / 2.0); }

    // ---- planes ----
    static Frame frameFor(int plane);
    Frame planeFrame() const { return frameFor(int(m_plane)); }
    QVector3D planeOrigin() const;
    void syncPlaneToView();

    // ---- snapping ----
    Snap snapCursor(const QPoint &screen, const QVector3D *planeThrough = nullptr);
    bool drawingTool() const;
    bool lockAllowed() const;
    QVector3D lockAnchor() const;
    void collectSnapPoints(QList<SnapCand> &out) const;
    QVector3D numericPoint(const QVector3D &cursor) const;
    QVector3D previewPoint() const;

    // ---- geometry ----
    QList<QVector<QVector3D>> strips(const SketchItem *it) const;
    QList<QVector<QVector3D>> stripsBase(const SketchItem *it) const;
    void drawSolid(QPainter &p, const SketchItem *it, const QVector<QVector3D> &ring) const;
    QVector<QVector3D> rectCorners(const SketchItem *it) const;
    QVector<QVector3D> imageCorners(const SketchItem *it) const;
    bool arcData(const QVector3D &a, const QVector3D &b, const QVector3D &c,
                 QVector3D &cen, qreal &rad, qreal &a0, qreal &sweep, QVector3D &e1,
                 QVector3D &e2) const;
    QRectF textRect(const SketchItem *it) const;
    qreal textPixelSize(const SketchItem *it) const;
    SketchItem *pick(const QPointF &s, qreal tolPx) const;
    QRectF itemScreenBounds(const SketchItem *it) const;
    QVector3D selectionCenter() const;
    double itemLength(const SketchItem *it) const;

    // ---- selection / undo helpers ----
    void deleteItems();
    void setSelected(SketchItem *it, bool sel);
    void clearSelection();
    void snapshotBegin() { m_snapshot = serialize(); }
    void snapshotCommit(const QString &name);
    void beginDrag(const QVector3D &start);
    void finishPending();
    void cancelPending();
    void emitHint();
    void emitSnapState();
    void applyNumeric();
    void addPoint(const QVector3D &p);
    void startOrbit(const QPoint &pos);

    // ---- dynamic input / edit operations (canvas_edit.cpp) ----
    int dynFieldCount() const;
    bool dynActive() const { return dynFieldCount() > 0; }
    bool dynTyping() const { return !m_dyn[0].text.isEmpty() || !m_dyn[1].text.isEmpty(); }
    void dynClear() { m_dyn[0].text.clear(); m_dyn[1].text.clear(); m_dynIdx = 0; }
    bool dynValue(int i, double &v) const;
    bool dynIsAngle(int i) const;
    QString dynShown(int i, bool caret) const;   // typed text, caret, and "= result" for a sum
    void dynNext();
    EditOp computeTrim(SketchItem *it, const QPoint &s);
    EditOp computeExtend(SketchItem *it, const QPoint &s);
    void applyEdit(const EditOp &op, const QString &name);
    void trimExtendClick(const QPoint &pos, bool shift);
    QList<QLineF> boundarySegs(const SketchItem *skip, const Frame &fr, qreal nref) const;
    QVector<qreal> pathIntersections(const QVector<QVector3D> &path, const QList<QLineF> &bounds,
                                     const Frame &fr) const;
    SketchItem *xformClone(const SketchItem *it, const std::function<QVector3D(const QVector3D &)> &f) const;
    QVector3D itemCenter(const SketchItem *it) const;
    DimGeom dimGeom(const SketchItem *it) const;
    void drawDynFields(QPainter &p, const QVector3D &anchor, const QVector3D &cur) const;
    void drawGhost(QPainter &p) const;

    // ---- cutting engine (canvas_cut.cpp) ----
    bool makeCutPath(const SketchItem *it, CutPath &cp) const;
    QVector3D cutPointAt(const CutPath &cp, qreal u) const;
    QVector<QVector3D> cutSample(const CutPath &cp, qreal u0, qreal u1) const;
    void cutPieces(const CutPath &cp, qreal u0, qreal u1, QList<Piece> &out) const;
    QVector<qreal> cutParams(const CutPath &cp) const;
    QVector<qreal> cutParams3D(const CutPath &cp) const;
    QVector<qreal> cutParamsView(const CutPath &cp, const QList<QLineF> &cutters) const;
    QList<QLineF> cutterScreenSegs(const SketchItem *skip) const;
    bool pathScreen(const CutPath &cp, QVector<QPointF> &sp, QVector<qreal> &dep, QVector<char> &ok) const;
    qreal viewT(qreal ts, qreal da, qreal db) const;
    qreal cutClickParam(const CutPath &cp, const QPointF &s) const;
    EditOp trimByClicks(const CutPath &cp, const QVector<qreal> &cuts, const QVector<qreal> &clicks) const;
    QVector<qreal> bladeParams(const CutPath &cp, const QVector3D &A, const QVector3D &B, bool infinite) const;
    int bladeSide(const QVector3D &A, const QVector3D &B, const QVector3D &p) const;
    EditOp bladeOp(const SketchItem *it, const QVector3D &A, const QVector3D &B, int removeSide, bool infinite) const;
    QList<SketchItem *> cutCandidates() const;
    void bladeApply(const QVector3D &A, const QVector3D &B, int removeSide);
    void updateBladePreview();
    void fenceTrim(const QPoint &a, const QPoint &b, bool extend);
    void applyEdits(const QList<EditOp> &ops, const QString &name);
    void drawBlade(QPainter &p) const;
    void drawFence(QPainter &p) const;

    // ---- operations ----
    void createTextAt(const QVector3D &p);
    void insertPendingImageAt(const QVector3D &p);
    void trimAt(const QPoint &s);
    void breakAt(const QPoint &s);
    void joinClick(SketchItem *it);
    void offsetClick(SketchItem *it, const QPoint &s);
    // parallel copy of it on the side of screen point s; d < 0 = through the cursor point
    SketchItem *offsetShape(const SketchItem *it, const QPoint &s, double d, QString *why,
                            double *usedDist = nullptr) const;
    void offsetCommit(const QPoint &s, double d);
    void updateOffsetPreview();
    bool offsetSourceValid() const { return m_offsetSrc && m_items.contains(m_offsetSrc); }
    // hatch (canvas_hatch.cpp)
    std::shared_ptr<HatchGraph> hatchGraph() const;
    bool hatchRegionAt(const QVector3D &p, QList<QVector<QVector3D>> &loops) const;
    void hatchClick(const QPoint &s);
    void updateHatchPreview();
    QList<QVector<QVector3D>> hatchLoops(const SketchItem *it) const;    // closed boundary loops, 3D
    bool hasHatch(const SketchItem *it) const;
    double hatchSpacing(const SketchItem *it, const QList<QVector<QVector3D>> &loops) const;
    QVector<QPair<QVector3D, QVector3D>> hatchSegments(const SketchItem *it, const QList<QVector<QVector3D>> &loops,
                                                       double spacing, int maxSegs, bool *tooDense) const;
    void drawHatch(QPainter &p, const SketchItem *it, const QList<QVector<QVector3D>> &loops) const;
    static double loopsArea(const QList<QVector<QPointF>> &loops);
    void startInlineEdit(SketchItem *it);
    void applyInlineEdit();
    void showContextMenu(const QPoint &global);
    void rotatePoint(QVector3D &p, const QVector3D &c, const QVector3D &axis, qreal deg) const;

    // ---- painting ----
    const Theme &theme() const;
    QColor adapt(const QColor &c) const;
    void drawScene(QPainter &p);
    void drawBackground(QPainter &p);
    void drawGrid(QPainter &p);
    void drawAxes(QPainter &p);
    void drawStrip(QPainter &p, const QVector<QVector3D> &s) const;
    void drawItem(QPainter &p, const SketchItem *it, bool preview = false) const;
    void drawDimension(QPainter &p, const SketchItem *it, const QPen &pen) const;
    void drawPending(QPainter &p) const;
    void drawGuides(QPainter &p) const;
    void drawSnapMarker(QPainter &p) const;
    void drawHandles(QPainter &p) const;
    void drawGizmo(QPainter &p);
    void drawHud(QPainter &p) const;
    QRectF gizmoRect() const;
    int gizmoHit(const QPoint &pos) const;  // 0..25 = cube face/edge/corner, kGizmoHome, -1 = none
    static QVector3D gizmoDirection(int hit);
    static constexpr int kGizmoHome = 26;
    static constexpr int kGizmoRollL = 27;   // roll the view 90 deg counter-clockwise
    static constexpr int kGizmoRollR = 28;   // ... clockwise
    static QColor accent() { return QColor(53, 224, 200); }
};
