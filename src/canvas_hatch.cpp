#include "canvas.h"
#include "canvas_util.h"
#include "units.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <numeric>

using V3 = QVector3D;

// ===========================================================================
// hatch patterns, in the style of AutoCAD .pat files. One unit = the hatch spacing.
// Each family: angle, origin x/y, shift along the line (dx), distance between lines (dy),
// dash list (positive = pen down, negative = gap, 0 = dot).
// ===========================================================================
namespace {
struct PatLine { double ang, x0, y0, dx, dy; QVector<double> dash; };
struct Pattern { const char *name; QVector<PatLine> lines; };

const QVector<Pattern> &patterns()
{
    static const QVector<Pattern> P = {
        {"Solid fill", {}},
        {"Diagonal  (ANSI31)", {{45, 0, 0, 0, 1, {}}}},
        {"Diagonal reverse", {{135, 0, 0, 0, 1, {}}}},
        {"Cross-hatch 45°  (ANSI37)", {{45, 0, 0, 0, 1, {}}, {135, 0, 0, 0, 1, {}}}},
        {"Horizontal lines", {{0, 0, 0, 0, 1, {}}}},
        {"Vertical lines", {{90, 0, 0, 0, 1, {}}}},
        {"Square grid", {{0, 0, 0, 0, 1, {}}, {90, 0, 0, 0, 1, {}}}},
        {"Steel  (ANSI32)", {{45, 0, 0, 0, 1, {}}, {45, 0.4714, 0, 0, 1, {}}}},
        {"Brick / block", {{0, 0, 0, 0, 1, {}}, {90, 0, 0, 1, 1, {1, -1}}}},
        {"Dots", {{0, 0, 0, 0.5, 1, {0, -1}}}},
        {"Earth", {{0, 0, 0, 1, 1, {1, -1}}, {0, 0, 0.375, 1, 1, {1, -1}}, {0, 0, 0.75, 1, 1, {1, -1}},
                   {90, 0.125, 0.875, 1, 1, {1, -1}}, {90, 0.375, 0.875, 1, 1, {1, -1}},
                   {90, 0.625, 0.875, 1, 1, {1, -1}}}},
    };
    return P;
}

inline qreal cross2(const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); }
inline qreal dot2(const QPointF &a, const QPointF &b) { return a.x() * b.x() + a.y() * b.y(); }

qreal signedArea(const QVector<QPointF> &p)
{
    qreal a = 0;
    for (int i = 0, n = p.size(); i < n; ++i)
        a += cross2(p[i], p[(i + 1) % n]);
    return a * 0.5;
}

// round up to 1, 2 or 5 x 10^n
double nice125(double s)
{
    s = std::max(1e-3, s);
    const double e = std::pow(10.0, std::floor(std::log10(s)));
    const double m = s / e;
    return (m <= 1.0 ? 1.0 : m <= 2.0 ? 2.0 : m <= 5.0 ? 5.0 : 10.0) * e;
}
} // namespace

QStringList Canvas::hatchPatternNames()
{
    QStringList l;
    for (const Pattern &p : patterns())
        l << QString::fromUtf8(p.name);
    return l;
}

QPixmap Canvas::hatchSwatch(int pattern, int size, const QColor &line)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF box(0.5, 0.5, size - 1, size - 1);
    p.setPen(QPen(QColor(120, 132, 150), 1));
    p.setBrush(QColor(20, 26, 36));
    p.drawRect(box);
    const auto &pats = patterns();
    const Pattern &pat = pats[std::clamp(pattern, 0, int(pats.size()) - 1)];
    if (pat.lines.isEmpty()) {
        p.fillRect(box.adjusted(2, 2, -2, -2), line);
        return pm;
    }
    p.setClipRect(box.adjusted(1, 1, -1, -1));
    const double s = size / 3.2;                    // about three pattern cells across
    const double diag = size * 1.5;
    for (const PatLine &L : pat.lines) {
        const double a = -L.ang * PI / 180.0;         // screen y points down
        const QPointF d(std::cos(a), std::sin(a)), n(-std::sin(a), std::cos(a));
        const QPointF O(L.x0 * s, size - L.y0 * s);
        const QPointF step = (d * L.dx - n * L.dy) * s;
        double cycle = 0;
        for (double dl : L.dash)
            cycle += std::fabs(dl) * s;
        for (int k = -12; k <= 12; ++k) {
            const QPointF B = O + step * double(k);
            if (L.dash.isEmpty() || cycle < 1e-9) {
                p.setPen(QPen(line, 1.2));
                p.drawLine(B - d * diag * 2, B + d * diag * 2);
                continue;
            }
            for (double base = -std::ceil(diag * 2 / cycle) * cycle; base < diag * 2; base += cycle) {
                double pos = base;
                for (double dl : L.dash) {
                    const double len = std::fabs(dl) * s;
                    if (dl == 0) {
                        p.setPen(QPen(line, 2.2, Qt::SolidLine, Qt::RoundCap));
                        p.drawPoint(B + d * pos);
                    } else if (dl > 0) {
                        p.setPen(QPen(line, 1.2));
                        p.drawLine(B + d * pos, B + d * (pos + len));
                    }
                    pos += len;
                }
            }
        }
    }
    return pm;
}

double Canvas::loopsArea(const QList<QVector<QPointF>> &loops)
{
    // outer loop minus the islands (nested islands count again, like the even-odd fill)
    double a = 0;
    for (int i = 0; i < loops.size(); ++i) {
        int depth = 0;
        for (int j = 0; j < loops.size(); ++j)
            if (j != i && !loops[i].isEmpty() && QPolygonF(loops[j]).containsPoint(loops[i][0], Qt::OddEvenFill))
                ++depth;
        a += (depth % 2 ? -1.0 : 1.0) * std::fabs(signedArea(loops[i]));
    }
    return std::fabs(a);
}

// closed boundary loops of a hatch (its stored loops) or of a closed shape (its outline)
QList<QVector<V3>> Canvas::hatchLoops(const SketchItem *it) const
{
    QList<QVector<V3>> out;
    if (it->type() == SketchItem::Hatch) {
        QList<int> sizes = it->loops;
        if (sizes.isEmpty())
            sizes << int(it->pts.size());
        int k = 0;
        for (int n : std::as_const(sizes)) {
            QVector<V3> l;
            for (int i = 0; i < n && k < it->pts.size(); ++i)
                l << it->pts[k++];
            if (l.size() >= 3) {
                l << l.first();
                out << l;
            }
        }
    } else if (closedShape(it)) {
        const auto ss = stripsBase(it);
        if (!ss.isEmpty() && ss.first().size() >= 4)
            out << ss.first();
    }
    return out;
}

bool Canvas::hasHatch(const SketchItem *it) const
{
    if (it->type() == SketchItem::Hatch)
        return true;
    return it->hatch > 0 && it->extrude.isNull() && closedShape(it);
}

// pattern spacing in mm: the stored value, or about 1/40 of the region rounded to 1-2-5
double Canvas::hatchSpacing(const SketchItem *it, const QList<QVector<V3>> &loops) const
{
    if (it->hatchScale > 0)
        return it->hatchScale;
    const Frame fr = frameFor(std::max(0, it->plane));
    QRectF box;
    bool any = false;
    for (const auto &l : loops)
        for (const V3 &q : l) {
            const QRectF r(QPointF(V3::dotProduct(q, fr.U), V3::dotProduct(q, fr.V)), QSizeF(1e-9, 1e-9));
            box = any ? box.united(r) : r;
            any = true;
        }
    return nice125(std::hypot(box.width(), box.height()) / 40.0);
}

// the pattern lines clipped to the loops (even-odd), as 3D segments; a == b is a dot
QVector<QPair<V3, V3>> Canvas::hatchSegments(const SketchItem *it, const QList<QVector<V3>> &loops, double s,
                                             int maxSegs, bool *tooDense) const
{
    QVector<QPair<V3, V3>> out;
    if (tooDense) *tooDense = false;
    const auto &pats = patterns();
    const Pattern &pat = pats[std::clamp(it->hatch, 0, int(pats.size()) - 1)];
    if (pat.lines.isEmpty() || loops.isEmpty() || s <= 0)
        return out;
    const Frame fr = frameFor(std::max(0, it->plane));
    const float n0 = V3::dotProduct(loops[0][0], fr.N);
    QVector<QLineF> edges;
    QRectF box;
    bool any = false;
    for (const auto &l : loops)
        for (int i = 0; i + 1 < l.size(); ++i) {
            const QPointF a(V3::dotProduct(l[i], fr.U), V3::dotProduct(l[i], fr.V));
            const QPointF b(V3::dotProduct(l[i + 1], fr.U), V3::dotProduct(l[i + 1], fr.V));
            edges << QLineF(a, b);
            const QRectF r(a, QSizeF(1e-9, 1e-9));
            box = any ? box.united(r) : r;
            any = true;
        }
    const QPointF corners[4] = {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight()};
    const double th = it->hatchAngle * PI / 180.0;
    const double ct = std::cos(th), st = std::sin(th);
    auto to3 = [&](const QPointF &q) { return fr.U * float(q.x()) + fr.V * float(q.y()) + fr.N * n0; };

    for (const PatLine &L : pat.lines) {
        const double a = L.ang * PI / 180.0 + th;
        const QPointF d(std::cos(a), std::sin(a)), n(-std::sin(a), std::cos(a));
        const QPointF O(s * (L.x0 * ct - L.y0 * st), s * (L.x0 * st + L.y0 * ct));
        const QPointF step = (d * L.dx + n * L.dy) * s;
        const double per = L.dy * s;
        if (std::fabs(per) < 1e-12)
            continue;
        double nmin = 1e300, nmax = -1e300;
        for (const QPointF &c : corners) {
            const double v = dot2(c - O, n);
            nmin = std::min(nmin, v);
            nmax = std::max(nmax, v);
        }
        long long k0 = (long long)std::ceil(std::min(nmin / per, nmax / per));
        long long k1 = (long long)std::floor(std::max(nmin / per, nmax / per));
        if (k1 - k0 > maxSegs) {
            if (tooDense) *tooDense = true;
            return {};
        }
        double cycle = 0;
        for (double dl : L.dash)
            cycle += std::fabs(dl) * s;
        QVector<double> ts;
        for (long long k = k0; k <= k1; ++k) {
            const QPointF B = O + step * double(k);
            ts.clear();
            for (const QLineF &e : std::as_const(edges)) {
                const double da = dot2(e.p1() - B, n), db = dot2(e.p2() - B, n);
                if ((da > 0) == (db > 0))
                    continue;
                const double f = da / (da - db);
                ts << dot2(e.p1() - B, d) + f * dot2(e.p2() - e.p1(), d);
            }
            std::sort(ts.begin(), ts.end());
            for (int i = 0; i + 1 < ts.size(); i += 2) {
                const double t0 = ts[i], t1 = ts[i + 1];
                if (t1 - t0 < 1e-12)
                    continue;
                if (L.dash.isEmpty() || cycle < 1e-12) {
                    out.append({to3(B + d * t0), to3(B + d * t1)});
                } else {
                    for (double base = std::floor(t0 / cycle) * cycle; base < t1; base += cycle) {
                        double pos = base;
                        for (double dl : L.dash) {
                            const double len = std::fabs(dl) * s;
                            if (dl == 0) {
                                if (pos >= t0 && pos <= t1)
                                    out.append({to3(B + d * pos), to3(B + d * pos)});
                            } else if (dl > 0) {
                                const double u0 = std::max(pos, t0), u1 = std::min(pos + len, t1);
                                if (u1 > u0)
                                    out.append({to3(B + d * u0), to3(B + d * u1)});
                            }
                            pos += len;
                        }
                        if (out.size() > maxSegs)
                            break;
                    }
                }
            }
            if (out.size() > maxSegs) {
                if (tooDense) *tooDense = true;
                return {};
            }
        }
    }
    return out;
}

void Canvas::drawHatch(QPainter &p, const SketchItem *it, const QList<QVector<V3>> &loops) const
{
    if (loops.isEmpty())
        return;
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (const auto &l : loops) {
        QPolygonF poly;
        for (const V3 &q : l) {
            QPointF sp;
            if (!projectD(q, sp))
                return;
            poly << sp;
        }
        path.addPolygon(poly);
        path.closeSubpath();
    }
    const auto &pats = patterns();
    const bool solid = pats[std::clamp(it->hatch, 0, int(pats.size()) - 1)].lines.isEmpty();
    const QColor line = m_plot && m_plot->mono ? QColor(Qt::black) : adapt(it->color);
    if (solid) {
        p.fillPath(path, it->fill.isValid() ? it->fill : line);
        return;
    }
    if (it->fill.isValid() && it->fill.alpha() > 0)
        p.fillPath(path, it->fill);
    const double s = hatchSpacing(it, loops);
    const qreal px = scaleAt(loops[0][0]);
    bool dense = s * px < 3.0;
    QVector<QPair<V3, V3>> segs;
    if (!dense)
        segs = hatchSegments(it, loops, s, 40000, &dense);
    if (dense) {                                   // too fine to see at this zoom: a tint of the pen colour
        QColor t = line;
        t.setAlphaF(t.alphaF() * 0.4);
        p.fillPath(path, t);
        return;
    }
    QVector<QLineF> lines;
    QVector<QPointF> dots;
    for (const auto &sg : std::as_const(segs)) {
        if (sg.first == sg.second) {
            QPointF sp;
            if (projectD(sg.first, sp))
                dots << sp;
        } else {
            QLineF l;
            if (projectSeg(sg.first, sg.second, l))
                lines << l;
        }
    }
    const qreal lw = m_plot ? itemPenPx(it, px) : std::clamp(it->width * px * m_set.lineScale, 1.0, 40.0);
    p.save();
    p.setClipPath(path, Qt::IntersectClip);
    p.setPen(QPen(line, lw, Qt::SolidLine, Qt::FlatCap));
    p.drawLines(lines);
    if (!dots.isEmpty()) {
        p.setPen(QPen(line, std::max(2.4, lw * 1.8), Qt::SolidLine, Qt::RoundCap));
        p.drawPoints(dots);
    }
    p.restore();
}

// ===========================================================================
// planar map of every boundary on the sketch plane: segments are split where they cross,
// dangling ends are dropped and the faces are traced. Rebuilt only when the geometry changes.
// ===========================================================================
struct Canvas::HatchGraph
{
    struct Face { QVector<QPointF> pts; double area = 0; QRectF box; int comp = 0; };
    size_t key = 0;
    int plane = 0;
    double n0 = 0;
    QVector<Face> faces;       // bounded faces, counter-clockwise
    QVector<Face> outers;      // outer boundary of each connected group of boundaries
    bool tooBig = false;
};

std::shared_ptr<Canvas::HatchGraph> Canvas::hatchGraph() const
{
    const Frame fr = planeFrame();
    const double n0 = V3::dotProduct(planeOrigin(), fr.N);

    size_t key = std::hash<int>()(int(m_plane)) ^ (std::hash<double>()(n0) << 1);
    auto mix = [&key](size_t v) { key ^= v + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2); };
    QList<const SketchItem *> src;
    for (const SketchItem *it : m_items) {
        const auto t = it->type();
        if (it->hidden || t == SketchItem::Text || t == SketchItem::ImageItem || t == SketchItem::Dimension ||
            t == SketchItem::Hatch)
            continue;
        src << it;
        mix(size_t(t) * 131 + size_t(it->plane + 7));
        for (const V3 &q : it->pts)
            mix(qHash(q.x()) ^ (size_t(qHash(q.y())) << 1) ^ (size_t(qHash(q.z())) << 2));
    }
    if (m_hatchGraph && m_hatchGraph->key == key)
        return m_hatchGraph;

    auto g = std::make_shared<HatchGraph>();
    g->key = key;
    g->plane = int(m_plane);
    g->n0 = n0;
    m_hatchGraph = g;

    // ---- segments of everything lying on the plane, in plane (u, v) coordinates ----
    struct Seg { QPointF a, b; };
    QVector<Seg> segs;
    const double tolN = 0.05 + 1e-6 * std::fabs(n0);
    const bool fine = m_fine;
    m_fine = true;
    QRectF span;
    bool any = false;
    for (const SketchItem *it : std::as_const(src)) {
        for (const auto &st : stripsBase(it)) {
            bool on = st.size() >= 2;
            for (const V3 &q : st)
                if (std::fabs(V3::dotProduct(q, fr.N) - n0) > tolN) { on = false; break; }
            if (!on)
                continue;
            for (int i = 0; i + 1 < st.size(); ++i) {
                const QPointF a(V3::dotProduct(st[i], fr.U), V3::dotProduct(st[i], fr.V));
                const QPointF b(V3::dotProduct(st[i + 1], fr.U), V3::dotProduct(st[i + 1], fr.V));
                if (QLineF(a, b).length() < 1e-9)
                    continue;
                segs.append({a, b});
                const QRectF r = QRectF(a, b).normalized();
                span = any ? span.united(r) : r;
                any = true;
            }
        }
    }
    m_fine = fine;
    if (segs.size() > 120000) {
        g->tooBig = true;
        return g;
    }
    if (segs.size() < 2)
        return g;
    const double eps = std::max(1e-4, std::max(span.width(), span.height()) * 1e-6);

    // ---- split points: crossings, T-junctions and collinear overlaps (sweep on x) ----
    const int S = segs.size();
    QVector<QVector<double>> cuts(S);
    QVector<int> order(S);
    std::iota(order.begin(), order.end(), 0);
    auto minX = [&](int i) { return std::min(segs[i].a.x(), segs[i].b.x()); };
    std::sort(order.begin(), order.end(), [&](int x, int y) { return minX(x) < minX(y); });
    for (int ii = 0; ii < S; ++ii) {
        const int i = order[ii];
        const Seg &A = segs[i];
        const double maxXi = std::max(A.a.x(), A.b.x()) + eps;
        const double y0 = std::min(A.a.y(), A.b.y()) - eps, y1 = std::max(A.a.y(), A.b.y()) + eps;
        const QPointF r = A.b - A.a;
        const double lr = std::hypot(r.x(), r.y());
        for (int jj = ii + 1; jj < S; ++jj) {
            const int j = order[jj];
            if (minX(j) > maxXi)
                break;
            const Seg &B = segs[j];
            if (std::max(B.a.y(), B.b.y()) < y0 || std::min(B.a.y(), B.b.y()) > y1)
                continue;
            const QPointF s = B.b - B.a;
            const double ls = std::hypot(s.x(), s.y());
            const double den = cross2(r, s);
            const QPointF qp = B.a - A.a;
            if (std::fabs(den) > 1e-9 * lr * ls) {
                const double t = cross2(qp, s) / den, u = cross2(qp, r) / den;
                const double et = eps / lr, eu = eps / ls;
                if (t >= -et && t <= 1 + et && u >= -eu && u <= 1 + eu) {
                    cuts[i] << std::clamp(t, 0.0, 1.0);
                    cuts[j] << std::clamp(u, 0.0, 1.0);
                }
            } else if (std::fabs(cross2(qp, r)) / lr <= eps) {   // collinear: overlapping ends split each other
                for (const QPointF &e : {B.a, B.b}) {
                    const double t = dot2(e - A.a, r) / (lr * lr);
                    if (t > 0 && t < 1) cuts[i] << t;
                }
                for (const QPointF &e : {A.a, A.b}) {
                    const double u = dot2(e - B.a, s) / (ls * ls);
                    if (u > 0 && u < 1) cuts[j] << u;
                }
            }
        }
    }

    // ---- vertices (merged within eps) and unique edges ----
    QVector<QPointF> verts;
    QHash<QPair<qint64, qint64>, QVector<int>> cells;
    const double cell = eps * 4;
    auto vertex = [&](const QPointF &p) {
        const qint64 cx = qint64(std::floor(p.x() / cell)), cy = qint64(std::floor(p.y() / cell));
        for (qint64 dx = -1; dx <= 1; ++dx)
            for (qint64 dy = -1; dy <= 1; ++dy) {
                const auto f = cells.constFind({cx + dx, cy + dy});
                if (f == cells.constEnd())
                    continue;
                for (int id : *f)
                    if (std::hypot(verts[id].x() - p.x(), verts[id].y() - p.y()) <= eps)
                        return id;
            }
        verts << p;
        cells[{cx, cy}] << int(verts.size() - 1);
        return int(verts.size() - 1);
    };
    QSet<QPair<int, int>> edgeSet;
    QVector<QPair<int, int>> edges;
    for (int i = 0; i < S; ++i) {
        QVector<double> &c = cuts[i];
        c << 0.0 << 1.0;
        std::sort(c.begin(), c.end());
        int prev = -1;
        for (double t : std::as_const(c)) {
            const int v = vertex(segs[i].a + (segs[i].b - segs[i].a) * t);
            if (prev >= 0 && v != prev) {
                const QPair<int, int> e(std::min(prev, v), std::max(prev, v));
                if (!edgeSet.contains(e)) {
                    edgeSet.insert(e);
                    edges << e;
                }
            }
            prev = v;
        }
    }
    const int nV = verts.size();
    QVector<QVector<int>> adj(nV);
    for (const auto &e : std::as_const(edges)) {
        adj[e.first] << e.second;
        adj[e.second] << e.first;
    }

    // ---- drop dangling ends (open lines cannot bound anything) ----
    QVector<int> stack;
    for (int v = 0; v < nV; ++v)
        if (adj[v].size() == 1)
            stack << v;
    while (!stack.isEmpty()) {
        const int v = stack.takeLast();
        if (adj[v].size() != 1)
            continue;
        const int w = adj[v].first();
        adj[v].clear();
        adj[w].removeOne(v);
        if (adj[w].size() == 1)
            stack << w;
    }

    // ---- connected groups (union-find) ----
    QVector<int> parent(nV);
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> root = [&](int x) { return parent[x] == x ? x : parent[x] = root(parent[x]); };
    for (int v = 0; v < nV; ++v)
        for (int w : std::as_const(adj[v]))
            parent[root(v)] = root(w);

    // ---- faces: walk every half-edge, always taking the next edge clockwise ----
    for (int v = 0; v < nV; ++v)
        std::sort(adj[v].begin(), adj[v].end(), [&](int a, int b) {
            return std::atan2(verts[a].y() - verts[v].y(), verts[a].x() - verts[v].x()) <
                   std::atan2(verts[b].y() - verts[v].y(), verts[b].x() - verts[v].x());
        });
    QHash<qint64, int> slot;                     // (v, w) -> index of w in adj[v]
    for (int v = 0; v < nV; ++v)
        for (int k = 0; k < adj[v].size(); ++k)
            slot[(qint64(v) << 32) | adj[v][k]] = k;
    QVector<QVector<char>> seen(nV);
    for (int v = 0; v < nV; ++v)
        seen[v].fill(0, adj[v].size());
    const double minArea = eps * eps * 4;
    for (int v = 0; v < nV; ++v)
        for (int k = 0; k < adj[v].size(); ++k) {
            if (seen[v][k])
                continue;
            QVector<QPointF> poly;
            int cv = v, ck = k, guard = 0;
            while (!seen[cv][ck] && ++guard < 2000000) {
                seen[cv][ck] = 1;
                poly << verts[cv];
                const int nv = adj[cv][ck];
                const int back = slot.value((qint64(nv) << 32) | cv, -1);
                if (back < 0)
                    break;
                const int deg = adj[nv].size();
                ck = (back - 1 + deg) % deg;
                cv = nv;
            }
            if (poly.size() < 3)
                continue;
            const double a = signedArea(poly);
            HatchGraph::Face f;
            f.comp = root(v);
            if (a > minArea) {
                f.pts = poly;
                f.area = a;
            } else if (a < -minArea) {
                std::reverse(poly.begin(), poly.end());
                f.pts = poly;
                f.area = -a;
            } else {
                continue;
            }
            f.box = QPolygonF(f.pts).boundingRect();
            (a > 0 ? g->faces : g->outers) << f;
        }
    return g;
}

// the smallest closed region around p on the sketch plane, with the islands inside it
bool Canvas::hatchRegionAt(const V3 &p, QList<QVector<V3>> &loops) const
{
    loops.clear();
    const auto g = hatchGraph();
    if (!g || g->tooBig)
        return false;
    const Frame fr = frameFor(g->plane);
    const QPointF q(V3::dotProduct(p, fr.U), V3::dotProduct(p, fr.V));
    int best = -1;
    for (int i = 0; i < g->faces.size(); ++i) {
        const auto &f = g->faces[i];
        if ((best < 0 || f.area < g->faces[best].area) && f.box.contains(q) &&
            QPolygonF(f.pts).containsPoint(q, Qt::OddEvenFill))
            best = i;
    }
    if (best < 0)
        return false;
    const auto &F = g->faces[best];
    const QPolygonF fp(F.pts);
    QList<QVector<QPointF>> uv{F.pts};
    for (const auto &o : g->outers)
        if (o.comp != F.comp && o.area < F.area && F.box.contains(o.box) && fp.containsPoint(o.pts[0], Qt::OddEvenFill))
            uv << o.pts;
    for (const auto &l : std::as_const(uv)) {
        QVector<V3> l3;
        for (const QPointF &c : l)
            l3 << fr.U * float(c.x()) + fr.V * float(c.y()) + fr.N * float(g->n0);
        l3 << l3.first();
        loops << l3;
    }
    return true;
}

void Canvas::updateHatchPreview()
{
    m_hatchPreview.clear();
    if (m_tool != Tool::Hatch || m_lastMouse.isNull() || !rect().contains(m_lastMouse))
        return;
    QVector3D ro, rd, hit;
    screenRay(QPointF(m_lastMouse), ro, rd);
    if (rayPlane(ro, rd, planeOrigin(), planeFrame().N, hit) && hatchRegionAt(hit, m_hatchPreview))
        m_hatchPreviewScale = m_hatchStyle.scale > 0 ? m_hatchStyle.scale
                                                     : nice125(10.0 / std::max(1e-6, scaleAt(hit)));
}

void Canvas::hatchClick(const QPoint &s)
{
    QVector3D ro, rd, hit;
    screenRay(QPointF(s), ro, rd);
    QList<QVector<V3>> loops;
    if (!rayPlane(ro, rd, planeOrigin(), planeFrame().N, hit)) {
        emit message("Hatch: the sketch plane is edge-on to this view - press Tab or turn the view");
        return;
    }
    if (const auto g = hatchGraph(); g && g->tooBig) {
        emit message("Hatch: too many boundary segments on this plane to search");
        return;
    }
    if (!hatchRegionAt(hit, loops)) {
        emit message("Hatch: no closed boundary around this point on the " + planeName(m_plane) +
                     " sketch plane - the lines must meet (check for gaps) and lie on the active plane");
        return;
    }
    auto *h = new SketchItem(SketchItem::Hatch);
    h->plane = int(m_plane);
    h->color = m_defColor;
    h->width = 0.18;
    h->lineStyle = 0;
    h->hatch = m_hatchStyle.pattern;
    // Auto: one spacing for the current zoom (about 10 px), so neighbouring hatches match
    h->hatchScale = m_hatchStyle.scale > 0 ? m_hatchStyle.scale : nice125(10.0 / std::max(1e-6, scaleAt(hit)));
    h->hatchAngle = m_hatchStyle.angle;
    h->fill = m_hatchStyle.fill;
    const Frame fr = planeFrame();
    QList<QVector<QPointF>> uv;
    for (const auto &l : std::as_const(loops)) {
        QVector<QPointF> l2;
        for (int i = 0; i + 1 < l.size(); ++i) {       // stored open; the closing point is implied
            h->pts << l[i];
            l2 << QPointF(V3::dotProduct(l[i], fr.U), V3::dotProduct(l[i], fr.V));
        }
        h->loops << int(l.size() - 1);
        uv << l2;
    }
    const QString pat = hatchPatternNames().value(h->hatch);
    snapshotBegin();
    m_items.append(h);
    snapshotCommit("Hatch");
    emit message(QString("Hatch: %1, area %2%3  -  click more regions, Esc to finish")
                     .arg(pat, Units::formatArea(loopsArea(uv)),
                          loops.size() > 1 ? QString(", %1 island(s) left open").arg(loops.size() - 1) : QString()));
    m_hatchPreview.clear();
    update();
}

void Canvas::applyHatchToSelection()
{
    if (m_selected.isEmpty()) {
        emit message("Hatch: select hatches or closed shapes first - or use the Hatch tool (H) and click inside a boundary");
        return;
    }
    snapshotBegin();
    int n = 0;
    for (SketchItem *it : std::as_const(m_selected)) {
        const bool own = it->type() == SketchItem::Hatch;
        if (!own && !(closedShape(it) && it->extrude.isNull()))
            continue;
        it->hatch = m_hatchStyle.pattern;
        it->hatchScale = m_hatchStyle.scale > 0 ? m_hatchStyle.scale
                                                : nice125(10.0 / std::max(1e-6, scaleAt(itemCenter(it))));
        it->hatchAngle = m_hatchStyle.angle;
        if (own || m_hatchStyle.pattern > 0)
            it->fill = m_hatchStyle.fill;
        else                                      // closed shape, solid: its fill colour is the solid
            it->fill = m_hatchStyle.fill.isValid() ? m_hatchStyle.fill
                                                   : QColor(it->color.red(), it->color.green(), it->color.blue(), 110);
        ++n;
    }
    snapshotCommit("Hatch style");
    emit message(n ? QString("Hatch style applied to %1 object(s)").arg(n)
                   : QString("Hatch style works on hatches and closed shapes (rectangle, circle, closed polyline)"));
    update();
}
