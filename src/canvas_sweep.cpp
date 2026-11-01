#include "canvas.h"
#include "canvas_util.h"
#include "units.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QQuaternion>

#include <algorithm>
#include <cmath>
#include <memory>

using V3 = QVector3D;

// ===========================================================================
// Revolve and Pipe: solids that are rings swept along a path.
//  Revolve: the closed profile turned about an axis (pts[0] -> pts[1]) by 'sweep' degrees.
//  Pipe: a circle (a ring for a tube) carried along the centre line, with true bends of radius bendR
//        at the corners; the cut list gets the developed length.
// Both are kept as parameters (they move, turn, mirror and save like any object); the faces are made
// from them when needed: quads between neighbouring rings (planar - a ring turned about an axis keeps
// its chords parallel), with end caps where the sweep stops.
// ===========================================================================
namespace {
quint64 sweepKey(const SketchItem *it, bool fine)
{
    quint64 h = quint64(it->type()) * 1000003u + (fine ? 7u : 0u);
    auto mix = [&h](double v) { h = (h ^ qHash(v)) * 0x100000001b3ULL; };
    for (const V3 &p : it->pts) { mix(p.x()); mix(p.y()); mix(p.z()); }
    mix(it->sweep); mix(it->pipeD); mix(it->pipeWall); mix(it->bendR);
    return h;
}

double chordStep(double r, double tol, double maxStep)
{
    if (r <= tol)
        return maxStep;
    return std::min(2 * std::acos(std::max(-1.0, 1.0 - tol / r)), maxStep);
}

V3 quadNormal(const V3 &a, const V3 &b, const V3 &c, const V3 &d)
{
    // Newell over the four corners: right also when two of them meet (a point on the axis)
    const V3 q[4] = {a, b, c, d};
    V3 n;
    for (int i = 0; i < 4; ++i) {
        const V3 &p = q[i], &r = q[(i + 1) % 4];
        n += V3((p.y() - r.y()) * (p.z() + r.z()), (p.z() - r.z()) * (p.x() + r.x()), (p.x() - r.x()) * (p.y() + r.y()));
    }
    return n;
}
} // namespace

std::shared_ptr<const Canvas::SweepData> Canvas::sweepData(const SketchItem *it) const
{
    const quint64 key = sweepKey(it, m_fine);
    auto f = m_sweepCache.constFind(key);
    if (f != m_sweepCache.constEnd())
        return *f;
    if (m_sweepCache.size() > 64)
        m_sweepCache.clear();
    auto d = std::make_shared<SweepData>();
    if (it->type() == SketchItem::Revolve)
        buildRevolve(it, *d);
    else if (it->type() == SketchItem::Pipe)
        buildPipe(it, *d);
    // the faces, made once: quads between neighbouring rings, then the caps
    for (const SweepGrid &g : std::as_const(d->grids)) {
        const int S = int(g.rings.size()), last = g.wrap ? S : S - 1;
        for (int s = 0; s < last; ++s) {
            const QVector<V3> &A = g.rings[s], &B = g.rings[(s + 1) % S];
            const int n = int(A.size());
            for (int k = 0; k < n; ++k) {
                const V3 a = A[k], b = A[(k + 1) % n], c = B[(k + 1) % n], e = B[k];
                V3 nrm = quadNormal(a, b, c, e);
                if (nrm.lengthSquared() < 1e-12f)
                    continue;
                QVector<V3> v;
                for (const V3 &q : {a, b, c, e})
                    if (v.isEmpty() || (v.last() - q).length() > 1e-5f)
                        v << q;
                if (v.size() > 3 && (v.first() - v.last()).length() < 1e-5f)
                    v.removeLast();
                if (v.size() < 3)
                    continue;
                d->faces.append({nullptr, 2 + int(d->faces.size()), v, (g.flip ? -nrm : nrm).normalized(), {}});
            }
        }
    }
    for (int i = 0; i < d->caps.size(); ++i)
        d->faces.append({nullptr, i == 0 ? 0 : 1, d->caps[i].v, d->caps[i].n, d->caps[i].loops});
    m_sweepCache.insert(key, d);
    return d;
}

void Canvas::buildRevolve(const SketchItem *it, SweepData &d) const
{
    if (it->pts.size() < 5)
        return;
    const V3 A = it->pts[0];
    const V3 ax = (it->pts[1] - it->pts[0]).normalized();
    QList<V3> prof = it->pts.mid(2);
    if ((prof.first() - prof.last()).length() < 1e-4f)
        prof.removeLast();
    const int n = int(prof.size());
    if (n < 3 || ax.isNull())
        return;
    double rmax = 0, a0 = 1e300, a1 = -1e300;
    V3 cen;
    for (const V3 &p : std::as_const(prof)) {
        const V3 w = p - A;
        const double along = V3::dotProduct(w, ax);
        rmax = std::max(rmax, double((w - ax * float(along)).length()));
        a0 = std::min(a0, along);
        a1 = std::max(a1, along);
        cen += p;
    }
    cen /= float(n);
    d.radius = rmax;
    d.axial = a1 - a0;
    const double sw = std::clamp(it->sweep, 0.1, 360.0);
    const bool full = sw >= 359.999;
    const double step = chordStep(rmax, m_fine ? 0.02 : 0.1, 2 * PI / 24);
    const int m = std::clamp(int(std::ceil(sw * PI / 180.0 / step)), 4, 144);
    SweepGrid g;
    g.wrap = full;
    for (int s = 0; s < (full ? m : m + 1); ++s) {
        const QQuaternion q = QQuaternion::fromAxisAndAngle(ax, float(sw * s / m));
        QVector<V3> ring;
        for (const V3 &p : std::as_const(prof))
            ring << A + q.rotatedVector(p - A);
        g.rings << ring;
    }
    for (int k = 0; k < n; ++k)
        g.sharp << char(!smoothVertex(prof[(k + n - 1) % n], prof[k], prof[(k + 1) % n]));
    // faces point out when the profile runs counter-clockwise seen along the sweep direction
    const V3 t = V3::crossProduct(ax, cen - A);
    V3 area;
    for (int k = 0; k < n; ++k)
        area += V3::crossProduct(prof[k], prof[(k + 1) % n]);
    g.flip = V3::dotProduct(area, t) < 0;
    d.grids << g;
    if (!full) {
        auto capOf = [&](const QVector<V3> &ring, float sign) {
            V3 c;
            for (const V3 &p : ring) c += p;
            c /= float(ring.size());
            return SweepCap{ring, {}, V3::crossProduct(ax, c - A).normalized() * sign};
        };
        d.caps << capOf(g.rings.first(), -1.0f) << capOf(g.rings.last(), 1.0f);
        for (const QVector<V3> &r : {g.rings.first(), g.rings.last()}) {
            QVector<V3> closed = r;
            closed << r.first();
            d.feature << closed;
        }
    }
    // creases of the profile become circles; a smooth profile shows its widest circle
    int widest = 0;
    double wr = -1;
    for (int k = 0; k < n; ++k) {
        const V3 w = prof[k] - A;
        const double r = (w - ax * V3::dotProduct(w, ax)).length();
        if (r > wr) { wr = r; widest = k; }
        if (!g.sharp[k] || r < 1e-6)
            continue;
        QVector<V3> c;
        for (const QVector<V3> &ring : std::as_const(g.rings))
            c << ring[k];
        if (full)
            c << g.rings.first()[k];
        d.feature << c;
    }
    if (d.feature.isEmpty() && wr > 1e-6) {
        QVector<V3> c;
        for (const QVector<V3> &ring : std::as_const(g.rings))
            c << ring[widest];
        if (full)
            c << g.rings.first()[widest];
        d.feature << c;
    }
}

void Canvas::buildPipe(const SketchItem *it, SweepData &d) const
{
    QList<V3> P;
    for (const V3 &p : it->pts)
        if (P.isEmpty() || (P.last() - p).length() > 1e-4f)
            P << p;
    const int n = int(P.size());
    const double r = it->pipeD / 2;
    if (n < 2 || r <= 0)
        return;
    const double ri = (it->pipeWall > 0 && it->pipeWall < r) ? r - it->pipeWall : 0;
    const double R = it->bendR > 0 ? it->bendR : 1.5 * it->pipeD;
    const double tol = m_fine ? 0.02 : 0.05;
    const int N = std::clamp(int(std::ceil(2 * PI / chordStep(r, tol, 2 * PI / 16))), 16, 64);

    struct Station { V3 c, u, v; };
    QList<Station> st;
    V3 d0 = (P[1] - P[0]).normalized();
    V3 u = V3::crossProduct(d0, std::fabs(d0.z()) < 0.9f ? V3(0, 0, 1) : V3(1, 0, 0)).normalized();
    V3 v = V3::crossProduct(d0, u);
    st << Station{P[0], u, v};
    V3 prev = P[0];
    double length = 0;
    for (int i = 1; i + 1 < n; ++i) {
        const V3 din = (P[i] - P[i - 1]).normalized(), dout = (P[i + 1] - P[i]).normalized();
        const double th = std::acos(std::clamp(double(V3::dotProduct(din, dout)), -1.0, 1.0));
        if (th < 1e-4 || th > PI - 1e-3)
            continue;                                      // straight on (or turning right back: no bend)
        const double lin = (P[i] - P[i - 1]).length(), lout = (P[i + 1] - P[i]).length();
        const double shareIn = i - 1 == 0 ? lin : lin / 2, shareOut = i + 1 == n - 1 ? lout : lout / 2;
        const double tl = std::min({R * std::tan(th / 2), shareIn, shareOut});
        const double Ri = tl / std::tan(th / 2);
        const V3 Tin = P[i] - din * float(tl);
        const V3 b = V3::crossProduct(din, dout).normalized();
        const V3 w = (dout - din * V3::dotProduct(dout, din)).normalized();
        const V3 C = Tin + w * float(Ri);
        length += (Tin - prev).length() + Ri * th;
        st << Station{Tin, u, v};
        const int mb = std::clamp(int(std::ceil(th / chordStep(Ri + r, tol, PI / 12))), 2, 48);
        QQuaternion q;
        for (int j = 1; j <= mb; ++j) {
            q = QQuaternion::fromAxisAndAngle(b, float(th * j / mb * 180.0 / PI));
            st << Station{C + q.rotatedVector(Tin - C), q.rotatedVector(u), q.rotatedVector(v)};
        }
        u = q.rotatedVector(u);
        v = q.rotatedVector(v);
        prev = st.last().c;
    }
    length += (P[n - 1] - prev).length();
    st << Station{P[n - 1], u, v};
    d.length = length;

    auto ringAt = [&](const Station &s, double rad, bool reverse) {
        QVector<V3> ring;
        for (int k = 0; k < N; ++k) {
            const double a = 2 * PI * k / N * (reverse ? -1 : 1);
            ring << s.c + (s.u * float(std::cos(a)) + s.v * float(std::sin(a))) * float(rad);
        }
        return ring;
    };
    SweepGrid outer;
    for (const Station &s : std::as_const(st))
        outer.rings << ringAt(s, r, false);
    outer.sharp = QVector<char>(N, 0);
    d.grids << outer;
    SweepGrid inner;
    if (ri > 0) {
        for (const Station &s : std::as_const(st))
            inner.rings << ringAt(s, ri, true);
        inner.sharp = QVector<char>(N, 0);
        d.grids << inner;
    }
    const V3 dEnd = (P[n - 1] - P[n - 2]).normalized();
    for (int e = 0; e < 2; ++e) {                          // the two ends: a disc, or a ring for a tube
        const int s = e == 0 ? 0 : int(st.size()) - 1;
        SweepCap cap;
        cap.v = outer.rings[s];
        cap.n = e == 0 ? -d0 : dEnd;
        if (ri > 0) {
            cap.v += inner.rings[s];
            cap.loops = {N, N};
        }
        d.caps << cap;
        QVector<V3> c = outer.rings[s];
        c << c.first();
        d.feature << c;
        if (ri > 0) {
            QVector<V3> ci = inner.rings[s];
            ci << ci.first();
            d.feature << ci;
        }
    }
    QVector<V3> centre;                                    // the bent centre line (inside: hidden in pictures)
    for (const Station &s : std::as_const(st))
        centre << s.c;
    d.feature << centre;
}

// outline of a swept solid seen from here: grid edges whose two faces turn different ways to the eye
QList<QVector<V3>> Canvas::sweepSilhouettes(const SketchItem *it) const
{
    QList<QVector<V3>> out;
    const auto dp = sweepData(it);
    const SweepData &d = *dp;
    const bool persp = perspectiveActive();
    const V3 eye = persp ? eyePos() : V3();
    for (const SweepGrid &g : d.grids) {
        const int S = int(g.rings.size());
        if (S < 2)
            continue;
        const int n = int(g.rings.first().size());
        const int F = g.wrap ? S : S - 1;                  // face rows
        // which way each face looks: +1 / -1, 0 = degenerate
        QVector<signed char> side(F * n, 0);
        for (int s = 0; s < F; ++s)
            for (int k = 0; k < n; ++k) {
                const V3 &a = g.rings[s][k], &b = g.rings[s][(k + 1) % n], &c = g.rings[(s + 1) % S][(k + 1) % n],
                         &e = g.rings[(s + 1) % S][k];
                const V3 nrm = quadNormal(a, b, c, e);
                if (nrm.lengthSquared() < 1e-12f)
                    continue;
                const V3 view = persp ? (a - eye) : m_bf;
                side[s * n + k] = V3::dotProduct(nrm, view) < 0 ? 1 : -1;
            }
        auto differ = [&](int f1, int f2) { return side[f1] != 0 && side[f2] != 0 && side[f1] != side[f2]; };
        for (int s = 0; s < F; ++s)                        // along the sweep, at smooth ring points
            for (int k = 0; k < n; ++k)
                if (!g.sharp.value(k) && differ(s * n + (k + n - 1) % n, s * n + k))
                    out.append({g.rings[s][k], g.rings[(s + 1) % S][k]});
        for (int s = g.wrap ? 0 : 1; s < (g.wrap ? S : S - 1); ++s)   // along the rings
            for (int k = 0; k < n; ++k)
                if (differ(((s - 1 + F) % F) * n + k, s * n + k))
                    out.append({g.rings[s][k], g.rings[s][(k + 1) % n]});
    }
    return out;
}

// ---------------------------------------------------------------------------
// making them
// ---------------------------------------------------------------------------
// the closed profile of a 2D shape as one ring (a circle as 96 sides); empty if it has holes / is open
QList<V3> Canvas::profileRing(const SketchItem *it) const
{
    if (it->type() == SketchItem::Circle) {
        QVector<V3> s = stripsBase(it).value(0);
        QList<V3> out;
        if (s.size() < 4)
            return out;
        const int step = std::max(1, int(s.size() - 1) / 96);
        for (int i = 0; i + 1 < s.size(); i += step)
            out << s[i];
        return out;
    }
    bool closed = true;
    const QList<QList<V3>> rings = profileRings(it, closed);
    if (rings.size() != 1 || !closed || rings.first().size() < 3)
        return {};
    return rings.first();
}

bool Canvas::makeRevolve(SketchItem *profile, const V3 &a, const V3 &b, double deg, QString &err)
{
    if (!profile || isSolid(profile) || !closedShape(profile)) {
        err = "the profile must be a closed 2D shape (rectangle, circle, closed polyline)";
        return false;
    }
    const V3 ax = b - a;
    if (ax.length() < 1e-4f) {
        err = "the axis needs two different points";
        return false;
    }
    const QList<V3> ring = profileRing(profile);
    if (ring.size() < 3) {
        err = "the profile must be one closed outline without holes";
        return false;
    }
    V3 nrm;
    for (int i = 0; i < ring.size(); ++i)
        nrm += V3::crossProduct(ring[i], ring[(i + 1) % ring.size()]);
    nrm.normalize();
    const V3 axn = ax.normalized();
    if (std::fabs(V3::dotProduct(nrm, axn)) > 1e-3f || std::fabs(V3::dotProduct(a - ring[0], nrm)) > 1e-2f) {
        err = "the axis must lie in the profile's plane (draw the axis line on the same sketch plane)";
        return false;
    }
    const V3 side = V3::crossProduct(nrm, axn);
    double lo = 1e300, hi = -1e300;
    for (const V3 &p : ring) {
        const double s = V3::dotProduct(p - a, side);
        lo = std::min(lo, s);
        hi = std::max(hi, s);
    }
    if (lo < -1e-4 && hi > 1e-4) {
        err = "the profile crosses the axis - it must lie on one side of it";
        return false;
    }
    auto *n = new SketchItem(SketchItem::Revolve);
    n->pts = {a, a + axn};
    n->pts += ring;
    n->sweep = std::clamp(std::fabs(deg), 0.1, 360.0);
    n->plane = profile->plane;
    n->color = profile->color;
    n->fill = profile->fill;
    n->width = profile->width;
    n->lineStyle = profile->lineStyle;
    n->name = profile->name;
    n->material = profile->material;
    n->group = profile->group;
    replaceItem(profile, n);
    return true;
}

bool Canvas::makePipe(SketchItem *path, QString &err)
{
    if (!path || isSolid(path) || path->hidden) {
        err = "pick a line, polyline, arc or curve as the centre line";
        return false;
    }
    QList<V3> pts;
    switch (path->type()) {
    case SketchItem::Line: case SketchItem::Polyline: case SketchItem::Arc: case SketchItem::Curve:
    case SketchItem::Rect: case SketchItem::Circle: {
        const bool fine = m_fine;
        m_fine = false;
        const auto ss = stripsBase(path);
        m_fine = fine;
        if (!ss.isEmpty())
            pts = QList<V3>(ss.first().begin(), ss.first().end());
        break;
    }
    default:
        break;
    }
    if (pts.size() < 2) {
        err = "pick a line, polyline, arc or curve as the centre line";
        return false;
    }
    if (m_pipeWall > 0 && 2 * m_pipeWall >= m_pipeD) {
        err = "the wall is thicker than half the diameter";
        return false;
    }
    auto *n = new SketchItem(SketchItem::Pipe);
    n->pts = pts;
    n->pipeD = m_pipeD;
    n->pipeWall = m_pipeWall;
    n->bendR = m_pipeBend;
    n->plane = path->plane;
    n->color = path->color;
    n->width = path->width;
    n->name = path->name;
    n->material = path->material;
    n->group = path->group;
    replaceItem(path, n);
    return true;
}

QString Canvas::pipeLabel(double d, double wall)
{
    return wall > 0 ? QString("Tube %1 x %2").arg(QString::fromUtf8("\xC3\x98") + Units::formatDim(d), Units::formatDim(wall))
                    : QString("Rod %1").arg(QString::fromUtf8("\xC3\x98") + Units::formatDim(d));
}

// Model > Revolve: the selected closed shape about the selected line
void Canvas::revolveSelection(double deg)
{
    SketchItem *prof = nullptr, *axis = nullptr;
    for (SketchItem *it : std::as_const(m_selected)) {
        if (it->type() == SketchItem::Line && it->pts.size() >= 2 && !axis)
            axis = it;
        else if (!isSolid(it) && closedShape(it) && !prof)
            prof = it;
    }
    if (!prof || !axis) {
        emit message("Revolve: select a closed shape AND a line (the axis) - or use the Revolve tool");
        return;
    }
    QString err;
    snapshotBegin();
    if (!makeRevolve(prof, axis->pts[0], axis->pts[1], deg, err)) {
        m_snapshot.clear();
        emit message("Revolve: " + err);
        return;
    }
    snapshotCommit("Revolve");
    emit selectionChanged();
    emit message(QString("Revolved %1\xC2\xB0").arg(deg));
    update();
}

// Model > Pipe: every selected line / polyline / arc / curve becomes a pipe with the current settings
void Canvas::pipeSelection()
{
    QList<SketchItem *> paths;
    for (SketchItem *it : std::as_const(m_selected))
        if (!isSolid(it) && it->type() != SketchItem::Text && it->type() != SketchItem::Dimension &&
            it->type() != SketchItem::ImageItem && it->type() != SketchItem::Hatch)
            paths << it;
    if (paths.isEmpty()) {
        emit message("Pipe: select the centre lines (lines, polylines, arcs) - or use the Pipe tool and click one");
        return;
    }
    snapshotBegin();
    int n = 0;
    QString err;
    for (SketchItem *p : std::as_const(paths))
        n += makePipe(p, err) ? 1 : 0;
    if (!n) {
        m_snapshot.clear();
        emit message("Pipe: " + err);
        return;
    }
    snapshotCommit("Pipe");
    emit selectionChanged();
    emit message(QString("%1 pipe(s): %2, bends R %3").arg(n).arg(pipeLabel(m_pipeD, m_pipeWall),
                                                              Units::format(m_pipeBend > 0 ? m_pipeBend : 1.5 * m_pipeD)));
    update();
}

// ---------------------------------------------------------------------------
// the tools: Revolve = click the profile, the axis start, the axis end (angle typed, Enter = 360);
// Pipe = click a centre line (diameter typed)
// ---------------------------------------------------------------------------
void Canvas::revolveClick(const QPoint &s)
{
    if (m_rvStage == 0) {
        SketchItem *it = pick(QPointF(s), 9.0);
        if (!it || isSolid(it) || !closedShape(it)) {
            emit message("Revolve: click a closed 2D shape (the profile)");
            return;
        }
        clearSelection();
        setSelected(it, true);
        m_rvProfile = int(m_items.indexOf(it));
        m_rvStage = 1;
    } else if (m_rvStage == 1) {
        m_rvA = m_cur.pt;
        m_rvStage = 2;
    } else {
        const V3 b = m_cur.pt;
        SketchItem *prof = m_items.value(m_rvProfile);
        QString err;
        snapshotBegin();
        if (!makeRevolve(prof, m_rvA, b, m_rvAngle, err)) {
            m_snapshot.clear();
            emit message("Revolve: " + err);
            m_rvStage = 1;
            emitHint();
            update();
            return;
        }
        snapshotCommit("Revolve");
        emit message(QString("Revolved %1\xC2\xB0 - click the next profile").arg(m_rvAngle));
        m_rvStage = 0;
        m_rvProfile = -1;
        emit selectionChanged();
    }
    emitHint();
    update();
}

void Canvas::pipeClick(const QPoint &s)
{
    SketchItem *it = pick(QPointF(s), 9.0);
    QString err;
    snapshotBegin();
    if (!makePipe(it, err)) {
        m_snapshot.clear();
        emit message("Pipe: " + err);
        return;
    }
    snapshotCommit("Pipe");
    emit selectionChanged();
    emit message(QString("Pipe %1 - click the next centre line").arg(pipeLabel(m_pipeD, m_pipeWall)));
    update();
}

void Canvas::drawSweepTool(QPainter &p) const
{
    if (m_tool != Tool::Revolve && m_tool != Tool::Pipe)
        return;
    QString txt;
    if (m_tool == Tool::Revolve) {
        if (m_rvStage == 2) {
            QPointF a, b;
            if (projectD(m_rvA, a) && projectD(m_cur.pt, b)) {
                p.setPen(QPen(QColor(255, 209, 102), 1.6, Qt::DashDotLine));
                p.drawLine(a, b);
            }
        }
        txt = QString::fromUtf8("\xE2\x88\xA0 ") + (dynTyping() ? dynShown(0, true) : QString("%1\xC2\xB0").arg(m_rvAngle)) +
              (m_rvStage == 0 ? "   click the profile" : m_rvStage == 1 ? "   axis: first point" : "   axis: second point");
    } else {
        txt = "D " + (dynTyping() ? dynShown(0, true) : Units::formatDim(m_pipeD)) + "   " + pipeLabel(m_pipeD, m_pipeWall);
    }
    QFont fnt = QApplication::font();
    fnt.setPixelSize(12);
    fnt.setBold(true);
    p.setFont(fnt);
    const QFontMetricsF fm(fnt);
    const bool typed = dynTyping();
    const QPointF at = QPointF(m_lastMouse) + QPointF(22, -26);
    const QRectF r(at.x(), at.y() - 11, fm.horizontalAdvance(txt) + 14, 22);
    p.setPen(QPen(typed ? QColor(255, 209, 102) : accent(), typed ? 2 : 1));
    p.setBrush(typed ? QColor(66, 52, 18, 235) : QColor(12, 18, 28, 225));
    p.drawRoundedRect(r, 5, 5);
    p.setPen(typed ? QColor(255, 224, 150) : QColor(215, 230, 250));
    p.drawText(r, Qt::AlignCenter, txt);
    p.setBrush(Qt::NoBrush);
}
