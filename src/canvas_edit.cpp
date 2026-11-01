#include "canvas.h"
#include "canvas_util.h"
#include "units.h"

#include <QQuaternion>

#include <algorithm>
#include <cmath>

using V3 = QVector3D;

// ===========================================================================
// dynamic input (the size fields that follow the cursor)
// ===========================================================================
int Canvas::dynFieldCount() const
{
    if (m_set.dynInput && m_tool == Tool::PressPull)
        return m_ppItem ? 1 : 0;                           // the pull distance
    if (m_tool == Tool::Rotate)
        return (m_set.dynInput && m_r3Stage == 2) ? 1 : 0;   // the angle
    if (m_tool == Tool::Fillet || m_tool == Tool::Chamfer || m_tool == Tool::Shell || m_tool == Tool::Revolve ||
        m_tool == Tool::Pipe)
        return 1;                                          // the radius / distance, typed at any time
    if (m_set.dynInput && m_gzActive >= 0)
        return (m_gizmoMode == GizmoMode::Rotate || m_gzActive < 3) ? 1 : 0;   // gizmo distance / angle
    if (!m_set.dynInput || m_pending.isEmpty())
        return 0;
    switch (m_tool) {
    case Tool::Box:
        return m_pending.size() >= 2 ? 1 : 2;              // length + width, then the height
    case Tool::Line: case Tool::Polyline: case Tool::Curve3: case Tool::Arc: case Tool::Rect: case Tool::Move:
        return 2;
    case Tool::Dimension:
        return m_pending.size() < 2 ? 2 : 0;
    case Tool::Circle:
        return 1;
    default:
        return 0;
    }
}

// the second field of the line-type tools is an angle; every other field is a length in the current units
bool Canvas::dynIsAngle(int i) const
{
    return (i == 1 && m_tool != Tool::Rect && m_tool != Tool::Box) ||
           (m_gzActive >= 0 && m_gizmoMode == GizmoMode::Rotate) || m_tool == Tool::Rotate || m_tool == Tool::Revolve;
}

// both kinds take a sum as well: 800-2*18, (2400-3*18)/4, 360/7
bool Canvas::dynValue(int i, double &v) const
{
    return dynIsAngle(i) ? Units::parseNumber(m_dyn[i].text, v) : Units::parse(m_dyn[i].text, v);
}

QString Canvas::dynShown(int i, bool caret) const
{
    QString s = m_dyn[i].text + (caret ? "|" : "");
    double v = 0;
    if (Units::isExpression(m_dyn[i].text) && dynValue(i, v))
        s += " = " + (dynIsAngle(i) ? QString("%1\xC2\xB0").arg(v, 0, 'g', 6) : Units::formatDim(v));
    return s;
}

void Canvas::dynNext()
{
    const int n = dynFieldCount();
    if (n > 1)
        m_dynIdx = (m_dynIdx + 1) % n;
}

// cursor position with the typed values applied (length / angle, W / H, radius)
V3 Canvas::numericPoint(const V3 &cursor) const
{
    if (m_pending.isEmpty() || !dynActive() || !dynTyping())
        return cursor;
    const Frame fr = planeFrame();
    const V3 anchor = m_pending.last();
    V3 d = cursor - anchor;
    d -= fr.N * V3::dotProduct(d, fr.N);
    double v0 = 0, v1 = 0;
    const bool s0 = dynValue(0, v0), s1 = dynValue(1, v1);

    if (m_tool == Tool::Rect || m_tool == Tool::Box) {
        const double du = V3::dotProduct(d, fr.U), dv = V3::dotProduct(d, fr.V);
        const double w = s0 ? std::fabs(v0) : std::fabs(du), h = s1 ? std::fabs(v1) : std::fabs(dv);
        return anchor + fr.U * float(w * (du < 0 ? -1 : 1)) + fr.V * float(h * (dv < 0 ? -1 : 1));
    }
    if (m_tool == Tool::Circle) {
        if (!s0)
            return cursor;
        const V3 dir = d.length() > 1e-6 ? d.normalized() : fr.U;
        return anchor + dir * float(std::fabs(v0));
    }
    if (m_axisLock) {
        if (!s0)
            return cursor;
        const V3 axis = m_axisLock == 1 ? V3(1, 0, 0) : (m_axisLock == 2 ? V3(0, 1, 0) : V3(0, 0, 1));
        return anchor + axis * float(std::fabs(v0) * (V3::dotProduct(cursor - anchor, axis) < 0 ? -1 : 1));
    }
    if (!s0 && !s1)
        return cursor;
    const double L = s0 ? v0 : d.length();
    const double A = s1 ? v1 * PI / 180.0 : std::atan2(V3::dotProduct(d, fr.V), V3::dotProduct(d, fr.U));
    return anchor + (fr.U * float(std::cos(A)) + fr.V * float(std::sin(A))) * float(L);
}

void Canvas::pickPoint(std::function<void(const V3 &)> cb, const QString &prompt)
{
    m_pickCb = std::move(cb);
    emit message(prompt);
    emit hint(prompt + "  (Esc cancels)");
    setFocus();
    update();
}

// ===========================================================================
// dimensions, measured and offset inside the sketch plane
// ===========================================================================
Canvas::DimGeom Canvas::dimGeom(const SketchItem *it) const
{
    DimGeom g;
    if (it->pts.size() < 3)
        return g;
    const Frame fr = frameFor(std::max(0, it->plane));
    const V3 a = it->pts[0], b = it->pts[1], o = it->pts[2];
    V3 d = b - a;
    d -= fr.N * V3::dotProduct(d, fr.N);
    if (it->dimMode == 1) {
        g.value = std::fabs(V3::dotProduct(d, fr.U));
        g.d1 = a + fr.V * V3::dotProduct(o - a, fr.V);
        g.d2 = b + fr.V * V3::dotProduct(o - b, fr.V);
    } else if (it->dimMode == 2) {
        g.value = std::fabs(V3::dotProduct(d, fr.V));
        g.d1 = a + fr.U * V3::dotProduct(o - a, fr.U);
        g.d2 = b + fr.U * V3::dotProduct(o - b, fr.U);
    } else {
        g.value = d.length();
        if (g.value < 1e-9)
            return g;
        const V3 n = V3::crossProduct(fr.N, d / float(g.value));
        const V3 off = n * V3::dotProduct(o - a, n);
        g.d1 = a + off;
        g.d2 = b + off;
    }
    g.ok = g.value > 1e-9 || it->dimMode == 0;
    return g;
}

// ===========================================================================
// transforms that return new items (Rect stays a Rect only while axis-aligned)
// ===========================================================================
V3 Canvas::itemCenter(const SketchItem *it) const
{
    V3 mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
    bool any = false;
    auto add = [&](const V3 &q) {
        mn = V3(std::min(mn.x(), q.x()), std::min(mn.y(), q.y()), std::min(mn.z(), q.z()));
        mx = V3(std::max(mx.x(), q.x()), std::max(mx.y(), q.y()), std::max(mx.z(), q.z()));
        any = true;
    };
    const auto ss = strips(it);
    if (ss.isEmpty())
        for (const V3 &q : it->pts) add(q);
    for (const auto &s : ss)
        for (const V3 &q : s) add(q);
    return any ? (mn + mx) * 0.5f : V3();
}

SketchItem *Canvas::xformClone(const SketchItem *it, const std::function<V3(const V3 &)> &f) const
{
    const Frame fr = frameFor(std::max(0, it->plane));
    SketchItem *n = nullptr;
    if (it->type() == SketchItem::Rect) {
        const QVector<V3> c = rectCorners(it);
        if (c.size() == 4) {
            const V3 t[4] = {f(c[0]), f(c[1]), f(c[2]), f(c[3])};
            V3 e = t[1] - t[0];
            if (e.length() < 1e-9f)
                e = t[3] - t[0];
            const float len = e.length();
            V3 nn = V3::crossProduct(t[1] - t[0], t[3] - t[0]);
            // stays a rectangle while it lies in a sketch plane with its sides along that plane's axes
            int plane = -1;
            if (len > 1e-9f && nn.lengthSquared() > 1e-12f) {
                nn.normalize();
                for (int cand : {std::max(0, it->plane), 0, 1, 2, 3}) {
                    const Frame g = frameFor(cand);
                    if (std::fabs(V3::dotProduct(nn, g.N)) > 0.999f &&
                        (std::fabs(V3::dotProduct(e, g.V)) < 1e-3f * len || std::fabs(V3::dotProduct(e, g.U)) < 1e-3f * len)) {
                        plane = cand;
                        break;
                    }
                }
            }
            QJsonObject o = it->toJson();                      // every property comes along (fill, name, body ...)
            if (plane < 0)
                o["type"] = "polyline";
            n = new SketchItem;
            n->fromJson(o);
            n->plane = plane >= 0 ? plane : it->plane;
            if (plane >= 0) n->pts = {t[0], t[2]};
            else n->pts = {t[0], t[1], t[2], t[3], t[0]};
        }
    }
    if (!n) {
        n = it->clone();
        for (V3 &p : n->pts)
            p = f(p);
    }
    if (!it->extrude.isNull() && !it->pts.isEmpty())           // a body keeps its thickness direction
        n->extrude = f(it->pts[0] + it->extrude) - f(it->pts[0]);
    // circles / polylines turned onto another sketch plane draw in that plane from now on
    if ((it->type() == SketchItem::Circle || it->type() == SketchItem::Polyline) && !it->pts.isEmpty()) {
        V3 nn = f(it->pts[0] + fr.N) - f(it->pts[0]);
        if (nn.lengthSquared() > 1e-12f && std::fabs(V3::dotProduct(nn.normalized(), fr.N)) < 0.999f)
            for (int cand = 0; cand < 3; ++cand)
                if (std::fabs(V3::dotProduct(nn.normalized(), frameFor(cand).N)) > 0.999f) {
                    n->plane = cand;
                    break;
                }
    }
    if (it->type() == SketchItem::Dimension && it->dimMode != 0) {
        const V3 e = f(it->pts[0] + fr.U) - f(it->pts[0]);
        const V3 e2 = f(it->pts[0] + fr.V) - f(it->pts[0]);
        const bool keeps = std::fabs(V3::dotProduct(e, fr.U)) > 0.999f * e.length() &&
                           std::fabs(V3::dotProduct(e2, fr.V)) > 0.999f * e2.length();
        if (!keeps)
            n->dimMode = 0;
    }
    return n;
}

static void adoptAll(QList<SketchItem *> &items, QList<SketchItem *> &selected,
                     const QList<SketchItem *> &made, bool keepOldSelected)
{
    if (!keepOldSelected) {
        for (SketchItem *o : std::as_const(selected)) o->selected = false;
        selected.clear();
    }
    for (SketchItem *t : made) {
        items.append(t);
        t->selected = true;
        selected.append(t);
    }
}

// ===========================================================================
// MIRROR - reflect about a line picked in the sketch plane
// ===========================================================================
void Canvas::mirrorAcross(const V3 &p1, const V3 &p2, bool keep)
{
    if (m_selected.isEmpty())
        return;
    const Frame fr = planeFrame();
    V3 d = p2 - p1;
    d -= fr.N * V3::dotProduct(d, fr.N);
    if (d.length() < 1e-6f) {
        emit message("Mirror: pick two different points for the mirror line");
        return;
    }
    d.normalize();
    const V3 m = V3::crossProduct(fr.N, d).normalized();
    auto f = [&](const V3 &p) { return p - m * float(2.0 * V3::dotProduct(p - p1, m)); };
    const QList<SketchItem *> src = m_selected;
    QList<SketchItem *> made;
    snapshotBegin();
    for (const SketchItem *it : src)
        made << xformClone(it, f);
    if (!keep) {
        for (SketchItem *it : src) {
            m_items.removeAll(it);
            if (m_hover == it) m_hover = nullptr;
            delete it;
        }
        m_selected.clear();
    }
    adoptAll(m_items, m_selected, made, false);
    regroup(made);
    snapshotCommit("Mirror");
    emit selectionChanged();
    emit message(QString("Mirrored %1 object(s)%2").arg(made.size()).arg(keep ? "" : " (original removed)"));
    update();
}

// mirror the selection across the plane through 'point' square to 'normal' (X / Y / Z in the Mirror tool)
void Canvas::mirrorPlane(const V3 &point, const V3 &normal, bool keep)
{
    if (m_selected.isEmpty() || normal.isNull())
        return;
    const V3 m = normal.normalized();
    auto f = [&](const V3 &p) { return p - m * float(2.0 * V3::dotProduct(p - point, m)); };
    const QList<SketchItem *> src = m_selected;
    QList<SketchItem *> made;
    snapshotBegin();
    for (const SketchItem *it : src)
        made << xformClone(it, f);
    if (!keep) {
        for (SketchItem *it : src) {
            m_items.removeAll(it);
            if (m_hover == it) m_hover = nullptr;
            delete it;
        }
        m_selected.clear();
    }
    adoptAll(m_items, m_selected, made, false);
    regroup(made);
    snapshotCommit("Mirror");
    emit selectionChanged();
    emit message(QString("Mirrored %1 object(s) across the plane through (%2, %3, %4) square to %5%6")
                     .arg(made.size()).arg(point.x(), 0, 'f', 1).arg(point.y(), 0, 'f', 1).arg(point.z(), 0, 'f', 1)
                     .arg(std::fabs(m.x()) > 0.9f ? "X" : std::fabs(m.y()) > 0.9f ? "Y" : std::fabs(m.z()) > 0.9f ? "Z" : "the normal")
                     .arg(keep ? "" : " (original removed)"));
    update();
}

// ===========================================================================
// PATTERNS
// ===========================================================================
void Canvas::patternRect(int rows, int cols, double dU, double dV)
{
    if (m_selected.isEmpty()) {
        emit message("Pattern: select the objects first");
        return;
    }
    rows = std::max(1, rows);
    cols = std::max(1, cols);
    if (rows * cols < 2 || rows * cols > 3000) {
        emit message("Pattern: rows x columns must be between 2 and 3000");
        return;
    }
    const Frame fr = planeFrame();
    const QList<SketchItem *> src = m_selected;
    QList<SketchItem *> made;
    snapshotBegin();
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            if (!r && !c) continue;
            const V3 off = fr.U * float(dU * c) + fr.V * float(dV * r);
            for (const SketchItem *it : src)
                made << xformClone(it, [&](const V3 &p) { return p + off; });
        }
    adoptAll(m_items, m_selected, made, true);
    regroup(made);
    snapshotCommit("Rectangular Pattern");
    emit selectionChanged();
    emit message(QString("Pattern: %1 copies created in the %2 plane").arg(made.size()).arg(planeName(m_plane)));
    update();
}

void Canvas::patternPolar(int count, double totalDeg, const V3 &center, bool rotateItems)
{
    if (m_selected.isEmpty()) {
        emit message("Pattern: select the objects first");
        return;
    }
    count = std::clamp(count, 2, 1000);
    const Frame fr = planeFrame();
    const V3 axis = V3::crossProduct(fr.U, fr.V).normalized();       // counter-clockwise U -> V
    const bool full = std::fabs(std::fabs(totalDeg) - 360.0) < 1e-6;
    const double step = full ? totalDeg / count : totalDeg / (count - 1);
    const QList<SketchItem *> src = m_selected;
    QList<SketchItem *> made;
    snapshotBegin();
    for (int k = 1; k < count; ++k) {
        const QQuaternion q = QQuaternion::fromAxisAndAngle(axis, float(step * k));
        for (const SketchItem *it : src) {
            if (rotateItems) {
                made << xformClone(it, [&](const V3 &p) { return center + q.rotatedVector(p - center); });
            } else {
                const V3 c0 = itemCenter(it);
                const V3 sh = center + q.rotatedVector(c0 - center) - c0;
                made << xformClone(it, [&](const V3 &p) { return p + sh; });
            }
        }
    }
    adoptAll(m_items, m_selected, made, true);
    regroup(made);
    snapshotCommit("Polar Pattern");
    emit selectionChanged();
    emit message(QString("Polar pattern: %1 copies").arg(made.size()));
    update();
}

// ===========================================================================
// TRIM / EXTEND (AutoCAD style: every other entity in the plane is a cutting edge)
// ===========================================================================
QList<QLineF> Canvas::boundarySegs(const SketchItem *skip, const Frame &fr, qreal nref) const
{
    QList<QLineF> out;
    struct Reset { bool &f; ~Reset() { f = false; } } reset{m_fine};
    m_fine = true;
    for (const SketchItem *o : m_items) {
        if (o == skip || o->type() == SketchItem::Dimension || o->type() == SketchItem::Text ||
            o->type() == SketchItem::ImageItem || o->type() == SketchItem::Hatch)
            continue;
        for (const auto &st : strips(o)) {
            bool planar = true;
            for (const V3 &q : st)
                planar = planar && std::fabs(V3::dotProduct(q, fr.N) - nref) < 0.05;
            if (!planar)
                continue;
            for (int k = 0; k + 1 < st.size(); ++k) {
                const QLineF l(QPointF(V3::dotProduct(st[k], fr.U), V3::dotProduct(st[k], fr.V)),
                               QPointF(V3::dotProduct(st[k + 1], fr.U), V3::dotProduct(st[k + 1], fr.V)));
                if (l.length() > 1e-9)
                    out.append(l);
            }
        }
    }
    return out;
}

QVector<qreal> Canvas::pathIntersections(const QVector<V3> &path, const QList<QLineF> &bounds,
                                         const Frame &fr) const
{
    QVector<qreal> xs;
    for (int i = 0; i + 1 < path.size(); ++i) {
        const QLineF seg(QPointF(V3::dotProduct(path[i], fr.U), V3::dotProduct(path[i], fr.V)),
                         QPointF(V3::dotProduct(path[i + 1], fr.U), V3::dotProduct(path[i + 1], fr.V)));
        if (seg.length() < 1e-12)
            continue;
        for (const QLineF &b : bounds) {
            qreal t;
            if (segIntersect(seg, b, &t))
                xs.append(i + t);
        }
    }
    std::sort(xs.begin(), xs.end());
    QVector<qreal> u;
    for (qreal x : std::as_const(xs))
        if (u.isEmpty() || x - u.last() > 1e-7)
            u.append(x);
    return u;
}

Canvas::EditOp Canvas::computeExtend(SketchItem *it, const QPoint &s)
{
    EditOp op;
    op.target = it;
    if (it->type() != SketchItem::Line && it->type() != SketchItem::Polyline) {
        op.why = "Extend works on lines and polylines";
        return op;
    }
    const Frame fr = frameFor(std::max(0, it->plane));
    const int last = it->pts.size() - 1;
    if (last < 1)
        return op;
    const qreal d0 = QLineF(project(it->pts[0]), QPointF(s)).length();
    const qreal d1 = QLineF(project(it->pts[last]), QPointF(s)).length();
    const int e = d0 < d1 ? 0 : last, nb = e == 0 ? 1 : last - 1;
    const V3 end = it->pts[e];
    V3 dir = end - it->pts[nb];
    dir -= fr.N * V3::dotProduct(dir, fr.N);
    if (dir.length() < 1e-9f) {
        op.why = "Extend: that segment has no direction";
        return op;
    }
    dir.normalize();
    const QList<QLineF> bounds = boundarySegs(it, fr, V3::dotProduct(end, fr.N));
    const QPointF a(V3::dotProduct(end, fr.U), V3::dotProduct(end, fr.V));
    const QPointF dv(V3::dotProduct(dir, fr.U), V3::dotProduct(dir, fr.V));
    const qreal BIG = 1e6;
    const QLineF ray(a, a + dv * BIG);
    qreal best = 1e18;
    for (const QLineF &b : bounds) {
        qreal t;
        if (segIntersect(ray, b, &t) && t * BIG > 1e-4 && t * BIG < best)
            best = t * BIG;
    }
    if (best > 1e17) {
        op.why = "Extend: no boundary edge in that direction";
        return op;
    }
    const V3 hit = end + dir * float(best);
    Piece p{it->type(), it->pts};
    p.pts[e] = hit;
    op.pieces << p;
    op.ghost.append(QVector<V3>{end, hit});
    op.marks << hit;
    op.ok = true;
    return op;
}

void Canvas::applyEdit(const EditOp &op, const QString &name)
{
    applyEdits(QList<EditOp>{op}, name);
}

void Canvas::trimExtendClick(const QPoint &pos, bool shift)
{
    const bool ext = (m_tool == Tool::Extend) != shift;
    SketchItem *it = pick(pos, 9.0);
    if (!it) {
        emit message(ext ? "Extend: click near the end of a line" : "Trim: click the part you want to remove");
        return;
    }
    const EditOp op = ext ? computeExtend(it, pos) : computeTrim(it, pos);
    if (!op.ok) {
        emit message(op.why);
        return;
    }
    applyEdit(op, ext ? "Extend" : "Trim");
}
