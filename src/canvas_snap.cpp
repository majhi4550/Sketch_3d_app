#include "canvas.h"
#include "canvas_util.h"
#include "units.h"

#include <QQuaternion>

#include <algorithm>
#include <cmath>

using V3 = QVector3D;

// ===========================================================================
// Object snap, 3D: every object offers its points from ONE place (allSnapPoints), so drawing, Move,
// Mirror, Rotate, Press/Pull and the gizmo all snap to the same things:
//   2D shapes  - endpoints, midpoints, centres;
//   boards     - every corner (both ends), every edge midpoint (also along the thickness), the centre
//                of every flat face, the centre of the body;
//   turned parts and pipes - the centres of their ends and creases, the ends of a pipe, the body centre.
// Each kind can be switched off (ViewSettings::osnap, View > Object snap settings).
// ===========================================================================
int Canvas::osnapBit(int kind)
{
    switch (kind) {
    case 2:  return OsEnd;
    case 3:  return OsMid;
    case 4:  return OsCen;
    case 6:  return OsInt;
    case 7:  return OsPerp;
    case 9:  return OsNear;
    case 10: return OsFaceCen;
    case 11: return OsBodyCen;
    case 12: return OsOnFace;
    default: return 0;
    }
}

bool Canvas::osnapOn(int kind) const
{
    const int b = osnapBit(kind);
    return b == 0 || (m_set.osnap & b);
}

QString Canvas::osnapName(int bit)
{
    switch (bit) {
    case OsEnd:     return "Endpoint / corner";
    case OsMid:     return "Midpoint (also edges along a board's thickness)";
    case OsCen:     return "Centre (circles, arcs, rectangles, round ends)";
    case OsInt:     return "Intersection";
    case OsPerp:    return "Perpendicular";
    case OsNear:    return "Nearest point on a line / edge";
    case OsFaceCen: return "Face centre (every flat face of a body)";
    case OsBodyCen: return "Body centre";
    case OsOnFace:  return "On a face (Move, Mirror, Rotate pivot: the point on the face under the cursor)";
    default:        return QString();
    }
}

void Canvas::allSnapPoints(const SketchItem *it, QList<SnapCand> &out) const
{
    if (!isSolid(it)) {
        itemSnapPoints(it, out);
        return;
    }
    // a body's points need its faces: made once and kept while the body is unchanged (every mouse move asks)
    quint64 key = quint64(it->type()) * 1000003u;
    auto mix = [&key](double v) { key = (key ^ qHash(v)) * 0x100000001b3ULL; };
    for (const V3 &p : it->pts) { mix(p.x()); mix(p.y()); mix(p.z()); }
    mix(it->extrude.x()); mix(it->extrude.y()); mix(it->extrude.z());
    mix(it->sweep); mix(it->pipeD); mix(it->pipeWall); mix(it->bendR);
    mix(double(it->loops.size()));
    auto f = m_snapCache.constFind(it);
    if (f != m_snapCache.constEnd() && f->key == key) {
        out += f->pts;
        return;
    }
    if (m_snapCache.size() > 8192)
        m_snapCache.clear();
    SnapCache c;
    c.key = key;
    solidSnapPoints(it, c.pts);
    out += c.pts;
    m_snapCache.insert(it, c);
}

void Canvas::solidSnapPoints(const SketchItem *it, QList<SnapCand> &out) const
{
    if (it->type() == SketchItem::Revolve || it->type() == SketchItem::Pipe) {
        const auto d = sweepData(it);
        V3 lo(1e30f, 1e30f, 1e30f), hi = -lo;
        auto grow = [&](const V3 &q) {
            lo = V3(std::min(lo.x(), q.x()), std::min(lo.y(), q.y()), std::min(lo.z(), q.z()));
            hi = V3(std::max(hi.x(), q.x()), std::max(hi.y(), q.y()), std::max(hi.z(), q.z()));
        };
        for (const SweepGrid &g : d->grids)
            for (const QVector<V3> &r : g.rings)
                for (const V3 &q : r)
                    grow(q);
        if (it->type() == SketchItem::Pipe) {
            if (!it->pts.isEmpty()) {
                out.append({it->pts.first(), 4});             // the centres of the two ends
                out.append({it->pts.last(), 4});
            }
            for (const SweepCap &c : d->caps)                  // the rim points where the ends meet the axes
                for (int k = 0, n = int(c.v.size()) / (c.loops.isEmpty() ? 1 : 2); k < n; k += std::max(1, n / 4))
                    out.append({c.v[k], 2});
        } else {
            const V3 A = it->pts.value(0), ax = (it->pts.value(1) - A).normalized();
            for (const QVector<V3> &f : d->feature) {         // a crease circle: its centre lies on the axis
                if (f.isEmpty())
                    continue;
                const V3 q = f.first();
                out.append({A + ax * V3::dotProduct(q - A, ax), 4});
                for (int k = 0; k + 1 < f.size(); k += std::max(1, int(f.size() - 1) / 4))
                    out.append({f[k], 2});
            }
            for (const SweepCap &c : d->caps) {               // the flat ends of a part turned less than 360
                V3 cen;
                for (const V3 &q : c.v) cen += q;
                out.append({cen / float(std::max<int>(1, c.v.size())), 10});
            }
        }
        if (lo.x() <= hi.x())
            out.append({(lo + hi) * 0.5f, 11});
        return;
    }
    // a board: the profile's points at both ends, the edges along the thickness, the faces, the body
    const int n0 = out.size();
    itemSnapPoints(it, out);
    const V3 e = it->extrude;
    const int n1 = out.size();
    for (int i = n0; i < n1; ++i)
        out.append({out[i].p + e, out[i].kind});
    V3 lo(1e30f, 1e30f, 1e30f), hi = -lo;
    for (const auto &ring : stripsBase(it)) {
        const int n = int(ring.size()) - ((ring.size() > 1 && (ring.first() - ring.last()).length() < 1e-4f) ? 1 : 0);
        for (int k = 0; k < n; ++k) {
            const V3 q = ring[k];
            for (const V3 &p : {q, q + e}) {
                lo = V3(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
                hi = V3(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
            }
            if (it->type() != SketchItem::Circle && (n < 3 || !smoothVertex(ring[(k + n - 1) % n], q, ring[(k + 1) % n])))
                out.append({q + e * 0.5f, 3});                  // mid-height of an edge along the thickness
        }
    }
    for (const SolidFace &f : solidFaces(it)) {                // the centre of every flat face
        if (f.kind >= 2) {
            // rounded walls (fillets, circles) are many narrow faces: no centres there
            const V3 a = f.v.value(0), b = f.v.value(1);
            if (it->type() == SketchItem::Circle || (b - a).length() < 0.02f * e.length() + 1.0f)
                continue;
        }
        V3 c;
        const int n = f.loops.isEmpty() ? int(f.v.size()) : f.loops.first();   // the outline, not the holes
        for (int k = 0; k < n; ++k)
            c += f.v[k];
        out.append({c / float(std::max(1, n)), 10});
    }
    out.append({(lo + hi) * 0.5f, 11});
}

// the face under s, skipping the bodies 'skip' says so
bool Canvas::faceAtIf(const QPointF &s, SolidFace &face, V3 &hit, const std::function<bool(const SketchItem *)> &skip) const
{
    V3 ro, rd;
    screenRay(s, ro, rd);
    const qreal nearT = perspectiveActive() ? nearDist() : -1e18;
    qreal best = 1e300;
    bool found = false;
    for (const SketchItem *it : m_items) {
        if (it->hidden || !isSolid(it) || (skip && skip(it)))
            continue;
        for (const SolidFace &f : solidFaces(it)) {
            const qreal dn = V3::dotProduct(rd, f.n);
            if (dn > -1e-6)
                continue;
            const qreal t = V3::dotProduct(f.v[0] - ro, f.n) / dn;
            if (t < nearT || t >= best)
                continue;
            const V3 q = ro + rd * float(t);
            // inside the face (holes count): the same test faceAt uses
            QList<int> sizes = f.loops.isEmpty() ? QList<int>{int(f.v.size())} : f.loops;
            const float ax = std::fabs(f.n.x()), ay = std::fabs(f.n.y()), az = std::fabs(f.n.z());
            auto uv = [&](const V3 &p) {
                if (ax >= ay && ax >= az) return QPointF(p.y(), p.z());
                return ay >= az ? QPointF(p.x(), p.z()) : QPointF(p.x(), p.y());
            };
            bool in = false;
            int k = 0;
            for (int n0 : sizes) {
                QPolygonF poly;
                for (int i = 0; i < n0 && k + i < f.v.size(); ++i)
                    poly << uv(f.v[k + i]);
                if (poly.containsPoint(uv(q), Qt::OddEvenFill))
                    in = !in;
                k += n0;
            }
            if (!in)
                continue;
            best = t;
            face = f;
            hit = q;
            found = true;
        }
    }
    return found;
}

// Move with its base point on a face of the moving body: over a parallel face of another body the
// move goes straight along that face's normal until the two faces meet ("up to face")
bool Canvas::moveUpToFace(const QPoint &screen, Snap &s)
{
    m_moveToValid = false;
    if (m_tool != Tool::Move || m_moveStage < 1 || m_pending.size() != 1 || !m_moveBaseOn)
        return false;
    SolidFace tf;
    V3 th;
    if (!faceAtIf(QPointF(screen), tf, th, [](const SketchItem *it) { return it->selected; }))
        return false;
    if (std::fabs(V3::dotProduct(tf.n, m_moveBaseN)) < 0.999f)
        return false;
    const double d = V3::dotProduct(tf.v[0] - m_moveBasePt, m_moveBaseN);
    s.pt = m_pending.first() + m_moveBaseN * float(d);
    s.kind = 13;
    s.valid = true;
    s.note = Units::format(std::fabs(d));
    m_moveTo = tf;
    m_moveToValid = true;
    return true;
}

// the face (of a selected body) the Move base point was picked on: its plane and normal
void Canvas::noteMoveBase(const QPoint &screen)
{
    m_moveBaseOn = false;
    SolidFace f;
    V3 hit;
    if (faceAt(QPointF(screen), f, hit) && f.it->selected) {
        m_moveBaseOn = true;
        m_moveBaseN = f.n;
        m_moveBasePt = f.v[0];
    }
}
