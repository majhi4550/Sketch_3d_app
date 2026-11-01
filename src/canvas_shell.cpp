#include "canvas.h"
#include "canvas_util.h"
#include "units.h"

#include <algorithm>
#include <cmath>

using V3 = QVector3D;

// ===========================================================================
// Shell: a board hollowed to walls of one thickness, with the face you click left open.
//  - a box becomes a carcass of separate boards: the pair of sides full size, the other pair between
//    them, the back inside all four - each its own part in the cut list, named, grouped;
//  - any other board (rounded, L-shaped, round) opened at an end: the walls (the profile with an
//    inner outline) and an end plate, grouped.
// ===========================================================================
namespace {
int worldAxis(const V3 &d)
{
    const float ax = std::fabs(d.x()), ay = std::fabs(d.y()), az = std::fabs(d.z());
    return ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
}

QString panelName(const V3 &n, bool back)
{
    if (std::fabs(n.z()) > 0.7f)
        return n.z() > 0 ? "Top" : "Bottom";
    if (back)
        return "Back";
    if (std::fabs(n.x()) > 0.7f)
        return "Side";
    return n.y() < 0 ? "Front" : "Back";
}

// the ring moved inward by t (mitred corners); empty when the wall does not fit
QList<V3> insetRing(QList<V3> r, const V3 &en, double t)
{
    const int n = int(r.size());
    V3 area;
    for (int i = 0; i < n; ++i)
        area += V3::crossProduct(r[i], r[(i + 1) % n]);
    if (V3::dotProduct(area, en) < 0)
        std::reverse(r.begin(), r.end());                  // counter-clockwise about en: inward = left
    QList<V3> out;
    for (int i = 0; i < n; ++i) {
        const V3 p = r[(i + n - 1) % n], c = r[i], q = r[(i + 1) % n];
        const V3 n1 = V3::crossProduct(en, c - p).normalized(), n2 = V3::crossProduct(en, q - c).normalized();
        const float den = 1.0f + V3::dotProduct(n1, n2);
        if (den < 0.1f)
            return {};                                     // a spike: no sensible wall
        out << c + (n1 + n2) * float(t / den);
    }
    for (int i = 0; i < n; ++i)                            // every inner edge must run the way its outer edge does
        if (V3::dotProduct(out[(i + 1) % n] - out[i], r[(i + 1) % n] - r[i]) <= 0)
            return {};
    return out;
}
} // namespace

// a board with corner o, profile edges a and b, swept along c: a rectangle when square to the axes
SketchItem *Canvas::boxItem(const V3 &o, const V3 &a, const V3 &b, const V3 &c, const SketchItem *like) const
{
    const int pl = std::fabs(c.normalized().z()) > 0.999f ? 0 : std::fabs(c.normalized().y()) > 0.999f ? 1
                 : std::fabs(c.normalized().x()) > 0.999f ? 2 : -1;
    SketchItem *n = nullptr;
    if (pl >= 0 && worldAxis(a) != worldAxis(c) && std::fabs(a.normalized()[worldAxis(a)]) > 0.999f) {
        n = new SketchItem(SketchItem::Rect);
        n->pts = {o, o + a + b};
        n->plane = pl;
    } else {
        n = new SketchItem(SketchItem::Polyline);
        n->pts = {o, o + a, o + a + b, o + b, o};
        n->plane = like ? like->plane : 0;
    }
    n->extrude = c;
    if (like) {
        n->color = like->color;
        n->fill = like->fill;
        n->width = like->width;
        n->lineStyle = like->lineStyle;
        n->material = like->material;
    }
    return n;
}

// it hollowed with the face (kind, outward normal openN) left open; the new boards replace it
bool Canvas::shellSolid(SketchItem *it, int openKind, const V3 &openN, double t, QString &err)
{
    if (t <= 0) {
        err = "give a wall thickness (type it + Enter)";
        return false;
    }
    int gid = 0;
    for (const SketchItem *x : std::as_const(m_items))
        gid = std::max(gid, x->group);
    ++gid;
    const QString prefix = it->name.isEmpty() ? QString() : it->name + " ";
    QList<SketchItem *> made;
    bool closed = true;
    const QList<QList<V3>> rings = profileRings(it, closed);
    if (isBoxSolid(it)) {
        const QList<V3> &r = rings.first();
        const V3 O = r[0];
        const V3 ax[3] = {r[1] - r[0], r[3] - r[0], it->extrude};
        V3 u[3];
        double L[3];
        for (int k = 0; k < 3; ++k) {
            L[k] = ax[k].length();
            u[k] = ax[k] / float(L[k]);
        }
        int k = 0;
        for (int i = 1; i < 3; ++i)
            if (std::fabs(V3::dotProduct(u[i], openN)) > std::fabs(V3::dotProduct(u[k], openN)))
                k = i;
        const bool openMax = V3::dotProduct(u[k], openN) > 0;
        const int i1 = (k + 1) % 3, i2 = (k + 2) % 3;
        // the full pair: across the other horizontal direction (a cabinet's sides), or along X when it opens up
        int p = i1;
        const int wk = worldAxis(u[k]);
        const int want = wk == 2 ? 0 : (wk == 0 ? 1 : 0);
        if (worldAxis(u[i2]) == want)
            p = i2;
        const int q = p == i1 ? i2 : i1;
        if (2 * t >= L[p] - 1e-6 || 2 * t >= L[q] - 1e-6 || t >= L[k] - 1e-6) {
            err = QString("the walls (%1) do not fit in this box").arg(Units::format(t));
            return false;
        }
        // a panel from local box coordinates lo..hi, its thickness along axis 'th'
        auto panel = [&](const double lo[3], const double hi[3], int th, const V3 &outward, bool back) {
            const int e1 = (th + 1) % 3, e2 = (th + 2) % 3;
            V3 o = O;
            for (int d = 0; d < 3; ++d)
                o += u[d] * float(lo[d]);
            SketchItem *n = boxItem(o, u[e1] * float(hi[e1] - lo[e1]), u[e2] * float(hi[e2] - lo[e2]),
                                    u[th] * float(hi[th] - lo[th]), it);
            n->name = prefix + panelName(outward, back);
            n->group = gid;
            made << n;
        };
        for (int side = 0; side < 2; ++side) {             // the full pair
            double lo[3] = {0, 0, 0}, hi[3] = {L[0], L[1], L[2]};
            lo[p] = side ? L[p] - t : 0;
            hi[p] = side ? L[p] : t;
            panel(lo, hi, p, u[p] * float(side ? 1 : -1), false);
        }
        for (int side = 0; side < 2; ++side) {             // between them
            double lo[3] = {0, 0, 0}, hi[3] = {L[0], L[1], L[2]};
            lo[p] = t;
            hi[p] = L[p] - t;
            lo[q] = side ? L[q] - t : 0;
            hi[q] = side ? L[q] : t;
            panel(lo, hi, q, u[q] * float(side ? 1 : -1), false);
        }
        double lo[3] = {0, 0, 0}, hi[3] = {L[0], L[1], L[2]};  // the back, inside all four
        lo[p] = t; hi[p] = L[p] - t;
        lo[q] = t; hi[q] = L[q] - t;
        lo[k] = openMax ? 0 : L[k] - t;
        hi[k] = openMax ? t : L[k];
        panel(lo, hi, k, u[k] * float(openMax ? -1 : 1), true);
    } else {
        if (openKind > 1 || openKind < 0) {
            err = "click an END face of this board to leave open (or shell a box from any side)";
            return false;
        }
        QList<V3> outer;
        if (it->type() == SketchItem::Circle) {
            const bool fine = m_fine;
            m_fine = true;
            QVector<V3> s = stripsBase(it).value(0);
            m_fine = fine;
            for (int i = 0; i + 1 < s.size(); i += 3)      // 120 sides: smooth, still light
                outer << s[i];
        } else {
            if (rings.size() != 1 || !closed) {
                err = "a board with holes cannot be shelled";
                return false;
            }
            outer = rings.first();
        }
        const V3 e = it->extrude, en = e.normalized();
        const double len = e.length();
        if (t >= len - 1e-6) {
            err = QString("the end plate (%1) is as thick as the board").arg(Units::format(t));
            return false;
        }
        const QList<V3> inner = insetRing(outer, en, t);
        if (inner.isEmpty()) {
            err = QString("the walls (%1) are too thick for this shape").arg(Units::format(t));
            return false;
        }
        const bool openTop = openKind == 1;                // the far end open: the plate at the base
        const V3 plateShift = openTop ? V3() : en * float(len - t), wallShift = openTop ? en * float(t) : V3();
        auto shifted = [](QList<V3> r, const V3 &d) { for (V3 &q : r) q += d; return r; };
        SketchItem *walls = ringsToItem(it, {shifted(outer, wallShift), shifted(inner, wallShift)}, true,
                                        en * float(len - t), it->plane);
        SketchItem *plate = ringsToItem(it, {shifted(outer, plateShift)}, true, en * float(t), it->plane);
        walls->name = prefix + "Walls";
        plate->name = prefix + (openTop ? "Bottom" : "Top");
        for (SketchItem *n : {walls, plate}) {
            n->group = gid;
            n->part = 0;
            made << n;
        }
    }
    int at = int(m_items.indexOf(it));
    m_items.removeAll(it);
    m_selected.removeAll(it);
    if (m_hover == it) m_hover = nullptr;
    if (m_editItem == it) m_editItem = nullptr;
    delete it;
    at = std::clamp(at, 0, int(m_items.size()));
    for (SketchItem *n : std::as_const(made))
        m_items.insert(at++, n);
    return true;
}

void Canvas::shellClick(const QPoint &s)
{
    SolidFace f;
    V3 hit;
    if (!faceAt(QPointF(s), f, hit) || f.it->locked) {
        emit message("Shell: click the face of a board to leave open (a cabinet's front, a drawer's top)");
        return;
    }
    QString err;
    snapshotBegin();
    if (!shellSolid(const_cast<SketchItem *>(f.it), f.kind, f.n, m_shellT, err)) {
        m_snapshot.clear();
        emit message("Shell: " + err);
        return;
    }
    snapshotCommit("Shell");
    m_hoverFaceValid = false;
    emit selectionChanged();
    emit message(QString("Shell %1: open where you clicked - the boards are separate parts, grouped").arg(Units::format(m_shellT)));
    update();
}

// Properties > Shell: every selected board, open at its far end (a box: the top of a drawer / tray)
void Canvas::shellBody(double wall)
{
    QList<SketchItem *> sel;
    for (SketchItem *it : std::as_const(m_selected))
        if (!it->extrude.isNull() && !it->locked)
            sel << it;
    if (sel.isEmpty()) {
        emit message("Shell: select a board (or use the Shell tool and click the face to leave open)");
        return;
    }
    snapshotBegin();
    int n = 0;
    QString err;
    for (SketchItem *it : std::as_const(sel)) {
        if (shellSolid(it, 1, it->extrude.normalized(), wall, err))
            ++n;
    }
    if (n == 0) {
        m_snapshot.clear();
        emit message("Shell: " + err);
        return;
    }
    snapshotCommit("Shell");
    emit selectionChanged();
    emit message(QString("Shelled %1 board(s), %2 walls, open at the far end%3").arg(n).arg(Units::format(wall))
                     .arg(err.isEmpty() ? QString() : "  (" + err + ")"));
    update();
}
