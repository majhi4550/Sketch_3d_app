#include "canvas.h"
#include "canvas_util.h"

#include <algorithm>
#include <cmath>

using V3 = QVector3D;

// ===========================================================================
// Cutting engine.
//
// Every vector entity (line, polyline, rectangle, circle, arc, curve) is turned into a
// CutPath: a fine polyline for finding crossings plus the exact data needed to rebuild
// the surviving pieces (true arcs stay arcs, curves stay curves). Each point on the path
// has a parameter u; cuts are lists of u values, so any shape in any plane is cut the
// same way.
//
//   View mode   - crossings are found in screen space: what you SEE crossing is cut,
//                 even when the shapes lie in different planes (final look is right).
//   True 3D     - crossings are real 3D contacts (closest approach < 0.05 mm).
// ===========================================================================
struct Canvas::CutPath
{
    const SketchItem *it = nullptr;
    QVector<V3> pts;          // fine polyline
    QVector<qreal> u;         // parameter at every pt (increasing)
    qreal T = 1;              // total parameter length
    bool closed = false;
    int kind = 0;             // 0 polyline / rect loop, 1 circle or arc, 2 quadratic curve
    QVector<V3> poly;         // kind 0 vertices
    V3 cen, e1, e2;           // kind 1
    qreal rad = 0, a0 = 0, sweep = 0;
    V3 c0, cc, c2;            // kind 2 (start, control, end)
};

static QVector<qreal> normCuts(QVector<qreal> v, qreal T, bool closed)
{
    const qreal eps = 1e-6;
    QVector<qreal> w;
    for (qreal x : std::as_const(v)) {
        if (closed) {
            x = std::fmod(x, T);
            if (x < 0) x += T;
            if (T - x < eps) x = 0;
            w.append(x);
        } else if (x > eps && x < T - eps) {
            w.append(x);
        }
    }
    std::sort(w.begin(), w.end());
    QVector<qreal> out;
    for (qreal x : std::as_const(w))
        if (out.isEmpty() || x - out.last() > eps)
            out.append(x);
    if (closed && out.size() > 1 && T - out.last() + out.first() < eps)
        out.removeLast();
    return out;
}

bool Canvas::makeCutPath(const SketchItem *it, CutPath &cp) const
{
    cp = CutPath();
    cp.it = it;
    if (it->hidden || it->locked || !it->extrude.isNull())
        return false;                              // hidden, fixed and solid bodies are never cut
    const QList<V3> &p = it->pts;
    switch (it->type()) {
    case SketchItem::Line:
    case SketchItem::Polyline:
        if (p.size() < 2)
            return false;
        cp.kind = 0;
        cp.poly = QVector<V3>(p.begin(), p.end());
        cp.pts = cp.poly;
        for (int i = 0; i < cp.pts.size(); ++i)
            cp.u << i;
        cp.T = cp.pts.size() - 1;
        return true;
    case SketchItem::Rect: {
        const QVector<V3> c = rectCorners(it);
        if (c.size() != 4)
            return false;
        cp.kind = 0;
        cp.poly = {c[0], c[1], c[2], c[3], c[0]};
        cp.pts = cp.poly;
        for (int i = 0; i < 5; ++i)
            cp.u << i;
        cp.T = 4;
        cp.closed = true;
        return true;
    }
    case SketchItem::Circle: {
        if (p.size() < 2)
            return false;
        const Frame fr = frameFor(std::max(0, it->plane));
        const qreal r = (p[1] - p[0]).length();
        if (r < 1e-6)
            return false;
        cp.kind = 1;
        cp.cen = p[0];
        cp.rad = r;
        cp.e1 = (p[1] - p[0]) / float(r);
        cp.e2 = V3::crossProduct(fr.N, cp.e1);
        cp.a0 = 0;
        cp.sweep = 2 * PI;
        cp.closed = true;
        cp.T = 1;
        break;
    }
    case SketchItem::Arc: {
        if (p.size() < 3)
            return false;
        if (!arcData(p[0], p[1], p[2], cp.cen, cp.rad, cp.a0, cp.sweep, cp.e1, cp.e2))
            return false;
        cp.kind = 1;
        cp.T = 1;
        break;
    }
    case SketchItem::Curve:
        if (p.size() < 3)
            return false;
        cp.kind = 2;
        cp.c0 = p[0];
        cp.cc = p[1] * 2.0f - (p[0] + p[2]) * 0.5f;
        cp.c2 = p[2];
        cp.T = 1;
        break;
    default:
        return false;
    }
    const int N = cp.kind == 2 ? 64 : (cp.closed ? 360 : std::clamp(int(std::fabs(cp.sweep) / (2 * PI) * 360) + 8, 24, 360));
    for (int i = 0; i <= N; ++i) {
        const qreal u = double(i) / N;
        cp.u << u;
        cp.pts << cutPointAt(cp, u);
    }
    return true;
}

V3 Canvas::cutPointAt(const CutPath &cp, qreal u) const
{
    if (cp.closed) {
        u = std::fmod(u, cp.T);
        if (u < 0) u += cp.T;
    }
    switch (cp.kind) {
    case 0: {
        const int n = cp.poly.size();
        const int i = std::clamp(int(std::floor(u)), 0, n - 2);
        return cp.poly[i] + (cp.poly[i + 1] - cp.poly[i]) * float(u - i);
    }
    case 1: {
        const qreal a = cp.a0 + cp.sweep * u;
        return cp.cen + (cp.e1 * float(std::cos(a)) + cp.e2 * float(std::sin(a))) * float(cp.rad);
    }
    default: {
        const float t = float(u), mt = 1.0f - t;
        return cp.c0 * (mt * mt) + cp.cc * (2 * mt * t) + cp.c2 * (t * t);
    }
    }
}

QVector<V3> Canvas::cutSample(const CutPath &cp, qreal u0, qreal u1) const
{
    QVector<V3> v;
    auto add = [&v](const V3 &q) {
        if (v.isEmpty() || (v.last() - q).length() > 1e-5f)
            v.append(q);
    };
    add(cutPointAt(cp, u0));
    if (cp.kind == 0) {
        for (int k = int(std::ceil(u0 + 1e-9)); k < u1 - 1e-9; ++k)
            add(cp.poly[k % int(cp.T)]);
    } else {
        const int steps = std::max(8, int((u1 - u0) / cp.T * 72));
        for (int i = 1; i < steps; ++i)
            add(cutPointAt(cp, u0 + (u1 - u0) * i / steps));
    }
    add(cutPointAt(cp, u1));
    return v;
}

void Canvas::cutPieces(const CutPath &cp, qreal u0, qreal u1, QList<Piece> &out) const
{
    if (u1 - u0 < 1e-7)
        return;
    if (cp.kind == 0) {
        const QVector<V3> v = cutSample(cp, u0, u1);
        if (v.size() >= 2)
            out << Piece{v.size() == 2 ? SketchItem::Line : SketchItem::Polyline, QList<V3>(v.begin(), v.end())};
    } else if (cp.kind == 1) {
        if (std::fabs(cp.sweep) * (u1 - u0) * cp.rad < 1e-3 || (u1 - u0) > cp.T - 1e-6)
            return;
        out << Piece{SketchItem::Arc, {cutPointAt(cp, u0), cutPointAt(cp, (u0 + u1) / 2), cutPointAt(cp, u1)}};
    } else {
        const V3 P0 = cutPointAt(cp, u0), P2 = cutPointAt(cp, u1);
        const float t = float(u0);
        const V3 d = ((cp.cc - cp.c0) * (1.0f - t) + (cp.c2 - cp.cc) * t) * 2.0f;   // B'(u0)
        const V3 C = P0 + d * float((u1 - u0) / 2.0);
        out << Piece{SketchItem::Curve, {P0, P0 * 0.25f + C * 0.5f + P2 * 0.25f, P2}};
    }
}

// ---------------------------------------------------------------------------
// screen-space helpers
// ---------------------------------------------------------------------------
qreal Canvas::viewT(qreal ts, qreal da, qreal db) const
{
    if (!perspectiveActive())
        return ts;
    const qreal den = db * (1 - ts) + ts * da;
    return std::fabs(den) < 1e-12 ? ts : ts * da / den;
}

bool Canvas::pathScreen(const CutPath &cp, QVector<QPointF> &sp, QVector<qreal> &dep, QVector<char> &ok) const
{
    const int n = cp.pts.size();
    sp.resize(n);
    dep.resize(n);
    ok.resize(n);
    bool any = false;
    for (int i = 0; i < n; ++i) {
        QPointF q;
        qreal d = 0;
        const bool g = projectD(cp.pts[i], q, &d);
        sp[i] = q;
        dep[i] = d;
        ok[i] = g;
        any = any || g;
    }
    return any;
}

QList<QLineF> Canvas::cutterScreenSegs(const SketchItem *skip) const
{
    QList<QLineF> out;
    struct Reset { bool &f; ~Reset() { f = false; } } reset{m_fine};
    m_fine = true;
    for (const SketchItem *o : m_items) {
        if (o == skip || o->type() == SketchItem::Dimension || o->type() == SketchItem::Text ||
            o->type() == SketchItem::ImageItem || o->type() == SketchItem::Hatch)
            continue;
        for (const auto &st : strips(o))
            for (int k = 0; k + 1 < st.size(); ++k) {
                QLineF l;
                if (projectSeg(st[k], st[k + 1], l) && l.length() > 1e-6)
                    out.append(l);
            }
    }
    return out;
}

QVector<qreal> Canvas::cutParamsView(const CutPath &cp, const QList<QLineF> &cutters) const
{
    QVector<QPointF> sp;
    QVector<qreal> dep, out;
    QVector<char> ok;
    if (!pathScreen(cp, sp, dep, ok))
        return out;
    qreal minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (int i = 0; i < sp.size(); ++i)
        if (ok[i]) {
            minx = std::min(minx, sp[i].x()); maxx = std::max(maxx, sp[i].x());
            miny = std::min(miny, sp[i].y()); maxy = std::max(maxy, sp[i].y());
        }
    QList<QLineF> rel;
    for (const QLineF &l : cutters) {
        if (std::max(l.x1(), l.x2()) < minx - 1 || std::min(l.x1(), l.x2()) > maxx + 1 ||
            std::max(l.y1(), l.y2()) < miny - 1 || std::min(l.y1(), l.y2()) > maxy + 1)
            continue;
        rel.append(l);
    }
    for (int i = 0; i + 1 < sp.size() && !rel.isEmpty(); ++i) {
        if (!ok[i] || !ok[i + 1])
            continue;
        const QLineF seg(sp[i], sp[i + 1]);
        if (seg.length() < 1e-9)
            continue;
        const qreal sx0 = std::min(seg.x1(), seg.x2()) - 1, sx1 = std::max(seg.x1(), seg.x2()) + 1;
        const qreal sy0 = std::min(seg.y1(), seg.y2()) - 1, sy1 = std::max(seg.y1(), seg.y2()) + 1;
        for (const QLineF &l : std::as_const(rel)) {
            if (std::max(l.x1(), l.x2()) < sx0 || std::min(l.x1(), l.x2()) > sx1 ||
                std::max(l.y1(), l.y2()) < sy0 || std::min(l.y1(), l.y2()) > sy1)
                continue;
            qreal t;
            if (segIntersect(seg, l, &t))
                out << cp.u[i] + (cp.u[i + 1] - cp.u[i]) * viewT(t, dep[i], dep[i + 1]);
        }
    }
    return normCuts(out, cp.T, cp.closed);
}

QVector<qreal> Canvas::cutParams3D(const CutPath &cp) const
{
    const qreal tol = 0.05;
    QVector<qreal> out;
    V3 mn = cp.pts[0], mx = cp.pts[0];
    for (const V3 &q : cp.pts) {
        mn = V3(std::min(mn.x(), q.x()), std::min(mn.y(), q.y()), std::min(mn.z(), q.z()));
        mx = V3(std::max(mx.x(), q.x()), std::max(mx.y(), q.y()), std::max(mx.z(), q.z()));
    }
    mn -= V3(tol, tol, tol);
    mx += V3(tol, tol, tol);
    struct Reset { bool &f; ~Reset() { f = false; } } reset{m_fine};
    m_fine = true;
    for (const SketchItem *o : m_items) {
        if (o == cp.it || o->type() == SketchItem::Dimension || o->type() == SketchItem::Text ||
            o->type() == SketchItem::ImageItem || o->type() == SketchItem::Hatch)
            continue;
        for (const auto &st : strips(o))
            for (int k = 0; k + 1 < st.size(); ++k) {
                const V3 &a = st[k], &b = st[k + 1];
                if (std::max(a.x(), b.x()) < mn.x() || std::min(a.x(), b.x()) > mx.x() ||
                    std::max(a.y(), b.y()) < mn.y() || std::min(a.y(), b.y()) > mx.y() ||
                    std::max(a.z(), b.z()) < mn.z() || std::min(a.z(), b.z()) > mx.z())
                    continue;
                for (int i = 0; i + 1 < cp.pts.size(); ++i) {
                    qreal s, t;
                    if (segSegClosest(cp.pts[i], cp.pts[i + 1], a, b, s, t) < tol)
                        out << cp.u[i] + (cp.u[i + 1] - cp.u[i]) * s;
                }
            }
    }
    return normCuts(out, cp.T, cp.closed);
}

QVector<qreal> Canvas::cutParams(const CutPath &cp) const
{
    return m_cutMode == 0 ? cutParamsView(cp, cutterScreenSegs(cp.it)) : cutParams3D(cp);
}

qreal Canvas::cutClickParam(const CutPath &cp, const QPointF &s) const
{
    QVector<QPointF> sp;
    QVector<qreal> dep;
    QVector<char> ok;
    if (!pathScreen(cp, sp, dep, ok))
        return -1;
    qreal best = 1e18, bu = -1;
    for (int i = 0; i + 1 < sp.size(); ++i) {
        if (!ok[i] || !ok[i + 1])
            continue;
        qreal t;
        const qreal d = distToSegment(s, QLineF(sp[i], sp[i + 1]), &t);
        if (d < best) {
            best = d;
            bu = cp.u[i] + (cp.u[i + 1] - cp.u[i]) * viewT(t, dep[i], dep[i + 1]);
        }
    }
    return bu;
}

// ---------------------------------------------------------------------------
// TRIM: remove the part(s) of the path under the click(s), between the nearest cuts
// ---------------------------------------------------------------------------
Canvas::EditOp Canvas::trimByClicks(const CutPath &cp, const QVector<qreal> &cuts, const QVector<qreal> &clicks) const
{
    EditOp op;
    op.target = const_cast<SketchItem *>(cp.it);
    for (qreal c : cuts)
        op.marks << cutPointAt(cp, c);
    const int k = cuts.size();
    if (k < (cp.closed ? 2 : 1)) {
        op.why = cp.closed ? "Trim: a closed shape needs two cut points - nothing (or only one edge) crosses it"
                           : "Trim: nothing crosses this entity here (cut mode: " +
                                 QString(m_cutMode == 0 ? "View" : "True 3D") + ")";
        return op;
    }
    struct R { qreal a, b; bool rm; };
    QVector<R> rs;
    if (!cp.closed) {
        qreal prev = 0;
        for (qreal c : cuts) { rs.append({prev, c, false}); prev = c; }
        rs.append({prev, cp.T, false});
    } else {
        for (int i = 0; i < k; ++i)
            rs.append({cuts[i], i + 1 < k ? cuts[i + 1] : cuts[0] + cp.T, false});
    }
    bool any = false;
    for (qreal uc : clicks) {
        if (uc < 0)
            continue;
        qreal x = uc;
        if (cp.closed) {
            x = std::fmod(x, cp.T);
            if (x < 0) x += cp.T;
            if (x < cuts[0]) x += cp.T;
        }
        for (R &r : rs)
            if (x >= r.a - 1e-12 && x < r.b + 1e-12) { r.rm = true; any = true; break; }
    }
    if (!any) {
        op.why = "Trim: click closer to the entity";
        return op;
    }
    QList<QPair<qreal, qreal>> kept;
    auto keepRange = [&](qreal a, qreal b) {
        if (!kept.isEmpty() && std::fabs(kept.last().second - a) < 1e-9)
            kept.last().second = b;
        else
            kept.append({a, b});
    };
    const int m = rs.size();
    if (!cp.closed) {
        for (const R &r : rs)
            if (!r.rm) keepRange(r.a, r.b);
    } else {
        int s = 0;
        while (s < m && !rs[s].rm) ++s;
        for (int j = 1; j < m; ++j) {
            const R &r = rs[(s + j) % m];
            if (r.rm) continue;
            const qreal off = (s + j >= m) ? cp.T : 0;
            keepRange(r.a + off, r.b + off);
        }
    }
    for (const R &r : rs)
        if (r.rm)
            op.ghost.append(cutSample(cp, r.a, r.b));
    for (const auto &kr : kept)
        cutPieces(cp, kr.first, kr.second, op.pieces);
    op.ok = true;
    return op;
}

Canvas::EditOp Canvas::computeTrim(SketchItem *it, const QPoint &s)
{
    EditOp op;
    op.target = it;
    CutPath cp;
    if (!makeCutPath(it, cp)) {
        op.why = "Trim works on lines, polylines, rectangles, circles, arcs and curves";
        return op;
    }
    const qreal uc = cutClickParam(cp, QPointF(s));
    if (uc < 0) {
        op.why = "Trim: this entity is not visible from the current view";
        return op;
    }
    return trimByClicks(cp, cutParams(cp), {uc});
}

// ---------------------------------------------------------------------------
// FENCE: drag a line across many entities; each crossed part is trimmed at once
// ---------------------------------------------------------------------------
void Canvas::fenceTrim(const QPoint &a, const QPoint &b, bool extend)
{
    if ((a - b).manhattanLength() < 4)
        return;
    const QList<QLineF> fence{QLineF(QPointF(a), QPointF(b))};
    QList<EditOp> ops;
    for (SketchItem *it : std::as_const(m_items)) {
        CutPath cp;
        if (!makeCutPath(it, cp))
            continue;
        const QVector<qreal> hits = cutParamsView(cp, fence);
        if (hits.isEmpty())
            continue;
        if (extend) {
            QPointF sp;
            if (!projectD(cutPointAt(cp, hits[0]), sp))
                continue;
            const EditOp op = computeExtend(it, sp.toPoint());
            if (op.ok) ops << op;
        } else {
            const EditOp op = trimByClicks(cp, cutParams(cp), hits);
            if (op.ok) ops << op;
        }
    }
    if (ops.isEmpty()) {
        emit message("Fence: nothing to " + QString(extend ? "extend" : "trim") + " along that line");
        return;
    }
    applyEdits(ops, extend ? "Fence Extend" : "Fence Trim");
    emit message(QString("Fence: %1 object(s) %2").arg(ops.size()).arg(extend ? "extended" : "trimmed"));
}

// ---------------------------------------------------------------------------
// CUTTER BLADE: a cutting wall through every shape in any plane
//   True 3D: the wall contains the blade line and the sketch-plane normal.
//   View:    the wall is the screen line (cuts what you see crossing the blade).
// ---------------------------------------------------------------------------
int Canvas::bladeSide(const V3 &A, const V3 &B, const V3 &p) const
{
    if (m_cutMode == 0) {
        const QPointF sA = project(A), dS = project(B) - sA, s = project(p);
        return (dS.x() * (s.y() - sA.y()) - dS.y() * (s.x() - sA.x())) >= 0 ? 1 : -1;
    }
    const Frame fr = planeFrame();
    V3 d = B - A;
    d -= fr.N * V3::dotProduct(d, fr.N);
    if (d.length() < 1e-9f)
        return 1;
    const V3 nW = V3::crossProduct(fr.N, d.normalized());
    return V3::dotProduct(p - A, nW) >= 0 ? 1 : -1;
}

QVector<qreal> Canvas::bladeParams(const CutPath &cp, const V3 &A, const V3 &B, bool infinite) const
{
    QVector<qreal> out;
    const int n = cp.pts.size();
    if (m_cutMode == 0) {
        const QPointF sA = project(A), dS = project(B) - sA;
        const qreal len = std::hypot(dS.x(), dS.y());
        if (len < 2.0)
            return out;
        QVector<QPointF> sp;
        QVector<qreal> dep;
        QVector<char> ok;
        if (!pathScreen(cp, sp, dep, ok))
            return out;
        auto g = [&](const QPointF &s) { return (dS.x() * (s.y() - sA.y()) - dS.y() * (s.x() - sA.x())) / len; };
        for (int i = 0; i + 1 < n; ++i) {
            if (!ok[i] || !ok[i + 1])
                continue;
            const qreal g0 = g(sp[i]), g1 = g(sp[i + 1]);
            if ((g0 >= 0) == (g1 >= 0))
                continue;
            const qreal ts = g0 / (g0 - g1);
            const QPointF q = sp[i] + (sp[i + 1] - sp[i]) * ts;
            if (!infinite) {
                const qreal c = ((q.x() - sA.x()) * dS.x() + (q.y() - sA.y()) * dS.y()) / (len * len);
                if (c < -1e-4 || c > 1 + 1e-4)
                    continue;
            }
            out << cp.u[i] + (cp.u[i + 1] - cp.u[i]) * viewT(ts, dep[i], dep[i + 1]);
        }
    } else {
        const Frame fr = planeFrame();
        V3 d = B - A;
        d -= fr.N * V3::dotProduct(d, fr.N);
        const qreal L = d.length();
        if (L < 1e-6)
            return out;
        const V3 dh = d / float(L), nW = V3::crossProduct(fr.N, dh).normalized();
        for (int i = 0; i + 1 < n; ++i) {
            const qreal g0 = V3::dotProduct(cp.pts[i] - A, nW), g1 = V3::dotProduct(cp.pts[i + 1] - A, nW);
            if ((g0 >= 0) == (g1 >= 0))
                continue;
            const qreal t = g0 / (g0 - g1);
            if (!infinite) {
                const V3 q = cp.pts[i] + (cp.pts[i + 1] - cp.pts[i]) * float(t);
                const qreal c = V3::dotProduct(q - A, dh);
                if (c < -1e-4 || c > L + 1e-4)
                    continue;
            }
            out << cp.u[i] + (cp.u[i + 1] - cp.u[i]) * t;
        }
    }
    return normCuts(out, cp.T, cp.closed);
}

Canvas::EditOp Canvas::bladeOp(const SketchItem *it, const V3 &A, const V3 &B, int removeSide, bool infinite) const
{
    EditOp op;
    CutPath cp;
    if (!makeCutPath(it, cp))
        return op;
    op.target = const_cast<SketchItem *>(it);
    QVector<qreal> cuts = bladeParams(cp, A, B, infinite);
    for (qreal c : cuts)
        op.marks << cutPointAt(cp, c);
    if (cp.closed && cuts.size() == 1)
        cuts.clear();

    struct R { qreal a, b; };
    QVector<R> rs;
    if (cuts.isEmpty()) {
        rs.append({0, cp.T});
    } else if (!cp.closed) {
        qreal prev = 0;
        for (qreal c : cuts) { rs.append({prev, c}); prev = c; }
        rs.append({prev, cp.T});
    } else {
        for (int i = 0; i < cuts.size(); ++i)
            rs.append({cuts[i], i + 1 < cuts.size() ? cuts[i + 1] : cuts[0] + cp.T});
    }

    if (removeSide == 0) {                         // split only
        if (cuts.isEmpty())
            return op;
        for (const R &r : rs)
            cutPieces(cp, r.a, r.b, op.pieces);
        op.ok = !op.pieces.isEmpty();
        return op;
    }
    QList<QPair<qreal, qreal>> kept;
    bool removed = false;
    for (const R &r : rs) {
        const int side = bladeSide(A, B, cutPointAt(cp, (r.a + r.b) / 2));
        if (side == removeSide) {
            removed = true;
            op.ghost.append(cutSample(cp, r.a, r.b));
        } else if (!kept.isEmpty() && std::fabs(kept.last().second - r.a) < 1e-9) {
            kept.last().second = r.b;
        } else {
            kept.append({r.a, r.b});
        }
    }
    if (!removed)
        return op;
    if (!cuts.isEmpty())
        for (const auto &kr : kept)
            cutPieces(cp, kr.first, kr.second, op.pieces);
    op.ok = true;
    return op;
}

QList<SketchItem *> Canvas::cutCandidates() const
{
    QList<SketchItem *> src = m_selected.isEmpty() ? m_items : m_selected;
    QList<SketchItem *> out;
    for (SketchItem *it : src)
        if (it->type() != SketchItem::Dimension && it->type() != SketchItem::Text &&
            it->type() != SketchItem::ImageItem && it->type() != SketchItem::Hatch)
            out << it;
    return out;
}

void Canvas::updateBladePreview()
{
    m_ghosts.clear();
    m_marks.clear();
    if (m_pending.isEmpty() || !m_cur.valid)
        return;
    const V3 A = m_pending[0];
    const V3 B = m_pending.size() >= 2 ? m_pending[1] : m_cur.pt;
    if ((B - A).length() < 1e-6f)
        return;
    const bool inf = m_bladeAction == 1;
    const int side = (inf && m_pending.size() >= 2) ? bladeSide(A, B, m_cur.pt) : 0;
    m_ghostExtend = false;
    for (const SketchItem *it : cutCandidates()) {
        const EditOp op = bladeOp(it, A, B, side, inf);
        m_marks += op.marks;
        m_ghosts += op.ghost;
    }
    update();
}

void Canvas::bladeApply(const V3 &A, const V3 &B, int removeSide)
{
    const bool inf = removeSide != 0;
    QList<EditOp> ops;
    for (const SketchItem *it : cutCandidates()) {
        const EditOp op = bladeOp(it, A, B, removeSide, inf);
        if (op.ok)
            ops << op;
    }
    if (ops.isEmpty()) {
        emit message("Cutter: the blade does not cross anything" +
                     QString(m_cutMode == 0 ? " you can see (View mode)" : " in 3D space (True 3D mode)"));
        return;
    }
    int pieces = 0;
    for (const EditOp &o : ops)
        pieces += o.pieces.size();
    applyEdits(ops, removeSide ? "Cut Away" : "Cutter Split");
    emit message(QString("Cutter: %1 object(s) %2").arg(ops.size()).arg(removeSide ? "cut" : "split into pieces"));
}

// ---------------------------------------------------------------------------
// apply one or many edits as a single undo step
// ---------------------------------------------------------------------------
void Canvas::applyEdits(const QList<EditOp> &ops, const QString &name)
{
    bool any = false;
    for (const EditOp &op : ops)
        any = any || (op.ok && op.target && m_items.contains(op.target));
    if (!any)
        return;
    snapshotBegin();
    clearSelection();
    m_hover = nullptr;
    m_joinFirst = nullptr;
    m_editItem = nullptr;
    for (const EditOp &op : ops) {
        if (!op.ok || !op.target || !m_items.contains(op.target))
            continue;
        const QColor col = op.target->color;
        const double lw = op.target->width;
        const int pl = op.target->plane;
        const int ls = op.target->lineStyle, grp = op.target->group;
        m_items.removeOne(op.target);
        delete op.target;
        for (const Piece &pc : op.pieces) {
            auto *n = new SketchItem(pc.type);
            n->pts = pc.pts;
            n->color = col;
            n->width = lw;
            n->lineStyle = ls;
            n->group = grp;
            n->plane = pl;
            m_items.append(n);
        }
    }
    m_ghosts.clear();
    m_marks.clear();
    snapshotCommit(name);
    emit selectionChanged();
    update();
}
