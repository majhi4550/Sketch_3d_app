#include "canvas.h"
#include "canvas_util.h"
#include "units.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QQuaternion>

#include <algorithm>
#include <cmath>
#include <memory>

using V3 = QVector3D;

// ===========================================================================
// Rotate tool (Shift+R), like AutoCAD ROTATE / 3DROTATE: pick the objects, a base point, then the
// angle - by the mouse (Shift = 15 degree steps, F8 ortho = 90, F10 polar = its step) or typed.
// X / Y / Z turn about that world axis instead of the sketch plane's normal; K leaves a copy.
// Plus rotateSelection3D for the quick 90 degree turns. Boards keep their thickness direction.
// ===========================================================================
static const V3 kWorldAxis[3] = {V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1)};

V3 Canvas::r3AxisVec() const
{
    return m_r3Axis >= 0 && m_r3Axis < 3 ? kWorldAxis[m_r3Axis] : planeFrame().N;
}

// angle (degrees) of the cursor around the axis through the base point, from the plane's first axis
double Canvas::r3AngleAt(const QPoint &s) const
{
    const V3 ax = r3AxisVec();
    V3 b1 = m_r3Axis >= 0 && m_r3Axis < 3 ? kWorldAxis[(m_r3Axis + 1) % 3] : planeFrame().U;
    b1 = (b1 - ax * V3::dotProduct(b1, ax)).normalized();
    const V3 b2 = V3::crossProduct(ax, b1);
    V3 ro, rd, hit;
    screenRay(QPointF(s), ro, rd);
    if (rayPlane(ro, rd, m_r3Base, ax, hit)) {
        const V3 w = hit - m_r3Base;
        if (w.length() < 1e-9f)
            return m_r3Angle;
        return std::atan2(V3::dotProduct(w, b2), V3::dotProduct(w, b1)) * 180.0 / PI;
    }
    const QPointF c = project(m_r3Base), d = QPointF(s) - c;   // axis seen end-on is handled above; edge-on here
    return (V3::dotProduct(ax, m_bf) < 0 ? 1.0 : -1.0) * std::atan2(-d.y(), d.x()) * 180.0 / PI;
}

double Canvas::r3SnapAngle(double a) const
{
    const bool shift = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
    if (m_set.ortho != shift && !shift)
        return std::round(a / 90.0) * 90.0;
    if (shift)
        return std::round(a / 15.0) * 15.0;
    if (m_set.polar && m_set.polarStep > 0) {
        const double r = std::round(a / m_set.polarStep) * m_set.polarStep;
        if (std::fabs(r - a) < 4.0)
            return r;
    }
    return std::round(a);
}

// text turned about its own plane's normal keeps reading the way the geometry turned
std::function<void(SketchItem *)> Canvas::textTurn(const V3 &axis, double deg) const
{
    return [this, axis, deg](SketchItem *n) {
        if (n->type() != SketchItem::Text || n->pts.isEmpty())
            return;
        const Frame fr = frameFor(std::max(0, n->plane));
        const float along = V3::dotProduct(axis, V3::crossProduct(fr.U, fr.V));
        if (std::fabs(along) < 0.999f)
            return;
        double sign = 1;
        QPointF s0, s1, s2;
        const V3 p = n->pts[0];
        if (projectD(p, s0) && projectD(p + fr.U, s1) && projectD(p + fr.V, s2)) {
            const QPointF a1 = s1 - s0, a2 = s2 - s0;
            sign = (a1.x() * a2.y() - a1.y() * a2.x()) < 0 ? 1 : -1;
        }
        n->textAngle += deg * sign * (along > 0 ? 1 : -1);
    };
}

void Canvas::rotateSelection3D(double deg, int axis)
{
    if (m_selected.isEmpty()) {
        emit message("Rotate: select the objects first");
        return;
    }
    if (std::fabs(deg) < 1e-9)
        return;
    const V3 ax = axis >= 0 && axis < 3 ? kWorldAxis[axis] : planeFrame().N;
    const V3 c = selectionCenter();
    const QQuaternion q = QQuaternion::fromAxisAndAngle(ax, float(deg));
    applyXformSelection([q, c](const V3 &p) { return c + q.rotatedVector(p - c); }, "Rotate", textTurn(ax, deg));
    static const char *names[] = {"X", "Y", "Z", "the sketch plane's normal"};
    emit message(QString("Rotated %1° about %2").arg(deg, 0, 'g', 6).arg(names[std::clamp(axis, 0, 3)]));
}

void Canvas::r3Apply(double deg)
{
    if (m_selected.isEmpty() || !std::isfinite(deg) || std::fabs(deg) < 1e-9) {
        emit message("Rotate: no turn");
        return;
    }
    const V3 ax = r3AxisVec(), c = m_r3Base;
    const QQuaternion q = QQuaternion::fromAxisAndAngle(ax, float(deg));
    auto f = [q, c](const V3 &p) { return c + q.rotatedVector(p - c); };
    const auto post = textTurn(ax, deg);
    if (m_moveCopy) {                                     // K: the original stays, the copy is turned
        snapshotBegin();
        QList<SketchItem *> made;
        for (SketchItem *it : std::as_const(m_selected)) {
            SketchItem *n = xformClone(it, f);
            post(n);
            m_items.append(n);
            made << n;
        }
        regroup(made);
        for (SketchItem *it : std::as_const(m_selected))
            it->selected = false;
        m_selected.clear();
        for (SketchItem *n : std::as_const(made)) {
            n->selected = true;
            m_selected << n;
        }
        snapshotCommit("Rotate Copy");
        emit selectionChanged();
    } else {
        applyXformSelection(f, "Rotate", post);
    }
    emit message(QString("Rotated %1°%2").arg(deg, 0, 'g', 6).arg(m_moveCopy ? " (copy)" : ""));
}

void Canvas::drawRotateTool(QPainter &p) const
{
    if (m_tool != Tool::Rotate || m_r3Stage < 2)
        return;
    double deg = m_r3Angle, v = 0;
    const bool typed = dynTyping() && dynValue(0, v);
    if (typed)
        deg = v;
    const V3 ax = r3AxisVec(), c = m_r3Base;
    const QQuaternion q = QQuaternion::fromAxisAndAngle(ax, float(deg));
    for (const SketchItem *it : m_selected) {
        std::unique_ptr<SketchItem> g(xformClone(it, [&](const V3 &pt) { return c + q.rotatedVector(pt - c); }));
        drawItem(p, g.get(), true);
    }
    QPointF sc;
    if (!projectD(c, sc))
        return;
    const double L = 90.0 / std::max(1e-6, double(scaleAt(c)));
    static const QColor axisCol[4] = {QColor(235, 85, 85), QColor(80, 200, 110), QColor(80, 145, 255), QColor(53, 224, 200)};
    const QColor col = axisCol[std::clamp(m_r3Axis, 0, 3)];
    QLineF l;
    if (projectSeg(c - ax * float(L), c + ax * float(L), l)) {          // the axis
        p.setPen(QPen(col, 2.0, Qt::DashDotLine));
        p.drawLine(l);
    }
    // the swept angle as an arc from the reference direction
    V3 b1 = m_r3Axis >= 0 && m_r3Axis < 3 ? kWorldAxis[(m_r3Axis + 1) % 3] : planeFrame().U;
    b1 = (b1 - ax * V3::dotProduct(b1, ax)).normalized();
    const V3 b2 = V3::crossProduct(ax, b1);
    QPolygonF arc;
    const int steps = std::max(2, int(std::fabs(deg) / 4));
    for (int k = 0; k <= steps; ++k) {
        const double t = deg * PI / 180.0 * k / steps;
        QPointF sp;
        if (projectD(c + (b1 * float(std::cos(t)) + b2 * float(std::sin(t))) * float(L * 0.6), sp))
            arc << sp;
    }
    QPointF r0, r1;
    p.setPen(QPen(QColor(255, 209, 102), 1.2, Qt::DashLine));
    if (projectD(c + b1 * float(L * 0.75), r0))
        p.drawLine(sc, r0);
    const double rad = deg * PI / 180.0;
    if (projectD(c + (b1 * float(std::cos(rad)) + b2 * float(std::sin(rad))) * float(L * 0.75), r1))
        p.drawLine(sc, r1);
    p.setPen(QPen(QColor(255, 209, 102), 2.0));
    p.drawPolyline(arc);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 209, 102));
    p.drawEllipse(sc, 4.0, 4.0);
    p.setBrush(Qt::NoBrush);

    QFont fnt = QApplication::font();
    fnt.setPixelSize(12);
    fnt.setBold(true);
    p.setFont(fnt);
    const QFontMetricsF fm(fnt);
    static const char *an[] = {"X", "Y", "Z", "plane"};
    const QString txt = QString::fromUtf8("\xE2\x88\xA0 ") + (dynTyping() ? dynShown(0, true) : QString("%1°").arg(deg, 0, 'g', 6)) +
                        QString("  about %1%2").arg(an[std::clamp(m_r3Axis, 0, 3)], m_moveCopy ? "  +copy" : "");
    const QPointF at = QPointF(m_lastMouse) + QPointF(22, -26);
    const QRectF r(at.x(), at.y() - 11, fm.horizontalAdvance(txt) + 14, 22);
    p.setPen(QPen(dynTyping() ? QColor(255, 209, 102) : accent(), dynTyping() ? 2 : 1));
    p.setBrush(dynTyping() ? QColor(66, 52, 18, 235) : QColor(12, 18, 28, 225));
    p.drawRoundedRect(r, 5, 5);
    p.setPen(dynTyping() ? QColor(255, 224, 150) : QColor(215, 230, 250));
    p.drawText(r, Qt::AlignCenter, txt);
    p.setBrush(Qt::NoBrush);
}
