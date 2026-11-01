#include "canvas.h"
#include "canvas_util.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <queue>

using V3 = QVector3D;

// ===========================================================================
// Printing: the drawing as vector linework at a true scale.
// The screen camera is scaled about the window centre by F (a perspective eye stays where it is), so one
// "plot pixel" is 1/kPlotPx paper mm, and the painter maps plot pixels onto the paper frame. Lines, text,
// dimensions and hatches come out as paths through the normal drawItem(). Solids are not the screen's
// z-buffer picture: their faces are vector polygons in exact pairwise depth order and their edges are cut
// against every face in front of them (analytic hidden-line removal), so a PDF holds no raster at all.
// ===========================================================================
namespace {
const qreal kPlotPx = 40.0;     // plot pixels per paper mm: text pixel sizes and curve steps stay fine
const double kDepthEps = 0.02;  // model mm: an edge this close to a face's plane lies on it, not behind it
const double kPicEps = 0.05;    // plot px: a point this close to a face's outline is covered by the face

double cross2(const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); }

// a point well inside region (the overlap of two face pictures); false when the pictures only touch
bool interiorPoint(const QPainterPath &region, QPointF &out)
{
    if (region.isEmpty())
        return false;
    const QRectF b = region.boundingRect();
    if (b.width() < kPicEps || b.height() < kPicEps)
        return false;
    const QList<QPolygonF> polys = region.toFillPolygons();
    double area = 0, perim = 0;
    for (const QPolygonF &pl : polys) {
        double a = 0;
        for (int i = 0, k = int(pl.size()); i < k; ++i) {
            a += cross2(pl[i], pl[(i + 1) % k]);
            perim += QLineF(pl[i], pl[(i + 1) % k]).length();
        }
        area += std::fabs(a) / 2;
    }
    if (perim <= 0 || area / perim < kPicEps)             // a sliver along a shared outline (rounding)
        return false;
    double best = 0;
    for (double f : {0.5, 0.31, 0.69, 0.17, 0.83}) {
        const double y = b.top() + b.height() * f;
        QVector<double> xs;
        for (const QPolygonF &pl : polys)
            for (int i = 0, k = int(pl.size()); i < k; ++i) {
                const QPointF &p1 = pl[i], &p2 = pl[(i + 1) % k];
                if ((p1.y() <= y && p2.y() > y) || (p2.y() <= y && p1.y() > y))
                    xs << p1.x() + (y - p1.y()) * (p2.x() - p1.x()) / (p2.y() - p1.y());
            }
        std::sort(xs.begin(), xs.end());
        for (int k = 0; k + 1 < xs.size(); k += 2)
            if (xs[k + 1] - xs[k] > best) {
                const QPointF c((xs[k] + xs[k + 1]) / 2, y);
                if (region.contains(c)) {
                    best = xs[k + 1] - xs[k];
                    out = c;
                }
            }
        if (best > 8 * kPicEps)
            return true;
    }
    return best > 2 * kPicEps;
}
} // namespace

QRectF Canvas::plotExtents() const
{
    QRectF bb;
    bool any = false;
    auto add = [&](const QRectF &r) {
        if (r.width() <= 0 && r.height() <= 0)
            return;
        bb = any ? bb.united(r) : r;
        any = true;
    };
    for (const SketchItem *it : m_items) {
        if (it->hidden)
            continue;
        if (it->type() == SketchItem::Hatch) {
            for (const auto &l : hatchLoops(it))
                for (const V3 &q : l) {
                    QPointF s;
                    if (projectD(q, s))
                        add(QRectF(s, QSizeF(0.01, 0.01)));
                }
            continue;
        }
        add(itemScreenBounds(it));
    }
    return any ? bb : QRectF();
}

Canvas::PlotResult Canvas::planPlot(const QRectF &frame, const PlotView &v) const
{
    PlotResult r;
    QRectF box = v.area == 0 ? plotExtents() : QRectF(rect());
    if (box.width() <= 0 && box.height() <= 0)
        box = QRectF(rect());                                // nothing drawn: the window
    const double bw = std::max(box.width(), 1e-6), bh = std::max(box.height(), 1e-6);
    double F;                                               // plot pixels per window pixel
    if (v.scale > 0) {
        F = v.scale * kPlotPx / m_zoom;
    } else {
        F = kPlotPx * std::min(frame.width() / bw, frame.height() / bh);
        r.fit = true;
    }
    F = std::clamp(F, 1e-9, 1e9);
    r.scale = F * m_zoom / kPlotPx;
    const qreal k = F / kPlotPx;                            // paper mm per window pixel
    r.screenToPaper = QTransform(k, 0, 0, k, frame.center().x() - k * box.center().x(), frame.center().y() - k * box.center().y());
    r.modelSize = QSizeF(box.width() / m_zoom, box.height() / m_zoom);
    r.clipped = box.width() * k > frame.width() + 1e-6 || box.height() * k > frame.height() + 1e-6;
    return r;
}

Canvas::PlotResult Canvas::paintPlot(QPainter &p, const QRectF &frame, const PlotView &v)
{
    PlotResult r = planPlot(frame, v);
    const double F = r.scale * kPlotPx / m_zoom;
    // the camera scaled about the window centre; the screen-only state off
    const qreal zoom0 = m_zoom;
    const ViewSettings set0 = m_set;
    SketchItem *hover0 = m_hover;
    const bool exp0 = m_exporting;
    const PlotState st{kPlotPx, v.mono};
    m_zoom *= F;
    m_focalMul = F;
    m_set.theme = 3;                                        // Light: pale colours turn dark on white paper
    m_set.lineScale = 1;
    m_hover = nullptr;
    m_exporting = true;
    m_plot = &st;

    p.save();
    p.setClipRect(frame, Qt::IntersectClip);
    // plot pixel s' = c + F (s - c)  ->  window pixel s  ->  paper mm
    const QPointF c = viewCenter();
    const QTransform plotToScreen =
        QTransform::fromTranslate(-c.x(), -c.y()) * QTransform::fromScale(1 / F, 1 / F) * QTransform::fromTranslate(c.x(), c.y());
    p.setTransform(plotToScreen * r.screenToPaper, true);
    p.setBrush(Qt::NoBrush);

    // the screen's order: images, hatches, solids, linework on top
    for (const SketchItem *it : std::as_const(m_items))
        if (!it->hidden && it->type() == SketchItem::ImageItem)
            drawItem(p, it);
    for (const SketchItem *it : std::as_const(m_items))
        if (!it->hidden && it->type() == SketchItem::Hatch)
            drawItem(p, it);
    plotSolids(p, v, r);
    for (const SketchItem *it : std::as_const(m_items))
        if (!it->hidden && it->type() != SketchItem::ImageItem && it->type() != SketchItem::Hatch && !isSolid(it))
            drawItem(p, it);
    p.restore();

    m_plot = nullptr;
    m_focalMul = 1;
    m_zoom = zoom0;
    m_set = set0;
    m_hover = hover0;
    m_exporting = exp0;
    return r;
}

void Canvas::plotSolids(QPainter &p, const PlotView &v, PlotResult &res)
{
    QList<const SketchItem *> solids;
    for (const SketchItem *it : std::as_const(m_items))
        if (!it->hidden && isSolid(it) && it->type() != SketchItem::Hatch)
            solids << it;
    if (solids.isEmpty())
        return;
    const bool persp = perspectiveActive();
    const V3 eye = persp ? eyePos() : V3();
    const V3 lamp = (-m_bf + m_bu * 0.7f - m_br * 0.35f).normalized();
    auto facing = [&](const V3 &n, const V3 &at) { return V3::dotProduct(n, persp ? at - eye : m_bf) < 0; };

    // ---- the faces turned to the eye, projected (the same culling and lighting as the screen)
    struct PF {
        QPolygonF poly;                                    // corners of every loop
        QList<int> sizes;                                  // corner count per loop
        QPainterPath path;
        QRectF box;
        V3 n, v0;
        QColor col;
        bool glass = false;
        double depth = 0;                                  // mean distance along the view (far = big)
    };
    QVector<PF> faces;
    for (const SketchItem *it : std::as_const(solids)) {
        const QColor base = it->fill.isValid() ? it->fill : QColor(196, 205, 220);
        const int alpha = it->fill.isValid() ? it->fill.alpha() : 255;
        for (const SolidFace &f : solidFaces(it)) {
            V3 cen;
            for (const V3 &q : f.v)
                cen += q;
            cen /= float(f.v.size());
            if (!facing(f.n, cen))
                continue;
            PF pf;
            bool ok = true;
            double dsum = 0;
            for (const V3 &q : f.v) {
                QPointF sp;
                qreal d = 0;
                if (!projectD(q, sp, &d)) { ok = false; break; }
                pf.poly << sp;
                dsum += d;
            }
            if (!ok || pf.poly.size() < 3)
                continue;
            pf.sizes = f.loops.isEmpty() ? QList<int>{int(pf.poly.size())} : f.loops;
            pf.path.setFillRule(Qt::OddEvenFill);
            double area = 0;
            int k = 0;
            for (int n0 : std::as_const(pf.sizes)) {
                QPolygonF lp;
                double a = 0;
                for (int i = 0; i < n0; ++i) {
                    lp << pf.poly[k + i];
                    a += cross2(pf.poly[k + i], pf.poly[k + (i + 1) % n0]);
                }
                area += std::fabs(a) / 2;
                pf.path.addPolygon(lp);
                pf.path.closeSubpath();
                k += n0;
            }
            if (area < 0.01)
                continue;                                  // edge-on
            pf.box = pf.poly.boundingRect();
            pf.n = f.n;
            pf.v0 = f.v[0];
            const double lit = 0.46 + 0.54 * std::max(0.0, double(V3::dotProduct(f.n, lamp)));
            pf.col = QColor(std::min(255, int(base.red() * lit)), std::min(255, int(base.green() * lit)),
                            std::min(255, int(base.blue() * lit)));
            pf.glass = alpha < 250;
            if (pf.glass)
                pf.col.setAlpha(alpha);
            pf.depth = dsum / f.v.size();
            faces << pf;
        }
    }
    QVector<int> opaque, glass;
    for (int i = 0; i < faces.size(); ++i)
        (faces[i].glass ? glass : opaque) << i;

    // ---- faces: shaded in depth order, or paper white (hidden line), or none (wireframe)
    if (v.faces != 2) {
        QElapsedTimer tm;
        tm.start();
        QVector<int> order;
        if (v.faces == 0) {
            // for every two faces whose pictures overlap, the one farther away at a point of the overlap is
            // drawn first. Faces that do not cut through each other keep one order over their whole overlap,
            // so this is exact for boards; a cycle (faces cutting through each other) breaks at the farthest
            const int n = int(opaque.size());
            QVector<QVector<int>> next(n);
            QVector<int> indeg(n, 0);
            for (int a = 0; a < n; ++a)
                for (int b = a + 1; b < n; ++b) {
                    const PF &A = faces[opaque[a]], &B = faces[opaque[b]];
                    if (!A.box.intersects(B.box))
                        continue;
                    QPointF pt;
                    if (!interiorPoint(A.path.intersected(B.path), pt))
                        continue;
                    V3 ro, rd;
                    screenRay(pt, ro, rd);
                    const double dna = V3::dotProduct(rd, A.n), dnb = V3::dotProduct(rd, B.n);
                    if (std::fabs(dna) < 1e-9 || std::fabs(dnb) < 1e-9)
                        continue;
                    const double ta = V3::dotProduct(A.v0 - ro, A.n) / dna, tb = V3::dotProduct(B.v0 - ro, B.n) / dnb;
                    if (std::fabs(ta - tb) < kDepthEps)
                        continue;                          // the same plane
                    const int farI = ta > tb ? a : b, nearI = ta > tb ? b : a;
                    next[farI] << nearI;
                    ++indeg[nearI];
                }
            std::priority_queue<std::pair<double, int>> ready;   // farthest ready face first
            for (int a = 0; a < n; ++a)
                if (indeg[a] == 0)
                    ready.push({faces[opaque[a]].depth, a});
            QVector<char> done(n, 0);
            while (order.size() < n) {
                int a = -1;
                while (!ready.empty() && a < 0) {
                    a = ready.top().second;
                    ready.pop();
                    if (done[a])
                        a = -1;
                }
                if (a < 0)                                 // a cycle: the farthest face left goes next
                    for (int k = 0; k < n; ++k)
                        if (!done[k] && (a < 0 || faces[opaque[k]].depth > faces[opaque[a]].depth))
                            a = k;
                done[a] = 1;
                order << opaque[a];
                for (int b : std::as_const(next[a]))
                    if (!done[b] && --indeg[b] == 0)
                        ready.push({faces[opaque[b]].depth, b});
            }
        } else {
            order = opaque;                                // white on white: the order does not show
        }
        res.orderMs += tm.nsecsElapsed() / 1e6;
        // a thin outline in the face colour closes the hairline seams PDF viewers show between fills
        const qreal seam = 0.08 * m_plot->pxPerMm;
        for (int i : std::as_const(order)) {
            const QColor col = v.faces == 1 ? QColor(Qt::white) : faces[i].col;
            p.setPen(QPen(col, seam, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(col);
            p.drawPath(faces[i].path);
        }
        if (v.faces == 0) {                                // see-through boards over the rest, far first
            std::sort(glass.begin(), glass.end(), [&](int a, int b) { return faces[a].depth > faces[b].depth; });
            p.setPen(Qt::NoPen);
            for (int i : std::as_const(glass)) {
                p.setBrush(faces[i].col);
                p.drawPath(faces[i].path);
            }
        }
        p.setBrush(Qt::NoBrush);
        res.faces += int(order.size() + (v.faces == 0 ? glass.size() : 0));
    }
    if (!v.edges)
        return;

    // ---- edges: every piece behind a face (inside its picture and behind its plane) is cut away
    QElapsedTimer tm;
    tm.start();
    QVector<int> occ;                                      // opaque faces hide edges; wireframe hides nothing
    if (v.faces != 2)
        occ = opaque;
    auto covers = [&](const PF &o, const QPointF &m) {
        bool in = false;
        double dmin = 1e300;
        int k = 0;
        for (int n0 : o.sizes) {
            for (int i = 0; i < n0; ++i) {
                const QPointF &p1 = o.poly[k + i], &p2 = o.poly[k + (i + 1) % n0];
                if ((p1.y() <= m.y()) != (p2.y() <= m.y()) &&
                    m.x() < p1.x() + (m.y() - p1.y()) * (p2.x() - p1.x()) / (p2.y() - p1.y()))
                    in = !in;
                dmin = std::min(dmin, double(distToSegment(m, QLineF(p1, p2))));
            }
            k += n0;
        }
        return in || dmin <= kPicEps;
    };
    // screen parameter intervals of segment A-B (window points sa-sb, view depths da, db) left visible
    auto visibleParts = [&](const V3 &A, const V3 &B, const QPointF &sa, const QPointF &sb, double da, double db) {
        QVector<QPair<double, double>> hid;
        const QPointF d = sb - sa;
        const QRectF sbox = QRectF(sa, sb).normalized().adjusted(-kPicEps, -kPicEps, kPicEps, kPicEps);
        for (int oi : std::as_const(occ)) {
            const PF &o = faces[oi];
            if (!o.box.intersects(sbox))
                continue;
            const double ha = V3::dotProduct(o.n, A - o.v0), hb = V3::dotProduct(o.n, B - o.v0);
            if (ha >= -kDepthEps && hb >= -kDepthEps)
                continue;                                  // never behind this face's plane
            // where the segment is behind the plane (linear along the 3D segment; perspective-correct on screen)
            double b0 = 0, b1 = 1;
            if (!(ha < -kDepthEps && hb < -kDepthEps)) {
                const double s = (-kDepthEps - ha) / (hb - ha);
                const double t = persp ? s * db / ((1 - s) * da + s * db) : s;
                (ha < -kDepthEps ? b1 : b0) = t;
            }
            // the pieces inside the face's picture: split where the segment crosses the outline
            QVector<double> ts{b0, b1};
            int k = 0;
            for (int n0 : o.sizes) {
                for (int i = 0; i < n0; ++i) {
                    const QPointF p1 = o.poly[k + i], e = o.poly[k + (i + 1) % n0] - p1;
                    const double den = cross2(d, e);
                    if (std::fabs(den) < 1e-12)
                        continue;
                    const QPointF w = p1 - sa;
                    const double t = cross2(w, e) / den, u = cross2(w, d) / den;
                    if (u >= -1e-9 && u <= 1 + 1e-9 && t > b0 && t < b1)
                        ts << t;
                }
                k += n0;
            }
            std::sort(ts.begin(), ts.end());
            for (int j = 0; j + 1 < ts.size(); ++j)
                if (ts[j + 1] - ts[j] > 1e-9 && covers(o, sa + d * ((ts[j] + ts[j + 1]) / 2)))
                    hid << qMakePair(ts[j], ts[j + 1]);
        }
        std::sort(hid.begin(), hid.end());
        QVector<QPair<double, double>> vis;
        double t = 0;
        for (const auto &h : std::as_const(hid)) {
            if (h.first > t + 1e-9)
                vis << qMakePair(t, h.first);
            t = std::max(t, h.second);
        }
        if (t < 1 - 1e-9)
            vis << qMakePair(t, 1.0);
        if (!hid.isEmpty())
            ++res.hiddenRuns;
        return vis;
    };

    const qreal nearD = persp ? nearDist() : 0;
    for (const SketchItem *it : std::as_const(solids)) {
        const QColor base = m_plot->mono ? QColor(Qt::black) : adapt(it->color);
        const qreal sc = scaleAt(it->pts.value(0));
        const qreal lw = itemPenPx(it, sc);
        QPen pen(base, lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        applyLineStyle(pen, it, lw, sc);
        QList<QVector<V3>> runs = strips(it);
        if (it->type() == SketchItem::Circle) {
            // a round body's outline: the side lines where its facets turn away from the eye
            QVector<V3> bot, top;
            QVector<char> front;
            for (const SolidFace &f : solidFaces(it))
                if (f.kind >= 2) {
                    bot << f.v[0];
                    top << f.v[3];
                    V3 cen = (f.v[0] + f.v[1] + f.v[2] + f.v[3]) / 4.0f;
                    front << char(facing(f.n, cen));
                }
            for (int i = 0, n = int(front.size()); i < n; ++i)
                if (front[i] != front[(i + n - 1) % n])
                    runs.append({bot[i], top[i]});
        } else {
            runs += silhouetteEdges(it);                   // rounded edges, revolved bodies, pipes
        }
        QPainterPath path;
        QPointF last;
        bool open = false;
        for (const auto &s : std::as_const(runs)) {
            open = false;
            for (int i = 0; i + 1 < s.size(); ++i) {
                V3 A = s[i], B = s[i + 1];
                if (persp) {                               // keep the part in front of the near plane
                    const double a = V3::dotProduct(A - eye, m_bf), b = V3::dotProduct(B - eye, m_bf);
                    if (a < nearD && b < nearD) { open = false; continue; }
                    if (a < nearD) A = A + (B - A) * float((nearD - a) / (b - a));
                    else if (b < nearD) B = A + (B - A) * float((nearD - a) / (b - a));
                }
                QPointF sa, sb;
                qreal da = 0, db = 0;
                if (!projectD(A, sa, &da) || !projectD(B, sb, &db)) { open = false; continue; }
                const QPointF d = sb - sa;
                const double len = std::hypot(d.x(), d.y());
                for (const auto &piece : visibleParts(A, B, sa, sb, da, db)) {
                    if (piece.first > 0 && piece.second < 1 && (piece.second - piece.first) * len < 0.25)
                        continue;                          // a speck between two covering faces
                    const QPointF p0 = sa + d * piece.first, p1 = sa + d * piece.second;
                    if (!open || QLineF(last, p0).length() > 0.01)
                        path.moveTo(p0);
                    path.lineTo(p1);
                    last = p1;
                    open = piece.second >= 1 - 1e-9;
                    ++res.edgeRuns;
                }
            }
        }
        p.setPen(pen);
        p.drawPath(path);
    }
    res.edgeMs += tm.nsecsElapsed() / 1e6;
}

QImage Canvas::zBufferImage()
{
    SketchItem *hover0 = m_hover;
    m_hover = nullptr;
    QImage tmp(size(), QImage::Format_ARGB32_Premultiplied);
    tmp.fill(Qt::transparent);
    {
        QPainter p(&tmp);
        renderSolids(p);
    }
    m_hover = hover0;
    m_zValid = false;                                      // the screen picture comes back with its highlight
    return m_zImg.copy();
}
