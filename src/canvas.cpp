#include "canvas.h"
#include "canvas_util.h"
#include "units.h"

#include <QAction>
#include <QApplication>
#include <QGuiApplication>
#include <QDateTime>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// names
// ---------------------------------------------------------------------------
QString Canvas::toolName(Tool t)
{
    switch (t) {
    case Tool::Select:     return "Select";
    case Tool::Line:       return "Line";
    case Tool::Curve3:     return "3-Point Curve";
    case Tool::Arc:        return "3-Point Arc";
    case Tool::Circle:     return "Circle";
    case Tool::Polyline:   return "Polyline";
    case Tool::Rect:       return "Rectangle";
    case Tool::Dimension:  return "Dimension";
    case Tool::Text:       return "Text";
    case Tool::InsertImage:return "Insert Image";
    case Tool::Trim:       return "Trim";
    case Tool::Join:       return "Join";
    case Tool::Break:      return "Break";
    case Tool::Offset:     return "Offset";
    case Tool::Extend:     return "Extend";
    case Tool::Mirror:     return "Mirror";
    case Tool::Cutter:     return "Cutter";
    case Tool::Edit:       return "Shape Edit";
    case Tool::FreeMove:   return "Drag Move";
    case Tool::DynMove:    return "Dynamic Move";
    case Tool::Move:       return "Move";
    case Tool::Hatch:      return "Hatch / Fill";
    case Tool::Box:        return "Box";
    case Tool::PressPull:  return "Press/Pull";
    case Tool::Rotate:     return "Rotate";
    case Tool::Fillet:     return "Fillet";
    case Tool::Chamfer:    return "Chamfer";
    case Tool::Shell:      return "Shell";
    case Tool::Revolve:    return "Revolve";
    case Tool::Pipe:       return "Pipe";
    }
    return "Select";
}

QString Canvas::planeName(Plane p)
{
    switch (p) {
    case Plane::XZ: return "XZ";
    case Plane::ZY: return "ZY";
    case Plane::YX: return "YX";
    default:        return "XY";
    }
}

QString Canvas::viewName(View v)
{
    switch (v) {
    case View::Front:  return "FRONT";
    case View::Side:   return "LEFT";
    case View::Iso:    return "ISO 3D";
    case View::Bottom: return "BOTTOM";
    case View::Back:   return "BACK";
    case View::Right:  return "RIGHT";
    default:           return "TOP";
    }
}

static Qt::CursorShape cursorFor(Canvas::Tool t)
{
    return (t == Canvas::Tool::Select || t == Canvas::Tool::Text) ? Qt::ArrowCursor
                                                                  : Qt::CrossCursor;
}

// ---------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------
Canvas::Canvas(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(480, 340);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::ArrowCursor);
    m_isoQ = camFromDirUp(QVector3D(1, 1, 1), QVector3D(0, 0, 1), QQuaternion());
    updateCamera();
}

Canvas::~Canvas()
{
    deleteItems();
}

// ---------------------------------------------------------------------------
// tool / plane / view state
// ---------------------------------------------------------------------------
void Canvas::setTool(Tool t)
{
    if (m_ppItem)
        pressPullCancel();
    if (m_gzActive >= 0)
        gzCancel();
    boxRestorePlane();
    m_hoverFaceValid = false;
    if (m_tool != Tool::Select && m_tool != t)
        m_lastTool = m_tool;
    m_tool = t;
    m_pending.clear();
    m_joinFirst = nullptr;
    m_editItem = nullptr;
    m_editIndex = -1;
    m_guides.clear();
    dynClear();
    m_ghosts.clear();
    m_marks.clear();
    m_fenceDown = m_fenceDrag = false;
    m_pickCb = nullptr;
    m_offsetSrc = nullptr;
    m_offsetTyped.clear();
    m_offsetThrough = false;
    m_lastOffsetMade = nullptr;
    m_hatchPreview.clear();
    m_mirrorStage =(t == Tool::Mirror && !m_selected.isEmpty()) ? 1 : 0;
    m_moveStage = (t == Tool::Move && !m_selected.isEmpty()) ? 1 : 0;
    m_r3Stage = (t == Tool::Rotate && !m_selected.isEmpty()) ? 1 : 0;
    m_r3Axis = 3;
    m_r3Angle = 0;
    m_fh = FilletHit();
    m_moveBaseOn = false;
    m_moveToValid = false;
    m_ppSet.clear();
    m_ppScope.clear();
    m_rvStage = 0;
    m_rvProfile = -1;
    m_magnetOn = false;
    m_axisLock = 0;
    setCursor(cursorFor(t));
    emit message("Tool: " + toolName(t));
    emitHint();
    emitSnapState();
    update();
}

// change the sketch plane: every plane keeps its own offset, and the cursor is re-snapped at once
void Canvas::switchPlane(Plane p)
{
    m_offsets[int(m_plane)] = m_planeOffset;
    m_plane = p;
    m_planeOffset = m_offsets[int(p)];
    m_pending.clear();
    dynClear();
    if (!m_lastMouse.isNull())
        m_cur = snapCursor(m_lastMouse);
}

void Canvas::setPlane(Plane p)
{
    switchPlane(p);
    m_pending.clear();
    m_editItem = nullptr;
    m_editIndex = -1;
    dynClear();
    emit message("Sketch plane: " + planeName(p));
    emit planeChanged();
    emitHint();
    update();
}

void Canvas::cyclePlane()
{
    const int n = (int(m_plane) == 0) ? 1 : (int(m_plane) == 1 ? 2 : 0);
    setPlane(Plane(n));
}

void Canvas::setPlaneOffset(double mm)
{
    m_planeOffset = mm;
    emit planeChanged();
    update();
}

// named views only pick a sketch plane when they really look along a world axis
// (with a user view orientation they may not)
void Canvas::syncPlaneToView()
{
    if (!m_set.syncPlane || m_view == View::Iso)
        return;
    Plane p = m_plane;
    if (std::fabs(m_bf.z()) > 0.999f)
        p = Plane::TopXY;
    else if (std::fabs(m_bf.y()) > 0.999f)
        p = Plane::XZ;
    else if (std::fabs(m_bf.x()) > 0.999f)
        p = Plane::ZY;
    else
        return;
    if (p != m_plane) {
        switchPlane(p);
        emit planeChanged();
    }
}

void Canvas::setView(View v)
{
    if (m_viewAnim)
        m_viewAnim->stop();
    m_view = v;
    m_rubberActive = false;
    updateCamera();
    syncPlaneToView();
    emit message("Viewport: " + viewName(v) + (customOrientation() && v != View::Iso ? " (user orientation)" : ""));
    emit viewChanged();
    emitSnapState();
    update();
}

// camera looking from eye towards the target, screen-up as close to up as possible
QQuaternion Canvas::camFromDirUp(const QVector3D &eye, const QVector3D &up, const QQuaternion &fallback)
{
    const QVector3D d = eye.normalized();
    QVector3D r = QVector3D::crossProduct(-d, up);
    if (r.length() < 1e-5f) {                       // looking along up: keep the old screen-right
        r = fallback.rotatedVector(QVector3D(1, 0, 0));
        r = (r - d * QVector3D::dotProduct(r, d));
        if (r.length() < 1e-5f)
            r = QVector3D::crossProduct(d, QVector3D(0, 1, 0)).length() > 1e-5f
                    ? QVector3D::crossProduct(QVector3D(0, 1, 0), d) : QVector3D(1, 0, 0);
    }
    r.normalize();
    const QVector3D u = QVector3D::crossProduct(d, r).normalized();
    return QQuaternion::fromAxes(r, u, d).normalized();
}

void Canvas::namedBasis(View v, QVector3D &r, QVector3D &u, bool withFrame) const
{
    switch (v) {
    case View::Front:  r = QVector3D(1, 0, 0);  u = QVector3D(0, 0, 1); break;
    case View::Side:   r = QVector3D(0, 0, 1);  u = QVector3D(0, 1, 0); break;
    case View::Back:   r = QVector3D(-1, 0, 0); u = QVector3D(0, 0, 1); break;
    case View::Right:  r = QVector3D(0, 0, -1); u = QVector3D(0, 1, 0); break;
    case View::Bottom: r = QVector3D(1, 0, 0);  u = QVector3D(0, -1, 0); break;
    default:           r = QVector3D(1, 0, 0);  u = QVector3D(0, 1, 0); break;
    }
    if (withFrame) {
        r = m_frame.rotatedVector(r);
        u = m_frame.rotatedVector(u);
    }
}

QQuaternion Canvas::namedViewQ(View v, bool withFrame) const
{
    QVector3D r, u;
    namedBasis(v, r, u, withFrame);
    return QQuaternion::fromAxes(r, u, QVector3D::crossProduct(r, u)).normalized();
}

bool Canvas::customOrientation() const
{
    return !qFuzzyCompare(std::fabs(m_frame.scalar()), 1.0f);
}

// smooth camera move (free 3D view); endView != Iso lands exactly on that named view
void Canvas::animateTo(const QQuaternion &target, const QString &msg, View endView)
{
    if (m_viewAnim)
        m_viewAnim->stop();
    if (m_view != View::Iso) {
        m_isoQ = currentCamQ();
        m_view = View::Iso;
    }
    m_rubberActive = false;
    const QQuaternion from = m_isoQ;
    QQuaternion to = target;
    if (QQuaternion::dotProduct(from, to) < 0)
        to = -to;
    auto finish = [this, to, endView, msg] {
        m_isoQ = to;
        if (endView != View::Iso) {
            setView(endView);
        } else {
            updateCamera();
            update();
        }
        if (!msg.isEmpty())
            emit message(msg);
    };
    if (QQuaternion::dotProduct(from, to) > 0.99999f) {
        finish();
    } else {
        auto *a = new QVariantAnimation(this);
        a->setStartValue(0.0);
        a->setEndValue(1.0);
        a->setDuration(280);
        a->setEasingCurve(QEasingCurve::InOutCubic);
        connect(a, &QVariantAnimation::valueChanged, this, [this, from, to](const QVariant &v) {
            m_isoQ = QQuaternion::slerp(from, to, float(v.toReal()));
            updateCamera();
            update();
        });
        connect(a, &QVariantAnimation::finished, this, finish);
        m_viewAnim = a;
        a->start(QAbstractAnimation::DeleteWhenStopped);
    }
    emit viewChanged();
    emitSnapState();
}

void Canvas::goHome(bool animate)
{
    const QQuaternion home = m_hasHome ? m_homeQ
                                       : camFromDirUp(m_frame.rotatedVector(QVector3D(1, 1, 1)), frameUp(), m_isoQ);
    if (animate) {
        animateTo(home, m_hasHome ? "Viewport: HOME (your saved view)" : "Viewport: HOME (ISO 3D)");
        return;
    }
    if (m_viewAnim)
        m_viewAnim->stop();
    m_isoQ = home;
    m_view = View::Iso;
    updateCamera();
    emit viewChanged();
    emitSnapState();
    update();
}

void Canvas::resetIsoView()
{
    goHome(false);
    zoomFit();
    emit message("Viewport: ISO 3D (reset)");
}

void Canvas::setCurrentViewAsHome()
{
    m_homeQ = currentCamQ();
    m_hasHome = true;
    emit message("Home view saved: the house button and Reset ISO now return here");
}

void Canvas::resetHome()
{
    if (!m_hasHome)
        return;
    m_hasHome = false;
    emit message("Home view reset to the standard ISO corner");
}

// the camera you are looking through right now becomes the named view v
void Canvas::setCurrentViewAs(View v)
{
    if (v == View::Iso)
        return;
    if (m_viewAnim)
        m_viewAnim->stop();
    const QQuaternion cur = currentCamQ();
    m_frame = (cur * namedViewQ(v, false).conjugated()).normalized();
    m_view = v;
    updateCamera();
    syncPlaneToView();
    emit message(QString("Current view is now %1 - the view cube and all named views follow. "
                         "View > Reset view orientation undoes it.").arg(viewName(v)));
    emit viewChanged();
    emitSnapState();
    update();
}

void Canvas::resetViewOrientation()
{
    if (m_viewAnim)
        m_viewAnim->stop();
    m_frame = QQuaternion();
    updateCamera();
    syncPlaneToView();
    emit message("View orientation reset: Top / Front / ... are the world axes again");
    emit viewChanged();
    emitSnapState();
    update();
}

void Canvas::rollView(qreal degrees)
{
    // degrees > 0 turns the picture counter-clockwise (the camera turns the other way)
    const QQuaternion q = QQuaternion::fromAxisAndAngle(-m_bf, float(-degrees)) * currentCamQ();
    animateTo(q.normalized(), QString("Viewport: rolled %1%2\xC2\xB0").arg(degrees > 0 ? "+" : "").arg(degrees));
}

void Canvas::setViewDirection(const QVector3D &cubeDir, bool animate)
{
    if (cubeDir.isNull())
        return;
    QStringList parts;               // e.g. "TOP FRONT RIGHT" (view-cube names)
    if (cubeDir.z() > 0) parts << "TOP";
    if (cubeDir.z() < 0) parts << "BOTTOM";
    if (cubeDir.y() < 0) parts << "FRONT";
    if (cubeDir.y() > 0) parts << "BACK";
    if (cubeDir.x() < 0) parts << "LEFT";
    if (cubeDir.x() > 0) parts << "RIGHT";
    const QQuaternion target = camFromDirUp(m_frame.rotatedVector(cubeDir), frameUp(), currentCamQ());
    const QString msg = "Viewport: " + parts.join(' ');
    if (animate) {
        animateTo(target, msg);
    } else {
        if (m_viewAnim)
            m_viewAnim->stop();
        m_isoQ = target;
        m_view = View::Iso;
        updateCamera();
        emit message(msg);
        emit viewChanged();
        emitSnapState();
        update();
    }
}

void Canvas::lookAtPlane()
{
    const Frame fr = planeFrame();
    if (!customOrientation()) {
        switch (m_plane) {
        case Plane::XZ: setView(View::Front); break;
        case Plane::ZY: setView(View::Side); break;
        default:        setView(View::Top); break;
        }
        return;
    }
    // user orientation: use a named view that looks along the plane normal, else look straight at it
    for (View v : {View::Top, View::Front, View::Side, View::Bottom, View::Back, View::Right}) {
        QVector3D r, u;
        namedBasis(v, r, u, true);
        if (std::fabs(QVector3D::dotProduct(QVector3D::crossProduct(r, u), fr.N)) > 0.999f) {
            setView(v);
            return;
        }
    }
    const QVector3D eye = QVector3D::dotProduct(m_bf, fr.N) > 0 ? -fr.N : fr.N;
    animateTo(camFromDirUp(eye, fr.V, currentCamQ()), "Viewport: looking straight at the sketch plane");
}

void Canvas::setSettings(const ViewSettings &s)
{
    m_set = s;
    Units::setOptions({s.units, s.unitDecimals, s.unitFrac});
    updateCamera();
    emit settingsChanged();
    emitSnapState();
    update();
}

void Canvas::setGridSize(double mm)
{
    m_gridSize = std::max(0.1, mm);
    update();
}

void Canvas::emitSnapState()
{
    QStringList l;
    if (m_set.snapGrid)   l << "GRID";
    if (m_set.snapObject) l << "OSNAP";
    if (m_set.ortho)      l << "ORTHO";
    if (m_set.polar)      l << QString("POLAR %1\u00B0").arg(m_set.polarStep, 0, 'g', 4);
    l << (perspectiveActive() ? "PERSP" : "ORTHO-CAM");
    if (m_axisLock)       l << QString("LOCK %1").arg(QChar("XYZ"[m_axisLock - 1]));
    emit snapStateChanged(l.join("  "));
}

void Canvas::emitHint()
{
    QString h;
    const int n = m_pending.size();
    switch (m_tool) {
    case Tool::Select:
        if (m_gzActive >= 0)
            h = m_gizmoMode == GizmoMode::Rotate ? QString("Gizmo: move round the ring (Shift = 15° steps) and click  |  type an angle + Enter  |  Esc cancels")
                                                 : QString("Gizmo: move and click - snaps the selection's side onto corners  |  type a distance + Enter  |  Esc cancels");
        else if (gizmoVisible())
            h = m_gizmoMode == GizmoMode::Rotate ? QString("Gizmo: drag a ring to turn the selection about X / Y / Z  |  click a board's face to select it")
                                                 : QString("Gizmo: drag an arrow to move along X / Y / Z, a square to move in that plane  |  Ctrl+click adds to the selection");
        else
            h = "Click to select  |  drag to move  |  drag a box to multi-select (left-to-right = inside, right-to-left = touching)";
        break;
    case Tool::Line:      h = n ? "Line: click the end point  |  type a length + Enter  |  X/Y/Z lock axis" : "Line: click the start point"; break;
    case Tool::Curve3:    h = n == 0 ? "Curve: click the start point" : (n == 1 ? "Curve: click a point it passes through" : "Curve: click the end point"); break;
    case Tool::Arc:       h = n == 0 ? "Arc: click the start point" : (n == 1 ? "Arc: click a point on the arc" : "Arc: click the end point"); break;
    case Tool::Circle:    h = n ? "Circle: click a point on the rim  |  type a radius + Enter" : "Circle: click the centre"; break;
    case Tool::Polyline:  h = n ? "Polyline: click next point  |  Enter / double-click to finish  |  X/Y/Z lock axis" : "Polyline: click the first point"; break;
    case Tool::Rect:      h = n ? "Rectangle: click the opposite corner  |  type W,H + Enter" : "Rectangle: click the first corner"; break;
    case Tool::Dimension: h = n == 0 ? "Dimension: click the first point" : (n == 1 ? "Dimension: click the second point" : "Dimension: click to place the dimension line"); break;
    case Tool::Text:      h = "Text: click where the text goes"; break;
    case Tool::InsertImage: h = "Image: click where the image goes"; break;
    case Tool::Trim:      h = QString("Trim [%1]: click a part to remove  |  DRAG across many parts (fence)  |  Shift = Extend  |  cuts every vector type")
                                  .arg(m_cutMode == 0 ? "View/virtual" : "True 3D"); break;
    case Tool::Cutter:
        if (n == 0) h = "Cutter: click the first point of the blade (draws on the sketch plane)";
        else if (n == 1) h = QString("Cutter: click the blade end - %1").arg(m_bladeAction == 0 ? "every crossed shape is split there" : "then click the side to cut away");
        else h = "Cutter: click on the side you want to REMOVE (red preview)";
        break;
    case Tool::Extend:    h = "Extend: click near the end of a line/polyline to run it to the next edge  |  hold Shift to Trim instead"; break;
    case Tool::Mirror:
        if (m_mirrorStage == 0) h = "Mirror: click the objects to mirror, then press Enter";
        else if (n == 0) h = QString("Mirror: point at the mirror point (snaps: Body centre, Face centre, corners...) and press X / Y / Z for the plane square to that axis  |  or click two points of a mirror line  |  K: keep original = %1").arg(m_mirrorKeep ? "yes" : "no");
        else h = QString("Mirror: X / Y / Z = the plane through the picked point square to that axis  |  or pick the second point of the mirror line  |  K: keep original = %1").arg(m_mirrorKeep ? "yes" : "no");
        break;
    case Tool::Join:      h = m_joinFirst ? "Join: click the second line" : "Join: click the first line"; break;
    case Tool::Break:     h = "Break: click a line where it should be split"; break;
    case Tool::Offset: {
        const QString dist = m_offsetThrough ? QString("THROUGH the cursor point")
                           : !m_offsetTyped.isEmpty() ? QString("distance: %1_  (Enter = accept)").arg(m_offsetTyped)
                           : Units::format(m_offsetDist);
        if (!offsetSourceValid())
            h = QString("Offset [%1]: click the object to offset  |  type a distance + Enter  |  T = through point%2")
                    .arg(dist, m_lastOffsetMade && m_items.contains(m_lastOffsetMade) ? "  |  Enter = offset the new copy again" : "");
        else
            h = QString("Offset [%1]: move to the side you want and click  |  type a distance + Enter  |  T = through point  |  Esc = other object")
                    .arg(dist);
        break;
    }
    case Tool::Edit:      h = m_editItem ? "Shape edit: drag a square handle" : "Shape edit: click an entity"; break;
    case Tool::FreeMove:  h = "Move: drag an entity  |  arrow keys nudge  |  X/Y/Z lock axis while dragging"; break;
    case Tool::Move:
        if (m_moveStage == 0) h = "Move: click the objects (or drag a box), then press Enter";
        else if (n == 0) h = QString("Move: pick the BASE point  |  K: copy = %1").arg(m_moveCopy ? "on" : "off");
        else h = QString("Move: pick the target point (3D snaps) - over a face parallel to the base face it goes UP TO that face  |  or type distance, Tab, angle, Enter  |  K: copy = %1")
                     .arg(m_moveCopy ? "on" : "off");
        break;
    case Tool::DynMove:   h = "Dynamic move: drag - locks to the dominant axis and shows the distance"; break;
    case Tool::Box:
        if (n == 0) h = "Box: click the first corner - start on a board's face and the box grows out of that face (it lights up blue)";
        else if (n == 1) h = "Box: click the opposite corner  |  or type length, Tab, width + Enter";
        else h = QString("Box: height - move the mouse (snaps to corners) and click, or type + Enter  |  Enter alone = %1 board")
                     .arg(Units::format(m_boxHeight));
        break;
    case Tool::Rotate: {
        static const char *an[] = {"X", "Y", "Z", "the plane normal"};
        if (m_r3Stage == 0) h = "Rotate: click the objects (or drag a box), then press Enter";
        else if (m_r3Stage == 1) h = QString("Rotate: pick the BASE point (the pivot)  |  K: copy = %1").arg(m_moveCopy ? "on" : "off");
        else h = QString("Rotate about %1: move the mouse and click, or type an angle + Enter  |  X / Y / Z = turn about that axis  |  "
                         "Shift 15°, F8 90°  |  K: copy = %2").arg(an[std::clamp(m_r3Axis, 0, 3)], m_moveCopy ? "on" : "off");
        break;
    }
    case Tool::PressPull:
        h = m_ppItem && m_ppFace.kind < 0
                ? QString("Extrude: move or snap, click  |  type a distance (800-2*18 works) + Enter  |  N new, J join, C cut, I intersect, A auto (now %1)  |  Esc")
                      .arg(extrudeOpName(m_ppOpNow))
            : m_ppItem ? QString("Press/Pull: move, or snap to a corner / over a parallel face (= up to that face), click  |  type a distance + Enter  |  Esc = put it back")
                     : m_ppSet.isEmpty()
                           ? QString("Press/Pull: click a face of a board (or inside a closed shape) to pull or push it  |  Shift+click several faces to pull them together")
                           : QString("Press/Pull: %1 face(s) chosen - Shift+click more (again = take off), then click a face and pull: all move").arg(m_ppSet.size());
        break;
    case Tool::Revolve:
        h = m_rvStage == 0 ? QString("Revolve %1\xC2\xB0: click the closed shape to turn (the profile)  |  type an angle + Enter").arg(m_rvAngle)
            : m_rvStage == 1 ? QString("Revolve: click the axis' FIRST point (it lies in the profile's plane, e.g. a line's end)")
                             : QString("Revolve %1\xC2\xB0: click the axis' SECOND point  |  type another angle + Enter first").arg(m_rvAngle);
        break;
    case Tool::Pipe:
        h = QString("Pipe %1, bends R %2: click a line, polyline, arc or curve (the centre line)  |  type a diameter + Enter  |  wall / bend: Model > Pipe settings")
                .arg(pipeLabel(m_pipeD, m_pipeWall), Units::format(m_pipeBend > 0 ? m_pipeBend : 1.5 * m_pipeD));
        break;
    case Tool::Shell:
        h = QString("Shell %1: click the face to leave OPEN (a cabinet's front, a drawer's top) - a box becomes separate boards  |  type a wall thickness + Enter  |  Esc")
                .arg(Units::format(m_shellT));
        break;
    case Tool::Fillet:
    case Tool::Chamfer:
        h = QString("%1 %2: point at a corner of a shape or an edge of a board (it lights up) and click  |  type a %3 + Enter  |  Esc = done")
                .arg(m_tool == Tool::Fillet ? "Fillet R" : "Chamfer", Units::format(m_tool == Tool::Fillet ? m_filletR : m_chamferD),
                     m_tool == Tool::Fillet ? "radius" : "distance");
        break;
    case Tool::Hatch:
        h = QString("Hatch / Fill [%1]: click inside any closed boundary on the %2 plane (lines, arcs, circles... may cross; islands stay open)  |  Esc = done")
                .arg(hatchPatternNames().value(m_hatchStyle.pattern), planeName(m_plane));
        break;
    }
    if (dynTyping())
        h = "Typing a size  -  Tab: next field  |  Enter: accept  |  Backspace: edit  |  Esc: clear";
    else if (dynActive())
        h += "  |  type a size, Tab = next field";
    if (m_tool == Tool::Dimension)
        h += QString("  |  mode: %1 (H horizontal, V vertical, K aligned)")
                 .arg(m_dimMode == 1 ? "horizontal" : (m_dimMode == 2 ? "vertical" : "aligned"));
    emit hint(h);
}

// ---------------------------------------------------------------------------
// camera / projection
// ---------------------------------------------------------------------------
void Canvas::viewBasis(QVector3D &r, QVector3D &u, QVector3D &f) const
{
    if (m_view == View::Iso) {
        r = m_isoQ.rotatedVector(QVector3D(1, 0, 0)).normalized();
        u = m_isoQ.rotatedVector(QVector3D(0, 1, 0)).normalized();
    } else {
        namedBasis(m_view, r, u, true);
    }
    f = -QVector3D::crossProduct(r, u);
}

void Canvas::updateCamera()
{
    viewBasis(m_br, m_bu, m_bf);
}

qreal Canvas::focalPx() const
{
    const qreal fov = std::clamp(m_set.fov, 10.0, 120.0) * PI / 180.0;
    return std::max(100, height()) * 0.5 / std::tan(fov / 2.0) * m_focalMul;
}

QVector3D Canvas::eyePos() const
{
    return m_target - m_bf * float(focalPx() / m_zoom);
}

qreal Canvas::nearDist() const
{
    return focalPx() / m_zoom * 0.03;
}

bool Canvas::projectD(const QVector3D &p, QPointF &out, qreal *depth) const
{
    const QPointF c = viewCenter();
    if (perspectiveActive()) {
        const QVector3D v = p - eyePos();
        const qreal d = QVector3D::dotProduct(v, m_bf);
        if (d < nearDist())
            return false;
        const qreal k = focalPx() / d;
        out = QPointF(c.x() + QVector3D::dotProduct(v, m_br) * k,
                      c.y() - QVector3D::dotProduct(v, m_bu) * k);
        if (depth)
            *depth = d;
        return true;
    }
    const QVector3D v = p - m_target;
    out = QPointF(c.x() + QVector3D::dotProduct(v, m_br) * m_zoom,
                  c.y() - QVector3D::dotProduct(v, m_bu) * m_zoom);
    if (depth)
        *depth = QVector3D::dotProduct(v, m_bf);
    return true;
}

QPointF Canvas::project(const QVector3D &p) const
{
    QPointF o(-1e6, -1e6);
    projectD(p, o);
    return o;
}

bool Canvas::projectSeg(const QVector3D &a, const QVector3D &b, QLineF &out) const
{
    QPointF pa, pb;
    if (!perspectiveActive()) {
        projectD(a, pa);
        projectD(b, pb);
        out = QLineF(pa, pb);
        return true;
    }
    const QVector3D eye = eyePos();
    const qreal near = nearDist();
    const qreal da = QVector3D::dotProduct(a - eye, m_bf);
    const qreal db = QVector3D::dotProduct(b - eye, m_bf);
    if (da < near && db < near)
        return false;
    QVector3D A = a, B = b;
    if (da < near)
        A = a + (b - a) * float((near - da) / (db - da));
    else if (db < near)
        B = a + (b - a) * float((near - da) / (db - da));
    if (!projectD(A, pa) || !projectD(B, pb))
        return false;
    out = QLineF(pa, pb);
    return true;
}

qreal Canvas::scaleAt(const QVector3D &p) const
{
    if (!perspectiveActive())
        return m_zoom;
    const qreal d = QVector3D::dotProduct(p - eyePos(), m_bf);
    return d > 1e-6 ? focalPx() / d : m_zoom;
}

void Canvas::screenRay(const QPointF &s, QVector3D &ro, QVector3D &rd) const
{
    const QPointF c = viewCenter();
    if (perspectiveActive()) {
        ro = eyePos();
        rd = (m_bf * float(focalPx()) + m_br * float(s.x() - c.x()) -
              m_bu * float(s.y() - c.y())).normalized();
    } else {
        const qreal dx = (s.x() - c.x()) / m_zoom, dy = (s.y() - c.y()) / m_zoom;
        ro = m_target + m_br * float(dx) - m_bu * float(dy);
        rd = m_bf;
    }
}

bool Canvas::rayPlane(const QVector3D &ro, const QVector3D &rd, const QVector3D &p0,
                      const QVector3D &n, QVector3D &out)
{
    const qreal dn = QVector3D::dotProduct(rd, n);
    if (std::fabs(dn) < 0.08)
        return false;
    const qreal t = QVector3D::dotProduct(p0 - ro, n) / dn;
    out = ro + rd * float(t);
    return true;
}

// ---------------------------------------------------------------------------
// planes
// ---------------------------------------------------------------------------
Canvas::Frame Canvas::frameFor(int plane)
{
    Frame f;
    switch (plane) {
    case 1:  f.U = QVector3D(1, 0, 0); f.V = QVector3D(0, 0, 1); break;
    case 2:  f.U = QVector3D(0, 0, 1); f.V = QVector3D(0, 1, 0); break;
    case 3:  f.U = QVector3D(0, 1, 0); f.V = QVector3D(1, 0, 0); break;
    default: f.U = QVector3D(1, 0, 0); f.V = QVector3D(0, 1, 0); break;
    }
    f.N = QVector3D::crossProduct(f.U, f.V);
    if (f.N.x() + f.N.y() + f.N.z() < 0)
        f.N = -f.N;               // always point along the positive world axis
    return f;
}

QVector3D Canvas::planeOrigin() const
{
    return planeFrame().N * float(m_planeOffset);
}

// ---------------------------------------------------------------------------
// snapping
// ---------------------------------------------------------------------------
bool Canvas::drawingTool() const
{
    switch (m_tool) {
    case Tool::Line: case Tool::Curve3: case Tool::Arc: case Tool::Circle:
    case Tool::Polyline: case Tool::Rect: case Tool::Dimension:
        return true;
    case Tool::Mirror:
        return m_mirrorStage >= 1;
    case Tool::Move:
        return m_moveStage >= 1;
    case Tool::Cutter:
        return true;
    case Tool::Box:
        return m_pending.size() < 2;
    case Tool::Rotate:
        return m_r3Stage == 1;
    case Tool::Revolve:
        return m_rvStage >= 1;
    default:
        return m_pickCb != nullptr;
    }
}

bool Canvas::lockAllowed() const
{
    return m_tool == Tool::Line || m_tool == Tool::Polyline || m_tool == Tool::Curve3 ||
           m_tool == Tool::Arc || m_tool == Tool::Dimension ||
           m_tool == Tool::Move;
}

QVector3D Canvas::lockAnchor() const
{
    if (m_dragging)
        return m_dragStart;
    return m_pending.isEmpty() ? QVector3D() : m_pending.last();
}

void Canvas::collectSnapPoints(QList<SnapCand> &out) const
{
    out.append({QVector3D(0, 0, 0), 4});
    for (int i = 0; i + 1 < m_pending.size(); ++i)   // allow closing a polyline on its start
        out.append({m_pending[i], 2});

    for (const SketchItem *it : m_items) {
        if (it->hidden || m_dragData.contains(const_cast<SketchItem *>(it)))
            continue;
        allSnapPoints(it, out);                       // bodies: corners, edge middles, face and body centres
    }
}

void Canvas::itemSnapPoints(const SketchItem *it, QList<SnapCand> &out) const
{
        const QList<QVector3D> &p = it->pts;
        switch (it->type()) {
        case SketchItem::Line:
        case SketchItem::Polyline:
            if (!it->loops.isEmpty()) {
                for (const auto &s : stripsBase(it))
                    for (int i = 0; i + 1 < s.size(); ++i) {
                        out.append({s[i], 2});
                        out.append({(s[i] + s[i + 1]) * 0.5f, 3});
                    }
                break;
            }
            for (int i = 0; i < p.size(); ++i) {
                out.append({p[i], 2});
                if (i + 1 < p.size())
                    out.append({(p[i] + p[i + 1]) * 0.5f, 3});
            }
            break;
        case SketchItem::Rect: {
            const QVector<QVector3D> c = rectCorners(it);
            if (c.size() == 4) {
                for (int i = 0; i < 4; ++i) {
                    out.append({c[i], 2});
                    out.append({(c[i] + c[(i + 1) % 4]) * 0.5f, 3});
                }
                out.append({(c[0] + c[2]) * 0.5f, 4});
            }
            break;
        }
        case SketchItem::Circle:
            if (p.size() >= 2) {
                const Frame fr = frameFor(std::max(0, it->plane));
                const float r = (p[1] - p[0]).length();
                out.append({p[0], 4});
                out.append({p[0] + fr.U * r, 2});
                out.append({p[0] - fr.U * r, 2});
                out.append({p[0] + fr.V * r, 2});
                out.append({p[0] - fr.V * r, 2});
            }
            break;
        case SketchItem::Arc:
            if (p.size() >= 3) {
                out.append({p[0], 2});
                out.append({p[2], 2});
                QVector3D cen, e1, e2;
                qreal rad, a0, sw;
                if (arcData(p[0], p[1], p[2], cen, rad, a0, sw, e1, e2))
                    out.append({cen, 4});
            }
            break;
        case SketchItem::Curve:
            if (p.size() >= 3) {
                out.append({p[0], 2});
                out.append({p[2], 2});
            }
            break;
        case SketchItem::Dimension:
            for (int i = 0; i < std::min<int>(2, p.size()); ++i)
                out.append({p[i], 2});
            break;
        case SketchItem::Hatch:                       // its boundary is other objects' geometry
            break;
        default:
            if (!p.isEmpty())
                out.append({p[0], 2});
            break;
        }
}

Canvas::Snap Canvas::snapCursor(const QPoint &screen, const QVector3D *planeThrough)
{
    Snap s;
    m_guides.clear();
    m_trackOn = false;
    QVector3D ro, rd;
    screenRay(screen, ro, rd);
    const bool drawing = drawingTool() || m_dragging;
    const Frame fr = planeFrame();
    const qreal g = m_gridSize;

    // ---- axis lock: slide along a world axis through the anchor point ----
    const bool lockActive = m_axisLock && ((drawingTool() && lockAllowed() && !m_pending.isEmpty()) ||
                                           m_dragging);
    if (lockActive) {
        const QVector3D axis = m_axisLock == 1 ? QVector3D(1, 0, 0)
                               : (m_axisLock == 2 ? QVector3D(0, 1, 0) : QVector3D(0, 0, 1));
        const QVector3D anchor = lockAnchor();
        const QVector3D w0 = anchor - ro;
        const qreal b = QVector3D::dotProduct(axis, rd);
        const qreal c = QVector3D::dotProduct(rd, rd);
        const qreal d = QVector3D::dotProduct(axis, w0);
        const qreal e = QVector3D::dotProduct(rd, w0);
        const qreal den = c - b * b;
        if (std::fabs(den) < 1e-4)
            return s;                       // axis points straight at the camera
        qreal t = (b * e - c * d) / den;
        if (m_set.snapGrid)
            t = std::round(t / g) * g;
        s.pt = anchor + axis * float(t);
        s.kind = m_set.snapGrid ? 1 : 0;
        s.valid = true;
        const float far = float(std::max(5000.0, m_set.gridExtent * 3));
        m_guides.append({anchor - axis * far, anchor + axis * far});
        return s;
    }

    // ---- ray-cast onto the sketch plane ----
    // A shape lives in ONE plane: once its first point is picked (possibly snapped to an object on
    // another plane/depth), the rest of the shape is drawn in the parallel plane through that point.
    const bool shapeTool = m_tool == Tool::Line || m_tool == Tool::Curve3 || m_tool == Tool::Arc ||
                           m_tool == Tool::Circle || m_tool == Tool::Polyline || m_tool == Tool::Rect ||
                           m_tool == Tool::Dimension || m_tool == Tool::Box;
    const bool shapeStarted = shapeTool && !m_pending.isEmpty() && !m_dragging;
    const QVector3D p0 = planeThrough ? *planeThrough : (shapeStarted ? m_pending.first() : planeOrigin());
    QVector3D raw;
    if (!rayPlane(ro, rd, p0, fr.N, raw)) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if ((drawingTool() || m_tool == Tool::Text) && now - m_edgeMsgAt > 4000) {
            m_edgeMsgAt = now;
            emit message("The view is almost edge-on to the sketch plane (" + planeName(m_plane) +
                         ") - rotate the view or press Tab / pick another plane to draw");
        }
        return s;
    }
    if (perspectiveActive() && QVector3D::dotProduct(raw - ro, m_bf) < nearDist())
        return s;
    s.valid = true;

    // ---- tracking anchor (last picked point, or the drag start) ----
    QVector3D anc;
    bool hasAnc = false;
    // a rectangle's / box's second point is the diagonal corner: ortho or polar would flatten it to zero
    if (m_tool != Tool::Rect && m_tool != Tool::Box) {
        if (drawingTool() && !m_pending.isEmpty() && !m_dragging) { anc = m_pending.last(); hasAnc = true; }
        else if (m_dragging && (m_tool == Tool::Select || m_tool == Tool::FreeMove)) { anc = m_dragStart; hasAnc = true; }
    }

    // First pick: snap to the REAL 3D point of any object, whatever plane it is on.
    // Later picks of a shape are projected into the shape's plane (unless 3D snap is on for lines),
    // so the shape never comes out slanted.
    const bool planeBound = shapeStarted && (!m_set.snap3D || m_tool == Tool::Rect || m_tool == Tool::Circle);
    const qreal nPlane = QVector3D::dotProduct(p0, fr.N);
    auto offPlane = [&](const QVector3D &q) { return std::fabs(QVector3D::dotProduct(q, fr.N) - nPlane) > 0.05; };

    // ---- object snaps: endpoint / midpoint / centre / intersection / perpendicular ----
    if (m_set.snapObject && drawing) {
        QList<SnapCand> cands;
        collectSnapPoints(cands);
        qreal best = 1e9, bestDepth = 1e300;
        QVector3D bp;
        int bk = -1;
        bool bOff = false;
        const QPointF cur(screen);
        for (const SnapCand &c : std::as_const(cands)) {
            if (!osnapOn(c.kind))
                continue;                              // that kind is switched off (Object snap settings)
            QPointF sp;
            qreal depth = 0;
            if (!projectD(c.p, sp, &depth))
                continue;
            // plane-bound shapes snap to the point's projection onto the sketch plane
            const bool off = offPlane(c.p);
            QVector3D cp = c.p;
            if (off && planeBound) {
                cp = c.p - fr.N * float(QVector3D::dotProduct(c.p, fr.N) - nPlane);
                if (!projectD(cp, sp))
                    continue;
            }
            const qreal d = QLineF(sp, cur).length();
            const qreal score = d - (c.kind == 2 ? 4.0 : (c.kind == 4 ? 2.0 : (c.kind == 11 ? 1.5 : (c.kind == 10 ? 1.0 : 0.0)))) +
                                (off && planeBound ? 3.0 : 0.0);
            // two points on top of each other on screen (a face centre in front of the body centre): the nearer wins
            const bool better = score < best - 0.5 || (score < best + 0.5 && depth < bestDepth);
            if (d < 12.0 && better) { best = score; bestDepth = depth; bp = cp; bk = c.kind; bOff = off; }
        }
        // segments close to the cursor (screen space) feed intersection + perpendicular
        struct Seg { QVector3D a, b; const SketchItem *o; int k; };
        QList<Seg> nearSegs;
        for (const SketchItem *o : std::as_const(m_items)) {
            if (o->hidden || o->type() == SketchItem::Dimension || o->type() == SketchItem::Text ||
                o->type() == SketchItem::ImageItem || o->type() == SketchItem::Hatch ||
                m_dragData.contains(const_cast<SketchItem *>(o)))
                continue;
            for (const auto &st : strips(o))
                for (int k = 0; k + 1 < st.size() && nearSegs.size() < 80; ++k) {
                    QLineF l;
                    if (projectSeg(st[k], st[k + 1], l) && distToSegment(cur, l) < 40.0)
                        nearSegs.append({st[k], st[k + 1], o, k});
                }
        }
        for (int i = 0; i < nearSegs.size() && osnapOn(6); ++i)
            for (int j = i + 1; j < nearSegs.size(); ++j) {
                if (nearSegs[i].o == nearSegs[j].o &&
                    (nearSegs[i].o->type() != SketchItem::Polyline || std::abs(nearSegs[i].k - nearSegs[j].k) <= 1))
                    continue;                                  // a shape's own corners are not "intersections"
                qreal sA, tB;
                if (segSegClosest(nearSegs[i].a, nearSegs[i].b, nearSegs[j].a, nearSegs[j].b, sA, tB) > 0.02)
                    continue;
                const QVector3D q = nearSegs[i].a + (nearSegs[i].b - nearSegs[i].a) * float(sA);
                QPointF sp;
                if (!projectD(q, sp))
                    continue;
                if (planeBound && offPlane(q))
                    continue;
                const qreal d = QLineF(sp, cur).length() - 3.0 + (planeBound && offPlane(q) ? 6.0 : 0.0);
                if (d < 9.0 && d < best) { best = d; bp = q; bk = 6; }
            }
        if (hasAnc && !m_dragging && osnapOn(7) && (lockAllowed() || m_tool == Tool::Cutter || m_tool == Tool::Mirror))
            for (const Seg &sg : std::as_const(nearSegs)) {
                const QVector3D ab = sg.b - sg.a;
                const qreal l2 = QVector3D::dotProduct(ab, ab);
                if (l2 < 1e-9)
                    continue;
                const qreal t = QVector3D::dotProduct(anc - sg.a, ab) / l2;
                if (t < 0.002 || t > 0.998)
                    continue;
                const QVector3D foot = sg.a + ab * float(t);
                if ((foot - anc).length() < 1e-3f)
                    continue;
                QPointF sp;
                if (!projectD(foot, sp))
                    continue;
                if (planeBound && offPlane(foot))
                    continue;
                const qreal d = QLineF(sp, cur).length() - 2.0;
                if (d < 10.0 && d < best) { best = d; bp = foot; bk = 7; }
            }
        if (bk < 0 && osnapOn(9)) {                   // "Nearest": any point ON a line / edge
            for (const Seg &sg : std::as_const(nearSegs)) {
                qreal sA = -1, tB = 0;
                if (segSegClosest(sg.a, sg.b, ro, ro + rd * 100000.0f, sA, tB) > 1e8)
                    continue;
                if (sA < 0.0 || sA > 1.0)
                    continue;
                const QVector3D q = sg.a + (sg.b - sg.a) * float(sA);
                if (planeBound && offPlane(q))
                    continue;
                QPointF sp;
                if (!projectD(q, sp))
                    continue;
                const qreal d = QLineF(sp, cur).length() + 2.0;
                if (d < 8.0 && d < best) { best = d; bp = q; bk = 9; }
            }
        }
        if (bk >= 0) {
            s.pt = bp;
            s.kind = bk;
            if (bOff || offPlane(bp))
                s.note = planeBound ? "projected" : "3D point";
            return s;
        }
        // no point near: Move over a parallel face goes up to it; Move / Mirror / Rotate take the point on the face
        if (moveUpToFace(screen, s))
            return s;
        const bool pointTool3D = (m_tool == Tool::Move && m_moveStage >= 1) || (m_tool == Tool::Mirror && m_mirrorStage >= 1) ||
                                 (m_tool == Tool::Rotate && m_r3Stage == 1);
        if (pointTool3D && osnapOn(12)) {
            SolidFace f;
            QVector3D hit;
            const bool targetStage = m_tool == Tool::Move && !m_pending.isEmpty();
            if (faceAtIf(QPointF(screen), f, hit, [targetStage](const SketchItem *it) { return targetStage && it->selected; })) {
                s.pt = hit;
                s.kind = 12;
                s.note = offPlane(hit) ? "3D point" : QString();
                return s;
            }
        }
    }

    qreal u = QVector3D::dotProduct(raw, fr.U);
    qreal v = QVector3D::dotProduct(raw, fr.V);
    const qreal n0 = QVector3D::dotProduct(p0, fr.N);

    // ---- keep new geometry on the visible sheet ----
    if (m_set.limitSheet && !planeThrough && drawing) {
        const qreal ext = m_set.gridExtent;
        bool clamped = false;
        if (std::fabs(u) > ext) { u = std::copysign(ext, u); clamped = true; }
        if (std::fabs(v) > ext) { v = std::copysign(ext, v); clamped = true; }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (clamped && now - m_limitMsgAt > 5000) {
            m_limitMsgAt = now;
            emit message("The cursor stops at the edge of the sketch sheet. Raise 'Grid half-size' in the "
                         "Viewport panel (or switch off View > Limit drawing to the sheet) to draw further out.");
        }
    }

    // ---- ortho / polar tracking from the anchor ----
    bool locked = false;
    QString note;
    const bool shift = !m_dragging && (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
    const bool orthoOn = m_dragging ? m_set.ortho : (m_set.ortho != shift);
    bool gu = false, gv = false;
    if (hasAnc && (orthoOn || m_set.polar)) {
        const qreal au = QVector3D::dotProduct(anc, fr.U), av = QVector3D::dotProduct(anc, fr.V);
        const qreal du = u - au, dv = v - av, len = std::hypot(du, dv);
        const qreal px = std::max(1e-6, scaleAt(raw));
        if (len > 1e-9) {
            const qreal ang = std::atan2(dv, du) * 180.0 / PI;
            const qreal step = orthoOn ? 90.0 : std::max(1.0, m_set.polarStep);
            const qreal a = std::round(ang / step) * step;
            const qreal diff = (ang - a) * PI / 180.0;
            if (orthoOn || len * std::fabs(std::sin(diff)) * px < 10.0) {
                locked = true;
                const qreal ca = std::cos(a * PI / 180.0), sa = std::sin(a * PI / 180.0);
                const QVector3D dir = fr.U * float(ca) + fr.V * float(sa);
                qreal L = std::max(0.0, len * std::cos(diff));
                note = QString("%1 %2\u00B0").arg(orthoOn ? "ORTHO" : "POLAR").arg(norm360(a), 0, 'f', a == std::round(a) ? 0 : 1);
                bool aligned = false;
                if (m_set.snapObject) {
                    QList<SnapCand> cands;
                    collectSnapPoints(cands);
                    qreal bestD = 9.0;
                    for (const SnapCand &c : std::as_const(cands)) {
                        for (int axis = 0; axis < 2; ++axis) {
                            const qreal comp = axis == 0 ? ca : sa;
                            if (std::fabs(comp) < 0.2)
                                continue;
                            const qreal pc = QVector3D::dotProduct(c.p, axis == 0 ? fr.U : fr.V);
                            const qreal t = (pc - (axis == 0 ? au : av)) / comp;
                            if (t < 1e-6)
                                continue;
                            QPointF sp;
                            if (!projectD(anc + dir * float(t), sp))
                                continue;
                            const qreal d = QLineF(sp, QPointF(screen)).length();
                            if (d < bestD) {
                                bestD = d; L = t; aligned = true;
                                m_guides.clear();
                                m_guides.append({c.p, anc + dir * float(t)});
                            }
                        }
                    }
                }
                if (aligned)
                    note += "  + ALIGN";
                else if (m_set.snapGrid)
                    L = std::round(L / g) * g;
                u = au + L * ca;
                v = av + L * sa;
                const float far = float(std::max(4000.0, m_set.gridExtent * 3));
                m_trackLine = {anc - dir * far, anc + dir * far};
                m_trackOn = true;
            }
        }
    }

    // ---- vector alignment with existing points ----
    if (!locked && m_set.snapObject && drawing) {
        QList<SnapCand> cands;
        collectSnapPoints(cands);
        const qreal tol = 7.0 / std::max(1e-6, scaleAt(raw));
        const qreal u0 = u, v0 = v;
        for (const SnapCand &c : std::as_const(cands)) {
            if (c.kind == 3)
                continue;
            const qreal pu = QVector3D::dotProduct(c.p, fr.U);
            const qreal pv = QVector3D::dotProduct(c.p, fr.V);
            if (!gu && std::fabs(u0 - pu) < tol) { u = pu; gu = true; }
            if (!gv && std::fabs(v0 - pv) < tol) { v = pv; gv = true; }
            if (gu && gv)
                break;
        }
        auto P = [&](qreal uu, qreal vv) { return fr.U * float(uu) + fr.V * float(vv) + fr.N * float(n0); };
        const qreal far = std::max(4000.0, m_set.gridExtent * 3);
        if (gu) m_guides.append({P(u, v - far), P(u, v + far)});
        if (gv) m_guides.append({P(u - far, v), P(u + far, v)});
        if (gu || gv)
            note = gu && gv ? "ALIGN  U + V" : "ALIGN";
    }

    if (!locked && m_set.snapGrid) {
        if (!gu) u = std::round(u / g) * g;
        if (!gv) v = std::round(v / g) * g;
    }

    s.pt = fr.U * float(u) + fr.V * float(v) + fr.N * float(n0);
    s.kind = locked ? 8 : ((gu || gv) ? 5 : (m_set.snapGrid ? 1 : 0));
    s.note = note;
    return s;
}

QVector3D Canvas::previewPoint() const
{
    return numericPoint(m_cur.pt);
}

// ---------------------------------------------------------------------------
// entity geometry (everything is computed in true 3D, then projected)
// ---------------------------------------------------------------------------
bool Canvas::arcData(const QVector3D &a, const QVector3D &b, const QVector3D &c,
                     QVector3D &cen, qreal &rad, qreal &a0, qreal &sweep, QVector3D &e1,
                     QVector3D &e2) const
{
    const QVector3D ab = b - a, ac = c - a;
    const QVector3D n = QVector3D::crossProduct(ab, ac);
    const qreal n2 = QVector3D::dotProduct(n, n);
    if (n2 < 1e-10)
        return false;
    const QVector3D t1 = QVector3D::crossProduct(n, ab) * float(QVector3D::dotProduct(ac, ac));
    const QVector3D t2 = QVector3D::crossProduct(ac, n) * float(QVector3D::dotProduct(ab, ab));
    cen = a + (t1 + t2) / float(2.0 * n2);
    rad = (a - cen).length();
    e1 = (a - cen).normalized();
    e2 = QVector3D::crossProduct(n.normalized(), e1);
    const qreal angB = std::atan2(QVector3D::dotProduct(b - cen, e2), QVector3D::dotProduct(b - cen, e1));
    const qreal angC = std::atan2(QVector3D::dotProduct(c - cen, e2), QVector3D::dotProduct(c - cen, e1));
    auto n2pi = [](qreal x) { x = std::fmod(x, 2 * PI); return x < 0 ? x + 2 * PI : x; };
    const qreal nb = n2pi(angB), nc = n2pi(angC);
    sweep = nc;
    if (nb > nc)
        sweep = nc - 2 * PI;
    a0 = 0;
    return true;
}

QVector<QVector3D> Canvas::rectCorners(const SketchItem *it) const
{
    QVector<QVector3D> c;
    if (it->pts.size() < 2)
        return c;
    const Frame fr = frameFor(std::max(0, it->plane));
    const QVector3D p0 = it->pts[0];
    const QVector3D d = it->pts[1] - p0;
    const float du = QVector3D::dotProduct(d, fr.U), dv = QVector3D::dotProduct(d, fr.V);
    c << p0 << p0 + fr.U * du << p0 + fr.U * du + fr.V * dv << p0 + fr.V * dv;
    return c;
}

QVector<QVector3D> Canvas::imageCorners(const SketchItem *it) const
{
    QVector<QVector3D> c;
    if (it->pts.isEmpty())
        return c;
    const Frame fr = frameFor(std::max(0, it->plane));
    const QVector3D ctr = it->pts[0];
    const QVector3D hu = fr.U * float(it->imageSizeMm.width() / 2.0);
    const QVector3D hv = fr.V * float(it->imageSizeMm.height() / 2.0);
    c << ctr - hu + hv << ctr + hu + hv << ctr + hu - hv << ctr - hu - hv;   // TL TR BR BL
    return c;
}

// Solid bodies: the profile, the swept copy and the side edges.
QList<QVector<QVector3D>> Canvas::strips(const SketchItem *it) const
{
    QList<QVector<QVector3D>> out = stripsBase(it);
    if (it->extrude.isNull() || out.isEmpty())
        return out;
    const QVector3D e = it->extrude;
    const QList<QVector<QVector3D>> rings = out;       // every loop (outer + holes) gets its far copy and sides
    for (const QVector<QVector3D> &ring : rings) {
        QVector<QVector3D> top;
        for (const QVector3D &q : ring)
            top.append(q + e);
        out.append(top);
        if (it->type() == SketchItem::Circle) {
            const int n = ring.size() - 1;
            for (int k = 0; k < 4 && n >= 4; ++k)
                out.append({ring[k * n / 4], ring[k * n / 4] + e});
        } else {
            // no line across a rounded face (fillets): its outline is drawn where it turns away (silhouetteEdges)
            const int n = int(ring.size()) - 1;
            for (int k = 0; k < n; ++k)
                if (n < 3 || !smoothVertex(ring[(k + n - 1) % n], ring[k], ring[(k + 1) % n]))
                    out.append({ring[k], ring[k] + e});
        }
    }
    return out;
}

QList<QVector<QVector3D>> Canvas::stripsBase(const SketchItem *it) const
{
    QList<QVector<QVector3D>> out;
    const QList<QVector3D> &p = it->pts;
    switch (it->type()) {
    case SketchItem::Line:
        if (p.size() >= 2)
            out.append({p[0], p[1]});
        break;
    case SketchItem::Polyline:
        if (!it->loops.isEmpty()) {                    // a board with holes: outer loop first, then the holes
            int k = 0;
            for (int n : it->loops) {
                QVector<QVector3D> s;
                for (int i = 0; i < n && k + i < p.size(); ++i)
                    s.append(p[k + i]);
                if (s.size() >= 3) {
                    s.append(s.first());
                    out.append(s);
                }
                k += n;
            }
        } else if (p.size() >= 2) {
            out.append(QVector<QVector3D>(p.begin(), p.end()));
        }
        break;
    case SketchItem::Rect: {
        QVector<QVector3D> c = rectCorners(it);
        if (c.size() == 4) {
            c.append(c[0]);
            out.append(c);
        }
        break;
    }
    case SketchItem::Revolve:
    case SketchItem::Pipe:
        out = sweepData(it)->feature;                  // creases, end outlines (canvas_sweep.cpp)
        break;
    case SketchItem::Circle:
        if (p.size() >= 2) {
            const Frame fr = frameFor(std::max(0, it->plane));
            const qreal r = (p[1] - p[0]).length();
            const int n = m_fine ? 360 : std::clamp(int(r * scaleAt(p[0]) / 3.0), 32, 256);
            const QVector3D e1 = r > 1e-9 ? (p[1] - p[0]).normalized() : fr.U;
            const QVector3D e2 = QVector3D::crossProduct(fr.N, e1);
            QVector<QVector3D> s;
            for (int i = 0; i <= n; ++i) {
                const qreal t = 2 * PI * i / n;
                s.append(p[0] + (e1 * float(std::cos(t)) + e2 * float(std::sin(t))) * float(r));
            }
            out.append(s);
        }
        break;
    case SketchItem::Arc:
        if (p.size() >= 3) {
            QVector3D cen, e1, e2;
            qreal rad, a0, sw;
            QVector<QVector3D> s;
            if (arcData(p[0], p[1], p[2], cen, rad, a0, sw, e1, e2)) {
                const int n = m_fine ? 240 : std::clamp(int(std::fabs(sw) * rad * scaleAt(cen) / 3.0), 16, 256);
                for (int i = 0; i <= n; ++i) {
                    const qreal t = a0 + sw * i / n;
                    s.append(cen + (e1 * float(std::cos(t)) + e2 * float(std::sin(t))) * float(rad));
                }
            } else {
                s << p[0] << p[2];
            }
            out.append(s);
        }
        break;
    case SketchItem::Curve:
        if (p.size() >= 3) {
            const QVector3D ctrl = p[1] * 2.0f - (p[0] + p[2]) * 0.5f;
            QVector<QVector3D> s;
            for (int i = 0; i <= 48; ++i) {
                const float t = i / 48.0f, mt = 1.0f - t;
                s.append(p[0] * (mt * mt) + ctrl * (2 * mt * t) + p[2] * (t * t));
            }
            out.append(s);
        }
        break;
    case SketchItem::Dimension: {
        const DimGeom g = dimGeom(it);
        if (g.ok) {
            out.append({p[0], g.d1});
            out.append({p[1], g.d2});
            out.append({g.d1, g.d2});
        }
        break;
    }
    case SketchItem::ImageItem: {
        QVector<QVector3D> c = imageCorners(it);
        if (c.size() == 4) {
            c.append(c[0]);
            out.append(c);
        }
        break;
    }
    default:
        break;
    }
    return out;
}

double Canvas::itemLength(const SketchItem *it) const
{
    if (it->type() == SketchItem::Dimension)
        return dimGeom(it).value;
    if (it->type() == SketchItem::Text || it->type() == SketchItem::ImageItem || it->type() == SketchItem::Hatch)
        return 0.0;
    double len = 0;
    for (const auto &s : strips(it))
        for (int i = 0; i + 1 < s.size(); ++i)
            len += (s[i + 1] - s[i]).length();
    return len;
}

qreal Canvas::textPixelSize(const SketchItem *it) const
{
    return std::max(6.0, it->textSize * scaleAt(it->pts.value(0)));
}

QFont Canvas::textFont(const SketchItem *it) const
{
    QFont f = QApplication::font();
    if (!it->fontFamily.isEmpty())
        f.setFamily(it->fontFamily);
    f.setBold(it->bold);
    f.setItalic(it->italic);
    f.setPixelSize(std::max(6, int(textPixelSize(it))));
    return f;
}

// unrotated box (screen space) of the text; textXf() rotates it about the anchor
QRectF Canvas::textRect(const SketchItem *it) const
{
    QPointF a;
    if (!projectD(it->pts.value(0), a))
        return QRectF();
    const QFontMetricsF fm(textFont(it));
    const QStringList lines = it->text.split('\n');
    qreal w = textPixelSize(it) * 1.5;
    for (const QString &l : lines)
        w = std::max(w, fm.horizontalAdvance(l));
    return QRectF(a.x(), a.y() - fm.height() / 2.0, w, fm.height() * lines.size());
}

QTransform Canvas::textXf(const SketchItem *it) const
{
    QTransform t;
    QPointF a;
    if (projectD(it->pts.value(0), a)) {
        t.translate(a.x(), a.y());
        t.rotate(-it->textAngle);
        t.translate(-a.x(), -a.y());
    }
    return t;
}

QRectF Canvas::itemScreenBounds(const SketchItem *it) const
{
    if (it->type() == SketchItem::Text)
        return textXf(it).mapRect(textRect(it));
    QRectF bb;
    bool any = false;
    for (const auto &s : strips(it))
        for (const QVector3D &q : s) {
            QPointF sp;
            if (!projectD(q, sp))
                continue;
            const QRectF r(sp, QSizeF(0.01, 0.01));
            bb = any ? bb.united(r) : r;
            any = true;
        }
    if (!any && !it->pts.isEmpty()) {
        QPointF sp;
        if (projectD(it->pts[0], sp))
            bb = QRectF(sp, QSizeF(0.01, 0.01));
    }
    return bb;
}

QVector3D Canvas::selectionCenter() const
{
    QVector3D mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
    bool any = false;
    auto add = [&](const QVector3D &q) {
        mn = QVector3D(std::min(mn.x(), q.x()), std::min(mn.y(), q.y()), std::min(mn.z(), q.z()));
        mx = QVector3D(std::max(mx.x(), q.x()), std::max(mx.y(), q.y()), std::max(mx.z(), q.z()));
        any = true;
    };
    for (const SketchItem *it : m_selected) {
        const auto ss = strips(it);
        if (ss.isEmpty()) {
            for (const QVector3D &q : it->pts)
                add(q);
        }
        for (const auto &s : ss)
            for (const QVector3D &q : s)
                add(q);
    }
    return any ? (mn + mx) * 0.5f : QVector3D();
}

SketchItem *Canvas::pick(const QPointF &s, qreal tolPx) const
{
    SketchItem *best = nullptr;
    qreal bd = tolPx;
    for (int i = m_items.size() - 1; i >= 0; --i) {
        SketchItem *it = m_items[i];
        if (it->hidden || it->locked || it->type() == SketchItem::Hatch || isSolid(it))
            continue;                                      // solid bodies are picked by their faces below
        if (it->type() == SketchItem::Text) {
            if (textRect(it).adjusted(-tolPx, -tolPx, tolPx, tolPx).contains(textXf(it).inverted().map(s)))
                return it;
            continue;
        }
        if (it->type() == SketchItem::ImageItem) {
            const QVector<QVector3D> c = imageCorners(it);
            QPolygonF poly;
            bool ok = c.size() == 4;
            for (const QVector3D &q : c) {
                QPointF sp;
                ok = ok && projectD(q, sp);
                poly << sp;
            }
            if (ok && poly.containsPoint(s, Qt::OddEvenFill))
                return it;
        }
        for (const auto &st : strips(it)) {
            for (int k = 0; k + 1 < st.size(); ++k) {
                QLineF l;
                if (!projectSeg(st[k], st[k + 1], l))
                    continue;
                const qreal d = distToSegment(s, l);
                if (d < bd) {
                    bd = d;
                    best = it;
                }
            }
        }
    }
    if (best)
        return best;
    // solid bodies: the face under the cursor, else a visible outline edge close by
    {
        SolidFace f;
        QVector3D hit;
        if (faceAt(s, f, hit) && !f.it->locked)
            return const_cast<SketchItem *>(f.it);
        for (int i = m_items.size() - 1; i >= 0; --i) {
            SketchItem *it = m_items[i];
            if (it->hidden || it->locked || !isSolid(it))
                continue;
            for (const auto &st : strips(it))
                for (int k = 0; k + 1 < st.size(); ++k) {
                    QLineF l;
                    if (projectSeg(st[k], st[k + 1], l)) {
                        const qreal d = distToSegment(s, l);
                        if (d < bd) { bd = d; best = it; }
                    }
                }
        }
        if (best)
            return best;
    }
    // a hatch is picked by clicking inside it, after the linework (only by tools that move / select)
    const bool hatchOk = m_tool == Tool::Select || m_tool == Tool::FreeMove || m_tool == Tool::DynMove ||
                         m_tool == Tool::Hatch || (m_tool == Tool::Move && m_moveStage == 0) ||
                         (m_tool == Tool::Mirror && m_mirrorStage == 0);
    if (!best && hatchOk)
        for (int i = m_items.size() - 1; i >= 0; --i) {
            SketchItem *it = m_items[i];
            if (it->hidden || it->locked || it->type() != SketchItem::Hatch)
                continue;
            QPainterPath path;
            path.setFillRule(Qt::OddEvenFill);
            bool ok = true;
            for (const auto &l : hatchLoops(it)) {
                QPolygonF poly;
                for (const QVector3D &q : l) {
                    QPointF sp;
                    ok = ok && projectD(q, sp);
                    poly << sp;
                }
                path.addPolygon(poly);
            }
            if (ok && path.contains(s))
                return it;
        }
    return best;
}

// ---------------------------------------------------------------------------
// zoom / fit
// ---------------------------------------------------------------------------
void Canvas::zoomIn()
{
    m_zoom = std::min(400.0, m_zoom * 1.25);
    update();
}

void Canvas::zoomOut()
{
    m_zoom = std::max(0.002, m_zoom / 1.25);
    update();
}

void Canvas::zoomFit()
{
    QList<QVector3D> pts;
    const Frame fr = planeFrame();
    const qreal w = m_pageSize.width() / 2, h = m_pageSize.height() / 2;
    if (m_set.showPaper)
        pts << QVector3D(-w, -h, 0) << QVector3D(w, -h, 0) << QVector3D(w, h, 0) << QVector3D(-w, h, 0);
    if (m_items.isEmpty() || m_set.showPlaneSheet) {
        const QVector3D o = planeOrigin();
        pts << o + fr.U * float(-w) + fr.V * float(-h) << o + fr.U * float(w) + fr.V * float(-h)
            << o + fr.U * float(w) + fr.V * float(h) << o + fr.U * float(-w) + fr.V * float(h);
    }
    for (const SketchItem *it : m_items) {
        const auto ss = strips(it);
        if (ss.isEmpty())
            for (const QVector3D &q : it->pts)
                pts << q;
        for (const auto &s : ss)
            for (const QVector3D &q : s)
                pts << q;
    }
    if (pts.isEmpty())
        return;
    QVector3D mn = pts[0], mx = pts[0];
    for (const QVector3D &q : std::as_const(pts)) {
        mn = QVector3D(std::min(mn.x(), q.x()), std::min(mn.y(), q.y()), std::min(mn.z(), q.z()));
        mx = QVector3D(std::max(mx.x(), q.x()), std::max(mx.y(), q.y()), std::max(mx.z(), q.z()));
    }
    m_target = (mn + mx) * 0.5f;
    qreal mdx = 1e-3, mdy = 1e-3;
    for (const QVector3D &q : std::as_const(pts)) {
        const QVector3D d = q - m_target;
        mdx = std::max<qreal>(mdx, std::fabs(QVector3D::dotProduct(d, m_br)));
        mdy = std::max<qreal>(mdy, std::fabs(QVector3D::dotProduct(d, m_bu)));
    }
    const qreal z = std::min((width() * 0.5 * 0.88) / mdx, (height() * 0.5 * 0.88) / mdy);
    m_zoom = std::clamp(z, 0.002, 400.0);
    update();
}

void Canvas::zoomFitSelection()
{
    if (m_selected.isEmpty()) {
        zoomFit();
        return;
    }
    QVector3D mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
    QList<QVector3D> pts;
    for (const SketchItem *it : m_selected) {
        const auto ss = strips(it);
        if (ss.isEmpty())
            for (const QVector3D &q : it->pts)
                pts << q;
        for (const auto &s : ss)
            for (const QVector3D &q : s)
                pts << q;
    }
    if (pts.isEmpty())
        return;
    for (const QVector3D &q : std::as_const(pts)) {
        mn = QVector3D(std::min(mn.x(), q.x()), std::min(mn.y(), q.y()), std::min(mn.z(), q.z()));
        mx = QVector3D(std::max(mx.x(), q.x()), std::max(mx.y(), q.y()), std::max(mx.z(), q.z()));
    }
    m_target = (mn + mx) * 0.5f;
    qreal mdx = 5.0, mdy = 5.0;
    for (const QVector3D &q : std::as_const(pts)) {
        const QVector3D d = q - m_target;
        mdx = std::max<qreal>(mdx, std::fabs(QVector3D::dotProduct(d, m_br)));
        mdy = std::max<qreal>(mdy, std::fabs(QVector3D::dotProduct(d, m_bu)));
    }
    m_zoom = std::clamp(std::min((width() * 0.5 * 0.7) / mdx, (height() * 0.5 * 0.7) / mdy), 0.002, 400.0);
    update();
}

// ---------------------------------------------------------------------------
// creation flow
// ---------------------------------------------------------------------------
void Canvas::addPoint(const QVector3D &p)
{
    if (!m_pending.isEmpty() && (m_pending.last() - p).length() < 1e-6 &&
        m_tool != Tool::Dimension)
        return;                                   // ignore zero-length clicks
    m_pending.append(p);
    switch (m_tool) {
    case Tool::Line: case Tool::Circle: case Tool::Rect:
        if (m_pending.size() == 2) finishPending();
        break;
    case Tool::Curve3: case Tool::Arc: case Tool::Dimension:
        if (m_pending.size() == 3) finishPending();
        break;
    case Tool::Move:
        if (m_pending.size() == 2) {
            const QVector3D d = m_pending[1] - m_pending[0];
            m_pending.clear();
            applyMove(d, m_moveCopy);
            if (!m_moveCopy) setTool(Tool::Select);
        }
        break;
    default:
        break;
    }
    dynClear();
    emitHint();
}

void Canvas::applyNumeric()
{
    if (!dynTyping() || !dynActive())
        return;
    double v = 0;
    if (m_tool == Tool::Revolve || m_tool == Tool::Pipe) {  // typed angle / diameter
        if (dynValue(0, v) && v > 0) {
            if (m_tool == Tool::Revolve) m_rvAngle = std::min(v, 360.0);
            else m_pipeD = v;
            emit toolSizesChanged();
            emit message(m_tool == Tool::Revolve ? QString("Revolve angle %1\xC2\xB0").arg(m_rvAngle)
                                                 : QString("Pipe diameter %1").arg(Units::format(m_pipeD)));
        }
        dynClear();
        emitHint();
        update();
        return;
    }
    if (m_tool == Tool::Shell) {                            // typed wall thickness
        if (dynValue(0, v) && v > 0) {
            m_shellT = v;
            emit toolSizesChanged();
            emit message(QString("Shell walls %1 - click the face to leave open").arg(Units::format(v)));
        }
        dynClear();
        emitHint();
        update();
        return;
    }
    if (m_tool == Tool::Fillet || m_tool == Tool::Chamfer) { // typed radius / distance for the next clicks
        if (dynValue(0, v) && v > 0) {
            (m_tool == Tool::Fillet ? m_filletR : m_chamferD) = v;
            emit toolSizesChanged();
            emit message(QString("%1 %2 - now click corners or edges").arg(toolName(m_tool), Units::format(v)));
        }
        dynClear();
        emitHint();
        update();
        return;
    }
    if (m_tool == Tool::Rotate && m_r3Stage == 2) {         // typed angle
        if (dynValue(0, v)) {
            dynClear();
            r3Apply(v);
            if (!m_moveCopy)
                setTool(Tool::Select);
        }
        return;
    }
    if (m_gzActive >= 0) {                          // typed distance along the arrow / angle of the ring
        if (dynValue(0, v)) {
            if (m_gizmoMode == GizmoMode::Rotate) m_gzAngle = v;
            else if (m_gzActive < 3) m_gzDelta = QVector3D(m_gzActive == 0, m_gzActive == 1, m_gzActive == 2) * float(v);
            gzCommit();
        }
        return;
    }
    if (m_tool == Tool::Box && m_pending.size() >= 2) {     // typed height, + = out of the face
        if (dynValue(0, v))
            finishBox(v * m_boxOutSign);
        return;
    }
    if (m_tool == Tool::PressPull && m_ppItem) {
        if (dynValue(0, v)) {
            applyPull(v);
            pressPullCommit();
        }
        return;
    }
    const QVector3D p = previewPoint();
    addPoint(p);
    dynClear();
    emitHint();
    update();
}

void Canvas::startOrbit(const QPoint &pos)
{
    if (m_viewAnim)
        m_viewAnim->stop();
    const bool wasNamed = m_view != View::Iso;
    if (wasNamed) {                                 // keep exactly what is on screen
        m_isoQ = currentCamQ();
        m_view = View::Iso;
    }
    updateCamera();
    if (wasNamed) {
        emit viewChanged();
        emitSnapState();
    }
    m_orbiting = true;
    m_lastScreen = pos;
    setCursor(Qt::ClosedHandCursor);
}

// ---------------------------------------------------------------------------
// mouse
// ---------------------------------------------------------------------------
void Canvas::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    m_pressScreen = e->pos();
    m_lastScreen = e->pos();

    if (e->button() == Qt::LeftButton && m_set.showGizmo && !m_exporting) {
        const int h = gizmoHit(e->pos());
        if (h == kGizmoHome) {           // Home keeps the zoom (Reset ISO also fits)
            goHome(true);
            return;
        }
        if (h == kGizmoRollL || h == kGizmoRollR) {
            rollView(h == kGizmoRollL ? 90.0 : -90.0);
            return;
        }
        if (h >= 0) {
            const QVector3D d = gizmoDirection(h);
            const int nonZero = (d.x() != 0) + (d.y() != 0) + (d.z() != 0);
            if (nonZero == 1) {          // face: swing round to the named orthographic view
                const View v = d.x() > 0 ? View::Right : d.x() < 0 ? View::Side
                             : d.y() > 0 ? View::Back  : d.y() < 0 ? View::Front
                             : d.z() > 0 ? View::Top   : View::Bottom;
                animateTo(namedViewQ(v, true), QString(), v);
            } else {                     // edge or corner: free 3D view from there
                setViewDirection(d);
            }
            return;
        }
    }
    if (e->button() == Qt::RightButton && m_set.showGizmo && !m_exporting && gizmoHit(e->pos()) >= 0) {
        m_rmbOnCube = true;              // view-cube menu opens on release
        return;
    }
    if (e->button() == Qt::RightButton) {
        m_rmbDown = true;
        return;
    }
    // Shift+left pans, except where Shift+click picks several things (objects, bodies, faces)
    const bool shiftPicks = m_tool == Tool::Select || m_tool == Tool::PressPull || (m_tool == Tool::Rotate && m_r3Stage == 0);
    if (e->button() == Qt::MiddleButton ||
        (e->button() == Qt::LeftButton && ((e->modifiers() & Qt::ShiftModifier && !shiftPicks &&
                                           m_tool != Tool::Trim && m_tool != Tool::Extend && !drawingTool()) || m_spaceDown))) {
        m_panning = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier)) {
        startOrbit(e->pos());
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;

    m_cur = snapCursor(e->pos());
    const bool pointTool = drawingTool() || m_tool == Tool::Text || m_tool == Tool::InsertImage;
    if (pointTool && !m_cur.valid) {
        emit message("The sketch plane is edge-on to this view - press Tab to change plane, "
                     "or use View > Look at sketch plane");
        return;
    }
    if (m_pickCb && m_cur.valid) {
        auto cb = m_pickCb;
        m_pickCb = nullptr;
        cb(m_cur.pt);
        emitHint();
        update();
        return;
    }

    switch (m_tool) {
    case Tool::Select: {
        if (m_gzActive >= 0) {                     // second click ends a gizmo move / turn
            gzCommit();
            break;
        }
        // Shift (or Ctrl) + click adds / removes lines, shapes and bodies; Shift + drag adds a box's worth
        const bool add = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
        if (!add) {
            const int gh = gzHit(e->pos());
            if (gh >= 0) {
                gzBegin(gh, e->pos());
                break;
            }
            const int hg = handleAt(e->pos());
            if (hg == 8) {
                beginRotate(e->pos());
                break;
            }
            if (hg >= 0) {
                beginResize(hg);
                break;
            }
        }
        SketchItem *it = pick(e->pos(), 9.0);
        if (it) {
            if (add)
                setSelected(it, !it->selected);
            else if (!it->selected) {
                clearSelection();
                setSelected(it, true);
            }
            if (m_cur.valid && !(e->modifiers() & Qt::ShiftModifier))
                beginDrag(m_cur.pt);
            emit message(QString("%1 selected  -  Shift+click adds / removes").arg(m_selected.size()));
        } else {
            if (!add)
                clearSelection();
            m_rubberActive = true;
            m_rubberBand = QRectF(e->pos(), e->pos());
        }
        break;
    }
    case Tool::Line: case Tool::Curve3: case Tool::Arc: case Tool::Circle:
    case Tool::Rect: case Tool::Polyline: case Tool::Dimension:
        // a closed shape started on a board's face is drawn in that face (then Press/Pull cuts or adds)
        if (m_pending.isEmpty() && (m_tool == Tool::Rect || m_tool == Tool::Circle || m_tool == Tool::Polyline) &&
            boxStartOnFace(e->pos()))
            m_cur = snapCursor(e->pos());
        addPoint(m_cur.pt);
        break;
    case Tool::Text:
        createTextAt(m_cur.pt);
        break;
    case Tool::InsertImage:
        insertPendingImageAt(m_cur.pt);
        break;
    case Tool::Trim:
    case Tool::Extend:
        m_fenceDown = true;                       // a click trims; a drag draws a trim fence
        m_fenceDrag = false;
        m_fenceA = m_fenceB = e->pos();
        break;
    case Tool::Cutter:
        if (m_pending.isEmpty()) {
            m_pending.append(m_cur.pt);
        } else if (m_pending.size() == 1) {
            if ((m_cur.pt - m_pending[0]).length() < 1e-6f)
                break;
            m_pending.append(m_cur.pt);
            if (m_bladeAction == 0) {
                bladeApply(m_pending[0], m_pending[1], 0);
                m_pending.clear();
            }
        } else {
            const int side = bladeSide(m_pending[0], m_pending[1], m_cur.pt);
            bladeApply(m_pending[0], m_pending[1], side);
            m_pending.clear();
        }
        m_ghosts.clear();
        m_marks.clear();
        emitHint();
        break;
    case Tool::Move:
        if (m_moveStage == 0) {
            if (SketchItem *it = pick(e->pos(), 9.0)) {
                setSelected(it, !it->selected);
            } else {
                m_rubberActive = true;
                m_rubberBand = QRectF(e->pos(), e->pos());
            }
            emitHint();
        } else {
            if (m_pending.isEmpty())
                noteMoveBase(e->pos());                // a base on a face of the moving body: "up to face" later
            addPoint(m_cur.pt);
        }
        break;
    case Tool::Mirror:
        if (m_mirrorStage == 0) {
            if (SketchItem *it = pick(e->pos(), 9.0))
                setSelected(it, !it->selected);
            emitHint();
        } else {
            m_pending.append(m_cur.pt);
            if (m_pending.size() == 2) {
                const QVector3D a = m_pending[0], b = m_pending[1];
                m_pending.clear();
                mirrorAcross(a, b, m_mirrorKeep);
                setTool(Tool::Select);
            }
            emitHint();
        }
        break;
    case Tool::Join:
        joinClick(pick(e->pos(), 9.0));
        emitHint();
        break;
    case Tool::Break:
        breakAt(e->pos());
        break;
    case Tool::Offset:
        offsetClick(pick(e->pos(), 9.0), e->pos());
        break;
    case Tool::Hatch:
        hatchClick(e->pos());
        break;
    case Tool::Box:
        if (m_pending.size() >= 2) {
            finishBox(m_boxCurH);
        } else {
            if (m_pending.isEmpty() && boxStartOnFace(e->pos()))   // dynamic UCS: the base lies on that face
                m_cur = snapCursor(e->pos());
            if (m_cur.valid)
                addPoint(m_cur.pt);
            m_hoverFaceValid = false;
        }
        break;
    case Tool::PressPull:
        if (m_ppItem)
            pressPullCommit();
        else if (e->modifiers() & Qt::ShiftModifier)
            pullSetToggle(e->pos());               // collect faces of several boards, pull them together
        else
            pressPullStart(e->pos());
        break;
    case Tool::Fillet:
    case Tool::Chamfer:
        updateFilletHover(e->pos());
        filletClick();
        break;
    case Tool::Shell:
        shellClick(e->pos());
        break;
    case Tool::Revolve:
        revolveClick(e->pos());
        break;
    case Tool::Pipe:
        pipeClick(e->pos());
        break;
    case Tool::Rotate:
        if (m_r3Stage == 0) {
            if (SketchItem *it = pick(e->pos(), 9.0)) {
                setSelected(it, !it->selected);
            } else {
                m_rubberActive = true;
                m_rubberBand = QRectF(e->pos(), e->pos());
            }
        } else if (m_r3Stage == 1) {
            m_r3Base = m_cur.pt;
            m_r3Stage = 2;
            m_r3Angle = 0;
            dynClear();
        } else {
            r3Apply(m_r3Angle);
            if (!m_moveCopy)
                setTool(Tool::Select);
            else
                m_r3Angle = 0;
        }
        emitHint();
        break;
    case Tool::Edit: {
        SketchItem *it = pick(e->pos(), 9.0);
        if (!it && m_editItem) {
            // a click close to a handle of the current item still counts
            QPointF sp;
            for (const QVector3D &q : m_editItem->pts)
                if (projectD(q, sp) && QLineF(sp, QPointF(e->pos())).length() < 12.0)
                    it = m_editItem;
        }
        if (it) {
            m_editItem = it;
            clearSelection();
            setSelected(it, true);
            if (m_cur.valid)
                beginDrag(m_cur.pt);
        } else {
            m_editItem = nullptr;
            clearSelection();
        }
        emitHint();
        break;
    }
    case Tool::FreeMove:
    case Tool::DynMove: {
        SketchItem *it = pick(e->pos(), 9.0);
        if (it) {
            if (!it->selected) {
                clearSelection();
                setSelected(it, true);
            }
            if (m_cur.valid)
                beginDrag(m_cur.pt);
        }
        break;
    }
    }
    update();
}

void Canvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_rmbDown && !m_orbiting && (e->pos() - m_pressScreen).manhattanLength() > 5)
        startOrbit(m_pressScreen);

    if (m_orbiting) {
        const QPoint d = e->pos() - m_lastScreen;
        m_lastScreen = e->pos();
        const float ax = float(-d.x() * m_set.orbitSpeed);   // drag right = turn the model right
        const float ay = float(d.y() * m_set.orbitSpeed);    // drag down  = look from higher up
        if (m_set.freeOrbit) {
            // any axis, 360 deg, straight over the top: turn about the screen axes (Ctrl = roll)
            QQuaternion q;
            if (e->modifiers() & Qt::ControlModifier)
                q = QQuaternion::fromAxisAndAngle(-m_bf, -ax);
            else
                q = QQuaternion::fromAxisAndAngle(m_bu, ax) * QQuaternion::fromAxisAndAngle(m_br, -ay);
            m_isoQ = (q * m_isoQ).normalized();
        } else {
            // turntable about Top's up: azimuth + elevation, elevation clamped short of the poles
            const QVector3D up = frameUp();
            const QVector3D eye = -m_bf;
            qreal elev = std::asin(std::clamp<qreal>(QVector3D::dotProduct(eye, up), -1, 1)) * 180.0 / PI;
            QVector3D h = eye - up * QVector3D::dotProduct(eye, up);
            if (h.length() < 1e-4f)                         // straight down / up: screen-up shows "back"
                h = -(m_bu - up * QVector3D::dotProduct(m_bu, up));
            h = QQuaternion::fromAxisAndAngle(up, ax).rotatedVector(h.normalized());
            elev = std::clamp(elev + ay, -89.5, 89.5) * PI / 180.0;
            const QVector3D ne = up * float(std::sin(elev)) + h * float(std::cos(elev));
            m_isoQ = camFromDirUp(ne, up, m_isoQ);
        }
        updateCamera();
        update();
        return;
    }
    if (m_panning) {
        const QPoint d = e->pos() - m_lastScreen;
        m_lastScreen = e->pos();
        m_target += (-m_br * float(d.x()) + m_bu * float(d.y())) / float(m_zoom);
        update();
        return;
    }

    m_lastMouse = e->pos();
    if (m_gzActive >= 0) {                          // a gizmo handle is held
        if (!dynTyping())
            gzUpdate(e->pos());
        update();
        return;
    }
    if (m_tool == Tool::Select && !m_dragging && !(e->buttons() & Qt::LeftButton)) {
        const int gh = gzHit(e->pos());
        if (gh != m_gzHover) {
            m_gzHover = gh;
            setCursor(gh >= 0 ? Qt::SizeAllCursor : cursorFor(m_tool));
            update();
        }
    }
    if (m_rotating) {
        doRotate(e->pos(), QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
        return;
    }
    if (m_tool == Tool::Select && !m_dragging && !(e->buttons() & Qt::LeftButton)) {
        const int hg = handleAt(e->pos());
        const bool rh = hg == 8;
        if (rh != m_rotHover) {
            m_rotHover = rh;
            if (rh) setCursor(Qt::PointingHandCursor);
            else setCursor(cursorFor(m_tool));
            update();
        }
    }
    if (m_resizing) {
        m_cur = snapCursor(e->pos(), &m_rsPlanePt);
        doResize(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
        return;
    }
    if (m_fenceDown) {
        m_fenceB = e->pos();
        if (!m_fenceDrag && (m_fenceB - m_fenceA).manhattanLength() > 8)
            m_fenceDrag = true;
        if (m_fenceDrag) {
            m_ghosts.clear();
            m_marks.clear();
            update();
            return;
        }
    }
    {
        const int h = m_set.showGizmo ? gizmoHit(e->pos()) : -1;
        if (h != m_gizmoHover) {
            m_gizmoHover = h;
            update(gizmoRect().toAlignedRect());
        }
        m_overGizmo = h >= 0;
    }
    m_cur = snapCursor(e->pos(), m_dragging ? &m_dragStart : nullptr);
    if (m_cur.valid)
        emit cursorMoved(m_cur.pt);
    if (m_tool == Tool::Cutter)
        updateBladePreview();
    if (m_tool == Tool::Hatch) {
        updateHatchPreview();
        update();
    }
    if ((m_tool == Tool::Box && m_pending.isEmpty()) || (m_tool == Tool::PressPull && !m_ppItem) || m_tool == Tool::Shell)
        updateSolidHover(e->pos());
    if (m_tool == Tool::Box && m_pending.size() >= 2 && !dynTyping()) {
        const double h = solidDistanceAt(e->pos(), m_pending[1], planeFrame().N, nullptr, &m_boxNote);
        if (std::isfinite(h))
            m_boxCurH = h;
        update();
    }
    if (m_tool == Tool::Rotate && m_r3Stage == 2 && !dynTyping()) {
        m_r3Angle = r3SnapAngle(r3AngleAt(e->pos()));
        update();
    }
    if (m_tool == Tool::Fillet || m_tool == Tool::Chamfer) {
        updateFilletHover(e->pos());
        update();
    }
    if (m_tool == Tool::PressPull && m_ppItem && !dynTyping()) {
        double d = solidDistanceAt(e->pos(), m_ppPoint, m_ppFace.n, m_ppItem, &m_ppNote);
        updatePullTarget(e->pos());               // over a parallel face: up to that face
        if (m_ppToFace)
            d = QVector3D::dotProduct(m_ppTo.v[0] - m_ppPoint, m_ppFace.n);
        if (std::isfinite(d))
            applyPull(d);
        update();
    }

    if (m_rubberActive) {
        m_rubberBand = QRectF(m_pressScreen, QPointF(e->pos())).normalized();
        update();
        return;
    }

    if (m_dragging && m_cur.valid) {
        if (!m_dragMoved && (e->pos() - m_pressScreen).manhattanLength() < 4)
            return;
        m_dragMoved = true;
        QVector3D delta = m_cur.pt - m_dragStart;

        if (m_tool == Tool::Edit && m_editItem && m_editIndex >= 0) {
            m_editItem->pts[m_editIndex] = m_cur.pt;
            update();
            return;
        }
        if (m_tool == Tool::DynMove) {
            const qreal ax = std::fabs(delta.x()), ay = std::fabs(delta.y()), az = std::fabs(delta.z());
            if (ax >= ay && ax >= az)      delta = QVector3D(delta.x(), 0, 0);
            else if (ay >= az)             delta = QVector3D(0, delta.y(), 0);
            else                           delta = QVector3D(0, 0, delta.z());
        }
        m_magnetOn = false;
        m_dynDelta = delta;
        for (auto it = m_dragData.begin(); it != m_dragData.end(); ++it) {
            SketchItem *item = it.key();
            const QList<QVector3D> &orig = it.value();
            for (int i = 0; i < orig.size() && i < item->pts.size(); ++i)
                item->pts[i] = orig[i] + delta;
        }
        m_smart.clear();
        if (m_set.snapObject && (m_tool == Tool::Select || m_tool == Tool::FreeMove) &&
            m_cur.kind != 2 && m_cur.kind != 3 && m_cur.kind != 4 && m_cur.kind != 6 && m_cur.kind != 7) {
            QVector3D fix = delta;
            bool hit = dragMagnet(fix);                       // exact vertex-to-vertex first
            if (!hit)
                hit = smartAlign(fix, delta);                 // otherwise edge / centre alignment
            if (hit && !(fix - delta).isNull()) {
                const QVector3D adj = fix - delta;
                for (auto it = m_dragData.begin(); it != m_dragData.end(); ++it)
                    for (QVector3D &pt : it.key()->pts)
                        pt += adj;
                m_dynDelta = fix;
            }
        }
        update();
        return;
    }

    // hover highlight
    SketchItem *h = nullptr;
    if (!m_dragging && e->buttons() == Qt::NoButton &&
        (m_tool == Tool::Select || m_tool == Tool::Edit || m_tool == Tool::FreeMove ||
         m_tool == Tool::DynMove || m_tool == Tool::Trim || m_tool == Tool::Join ||
         m_tool == Tool::Break || m_tool == Tool::Offset || m_tool == Tool::Extend ||
         (m_tool == Tool::Mirror && m_mirrorStage == 0) || (m_tool == Tool::Move && m_moveStage == 0) ||
         (m_tool == Tool::Rotate && m_r3Stage == 0)))
        h = pick(e->pos(), 9.0);
    m_hover = h;
    if (m_tool == Tool::Offset && offsetSourceValid()) {
        updateOffsetPreview();
        update();
    }
    if (m_tool == Tool::Trim || m_tool == Tool::Extend) {
        m_ghosts.clear();
        m_marks.clear();
        if (h && !m_fenceDown) {
            const bool ext = (m_tool == Tool::Extend) != bool(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
            const EditOp op = ext ? computeExtend(h, e->pos()) : computeTrim(h, e->pos());
            if (op.ok) {
                m_ghosts = op.ghost;
                m_marks = op.marks;
                m_ghostExtend = ext;
            }
        }
    }
    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_rotating && e->button() == Qt::LeftButton) {
        finishRotate();
        return;
    }
    if (m_resizing && e->button() == Qt::LeftButton) {
        m_resizing = false;
        m_rsOrig.clear();
        snapshotCommit("Resize");
        emit selectionChanged();
        update();
        return;
    }
    if (e->button() == Qt::RightButton && m_rmbOnCube) {
        m_rmbOnCube = false;
        showCubeMenu(e->globalPosition().toPoint());
        return;
    }
    if (e->button() == Qt::RightButton) {
        const bool wasOrbit = m_orbiting;
        m_rmbDown = false;
        m_orbiting = false;
        setCursor(cursorFor(m_tool));
        if (!wasOrbit)
            showContextMenu(e->globalPosition().toPoint());
        return;
    }
    if (m_panning) {
        m_panning = false;
        setCursor(m_spaceDown ? Qt::OpenHandCursor : cursorFor(m_tool));
        return;
    }
    if (m_orbiting) {
        m_orbiting = false;
        setCursor(cursorFor(m_tool));
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (m_gzActive >= 0 && (e->pos() - m_pressScreen).manhattanLength() > 8) {
        gzCommit();                                // press - drag - release on a gizmo handle
        return;
    }
    // Press/Pull also works as press - drag - release
    if (m_tool == Tool::PressPull && m_ppItem && (e->pos() - m_pressScreen).manhattanLength() > 8) {
        pressPullCommit();
        return;
    }

    if (m_fenceDown) {
        const bool drag = m_fenceDrag;
        m_fenceDown = m_fenceDrag = false;
        const bool shift = e->modifiers() & Qt::ShiftModifier;
        if (drag)
            fenceTrim(m_fenceA, e->pos(), (m_tool == Tool::Extend) != shift);
        else
            trimExtendClick(e->pos(), shift);
        update();
        return;
    }

    if (m_rubberActive) {
        m_rubberActive = false;
        const QRectF band = m_rubberBand;
        const bool windowMode = e->pos().x() >= m_pressScreen.x();   // L->R: fully inside
        if (band.width() > 3 || band.height() > 3) {
            for (SketchItem *it : m_items) {
                if (it->hidden || it->locked)
                    continue;
                const QRectF bb = itemScreenBounds(it);
                if (bb.isNull())
                    continue;
                if (windowMode ? band.contains(bb) : band.intersects(bb))
                    setSelected(it, true);
            }
        }
        update();
        return;
    }

    if (m_dragging) {
        m_dragging = false;
        m_boxesValid = false;
        m_smart.clear();
        m_magnetOn = false;
        m_editIndex = -1;
        m_dynDelta = QVector3D();
        if (m_dragMoved && !m_snapshot.isEmpty())
            snapshotCommit(m_tool == Tool::Edit ? "Shape Edit"
                           : (m_tool == Tool::DynMove ? "Dynamic Move" : "Move"));
        else
            m_snapshot.clear();
        m_dragData.clear();
        m_dragMoved = false;
        update();
    }
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (m_tool == Tool::Polyline && m_pending.size() >= 2) {
        // the first click of the double-click already added a point; drop the duplicate
        if ((m_pending[m_pending.size() - 1] - m_pending[m_pending.size() - 2]).length() < 1e-6)
            m_pending.removeLast();
        finishPending();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        SketchItem *it = pick(e->pos(), 10.0);
        if (it && it->type() == SketchItem::Text) {
            startInlineEdit(it);
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(e);
}

void Canvas::wheelEvent(QWheelEvent *e)
{
    qreal steps = e->angleDelta().y() / 120.0;
    if (m_set.invertWheel)
        steps = -steps;
    const qreal factor = std::pow(1.15, steps);
    const QPointF pos = e->position();
    const QPointF c = viewCenter();
    const QPointF d0 = (pos - c) / m_zoom;
    m_zoom = std::clamp(m_zoom * factor, 0.002, 400.0);
    if (m_set.zoomToCursor) {
        const QPointF d1 = (pos - c) / m_zoom;
        const QPointF dd = d0 - d1;
        m_target += m_br * float(dd.x()) - m_bu * float(dd.y());
    }
    update();
}

void Canvas::leaveEvent(QEvent *)
{
    m_hover = nullptr;
    update();
}

void Canvas::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    update();
}

// ---------------------------------------------------------------------------
// keyboard
// ---------------------------------------------------------------------------
// Offset owns T (through) and number keys while it runs, ahead of the tool shortcuts
bool Canvas::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride && m_tool == Tool::Offset) {
        auto *ke = static_cast<QKeyEvent *>(e);
        const int k = ke->key();
        if (!(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) &&
            (k == Qt::Key_T || (k >= Qt::Key_0 && k <= Qt::Key_9) || k == Qt::Key_Period ||
             k == Qt::Key_Comma || k == Qt::Key_Apostrophe || k == Qt::Key_QuoteDbl || k == Qt::Key_Slash ||
             (k == Qt::Key_Backspace && !m_offsetTyped.isEmpty()))) {
            e->accept();
            return true;
        }
    }
    // while a value is typed, the calculator keys belong to it
    if (e->type() == QEvent::ShortcutOverride && (dynActive() || m_tool == Tool::Offset)) {
        auto *ke = static_cast<QKeyEvent *>(e);
        const int k = ke->key();
        if (!(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) &&
            (k == Qt::Key_Plus || k == Qt::Key_Minus || k == Qt::Key_Asterisk || k == Qt::Key_Slash ||
             k == Qt::Key_ParenLeft || k == Qt::Key_ParenRight)) {
            e->accept();
            return true;
        }
    }
    // a shape being extruded: N / J / C / I / A pick what the new body does, ahead of the tool shortcuts
    if (e->type() == QEvent::ShortcutOverride && m_tool == Tool::PressPull && m_ppItem && m_ppFace.kind < 0) {
        auto *ke = static_cast<QKeyEvent *>(e);
        const int k = ke->key();
        if (!(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) &&
            (k == Qt::Key_N || k == Qt::Key_J || k == Qt::Key_C || k == Qt::Key_I || k == Qt::Key_A)) {
            e->accept();
            return true;
        }
    }
    // the dimension tool's H / V / K mode keys come before the tool shortcuts (H = Hatch)
    if (e->type() == QEvent::ShortcutOverride && m_tool == Tool::Dimension) {
        auto *ke = static_cast<QKeyEvent *>(e);
        const int k = ke->key();
        if (!(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) &&
            (k == Qt::Key_H || k == Qt::Key_V || k == Qt::Key_K)) {
            e->accept();
            return true;
        }
    }
    return QWidget::event(e);
}

void Canvas::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Shift && !e->isAutoRepeat() && !m_lastMouse.isNull()) {
        m_cur = snapCursor(m_lastMouse, m_dragging ? &m_dragStart : nullptr);
        update();
    }
    const int k = e->key();
    const bool plain = !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier));

    // a space inside a typed feet-inch value ("6 1/2") is part of the value, not the pan key
    const bool typingLen = (m_tool == Tool::Offset && !m_offsetTyped.isEmpty()) ||
                           (dynActive() && !m_dyn[m_dynIdx].text.isEmpty());
    if (k == Qt::Key_Space && plain && typingLen && Units::isImperial()) {
        QString &txt = m_tool == Tool::Offset ? m_offsetTyped : m_dyn[m_dynIdx].text;
        if (!txt.endsWith(' '))
            txt += ' ';
        emitHint();
        update();
        return;
    }
    if (k == Qt::Key_Space && !e->isAutoRepeat() && plain) {
        m_spaceDown = true;
        if (!m_panning)
            setCursor(Qt::OpenHandCursor);
        return;
    }

    // typed numeric input while drawing (feet ' inches " and fractions / too)
    const bool numKey = (k >= Qt::Key_0 && k <= Qt::Key_9) || k == Qt::Key_Period || k == Qt::Key_Minus ||
                        k == Qt::Key_Plus || k == Qt::Key_Asterisk || k == Qt::Key_ParenLeft || k == Qt::Key_ParenRight;
    const bool lenKey = k == Qt::Key_Apostrophe || k == Qt::Key_QuoteDbl || k == Qt::Key_Slash;

    // Offset: type the distance, Enter accepts, T = through point
    if (plain && m_tool == Tool::Offset) {
        auto refresh = [this] {
            updateOffsetPreview();
            emitHint();
            update();
        };
        if (numKey || lenKey || k == Qt::Key_Comma) {
            if (k == Qt::Key_Minus && m_offsetTyped.isEmpty())
                return;                                // offsets are always positive; the cursor picks the side
            const QString ch = (k == Qt::Key_Comma) ? QString(".") : e->text();
            m_offsetTyped += ch;                       // the calculator checks it (800-2*18 has two numbers)
            m_offsetThrough = false;
            refresh();
            return;
        }
        if (k == Qt::Key_Backspace && !m_offsetTyped.isEmpty()) {
            m_offsetTyped.chop(1);
            refresh();
            return;
        }
        if (k == Qt::Key_T) {
            m_offsetThrough = !m_offsetThrough;
            m_offsetTyped.clear();
            emit message(m_offsetThrough ? "Offset: THROUGH - the copy passes through the point you click"
                                         : QString("Offset: fixed distance %1").arg(Units::format(m_offsetDist)));
            refresh();
            return;
        }
        if (k == Qt::Key_Return || k == Qt::Key_Enter) {
            if (!m_offsetTyped.isEmpty()) {
                bool ok = false;
                double v = 0;
                ok = Units::parse(m_offsetTyped, v);
                m_offsetTyped.clear();
                if (!ok || v <= 0) {
                    emit message("Offset: type a distance greater than 0");
                    refresh();
                    return;
                }
                setOffsetDistance(v);
                emit message(QString("Offset distance = %1").arg(Units::format(v)));
                // object already picked and cursor on a side: make the copy right away
                if (offsetSourceValid() && rect().contains(m_lastMouse))
                    offsetCommit(m_lastMouse, v);
                refresh();
                return;
            }
            if (!offsetSourceValid() && m_lastOffsetMade && m_items.contains(m_lastOffsetMade)) {
                offsetClick(m_lastOffsetMade, m_lastMouse);   // repeat on the copy just made
                return;
            }
            if (offsetSourceValid() && rect().contains(m_lastMouse)) {
                offsetClick(nullptr, m_lastMouse);
                return;
            }
        }
    }
    if (plain && dynActive()) {
        QString &txt = m_dyn[m_dynIdx].text;
        if (numKey || lenKey) {
            txt += e->text();                          // a sum is fine: 800-2*18, (2400-54)/4
            emitHint();
            update();
            return;
        }
        if (k == Qt::Key_Comma || k == Qt::Key_Less || k == Qt::Key_Semicolon || k == Qt::Key_Tab) {
            dynNext();
            emitHint();
            update();
            return;
        }
    }
    if (plain && m_tool == Tool::Dimension) {
        int mode = -1;
        if (k == Qt::Key_H) mode = 1;
        else if (k == Qt::Key_V) mode = 2;
        else if (k == Qt::Key_K) mode = 0;
        if (mode >= 0) {
            setDimMode(mode);
            return;
        }
    }
    if (plain && m_tool == Tool::Rotate && m_r3Stage >= 1 &&
        (k == Qt::Key_X || k == Qt::Key_Y || k == Qt::Key_Z || k == Qt::Key_K)) {
        if (k == Qt::Key_K) {
            setMoveCopy(!m_moveCopy);
        } else {
            const int want = k == Qt::Key_X ? 0 : (k == Qt::Key_Y ? 1 : 2);
            m_r3Axis = m_r3Axis == want ? 3 : want;          // the same key again: back to the plane normal
            if (m_r3Stage == 2 && !m_lastMouse.isNull())
                m_r3Angle = r3SnapAngle(r3AngleAt(m_lastMouse));
        }
        emitHint();
        update();
        return;
    }
    if (plain && m_tool == Tool::PressPull && m_ppItem && m_ppFace.kind < 0 &&
        (k == Qt::Key_N || k == Qt::Key_J || k == Qt::Key_C || k == Qt::Key_I || k == Qt::Key_A)) {
        m_ppOpNow = k == Qt::Key_A ? 0 : k == Qt::Key_N ? 1 : k == Qt::Key_J ? 2 : k == Qt::Key_C ? 3 : 4;   // this pull only
        emit message("Extrude: " + extrudeOpName(m_ppOpNow) + " (this pull)");
        emitHint();
        update();
        return;
    }
    if (plain && m_tool == Tool::Move && k == Qt::Key_K) {
        setMoveCopy(!m_moveCopy);
        return;
    }
    // Mirror across the plane through the picked point (or the snap point under the cursor) square to X / Y / Z
    if (plain && m_tool == Tool::Mirror && m_mirrorStage >= 1 && (k == Qt::Key_X || k == Qt::Key_Y || k == Qt::Key_Z)) {
        const bool have = !m_pending.isEmpty() || m_cur.valid;
        if (have) {
            const QVector3D at = m_pending.isEmpty() ? m_cur.pt : m_pending.first();
            m_pending.clear();
            mirrorPlane(at, k == Qt::Key_X ? QVector3D(1, 0, 0) : k == Qt::Key_Y ? QVector3D(0, 1, 0) : QVector3D(0, 0, 1), m_mirrorKeep);
            setTool(Tool::Select);
        }
        return;
    }
    if (plain && m_tool == Tool::Mirror && k == Qt::Key_K) {
        setMirrorKeep(!m_mirrorKeep);
        emitHint();
        return;
    }

    switch (k) {
    case Qt::Key_Backspace:
        if (dynTyping()) {
            m_dyn[m_dynIdx].text.chop(1);
            emitHint();
            update();
        } else {
            deleteSelection();
        }
        return;
    case Qt::Key_Delete:
        deleteSelection();
        return;
    case Qt::Key_Escape:
        cancelPending();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (dynTyping()) {
            applyNumeric();
            return;
        }
        if (m_gzActive >= 0) {
            gzCommit();
            return;
        }
        if (m_tool == Tool::Box && m_pending.size() >= 2) {   // Enter = the default board thickness
            finishBox(m_boxHeight * m_boxOutSign);
            return;
        }
        if (m_tool == Tool::PressPull && m_ppItem) {
            pressPullCommit();
            return;
        }
        if (m_tool == Tool::Rotate && m_r3Stage == 0) {
            if (hasSelection()) { m_r3Stage = 1; emitHint(); update(); }
            else emit message("Rotate: pick at least one object first");
            return;
        }
        if (m_tool == Tool::Rotate && m_r3Stage == 2) {
            r3Apply(m_r3Angle);
            if (!m_moveCopy)
                setTool(Tool::Select);
            return;
        }
        if (m_tool == Tool::Move && m_moveStage == 0) {
            if (hasSelection()) {
                m_moveStage = 1;
                emitHint();
                update();
            } else {
                emit message("Move: pick at least one object first");
            }
            return;
        }
        if (m_tool == Tool::Mirror && m_mirrorStage == 0) {
            if (hasSelection()) {
                m_mirrorStage = 1;
                emitHint();
                update();
            } else {
                emit message("Mirror: pick at least one object first");
            }
            return;
        }
        if (m_tool == Tool::Polyline) {
            finishPending();
            return;
        }
        if (m_tool == Tool::Select && m_pending.isEmpty() && m_lastTool != Tool::Select) {
            setTool(m_lastTool);                      // Enter repeats the previous command
            return;
        }
        break;
    case Qt::Key_X:
    case Qt::Key_Y:
    case Qt::Key_Z:
        if (plain) {
            const int want = k == Qt::Key_X ? 1 : (k == Qt::Key_Y ? 2 : 3);
            const bool can = (drawingTool() && lockAllowed() && !m_pending.isEmpty()) || m_dragging;
            if (!can) {
                emit message("Axis lock works after the first point of a Line / Polyline / Curve / "
                             "Arc, or while dragging");
                return;
            }
            m_axisLock = (m_axisLock == want) ? 0 : want;
            emit message(m_axisLock ? QString("Locked to %1 axis (press again to unlock)")
                                          .arg(QChar("XYZ"[m_axisLock - 1]))
                                    : QString("Axis lock off"));
            emitSnapState();
            if (!m_lastMouse.isNull()) {
                m_cur = snapCursor(m_lastMouse, m_dragging ? &m_dragStart : nullptr);
            }
            update();
            return;
        }
        break;
    case Qt::Key_Tab:
        cyclePlane();
        return;
    case Qt::Key_O:
        if (plain && m_cur.valid) {
            const Frame fr = planeFrame();
            setPlaneOffset(QVector3D::dotProduct(m_cur.pt, fr.N));
            emit message(QString("Sketch plane moved through the cursor point (offset %1)")
                             .arg(Units::format(m_planeOffset)));
            return;
        }
        break;
    case Qt::Key_F:
    case Qt::Key_Home:
        if (plain) {
            zoomFitSelection();
            return;
        }
        break;
    case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down:
    case Qt::Key_PageUp: case Qt::Key_PageDown:
        if (hasSelection()) {
            const Frame fr = planeFrame();
            const float st = float(m_gridSize * ((e->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0));
            QVector3D d;
            if (k == Qt::Key_Left)       d = -fr.U * st;
            else if (k == Qt::Key_Right) d = fr.U * st;
            else if (k == Qt::Key_Up)    d = fr.V * st;
            else if (k == Qt::Key_Down)  d = -fr.V * st;
            else if (k == Qt::Key_PageUp) d = fr.N * st;
            else                          d = -fr.N * st;
            moveSelectionBy(d);
            return;
        }
        break;
    default:
        break;
    }
    QWidget::keyPressEvent(e);
}

void Canvas::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Shift && !m_lastMouse.isNull()) {
        m_cur = snapCursor(m_lastMouse, m_dragging ? &m_dragStart : nullptr);
        update();
    }
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceDown = false;
        if (!m_panning)
            setCursor(cursorFor(m_tool));
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void Canvas::cancelPending()
{
    if (m_editor) {
        applyInlineEdit();
        return;
    }
    if (m_pickCb) {
        m_pickCb = nullptr;
        emit message("Pick cancelled");
    } else if (dynTyping()) {
        dynClear();
    } else if (m_axisLock) {
        m_axisLock = 0;
        emitSnapState();
    } else if (m_gzActive >= 0) {
        gzCancel();
    } else if (m_tool == Tool::PressPull && m_ppItem) {
        pressPullCancel();
    } else if (m_tool == Tool::Box && !m_pending.isEmpty()) {
        m_pending.clear();
        boxRestorePlane();
        emit message("Box cancelled");
    } else if (m_tool == Tool::Offset && !m_offsetTyped.isEmpty()) {
        m_offsetTyped.clear();
        updateOffsetPreview();
    } else if (m_tool == Tool::Offset && offsetSourceValid()) {
        m_offsetSrc = nullptr;
        m_ghosts.clear();
        clearSelection();
        emit message("Offset: pick another object");
    } else if (!m_pending.isEmpty()) {
        m_pending.clear();
        m_guides.clear();
        boxRestorePlane();
        emit message("Cancelled");
    } else if (m_joinFirst) {
        m_joinFirst = nullptr;
        emit message("Join cancelled");
    } else if (m_editItem) {
        m_editItem = nullptr;
        m_editIndex = -1;
    } else if (m_tool != Tool::Select) {
        setTool(Tool::Select);                    // Esc on an idle command ends it
    } else {
        clearSelection();
    }
    emitHint();
    update();
}

// ---------------------------------------------------------------------------
// right-click menu
// ---------------------------------------------------------------------------
void Canvas::showContextMenu(const QPoint &global)
{
    QMenu menu(this);
    if (m_tool == Tool::Polyline && m_pending.size() >= 2)
        menu.addAction("Finish polyline", this, [this] { finishPending(); });
    if (!m_pending.isEmpty() || dynTyping())
        menu.addAction("Cancel current operation", this, [this] { cancelPending(); });
    if (m_axisLock)
        menu.addAction("Release axis lock", this, [this] { m_axisLock = 0; emitSnapState(); update(); });
    if (!menu.isEmpty())
        menu.addSeparator();

    menu.addAction("Select tool", this, [this] { setTool(Tool::Select); });
    if (hasSelection()) {
        menu.addAction("Duplicate", this, [this] { duplicateSelection(); });
        menu.addAction("Copy", this, [this] { copySelection(); });
        menu.addAction("Delete", this, [this] { deleteSelection(); });
        menu.addAction("Zoom to selection", this, [this] { zoomFitSelection(); });
        if (m_selected.size() > 1)
            menu.addAction("Group  (Ctrl+G)", this, [this] { groupSelection(); });
        if (selectionHasGroup())
            menu.addAction("Ungroup  (Ctrl+Shift+G)", this, [this] { ungroupSelection(); });
        menu.addAction("Move...  (M)", this, [this] { setTool(Tool::Move); });
        menu.addAction("Explode  (Ctrl+Shift+X)", this, [this] { explodeSelection(); });
        QMenu *pen = menu.addMenu("Line colour / thickness / type");
        const struct { const char *n; QColor c; } cols[] = {
            {"Black", QColor(30, 30, 30)}, {"Red", QColor(220, 50, 47)}, {"Blue", QColor(38, 110, 220)},
            {"Green", QColor(40, 160, 70)}, {"Orange", QColor(240, 140, 30)}, {"Magenta", QColor(200, 60, 180)},
            {"Cyan", QColor(30, 180, 200)}, {"Grey", QColor(130, 130, 130)}};
        QMenu *cm = pen->addMenu("Colour");
        for (const auto &c : cols) {
            const QColor cc = c.c;
            QPixmap pm(14, 14);
            pm.fill(cc);
            cm->addAction(QIcon(pm), c.n, this, [this, cc] { setSelectedColor(cc); emit selectionChanged(); });
        }
        QMenu *wm = pen->addMenu("Thickness");
        for (double w : {0.13, 0.18, 0.25, 0.35, 0.5, 0.7, 1.0, 1.4, 2.0}) {
            QAction *a = wm->addAction(QString::number(w, 'g', 3) + " mm", this, [this, w] { setSelectedWidth(w); emit selectionChanged(); });
            a->setCheckable(true);
            a->setChecked(!m_selected.isEmpty() && std::fabs(m_selected.first()->width - w) < 1e-6);
        }
        QMenu *tm = pen->addMenu("Line type");
        const char *tn[] = {"Solid", "Dash", "Dot", "Dash-dot", "Centre", "Hidden"};
        for (int k = 0; k < 6; ++k) {
            QAction *a = tm->addAction(tn[k], this, [this, k] { setSelectedLineStyle(k); emit selectionChanged(); });
            a->setCheckable(true);
            a->setChecked(!m_selected.isEmpty() && m_selected.first()->lineStyle == k);
        }
        if (selectedText())
            menu.addAction("Edit text  (F2)", this, [this] { editSelectedText(); });
    }
    if (hasClipboard())
        menu.addAction("Paste", this, [this] { pasteClipboard(); });
    menu.addSeparator();

    QMenu *vm = menu.addMenu("Viewport");
    const struct { const char *n; View v; } views[] = {
        {"Top", View::Top}, {"Bottom", View::Bottom}, {"Front", View::Front}, {"Back", View::Back},
        {"Left", View::Side}, {"Right", View::Right}, {"ISO 3D", View::Iso}};
    for (const auto &v : views) {
        const View vv = v.v;
        vm->addAction(v.n, this, [this, vv] { setView(vv); });
    }
    QMenu *pm = menu.addMenu("Sketch plane");
    const struct { const char *n; Plane p; } planes[] = {
        {"XY (Top)", Plane::TopXY}, {"XZ", Plane::XZ}, {"ZY", Plane::ZY}, {"YX", Plane::YX}};
    for (const auto &p : planes) {
        const Plane pp = p.p;
        pm->addAction(p.n, this, [this, pp] { setPlane(pp); });
    }
    QAction *persp = menu.addAction("Perspective camera (ISO view)");
    persp->setCheckable(true);
    persp->setChecked(m_set.perspective);
    connect(persp, &QAction::toggled, this, [this](bool on) {
        ViewSettings s = m_set;
        s.perspective = on;
        setSettings(s);
    });
    QAction *free = menu.addAction("Free orbit (any axis, 360\xC2\xB0)");
    free->setCheckable(true);
    free->setChecked(m_set.freeOrbit);
    connect(free, &QAction::toggled, this, [this](bool on) {
        ViewSettings s = m_set;
        s.freeOrbit = on;
        setSettings(s);
    });
    menu.addAction("Zoom to fit", this, [this] { zoomFit(); });
    menu.exec(global);
}

// right-click on the view cube (Fusion style)
void Canvas::showCubeMenu(const QPoint &global)
{
    QMenu menu(this);
    menu.addAction("Home", this, [this] { goHome(true); });
    menu.addAction("Set current view as Home", this, [this] { setCurrentViewAsHome(); });
    QAction *rh = menu.addAction("Reset Home", this, [this] { resetHome(); });
    rh->setEnabled(m_hasHome);
    menu.addSeparator();
    QMenu *as = menu.addMenu("Set current view as");
    const struct { const char *n; View v; } views[] = {
        {"Top", View::Top}, {"Front", View::Front}, {"Left", View::Side},
        {"Right", View::Right}, {"Back", View::Back}, {"Bottom", View::Bottom}};
    for (const auto &v : views) {
        const View vv = v.v;
        as->addAction(v.n, this, [this, vv] { setCurrentViewAs(vv); });
    }
    QAction *ro = menu.addAction("Reset view orientation (world Top / Front)", this, [this] { resetViewOrientation(); });
    ro->setEnabled(customOrientation());
    menu.addSeparator();
    menu.addAction("Roll view 90\xC2\xB0 left", this, [this] { rollView(90); });
    menu.addAction("Roll view 90\xC2\xB0 right", this, [this] { rollView(-90); });
    menu.addSeparator();
    QAction *free = menu.addAction("Free orbit (any axis, 360\xC2\xB0)");
    free->setCheckable(true);
    free->setChecked(m_set.freeOrbit);
    connect(free, &QAction::toggled, this, [this](bool on) {
        ViewSettings s = m_set;
        s.freeOrbit = on;
        setSettings(s);
        emit message(on ? "Free orbit: right-drag turns the model about any axis, over the top too (Ctrl = roll)"
                        : "Turntable orbit: the Top direction stays up");
    });
    QAction *persp = menu.addAction("Perspective camera");
    persp->setCheckable(true);
    persp->setChecked(m_set.perspective);
    connect(persp, &QAction::toggled, this, [this](bool on) {
        ViewSettings s = m_set;
        s.perspective = on;
        setSettings(s);
    });
    menu.exec(global);
}

void Canvas::setDimMode(int m)
{
    m_dimMode = std::clamp(m, 0, 2);
    if (m_tool == Tool::Dimension && m_pending.size() < 2)
        ;                                         // applies to the next dimension
    emit dimModeChanged(m_dimMode);
    emit message(QString("Dimension mode: %1").arg(m_dimMode == 1 ? "horizontal" : (m_dimMode == 2 ? "vertical" : "aligned")));
    emitHint();
    update();
}

void Canvas::setCutMode(int m)
{
    m_cutMode = std::clamp(m, 0, 1);
    m_ghosts.clear();
    m_marks.clear();
    emit cutModeChanged(m_cutMode);
    emit message(m_cutMode == 0 ? "Cut mode: View - cuts exactly where shapes cross on screen, in any plane"
                                : "Cut mode: True 3D - cuts only where shapes really touch in 3D space");
    emitHint();
    update();
}

void Canvas::setBladeAction(int a)
{
    m_bladeAction = std::clamp(a, 0, 1);
    m_pending.clear();
    emit bladeActionChanged(m_bladeAction);
    emitHint();
    update();
}

// While dragging, snap any vertex / midpoint / centre of the moved objects onto a static snap point
// (screen tolerance 10 px). This makes multi-object moves land exactly where intended.
bool Canvas::dragMagnet(QVector3D &delta)
{
    QList<SnapCand> mov, stat;
    for (auto it = m_dragData.constBegin(); it != m_dragData.constEnd(); ++it)
        itemSnapPoints(it.key(), mov);
    collectSnapPoints(stat);
    if (mov.isEmpty() || stat.isEmpty())
        return false;
    if (mov.size() > 400)
        mov.resize(400);
    QVector<QPointF> ss;
    QVector<char> sok;
    ss.reserve(stat.size());
    for (const SnapCand &s : std::as_const(stat)) {
        QPointF q;
        sok.append(projectD(s.p, q));
        ss.append(q);
    }
    qreal best = 10.0;
    bool found = false;
    QVector3D bestFix;
    for (const SnapCand &m : std::as_const(mov)) {
        QPointF mp;
        if (!projectD(m.p, mp))
            continue;
        for (int i = 0; i < stat.size(); ++i) {
            if (!sok[i])
                continue;
            const qreal dx = ss[i].x() - mp.x();
            if (dx > best || dx < -best)
                continue;
            const qreal d = std::hypot(dx, ss[i].y() - mp.y());
            if (d < best) {
                best = d;
                bestFix = stat[i].p - m.p;
                m_magnet = stat[i].p;
                found = true;
            }
        }
    }
    if (!found)
        return false;
    delta += bestFix;
    m_magnetOn = true;
    return true;
}
