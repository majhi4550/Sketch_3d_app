#include "canvas.h"
#include "canvas_util.h"
#include "units.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QVarLengthArray>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

using V3 = QVector3D;

// ===========================================================================
// 3D boards: every solid is a closed profile (rectangle, circle, closed polyline) swept along its
// extrude vector. This file gives them real faces: picking by face, depth-buffered drawing,
// the Box tool (AutoCAD BOX with dynamic UCS) and Press/Pull, plus the cut list.
// ===========================================================================
namespace {
// point inside a planar polygon (with holes: loops = corner count per loop), tested in the 2D
// projection that drops the normal's main axis
bool insidePoly(const V3 &q, const QVector<V3> &v, const V3 &n, const QList<int> &loops = {})
{
    const float ax = std::fabs(n.x()), ay = std::fabs(n.y()), az = std::fabs(n.z());
    auto uv = [&](const V3 &p) {
        if (ax >= ay && ax >= az) return QPointF(p.y(), p.z());
        return ay >= az ? QPointF(p.x(), p.z()) : QPointF(p.x(), p.y());
    };
    const QPointF pt = uv(q);
    const QList<int> sizes = loops.isEmpty() ? QList<int>{int(v.size())} : loops;
    bool in = false;
    int k = 0;
    for (int n0 : sizes) {
        QPolygonF poly;
        for (int i = 0; i < n0 && k + i < v.size(); ++i)
            poly << uv(v[k + i]);
        if (poly.containsPoint(pt, Qt::OddEvenFill))
            in = !in;
        k += n0;
    }
    return in;
}

V3 newell(const QVector<V3> &v)
{
    V3 n;
    for (int i = 0, k = v.size(); i < k; ++i) {
        const V3 &a = v[i], &b = v[(i + 1) % k];
        n += V3((a.y() - b.y()) * (a.z() + b.z()), (a.z() - b.z()) * (a.x() + b.x()), (a.x() - b.x()) * (a.y() + b.y()));
    }
    return n;
}

QVector<V3> openRing(QVector<V3> r)
{
    if (r.size() >= 2 && (r.first() - r.last()).length() < 1e-4f)
        r.removeLast();
    return r;
}

QColor mixColor(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() * (1 - t) + b.redF() * t), float(a.greenF() * (1 - t) + b.greenF() * t),
                            float(a.blueF() * (1 - t) + b.blueF() * t), a.alphaF());
}
} // namespace

QList<Canvas::SolidFace> Canvas::solidFaces(const SketchItem *it) const
{
    QList<SolidFace> out;
    if (it->type() == SketchItem::Revolve || it->type() == SketchItem::Pipe) {
        out = sweepData(it)->faces;
        for (SolidFace &f : out)
            f.it = it;
        return out;
    }
    if (it->extrude.isNull())
        return out;
    const auto ss = stripsBase(it);
    if (ss.isEmpty())
        return out;
    QList<QVector<V3>> rings;                              // outer loop first, then the holes
    for (const auto &s : ss) {
        const QVector<V3> r = openRing(s);
        if (r.size() >= 3)
            rings << r;
    }
    if (rings.isEmpty())
        return out;
    const V3 e = it->extrude, en = e.normalized();
    QVector<V3> bot, top;
    QList<int> sizes;
    for (const QVector<V3> &r : std::as_const(rings)) {
        for (const V3 &q : r) {
            bot << q;
            top << q + e;
        }
        sizes << int(r.size());
    }
    if (rings.size() == 1)
        sizes.clear();
    out.append({it, 0, bot, -en, sizes});
    out.append({it, 1, top, en, sizes});
    int edge = 0;                                          // side faces are numbered over all loops
    for (int l = 0; l < rings.size(); ++l) {
        const QVector<V3> &ring = rings[l];
        const int n = ring.size();
        // ring direction about en; a hole's wall faces into the hole
        const float turn = (V3::dotProduct(newell(ring), en) >= 0 ? 1.0f : -1.0f) * (l == 0 ? 1.0f : -1.0f);
        for (int i = 0; i < n; ++i, ++edge) {
            const V3 a = ring[i], b = ring[(i + 1) % n];
            const V3 nrm = V3::crossProduct(b - a, en) * turn;       // outward, also for concave profiles
            if (nrm.lengthSquared() < 1e-12f)
                continue;
            out.append({it, 2 + edge, {a, b, b + e, a + e}, nrm.normalized(), {}});
        }
    }
    return out;
}

// the face of a solid under screen point s (nearest to the eye)
bool Canvas::faceAt(const QPointF &s, SolidFace &face, V3 &hit, const SketchItem *skip) const
{
    V3 ro, rd;
    screenRay(s, ro, rd);
    const qreal nearT = perspectiveActive() ? nearDist() : -1e18;
    qreal best = 1e300;
    bool found = false;
    for (const SketchItem *it : m_items) {
        if (it == skip || it->hidden || !isSolid(it))
            continue;
        for (const SolidFace &f : solidFaces(it)) {
            const qreal dn = V3::dotProduct(rd, f.n);
            if (dn > -1e-6)
                continue;                                  // seen from behind
            const qreal t = V3::dotProduct(f.v[0] - ro, f.n) / dn;
            if (t < nearT || t >= best)
                continue;
            const V3 q = ro + rd * float(t);
            if (!insidePoly(q, f.v, f.n, f.loops))
                continue;
            best = t;
            face = f;
            hit = q;
            found = true;
        }
    }
    return found;
}

// a closed 2D shape (not yet a body) under s: Press/Pull turns it into one
bool Canvas::flatShapeAt(const QPointF &s, SolidFace &face, V3 &hit) const
{
    V3 ro, rd;
    screenRay(s, ro, rd);
    const qreal nearT = perspectiveActive() ? nearDist() : -1e18;
    qreal best = 1e300;
    bool found = false;
    for (const SketchItem *it : m_items) {
        if (it->hidden || it->locked || !it->extrude.isNull() || !closedShape(it))
            continue;
        const QVector<V3> ring = openRing(stripsBase(it).value(0));
        if (ring.size() < 3)
            continue;
        V3 n = newell(ring);
        if (n.lengthSquared() < 1e-12f)
            continue;
        n.normalize();
        if (V3::dotProduct(rd, n) > 0)
            n = -n;                                        // the side facing the camera
        const qreal dn = V3::dotProduct(rd, n);
        if (std::fabs(dn) < 1e-6)
            continue;
        const qreal t = V3::dotProduct(ring[0] - ro, n) / dn;
        if (t < nearT || t >= best)
            continue;
        const V3 q = ro + rd * float(t);
        if (!insidePoly(q, ring, n))
            continue;
        best = t;
        face = {it, -1, ring, n};
        hit = q;
        found = true;
    }
    return found;
}

// ===========================================================================
// depth-buffered solids: faces are rasterised into an image with a z-buffer, so boards hide
// each other correctly from any side; edges are drawn only where they are not hidden.
// ===========================================================================
void Canvas::renderSolids(QPainter &p)
{
    QList<const SketchItem *> solids;
    for (const SketchItem *it : std::as_const(m_items))
        if (!it->hidden && isSolid(it) && it->type() != SketchItem::Hatch)
            solids << it;
    if (solids.isEmpty()) {
        m_zValid = false;
        return;
    }
    const QTransform dt = p.deviceTransform();
    const qreal sc = std::max(0.25, std::hypot(dt.m11(), dt.m12()));
    const int W = std::max(1, int(std::ceil(width() * sc))), H = std::max(1, int(std::ceil(height() * sc)));
    const bool persp = perspectiveActive();
    const V3 eye = persp ? eyePos() : V3();

    // the picture is rebuilt only when the camera, the bodies or their highlight change
    size_t key = std::hash<double>()(sc) ^ (size_t(W) << 20) ^ size_t(H) ^ (persp ? 0x5bd1e995u : 0u);
    auto mix = [&key](size_t v) { key ^= v + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2); };
    auto mixV = [&](const V3 &q) { mix(qHash(q.x())); mix(qHash(q.y())); mix(qHash(q.z())); };
    mixV(m_br); mixV(m_bu); mixV(m_bf); mixV(m_target);
    mix(qHash(double(m_zoom)));
    mix(qHash(m_set.fov));
    for (const SketchItem *it : std::as_const(solids)) {
        for (const V3 &q : it->pts)
            mixV(q);
        mixV(it->extrude);
        mix(qHash(it->sweep) ^ (qHash(it->pipeD) << 1) ^ (qHash(it->pipeWall) << 2) ^ (qHash(it->bendR) << 3));
        mix(it->fill.isValid() ? it->fill.rgba() : 1u);
        mix(size_t(it->type()) * 7 + (it->selected ? 1 : 0) + (it == m_hover ? 2 : 0));
    }

    if (!m_zValid || key != m_zKey || m_zImg.width() != W || m_zImg.height() != H) {
        if (m_zImg.width() != W || m_zImg.height() != H)
            m_zImg = QImage(W, H, QImage::Format_ARGB32_Premultiplied);
        m_zImg.fill(Qt::transparent);
        m_zBuf.fill(std::numeric_limits<float>::max(), W * H);
        m_zId.fill(-1, W * H);
        m_zPlanes.clear();
        m_zScale = sc;
        quint32 *bits = reinterpret_cast<quint32 *>(m_zImg.bits());
        const int stride = int(m_zImg.bytesPerLine() / 4);
        float *zb = m_zBuf.data();
        int *zid = m_zId.data();
        const V3 lamp = (-m_bf + m_bu * 0.7f - m_br * 0.35f).normalized();

        // scanline fill of one face; depth is a plane z = a x + b y + c in image space
        // (orthographic: depth, perspective: -1/distance - both are linear on screen)
        auto raster = [&](const QPolygonF &poly, const QList<int> &loops, float a, float b, float c, QRgb col, int alpha,
                          int face) {
            double ymin = 1e300, ymax = -1e300;
            for (const QPointF &q : poly) { ymin = std::min(ymin, q.y()); ymax = std::max(ymax, q.y()); }
            const int y0 = std::max(0, int(std::floor(ymin))), y1 = std::min(H - 1, int(std::ceil(ymax)));
            // the polygon's edges, loop by loop (a cap with holes has several loops)
            QVarLengthArray<QLineF, 64> edges;
            const QList<int> sizes = loops.isEmpty() ? QList<int>{int(poly.size())} : loops;
            int base = 0;
            for (int n0 : sizes) {
                for (int i = 0; i < n0; ++i)
                    edges.append(QLineF(poly[base + i], poly[base + (i + 1) % n0]));
                base += n0;
            }
            QVarLengthArray<double, 16> xs;
            const quint32 ia = quint32(alpha), inv = 255u - ia;
            for (int y = y0; y <= y1; ++y) {
                const double yc = y + 0.5;
                xs.clear();
                for (const QLineF &ed : edges) {
                    const QPointF p1 = ed.p1(), p2 = ed.p2();
                    if ((p1.y() <= yc && p2.y() > yc) || (p2.y() <= yc && p1.y() > yc))
                        xs.append(p1.x() + (yc - p1.y()) * (p2.x() - p1.x()) / (p2.y() - p1.y()));
                }
                std::sort(xs.begin(), xs.end());
                quint32 *row = bits + y * stride;
                float *zr = zb + y * W;
                int *ir = zid + y * W;
                for (int k = 0; k + 1 < xs.size(); k += 2) {
                    const int xa = std::max(0, int(std::ceil(xs[k] - 0.5)));
                    const int xe = std::min(W - 1, int(std::floor(xs[k + 1] - 0.5)));
                    float z = a * (float(xa) + 0.5f) + b * float(yc) + c;
                    for (int x = xa; x <= xe; ++x, z += a) {
                        if (z >= zr[x])
                            continue;
                        if (alpha >= 250) {
                            zr[x] = z;
                            ir[x] = face;
                            row[x] = col;
                        } else {                               // translucent: blend, keep the depth behind
                            const quint32 d = row[x];
                            auto ch = [&](int sh) {
                                const quint32 s = ((col >> sh) & 0xff) * ia / 255u;
                                return ((s + ((d >> sh) & 0xff) * inv / 255u) & 0xff) << sh;
                            };
                            row[x] = ch(24) | ch(16) | ch(8) | ch(0);
                        }
                    }
                }
            }
        };

        struct Glass { QPolygonF poly; QList<int> loops; float a, b, c, depth; QRgb col; int alpha; };
        QList<Glass> glass;
        for (const SketchItem *it : std::as_const(solids)) {
            QColor base = it->fill.isValid() ? it->fill : QColor(196, 205, 220);
            if (it->selected)
                base = mixColor(base, accent(), 0.35);
            else if (it == m_hover)
                base = mixColor(base, QColor(255, 209, 102), 0.3);
            const int alpha = it->fill.isValid() ? it->fill.alpha() : 255;
            for (const SolidFace &f : solidFaces(it)) {
                V3 cen;
                for (const V3 &q : f.v)
                    cen += q;
                cen /= float(f.v.size());
                if (V3::dotProduct(f.n, persp ? cen - eye : m_bf) >= 0)
                    continue;                                  // back face
                QPolygonF poly;
                QVector<float> zs;
                bool ok = true;
                for (const V3 &q : f.v) {
                    QPointF sp;
                    qreal d = 0;
                    if (!projectD(q, sp, &d)) { ok = false; break; }
                    poly << sp * sc;
                    zs << (persp ? float(-1.0 / d) : float(d));
                }
                if (!ok || poly.size() < 3)
                    continue;
                // depth plane through the widest triangle of the face
                int j = 1;
                for (int k = 2; k < poly.size(); ++k)
                    if (QLineF(poly[0], poly[k]).length() > QLineF(poly[0], poly[j]).length())
                        j = k;
                int k2 = -1;
                double area = 0;
                for (int k = 1; k < poly.size(); ++k) {
                    const QPointF d1 = poly[j] - poly[0], d2 = poly[k] - poly[0];
                    const double ar = d1.x() * d2.y() - d1.y() * d2.x();
                    if (std::fabs(ar) > std::fabs(area)) { area = ar; k2 = k; }
                }
                if (k2 < 0 || std::fabs(area) < 0.5)
                    continue;                                  // edge-on
                const double x0 = poly[0].x(), y0 = poly[0].y(), z0 = zs[0];
                const double x1 = poly[j].x() - x0, y1 = poly[j].y() - y0, z1 = zs[j] - z0;
                const double x2 = poly[k2].x() - x0, y2 = poly[k2].y() - y0, z2 = zs[k2] - z0;
                const double a = (z1 * y2 - z2 * y1) / area;
                const double b = (x1 * z2 - x2 * z1) / area;
                const double c = z0 - a * x0 - b * y0;
                const double lit = 0.46 + 0.54 * std::max(0.0, double(V3::dotProduct(f.n, lamp)));
                const QRgb col = qRgb(std::min(255, int(base.red() * lit)), std::min(255, int(base.green() * lit)),
                                      std::min(255, int(base.blue() * lit)));
                if (alpha >= 250) {
                    m_zPlanes.append(QVector3D(float(a), float(b), float(c)));
                    raster(poly, f.loops, float(a), float(b), float(c), col, 255, int(m_zPlanes.size()) - 1);
                } else {
                    float dsum = 0;
                    for (float z : std::as_const(zs)) dsum += z;
                    glass.append({poly, f.loops, float(a), float(b), float(c), dsum / zs.size(), col, alpha});
                }
            }
        }
        std::sort(glass.begin(), glass.end(), [](const Glass &x, const Glass &y) { return x.depth > y.depth; });
        for (const Glass &g : std::as_const(glass))
            raster(g.poly, g.loops, g.a, g.b, g.c, g.col, g.alpha, -1);
        m_zKey = key;
        m_zValid = true;
    }
    p.drawImage(QRectF(0, 0, W / sc, H / sc), m_zImg);

    // edges, only where nothing is in front of them
    for (const SketchItem *it : std::as_const(solids)) {
        const bool sel = it->selected, hov = it == m_hover && !sel;
        const qreal lw = std::clamp(it->width * scaleAt(it->pts.value(0)) * m_set.lineScale, 1.0, 400.0);
        if (sel || hov) {
            QColor glow = sel ? accent() : QColor(255, 209, 102);
            glow.setAlpha(sel ? 120 : 130);
            drawSolidEdges(p, it, QPen(glow, lw + 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        QPen pen(adapt(it->color), lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        if (it->lineStyle > 0)
            pen.setStyle(Qt::DashLine);
        drawSolidEdges(p, it, pen);
    }
}

void Canvas::drawSolidEdges(QPainter &p, const SketchItem *it, const QPen &pen) const
{
    p.setPen(pen);
    const bool haveZ = m_zValid && !m_zBuf.isEmpty();
    const int W = m_zImg.width(), H = m_zImg.height();
    const qreal sc = m_zScale;
    const bool persp = perspectiveActive();
    QList<QVector<V3>> segs = strips(it);
    segs += silhouetteEdges(it);                           // the outline of rounded faces
    for (const auto &strip : std::as_const(segs)) {
        for (int i = 0; i + 1 < strip.size(); ++i) {
            QPointF sa, sb;
            qreal da = 0, db = 0;
            if (!haveZ || !projectD(strip[i], sa, &da) || !projectD(strip[i + 1], sb, &db)) {
                QLineF l;
                if (projectSeg(strip[i], strip[i + 1], l))
                    p.drawLine(l);
                continue;
            }
            const float za = persp ? float(-1.0 / da) : float(da), zbv = persp ? float(-1.0 / db) : float(db);
            const QPointF ia = sa * sc, ib = sb * sc;
            const int steps = std::clamp(int(QLineF(ia, ib).length() / 1.5), 1, 6000);
            // the face that owns the pixel is evaluated exactly at the edge point: an edge on that face
            // (or behind nothing) is visible, one behind it is not - no pixel-size depth slop
            auto visible = [&](double t) {
                const QPointF q = ia + (ib - ia) * t;
                const float z = za + (zbv - za) * float(t);
                const int x = int(std::floor(q.x())), y = int(std::floor(q.y()));
                if (x < 0 || y < 0 || x >= W || y >= H)
                    return true;
                const int id = m_zId[y * W + x];
                if (id < 0 || id >= m_zPlanes.size())
                    return true;
                const QVector3D &pl = m_zPlanes[id];
                const float zf = pl.x() * float(q.x()) + pl.y() * float(q.y()) + pl.z();
                const float tol = persp ? std::fabs(z) * 2e-4f : 0.05f + std::fabs(z) * 2e-5f;
                return z <= zf + tol;
            };
            int run = -1;
            for (int k = 0; k <= steps; ++k) {
                const bool v = visible(double(k) / steps);
                if (v && run < 0)
                    run = k;
                if (run >= 0 && (!v || k == steps)) {
                    const int end = v ? k : k - 1;
                    if (end > run)
                        p.drawLine(sa + (sb - sa) * (double(run) / steps), sa + (sb - sa) * (double(end) / steps));
                    run = -1;
                }
            }
        }
    }
}

void Canvas::drawFaceHighlight(QPainter &p, const QVector<V3> &v, const QColor &c, const QList<int> &loops) const
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    const QList<int> sizes = loops.isEmpty() ? QList<int>{int(v.size())} : loops;
    int k = 0;
    for (int n0 : sizes) {
        QPolygonF poly;
        for (int i = 0; i < n0 && k + i < v.size(); ++i) {
            QPointF sp;
            if (!projectD(v[k + i], sp))
                return;
            poly << sp;
        }
        path.addPolygon(poly);
        path.closeSubpath();
        k += n0;
    }
    p.save();
    QColor f = c;
    f.setAlpha(70);
    p.setBrush(f);
    p.setPen(QPen(c, 2.0));
    p.drawPath(path);
    p.restore();
}

// Box / Press-Pull feedback drawn over the scene
void Canvas::drawSolidOverlay(QPainter &p) const
{
    if (m_tool == Tool::Shell) {                           // the face that will be left open
        if (m_hoverFaceValid)
            drawFaceHighlight(p, m_hoverFace.v, QColor(230, 90, 80), m_hoverFace.loops);
        QFont fnt = QApplication::font();
        fnt.setPixelSize(12);
        fnt.setBold(true);
        p.setFont(fnt);
        const QFontMetricsF fm(fnt);
        const bool typed = !m_dyn[0].text.isEmpty();
        const QString txt = "T " + (typed ? dynShown(0, true) : Units::formatDim(m_shellT)) +
                            (m_hoverFaceValid && !typed ? "   open this face" : QString());
        const QPointF c = QPointF(m_lastMouse) + QPointF(22, -26);
        const QRectF r(c.x(), c.y() - 11, fm.horizontalAdvance(txt) + 14, 22);
        p.setPen(QPen(typed ? QColor(255, 209, 102) : accent(), typed ? 2 : 1));
        p.setBrush(typed ? QColor(66, 52, 18, 235) : QColor(12, 18, 28, 225));
        p.drawRoundedRect(r, 5, 5);
        p.setPen(typed ? QColor(255, 224, 150) : QColor(215, 230, 250));
        p.drawText(r, Qt::AlignCenter, txt);
        p.setBrush(Qt::NoBrush);
        return;
    }
    if (m_tool != Tool::Box && m_tool != Tool::PressPull)
        return;
    if (m_tool == Tool::PressPull && !m_ppItem)             // Shift+clicked faces waiting to be pulled
        for (const PullPick &pf : m_ppSet)
            drawFaceHighlight(p, pf.face.v, accent(), pf.face.loops);
    if (m_hoverFaceValid && ((m_tool == Tool::Box && m_pending.isEmpty()) || (m_tool == Tool::PressPull && !m_ppItem)))
        drawFaceHighlight(p, m_hoverFace.v, QColor(70, 140, 255), m_hoverFace.loops);
    if (m_tool != Tool::PressPull || !m_ppItem)
        return;
    if (m_ppToFace)                                        // the face it is pulled up to
        drawFaceHighlight(p, m_ppTo.v, QColor(80, 220, 120), m_ppTo.loops);
    // the moved faces, the pull arrow and the distance field
    for (const SolidFace &f0 : m_ppFaces) {
        SolidFace f = f0;
        const SketchItem *it = f0.it;
        if (f.kind >= 0)
            for (const SolidFace &g : solidFaces(it))
                if (g.kind == f.kind) { f = g; break; }
        if (f.kind >= 0 || !it->extrude.isNull()) {
            QVector<V3> v = f.v;
            if (f.kind < 0)
                for (V3 &q : v) q += it->extrude;
            drawFaceHighlight(p, v, accent(), f.loops);
        }
    }
    QPointF a, b;
    const V3 tip = m_ppPoint + m_ppFace.n * float(m_ppDist);
    if (projectD(m_ppPoint, a) && projectD(tip, b)) {
        p.setPen(QPen(QColor(255, 209, 102), 1.6, Qt::DashLine));
        p.drawLine(a, b);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 209, 102));
        p.drawEllipse(a, 3.5, 3.5);
        p.drawEllipse(b, 3.5, 3.5);
        p.setBrush(Qt::NoBrush);
    }
    QFont fnt = QApplication::font();
    fnt.setPixelSize(12);
    fnt.setBold(true);
    p.setFont(fnt);
    const QFontMetricsF fm(fnt);
    const bool typed = !m_dyn[0].text.isEmpty();
    QString txt = "D " + (typed ? dynShown(0, true) : Units::formatDim(m_ppDist)) +
                  (m_ppNote.isEmpty() ? QString() : "  " + m_ppNote);
    if (m_ppFaces.size() > 1)
        txt += QString("   %1 faces").arg(m_ppFaces.size());
    if (m_ppFace.kind < 0 && !m_ppItem->extrude.isNull())  // what the new body will do: N / J / C / I
        txt += "   " + (m_ppOpNow == 0 ? "Auto: " + extrudeOpName(extrudeAutoOp(m_ppItem)) : extrudeOpName(m_ppOpNow));
    const QPointF c = QPointF(m_lastMouse) + QPointF(22, -26);
    const QRectF r(c.x(), c.y() - 11, fm.horizontalAdvance(txt) + 14, 22);
    p.setPen(QPen(typed ? QColor(255, 209, 102) : accent(), typed ? 2 : 1));
    p.setBrush(typed ? QColor(66, 52, 18, 235) : QColor(12, 18, 28, 225));
    p.drawRoundedRect(r, 5, 5);
    p.setPen(typed ? QColor(255, 224, 150) : QColor(215, 230, 250));
    p.drawText(r, Qt::AlignCenter, txt);
    p.setBrush(Qt::NoBrush);
}

void Canvas::updateSolidHover(const QPoint &s)
{
    SolidFace f;
    V3 hit;
    bool ok = m_tool == Tool::PressPull ? pickPullFace(s, f, hit) : faceAt(s, f, hit);
    if (ok && m_tool == Tool::Box) {                       // only faces square to a world axis carry a box
        const V3 n = f.n;
        ok = std::fabs(n.x()) > 0.999f || std::fabs(n.y()) > 0.999f || std::fabs(n.z()) > 0.999f;
    }
    if (ok != m_hoverFaceValid || (ok && f.v != m_hoverFace.v)) {
        m_hoverFaceValid = ok;
        if (ok)
            m_hoverFace = f;
        update();
    }
}

// signed distance along dir from 'from' picked by the mouse: an object snap point wins, else the
// point of the line closest to the cursor ray (rounded to the grid)
double Canvas::solidDistanceAt(const QPoint &s, const V3 &from, const V3 &dir, const SketchItem *skip,
                               QString *note) const
{
    if (note)
        note->clear();
    if (m_set.snapObject) {
        QList<SnapCand> cands;
        for (const SketchItem *it : m_items) {
            if (it == skip || it->hidden)
                continue;
            allSnapPoints(it, cands);
        }
        qreal best = 12.0;
        double d = 0;
        bool hit = false;
        for (const SnapCand &c : std::as_const(cands)) {
            QPointF sp;
            if (!projectD(c.p, sp))
                continue;
            const qreal px = QLineF(sp, QPointF(s)).length();
            const double dd = V3::dotProduct(c.p - from, dir);
            if (px < best && std::fabs(dd) > 1e-6) {
                best = px;
                d = dd;
                hit = true;
            }
        }
        if (hit) {
            if (note)
                *note = "snap";
            return d;
        }
    }
    V3 ro, rd;
    screenRay(QPointF(s), ro, rd);
    const V3 w0 = from - ro;
    const double b = V3::dotProduct(dir, rd), d0 = V3::dotProduct(dir, w0), e = V3::dotProduct(rd, w0);
    const double den = 1.0 - b * b;
    double t = 0;
    if (den < 1e-4) {
        // looking straight along the direction (a box's height in the Top view): moving the mouse up
        // the screen from the start point raises it, at the drawing scale there
        QPointF sp;
        if (!projectD(from, sp))
            return std::numeric_limits<double>::quiet_NaN();
        const double up = sp.y() - s.y();
        t = up / std::max(1e-6, double(scaleAt(from))) * (V3::dotProduct(dir, m_bf) < 0 ? 1.0 : -1.0);
        if (note)
            *note = "drag up / down";
    } else {
        t = (b * e - d0) / den;
    }
    if (m_set.snapGrid && m_gridSize > 0)
        t = std::round(t / m_gridSize) * m_gridSize;
    return t;
}

// ===========================================================================
// Box: first corner (on a face = that face's plane), opposite corner, height
// ===========================================================================
bool Canvas::boxStartOnFace(const QPoint &s)
{
    SolidFace f;
    V3 hit;
    if (!faceAt(s, f, hit))
        return false;
    int plane = -1;
    if (std::fabs(f.n.z()) > 0.999f) plane = 0;
    else if (std::fabs(f.n.y()) > 0.999f) plane = 1;
    else if (std::fabs(f.n.x()) > 0.999f) plane = 2;
    if (plane < 0)
        return false;
    if (m_boxSavedPlane < 0) {
        m_boxSavedPlane = int(m_plane);
        m_boxSavedOffset = m_planeOffset;
    }
    const Frame fr = frameFor(plane);
    m_plane = Plane(plane);
    m_planeOffset = V3::dotProduct(f.v[0], fr.N);
    m_boxOutSign = V3::dotProduct(f.n, fr.N) >= 0 ? 1.0 : -1.0;
    emit planeChanged();
    return true;
}

void Canvas::boxRestorePlane()
{
    if (m_boxSavedPlane < 0)
        return;
    m_plane = Plane(m_boxSavedPlane);
    m_planeOffset = m_boxSavedOffset;
    m_boxSavedPlane = -1;
    m_boxOutSign = 1;
    emit planeChanged();
}

void Canvas::finishBox(double h)
{
    if (m_pending.size() < 2)
        return;
    if (!std::isfinite(h) || std::fabs(h) < 1e-6) {
        emit message("Box: move the mouse away from the base or type a height (Enter = " + Units::format(m_boxHeight) + ")");
        return;
    }
    const Frame fr = planeFrame();
    const V3 a = m_pending[0], d = m_pending[1] - a;
    const double du = V3::dotProduct(d, fr.U), dv = V3::dotProduct(d, fr.V);
    auto *it = new SketchItem(SketchItem::Rect);
    it->pts = {a, a + fr.U * float(du) + fr.V * float(dv)};
    it->plane = int(m_plane);
    it->extrude = fr.N * float(h);
    it->color = m_defColor;
    it->width = m_defWidth;
    it->lineStyle = m_defStyle;
    m_pending.clear();
    dynClear();
    m_boxCurH = 0;
    boxRestorePlane();
    snapshotBegin();
    m_items.append(it);
    snapshotCommit("Box");
    emit message(QString("Box %1 x %2 x %3  -  click the next corner, Enter repeats, Esc ends")
                     .arg(Units::format(std::fabs(du)), Units::format(std::fabs(dv)), Units::format(std::fabs(h))));
    emitHint();
    update();
}

// ===========================================================================
// Press/Pull: click a face, move, click (or type a distance)
// ===========================================================================
// what Press/Pull grabs under s: a closed shape drawn on (or in front of) a board's face wins over
// that face - it is what you drew to extrude, cut or join
bool Canvas::pickPullFace(const QPoint &s, SolidFace &f, V3 &hit) const
{
    bool ok = faceAt(s, f, hit) && !f.it->locked;
    SolidFace g;
    V3 h2;
    if (flatShapeAt(s, g, h2)) {
        V3 ro, rd;
        screenRay(QPointF(s), ro, rd);
        if (!ok || V3::dotProduct(h2 - ro, rd) <= V3::dotProduct(hit - ro, rd) + 0.05f) {
            f = g;
            hit = h2;
            ok = true;
        }
    }
    return ok;
}

// Shift+click in Press/Pull: faces of several boards (or several shapes) chosen to pull together
void Canvas::pullSetToggle(const QPoint &s)
{
    SolidFace f;
    V3 hit;
    if (!pickPullFace(s, f, hit)) {
        emit message("Press/Pull: Shift+click faces (or closed shapes) to pull them together");
        return;
    }
    const int idx = int(m_items.indexOf(const_cast<SketchItem *>(f.it)));
    for (int i = 0; i < m_ppSet.size(); ++i)
        if (m_ppSet[i].item == idx && m_ppSet[i].face.kind == f.kind) {
            m_ppSet.removeAt(i);
            emit message(QString("Press/Pull: face taken off - %1 chosen").arg(m_ppSet.size()));
            emitHint();
            update();
            return;
        }
    m_ppSet.append({idx, f});
    emit message(QString("Press/Pull: %1 face(s) chosen - Shift+click more, then click a face and pull: they all move")
                     .arg(m_ppSet.size()));
    emitHint();
    update();
}

bool Canvas::pressPullStart(const QPoint &s)
{
    SolidFace f;
    V3 hit;
    bool ok = pickPullFace(s, f, hit);
    if (!ok) {
        emit message("Press/Pull: click a face of a board, or inside a closed shape (rectangle, circle, closed polyline)");
        return false;
    }
    // the clicked face leads; the Shift+clicked ones move with it, each along its own normal
    m_ppFaces = {f};
    for (const PullPick &pf : std::as_const(m_ppSet)) {
        SketchItem *it = m_items.value(pf.item);
        if (!it || (it == f.it && pf.face.kind == f.kind))
            continue;
        SolidFace g = pf.face;
        g.it = it;
        m_ppFaces << g;
    }
    m_ppSet.clear();
    m_ppOrig.clear();
    for (const SolidFace &g : std::as_const(m_ppFaces)) {
        SketchItem *it = const_cast<SketchItem *>(g.it);
        if (std::none_of(m_ppOrig.begin(), m_ppOrig.end(), [it](const PullOrig &o) { return o.it == it; }))
            m_ppOrig.append({it, it->pts, it->extrude});
    }
    // an extruded shape cuts / joins only the selected boards, when it meets any of them
    m_ppScope.clear();
    for (SketchItem *it : std::as_const(m_selected))
        if (!it->extrude.isNull())
            m_ppScope << it;
    m_ppItem = const_cast<SketchItem *>(f.it);
    m_ppFace = f;
    m_ppPoint = hit;
    m_ppOrigPts = m_ppItem->pts;
    m_ppOrigExt = m_ppItem->extrude;
    m_ppDist = 0;
    m_ppNote.clear();
    m_ppToFace = false;
    m_ppOpNow = m_ppOp;                                    // every pull starts at the default operation
    m_hoverFaceValid = false;
    dynClear();
    snapshotBegin();
    emitHint();
    update();
    return true;
}

bool Canvas::isPulling(const SketchItem *it) const
{
    return std::any_of(m_ppOrig.begin(), m_ppOrig.end(), [it](const PullOrig &o) { return o.it == it; });
}

// the face under s while pulling, when it is parallel to the pulled face: pull up to its plane
void Canvas::updatePullTarget(const QPoint &s)
{
    m_ppToFace = false;
    if (!m_set.snapObject)
        return;                                            // (over a parallel face this wins over a point snap:
                                                           //  a point on that face gives the same distance)
    SolidFace tf;
    V3 th;
    if (faceAt(QPointF(s), tf, th, m_ppItem) && !isPulling(tf.it) && std::fabs(V3::dotProduct(tf.n, m_ppFace.n)) > 0.999f) {
        m_ppTo = tf;
        m_ppToFace = true;
        m_ppNote = "to face";
    }
}

// one face of it moved by d along its normal, from the board's present shape; returns the distance used
double Canvas::pullFace(SketchItem *it, const SolidFace &f, double d)
{
    const int k = f.kind;
    if (k < 0) {                                           // a flat shape becomes a body
        it->extrude = std::fabs(d) > 1e-6 ? f.n * float(d) : V3();
    } else if (k <= 1) {                                   // the end caps: longer / shorter
        const V3 en = it->extrude.normalized();
        const double len = it->extrude.length();
        d = std::max(d, 0.1 - len);
        it->extrude = en * float(len + d);
        if (k == 0)
            for (V3 &q : it->pts)
                q -= en * float(d);
    } else {                                               // a side face moves, its neighbours stretch
        const V3 n = f.n;
        if (it->type() == SketchItem::Rect && it->pts.size() >= 2) {
            const Frame fr = frameFor(std::max(0, it->plane));
            const V3 ax = std::fabs(V3::dotProduct(n, fr.U)) > 0.5f ? fr.U : fr.V;
            const double sgn = V3::dotProduct(n, ax) > 0 ? 1.0 : -1.0;
            const double ca = V3::dotProduct(it->pts[0], ax), cb = V3::dotProduct(it->pts[1], ax);
            const bool first = sgn > 0 ? ca > cb : ca < cb;
            d = std::max(d, 0.1 - std::fabs(ca - cb));
            it->pts[first ? 0 : 1] += ax * float(sgn * d);
        } else if (it->type() == SketchItem::Circle && it->pts.size() >= 2) {
            const double r = (it->pts[1] - it->pts[0]).length();
            d = std::max(d, 0.1 - r);
            it->pts[1] = it->pts[0] + (it->pts[1] - it->pts[0]).normalized() * float(r + d);
        } else if (it->type() == SketchItem::Polyline && !it->loops.isEmpty()) {
            int g = k - 2, start = 0;                      // side faces are numbered over all loops
            for (int n0 : std::as_const(it->loops)) {
                if (g < n0) {
                    it->pts[start + g] += n * float(d);
                    it->pts[start + (g + 1) % n0] += n * float(d);
                    break;
                }
                g -= n0;
                start += n0;
            }
        } else if (it->type() == SketchItem::Polyline && it->pts.size() >= 3) {
            const int np = it->pts.size();
            const bool dup = (it->pts.first() - it->pts.last()).length() < 1e-3f;
            const int n0 = dup ? np - 1 : np;
            const int ia = (k - 2) % n0, ib = (k - 1) % n0;
            it->pts[ia] += n * float(d);
            it->pts[ib] += n * float(d);
            if (dup)
                it->pts[np - 1] = it->pts[0];
        }
    }
    return d;
}

void Canvas::applyPull(double d)
{
    if (!m_ppItem || !std::isfinite(d))
        return;
    for (const PullOrig &o : std::as_const(m_ppOrig)) {
        o.it->pts = o.pts;
        o.it->extrude = o.ext;
    }
    double lead = d;
    for (int i = 0; i < m_ppFaces.size(); ++i) {
        const SolidFace &f = m_ppFaces[i];
        double di = d;
        // up to a face: every pulled face parallel to it lands exactly on its plane
        if (m_ppToFace && std::fabs(V3::dotProduct(f.n, m_ppTo.n)) > 0.999f)
            di = V3::dotProduct(m_ppTo.v[0] - f.v[0], f.n);
        const double used = pullFace(const_cast<SketchItem *>(f.it), f, di);
        if (i == 0)
            lead = used;
    }
    m_ppDist = lead;
}

void Canvas::pressPullCommit()
{
    if (!m_ppItem)
        return;
    const double d = m_ppDist;
    QList<SketchItem *> bodies;                            // shapes that became bodies
    for (const SolidFace &f : std::as_const(m_ppFaces))
        if (f.kind < 0 && !f.it->extrude.isNull())
            bodies << const_cast<SketchItem *>(f.it);
    const int nFaces = int(m_ppFaces.size());
    const bool toFace = m_ppToFace;
    m_ppItem = nullptr;
    m_ppFaces.clear();
    m_ppOrig.clear();
    m_ppToFace = false;
    m_ppNote.clear();
    dynClear();
    // a shape pulled into a body meets the boards around it: new body, join, cut (a groove, a housing,
    // a hole) or intersect
    QStringList how;
    for (SketchItem *b : std::as_const(bodies))
        if (m_items.contains(b))
            how << extrudeCombine(b, m_ppOpNow);
    how.removeDuplicates();
    m_ppScope.clear();
    snapshotCommit(bodies.isEmpty() ? "Press/Pull" : "Extrude");   // item pointers are rebuilt after this
    emit message(std::fabs(d) > 1e-9 || toFace
                     ? QString("%1 %2%3%4  -  click the next face (Shift+click several)")
                           .arg(bodies.isEmpty() ? "Press/Pull" : "Extrude", toFace ? QString("up to the face") : Units::format(d),
                                nFaces > 1 ? QString(", %1 faces").arg(nFaces) : QString(),
                                how.isEmpty() ? QString() : "  -  " + how.join("; "))
                     : QString("Press/Pull: no change"));
    emitHint();
    update();
}

void Canvas::pressPullCancel()
{
    if (!m_ppItem)
        return;
    for (const PullOrig &o : std::as_const(m_ppOrig)) {
        o.it->pts = o.pts;
        o.it->extrude = o.ext;
    }
    m_ppItem = nullptr;
    m_ppFaces.clear();
    m_ppOrig.clear();
    m_ppScope.clear();
    m_ppToFace = false;
    m_snapshot.clear();
    dynClear();
    emit message("Press/Pull cancelled");
    emitHint();
    update();
}
// ===========================================================================
// part names and the cut list
// ===========================================================================
void Canvas::setSelectedName(const QString &name)
{
    if (m_selected.isEmpty())
        return;
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected))
        it->name = name.trimmed();
    snapshotCommit("Rename");
    emit selectionChanged();
}

void Canvas::setSelectedMaterial(const QString &material)
{
    if (m_selected.isEmpty())
        return;
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected))
        it->material = material.trimmed();
    snapshotCommit("Material");
    emit selectionChanged();
}

QString Canvas::selectedMaterial() const
{
    return m_selected.isEmpty() ? QString() : m_selected.first()->material;
}

QStringList Canvas::materialsInUse() const
{
    QStringList out;
    for (const SketchItem *it : m_items)
        if (!it->material.isEmpty() && !out.contains(it->material))
            out << it->material;
    out.sort();
    return out;
}

QString Canvas::selectedName() const
{
    return m_selected.isEmpty() ? QString() : m_selected.first()->name;
}

QList<Canvas::CutRow> Canvas::cutList(bool selectionOnly) const
{
    QList<CutRow> rows;
    const bool sel = selectionOnly && !m_selected.isEmpty();
    const QList<SketchItem *> &src = sel ? m_selected : m_items;
    auto r1 = [](double x) { return std::round(x * 10.0) / 10.0; };
    // a board cut into pieces by Union / Subtract (same part id) is one part of the cut list
    QList<QList<const SketchItem *>> units;
    QHash<int, int> partUnit;
    for (const SketchItem *it : src) {
        if (it->hidden || !isSolid(it))
            continue;
        if (it->part) {
            const auto f = partUnit.constFind(it->part);
            if (f != partUnit.constEnd()) {
                units[*f] << it;
                continue;
            }
            partUnit.insert(it->part, int(units.size()));
        }
        units << QList<const SketchItem *>{it};
    }
    for (const QList<const SketchItem *> &unit : std::as_const(units)) {
        const SketchItem *it = unit.first();
        if (it->type() == SketchItem::Revolve || it->type() == SketchItem::Pipe) {
            // linear stock: a pipe by its developed length, a turned part by its square blank
            const auto d = sweepData(it);
            const bool pipe = it->type() == SketchItem::Pipe;
            const double l = r1(pipe ? d->length : d->axial), w = r1(pipe ? it->pipeD : 2 * d->radius);
            const double t = r1(pipe ? (it->pipeWall > 0 ? it->pipeWall : it->pipeD) : 2 * d->radius);
            const QString name = !it->name.isEmpty() ? it->name : pipe ? pipeLabel(it->pipeD, it->pipeWall) : QStringLiteral("Turned part");
            bool merged = false;
            for (CutRow &r : rows)
                if (r.linear && r.name == name && r.material == it->material && std::fabs(r.l - l) < 0.05 &&
                    std::fabs(r.w - w) < 0.05 && std::fabs(r.t - t) < 0.05) {
                    ++r.qty;
                    merged = true;
                    break;
                }
            if (!merged)
                rows.append({name, l, w, t, 1, it->material, true});
            continue;
        }
        double a = 0, b = 0, t = it->extrude.length();
        if (unit.size() == 1 && it->type() == SketchItem::Circle && it->pts.size() >= 2) {
            a = b = 2 * (it->pts[1] - it->pts[0]).length();
        } else {
            const QVector<V3> ring = openRing(stripsBase(it).value(0));
            if (ring.size() < 3)
                continue;
            const V3 en = it->extrude.normalized();
            V3 u;
            double longest = -1;                           // measured along the profile's longest edge
            for (int i = 0; i < ring.size(); ++i) {
                V3 d = ring[(i + 1) % ring.size()] - ring[i];
                d -= en * V3::dotProduct(d, en);
                if (d.length() > longest) { longest = d.length(); u = d.normalized(); }
            }
            const V3 v = V3::crossProduct(en, u);
            double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300, n0 = 1e300, n1 = -1e300;
            for (const SketchItem *piece : unit)
                for (const auto &s : stripsBase(piece))
                    for (const V3 &q0 : s)
                        for (const V3 &q : {q0, q0 + piece->extrude}) {
                            const double uu = V3::dotProduct(q, u), vv = V3::dotProduct(q, v), nn = V3::dotProduct(q, en);
                            u0 = std::min(u0, uu); u1 = std::max(u1, uu);
                            v0 = std::min(v0, vv); v1 = std::max(v1, vv);
                            n0 = std::min(n0, nn); n1 = std::max(n1, nn);
                        }
            a = u1 - u0;
            b = v1 - v0;
            t = n1 - n0;
        }
        double dims[3] = {r1(a), r1(b), r1(t)};
        std::sort(dims, dims + 3, std::greater<double>());
        const QString name = it->name.isEmpty() ? QStringLiteral("Panel") : it->name;
        bool merged = false;
        for (CutRow &r : rows)
            if (r.name == name && r.material == it->material && std::fabs(r.l - dims[0]) < 0.05 &&
                std::fabs(r.w - dims[1]) < 0.05 && std::fabs(r.t - dims[2]) < 0.05) {
                ++r.qty;
                merged = true;
                break;
            }
        if (!merged)
            rows.append({name, dims[0], dims[1], dims[2], 1, it->material});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const CutRow &x, const CutRow &y) {
        if (x.material != y.material) return x.material < y.material;
        if (std::fabs(x.t - y.t) > 0.05) return x.t > y.t;
        if (x.name != y.name) return x.name < y.name;
        return x.l > y.l;
    });
    return rows;
}

QString Canvas::cutListText(const QList<CutRow> &rows)
{
    QStringList out;
    int nameW = 4, numW = 6, matW = 0;
    for (const CutRow &r : rows) {
        nameW = std::max(nameW, int(r.name.size()));
        matW = std::max(matW, int(r.material.size()));
        for (double v : {r.l, r.w, r.t})
            numW = std::max(numW, int(Units::formatDim(v).size()));
    }
    nameW = std::min(nameW, 28);
    auto pad = [](const QString &s, int w, bool right) { return right ? s.rightJustified(w) : s.leftJustified(w, ' ', true); };
    out << QString("CUT LIST (%1)").arg(Units::unitLabel());
    matW = std::min(matW, 22);
    const QString matHead = matW ? "  " + pad("Material", std::max(matW, 8), false) : QString();
    if (matW)
        matW = std::max(matW, 8);
    out << pad("Part", nameW, false) + "  " + pad("L", numW, true) + "   " + pad("W", numW, true) + "   " +
               pad("T", numW, true) + "   Qty" + matHead;
    out << QString(nameW + 3 * numW + 16 + (matW ? matW + 2 : 0), '-');
    double area = 0;
    int parts = 0;
    for (const CutRow &r : rows) {
        out << pad(r.name, nameW, false) + "  " + pad(Units::formatDim(r.l), numW, true) + " x " +
                   pad(Units::formatDim(r.w), numW, true) + " x " + pad(Units::formatDim(r.t), numW, true) + "   " +
                   QString::number(r.qty).rightJustified(3) + (matW ? "  " + pad(r.material, matW, false) : QString());
        area += r.l * r.w * r.qty;
        parts += r.qty;
    }
    out << QString(nameW + 3 * numW + 16 + (matW ? matW + 2 : 0), '-');
    out << QString("%1 parts   board area %2").arg(parts).arg(Units::formatBoardArea(area));
    return out.join('\n');
}

void Canvas::placeCutList(const V3 &at, bool selectionOnly)
{
    const QList<CutRow> rows = cutList(selectionOnly);
    if (rows.isEmpty()) {
        emit message("Cut list: there are no solid boards - make them with Box, Press/Pull or Extrude solid");
        return;
    }
    auto *t = new SketchItem(SketchItem::Text);
    t->pts = {at};
    t->plane = int(m_plane);
    t->text = cutListText(rows);
    t->fontFamily = "Consolas";
    t->textSize = std::max(m_defTextSize, 12.0);
    t->color = m_defColor;
    snapshotBegin();
    m_items.append(t);
    snapshotCommit("Cut List");
    emit message(QString("Cut list placed: %1 line(s)").arg(rows.size()));
    update();
}
