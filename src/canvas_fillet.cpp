#include "canvas.h"
#include "canvas_util.h"
#include "units.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>
#include <cmath>

using V3 = QVector3D;

// ===========================================================================
// Fillet and Chamfer tools: point at a corner or an edge, click. Works on
//  - corners of 2D rectangles and polylines, and two lines that meet;
//  - board edges along the thickness (the profile's corners), any board;
//  - any edge of a box-shaped board (it is re-made as a profile across that edge).
// Arcs are kept as short straight pieces (chord error 0.05 mm), which boards, booleans,
// the cut list and DXF all understand.
// ===========================================================================
namespace {
// corner i of a ring (closed) or path rounded with radius r, or bevelled by r: the corner point is
// replaced by the tangent points and the arc between them
bool roundCorner(QList<V3> &ring, int i, bool closed, double r, bool chamfer, QString &err)
{
    const int n = int(ring.size());
    if (n < 3 || (!closed && (i <= 0 || i >= n - 1))) {
        err = "that point is not a corner";
        return false;
    }
    if (r <= 1e-9) {
        err = chamfer ? "give a chamfer distance (type it + Enter)" : "give a radius (type it + Enter)";
        return false;
    }
    const V3 P = ring[i], A = ring[(i + n - 1) % n], B = ring[(i + 1) % n];
    const double la = (A - P).length(), lb = (B - P).length();
    if (la < 1e-6 || lb < 1e-6) {
        err = "the corner has a zero-length edge";
        return false;
    }
    const V3 ua = (A - P) / float(la), ub = (B - P) / float(lb);
    const double theta = std::acos(std::clamp(double(V3::dotProduct(ua, ub)), -1.0, 1.0));
    if (theta > PI - 1e-3) {
        err = "the edges there are in line - no corner to round";
        return false;
    }
    QList<V3> rep;
    if (chamfer) {
        if (r > la + 1e-6 || r > lb + 1e-6) {
            err = QString("chamfer %1 is longer than the edge (max %2)").arg(Units::format(r), Units::format(std::min(la, lb)));
            return false;
        }
        rep << P + ua * float(r) << P + ub * float(r);
    } else {
        const double t = r / std::tan(theta / 2);
        if (t > la + 1e-6 || t > lb + 1e-6) {
            err = QString("radius %1 is too large for this corner (max %2)")
                      .arg(Units::format(r), Units::format(std::min(la, lb) * std::tan(theta / 2)));
            return false;
        }
        const V3 Ta = P + ua * float(t), Tb = P + ub * float(t);
        const V3 bis = (ua + ub).normalized();
        const V3 C = P + bis * float(r / std::sin(theta / 2));
        const V3 va = Ta - C, vb = Tb - C;
        const double phi = PI - theta;
        const double step = std::min(2 * std::acos(std::max(0.0, 1.0 - 0.05 / r)), 22.0 * PI / 180.0);
        const int segs = std::clamp(int(std::ceil(phi / step)), 2, 48);
        for (int k = 0; k <= segs; ++k) {
            const double s = double(k) / segs;
            rep << C + (va * float(std::sin((1 - s) * phi)) + vb * float(std::sin(s * phi))) / float(std::sin(phi));
        }
    }
    ring.removeAt(i);
    for (int k = 0; k < rep.size(); ++k)
        ring.insert(i + k, rep[k]);
    for (int k = int(ring.size()) - 1; k >= 0 && ring.size() > 3; --k) {   // a tangent point on the next corner
        const int nk = (k + 1) % int(ring.size());
        if ((nk != 0 || closed) && (ring[k] - ring[nk]).length() < 1e-4f)
            ring.removeAt(k);
    }
    return true;
}

int planeForNormal(const V3 &d, int fallback)
{
    if (std::fabs(d.z()) > 0.999f) return 0;
    if (std::fabs(d.y()) > 0.999f) return 1;
    if (std::fabs(d.x()) > 0.999f) return 2;
    return fallback;
}

double segDistance(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF d = b - a;
    const double l2 = d.x() * d.x() + d.y() * d.y();
    const double t = l2 > 1e-12 ? std::clamp(((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / l2, 0.0, 1.0) : 0.0;
    return QLineF(p, a + d * t).length();
}
} // namespace

// the profile of a rectangle / polyline as open rings (outer first, then holes); closed = a loop
QList<QList<V3>> Canvas::profileRings(const SketchItem *it, bool &closed) const
{
    QList<QList<V3>> out;
    closed = true;
    if (it->type() == SketchItem::Rect) {
        const QVector<V3> c = rectCorners(it);
        if (c.size() == 4)
            out << QList<V3>(c.begin(), c.end());
    } else if (it->type() == SketchItem::Polyline) {
        if (!it->loops.isEmpty()) {
            int k = 0;
            for (int n : it->loops) {
                out << it->pts.mid(k, n);
                k += n;
            }
        } else if (it->pts.size() >= 2) {
            QList<V3> r = it->pts;
            closed = r.size() >= 4 && (r.first() - r.last()).length() < 1e-3f;
            if (closed)
                r.removeLast();
            out << r;
        }
    }
    return out;
}

// a closed polyline (with holes) made from rings, carrying every other property of src
SketchItem *Canvas::ringsToItem(const SketchItem *src, const QList<QList<V3>> &rings, bool closed, const V3 &extrude,
                                int plane) const
{
    QJsonObject o = src->toJson();
    o["type"] = "polyline";
    auto *n = new SketchItem(SketchItem::Polyline);
    n->fromJson(o);
    n->pts.clear();
    n->loops.clear();
    if (rings.size() == 1) {
        n->pts = rings.first();
        if (closed)
            n->pts << rings.first().first();
    } else {
        for (const QList<V3> &r : rings) {
            n->pts << r;
            n->loops << int(r.size());
        }
    }
    n->extrude = extrude;
    n->plane = plane;
    n->selected = false;
    return n;
}

void Canvas::replaceItem(SketchItem *old, SketchItem *n)
{
    const int idx = int(m_items.indexOf(old));
    if (idx < 0) {
        m_items.append(n);
        return;
    }
    m_items[idx] = n;
    m_selected.removeAll(old);
    if (m_hover == old) m_hover = nullptr;
    if (m_editItem == old) m_editItem = nullptr;
    if (m_joinFirst == old) m_joinFirst = nullptr;
    delete old;
}

// a board whose profile is a rectangle with the thickness square to it
bool Canvas::isBoxSolid(const SketchItem *it) const
{
    if (it->extrude.isNull())
        return false;
    bool closed = true;
    const QList<QList<V3>> rings = profileRings(it, closed);
    if (rings.size() != 1 || rings.first().size() != 4 || !closed)
        return false;
    const QList<V3> &r = rings.first();
    const V3 en = it->extrude.normalized();
    for (int i = 0; i < 4; ++i) {
        const V3 a = (r[(i + 1) % 4] - r[i]).normalized(), b = (r[(i + 2) % 4] - r[(i + 1) % 4]).normalized();
        if (std::fabs(V3::dotProduct(a, b)) > 1e-3f || std::fabs(V3::dotProduct(a, en)) > 1e-3f)
            return false;
    }
    return true;
}

void Canvas::updateFilletHover(const QPoint &s)
{
    FilletHit h;
    const QPointF sp(s);
    double best = 12.0;
    // corners of 2D shapes, and the corner where two lines meet
    for (SketchItem *it : std::as_const(m_items)) {
        if (it->hidden || it->locked || !it->extrude.isNull())
            continue;
        if (it->type() == SketchItem::Rect || it->type() == SketchItem::Polyline) {
            bool closed = true;
            const QList<QList<V3>> rings = profileRings(it, closed);
            for (int l = 0; l < rings.size(); ++l)
                for (int i = 0; i < rings[l].size(); ++i) {
                    if (!closed && (i == 0 || i == rings[l].size() - 1))
                        continue;
                    QPointF q;
                    if (!projectD(rings[l][i], q))
                        continue;
                    const double d = QLineF(q, sp).length();
                    if (d < best) {
                        best = d;
                        h = FilletHit{it, 0, l, i, rings[l][i], rings[l][i], nullptr, true, QString()};
                    }
                }
        } else if (it->type() == SketchItem::Line && it->pts.size() >= 2) {
            for (int e = 0; e < 2; ++e) {
                QPointF q;
                if (!projectD(it->pts[e], q) || QLineF(q, sp).length() >= best)
                    continue;
                for (SketchItem *o : std::as_const(m_items)) {
                    if (o == it || o->hidden || o->locked || o->type() != SketchItem::Line || o->pts.size() < 2)
                        continue;
                    if ((o->pts[0] - it->pts[e]).length() < 1e-3f || (o->pts[1] - it->pts[e]).length() < 1e-3f) {
                        best = QLineF(q, sp).length();
                        h = FilletHit{it, 3, 0, e, it->pts[e], it->pts[e], o, true, QString()};
                        break;
                    }
                }
            }
        }
    }
    // a board edge: the nearest edge of the face under the cursor
    SolidFace f;
    V3 hit;
    if (!h.valid && faceAt(sp, f, hit) && !f.it->locked) {
        double bestE = 14.0;
        const QList<int> sizes = f.loops.isEmpty() ? QList<int>{int(f.v.size())} : f.loops;
        int base = 0;
        V3 ea, eb;
        bool found = false;
        for (int n0 : sizes) {
            for (int i = 0; i < n0; ++i) {
                const V3 a = f.v[base + i], b = f.v[base + (i + 1) % n0];
                QPointF pa, pb;
                if (!projectD(a, pa) || !projectD(b, pb))
                    continue;
                const double d = segDistance(sp, pa, pb);
                if (d < bestE) {
                    bestE = d;
                    ea = a;
                    eb = b;
                    found = true;
                }
            }
            base += n0;
        }
        if (found) {
            SketchItem *it = const_cast<SketchItem *>(f.it);
            const V3 e = it->extrude, en = e.normalized();
            const V3 dd = (eb - ea).normalized();
            h = FilletHit{it, -1, 0, 0, ea, eb, nullptr, true, QString()};
            if (std::fabs(V3::dotProduct(dd, en)) > 0.999f) {          // along the thickness: a profile corner
                bool closed = true;
                const QList<QList<V3>> rings = profileRings(it, closed);
                for (int l = 0; l < rings.size() && h.kind < 0; ++l)
                    for (int i = 0; i < rings[l].size(); ++i)
                        for (const V3 &q : {ea, eb, ea - e, eb - e})
                            if ((rings[l][i] - q).length() < 1e-3f) {
                                h.kind = 1;
                                h.loop = l;
                                h.idx = i;
                            }
                if (h.kind < 0)
                    h.why = it->type() == SketchItem::Circle ? "a round board has no corners" : "this edge is part of a curve";
            } else if (isBoxSolid(it)) {
                h.kind = 2;
            } else {
                h.why = "only edges along the board's thickness can be rounded here (or any edge of a box)";
            }
        }
    }
    const bool changed = h.valid != m_fh.valid || h.it != m_fh.it || h.a != m_fh.a || h.b != m_fh.b || h.kind != m_fh.kind;
    m_fh = h;
    if (changed)
        update();
}

void Canvas::filletClick()
{
    const FilletHit h = m_fh;
    const bool ch = m_tool == Tool::Chamfer;
    const QString what = ch ? "Chamfer" : "Fillet";
    const double r = ch ? m_chamferD : m_filletR;
    if (!h.valid) {
        emit message(what + ": point at a corner of a shape or an edge of a board, then click");
        return;
    }
    if (h.kind < 0) {
        emit message(what + ": " + h.why);
        return;
    }
    if (h.kind == 3) {                                     // two lines: the classic two-line fillet
        const QList<SketchItem *> keep = m_selected;
        for (SketchItem *it : std::as_const(m_items))
            it->selected = false;
        m_selected = {h.it, h.other};
        h.it->selected = h.other->selected = true;
        filletLines(r, ch);
        for (SketchItem *it : std::as_const(m_items))
            it->selected = false;
        m_selected.clear();
        m_fh = FilletHit();
        updateFilletHover(m_lastMouse);
        return;
    }
    SketchItem *it = h.it;
    QString err;
    QList<QList<V3>> rings;
    bool closed = true;
    V3 ext = it->extrude;
    int plane = it->plane;
    if (h.kind == 0 || h.kind == 1) {
        rings = profileRings(it, closed);
        if (h.loop >= rings.size() || !roundCorner(rings[h.loop], h.idx, closed, r, ch, err)) {
            emit message(what + ": " + err);
            return;
        }
    } else {
        // a box edge across the thickness: the box is re-made as the profile across that edge
        bool c2 = true;
        const QList<V3> base = profileRings(it, c2).value(0);
        const V3 e = it->extrude, H = e.normalized(), D = (h.b - h.a).normalized();
        const V3 W = V3::crossProduct(H, D).normalized();
        double d0 = 1e300, d1 = -1e300, w0 = 1e300, w1 = -1e300, h0 = 1e300, h1 = -1e300;
        for (const V3 &q0 : base)
            for (const V3 &q : {q0, q0 + e}) {
                const double dd = V3::dotProduct(q, D), ww = V3::dotProduct(q, W), hh = V3::dotProduct(q, H);
                d0 = std::min(d0, dd); d1 = std::max(d1, dd);
                w0 = std::min(w0, ww); w1 = std::max(w1, ww);
                h0 = std::min(h0, hh); h1 = std::max(h1, hh);
            }
        auto P = [&](double w, double hh) { return D * float(d0) + W * float(w) + H * float(hh); };
        QList<V3> ring{P(w0, h0), P(w1, h0), P(w1, h1), P(w0, h1)};
        const double we = V3::dotProduct(h.a, W), he = V3::dotProduct(h.a, H);
        const bool nearW0 = std::fabs(we - w0) < std::fabs(we - w1), nearH0 = std::fabs(he - h0) < std::fabs(he - h1);
        const int idx = nearW0 ? (nearH0 ? 0 : 3) : (nearH0 ? 1 : 2);
        if (!roundCorner(ring, idx, true, r, ch, err)) {
            emit message(what + ": " + err);
            return;
        }
        rings = {ring};
        closed = true;
        ext = D * float(d1 - d0);
        plane = planeForNormal(D, it->plane);
    }
    snapshotBegin();
    replaceItem(it, ringsToItem(it, rings, closed, ext, plane));
    snapshotCommit(what);                                  // item pointers are rebuilt after this
    m_fh = FilletHit();
    emit message(QString("%1 %2  -  click the next corner or edge, type a new %3 + Enter, Esc = done")
                     .arg(what, Units::format(r), ch ? "distance" : "radius"));
    updateFilletHover(m_lastMouse);
    update();
}

void Canvas::drawFilletTool(QPainter &p) const
{
    if (m_tool != Tool::Fillet && m_tool != Tool::Chamfer)
        return;
    const bool ch = m_tool == Tool::Chamfer;
    if (m_fh.valid) {
        const QColor c = m_fh.kind < 0 ? QColor(230, 90, 80) : QColor(255, 209, 102);
        QPointF a, b;
        if (projectD(m_fh.a, a) && projectD(m_fh.b, b)) {
            if (m_fh.kind == 0 || m_fh.kind == 3) {
                p.setPen(QPen(c, 2.0));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(a, 7.0, 7.0);
            } else {
                p.setPen(QPen(c, 5.0, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(a, b);
            }
        }
    }
    QFont fnt = QApplication::font();
    fnt.setPixelSize(12);
    fnt.setBold(true);
    p.setFont(fnt);
    const QFontMetricsF fm(fnt);
    const bool typed = !m_dyn[0].text.isEmpty();
    QString txt = (ch ? "D " : "R ") + (typed ? dynShown(0, true) : Units::formatDim(ch ? m_chamferD : m_filletR));
    if (m_fh.valid && m_fh.kind < 0)
        txt += "   " + m_fh.why;
    const QPointF at = QPointF(m_lastMouse) + QPointF(22, -26);
    const QRectF r(at.x(), at.y() - 11, fm.horizontalAdvance(txt) + 14, 22);
    p.setPen(QPen(typed ? QColor(255, 209, 102) : accent(), typed ? 2 : 1));
    p.setBrush(typed ? QColor(66, 52, 18, 235) : QColor(12, 18, 28, 225));
    p.drawRoundedRect(r, 5, 5);
    p.setPen(typed ? QColor(255, 224, 150) : QColor(215, 230, 250));
    p.drawText(r, Qt::AlignCenter, txt);
    p.setBrush(Qt::NoBrush);
}

// vertical edges of a board at smooth vertices (fillets, circles) are drawn only where the surface
// turns away from the eye: the outline, as CAD shows a rounded edge
QList<QVector<V3>> Canvas::silhouetteEdges(const SketchItem *it) const
{
    if (it->type() == SketchItem::Revolve || it->type() == SketchItem::Pipe)
        return sweepSilhouettes(it);
    QList<QVector<V3>> out;
    if (it->extrude.isNull() || (it->type() != SketchItem::Polyline && it->type() != SketchItem::Circle))
        return out;
    const V3 e = it->extrude, en = e.normalized();
    const bool persp = perspectiveActive();
    const V3 eye = persp ? eyePos() : V3();
    for (const QVector<V3> &s : stripsBase(it)) {
        QVector<V3> r = s;
        if (r.size() >= 2 && (r.first() - r.last()).length() < 1e-4f)
            r.removeLast();
        const int n = int(r.size());
        if (n < 3)
            continue;
        for (int k = 0; k < n; ++k) {
            const V3 prev = r[(k + n - 1) % n], P = r[k], next = r[(k + 1) % n];
            if (it->type() != SketchItem::Circle && !smoothVertex(prev, P, next))
                continue;
            const V3 view = persp ? P - eye : m_bf;
            const float a = V3::dotProduct(V3::crossProduct(P - prev, en), view);
            const float b = V3::dotProduct(V3::crossProduct(next - P, en), view);
            if ((a < 0) != (b < 0))
                out.append({P, P + e});
        }
    }
    return out;
}

bool Canvas::smoothVertex(const V3 &prev, const V3 &p, const V3 &next)
{
    const V3 a = p - prev, b = next - p;
    const float la = a.length(), lb = b.length();
    if (la < 1e-6f || lb < 1e-6f)
        return false;
    return V3::dotProduct(a, b) / (la * lb) > std::cos(24.0 * PI / 180.0);
}
