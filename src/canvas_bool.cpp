#include "canvas.h"
#include "canvas_util.h"
#include "units.h"

#include <QPainterPath>

#include <algorithm>
#include <cmath>

using V3 = QVector3D;

// ===========================================================================
// Union / Subtract / Intersect of solid boards.
// Every board is a profile swept along a direction. Along one common direction the solids are cut
// into slabs (between every start / end level); in each slab the profiles are combined in 2D with
// QPainterPath and the result is swept again. Of the possible directions the one that gives the
// fewest pieces is used, so a notch through a panel keeps it one board.
// ===========================================================================
namespace {
double polyArea(const QPolygonF &p)
{
    double a = 0;
    for (int i = 0, n = p.size(); i < n; ++i)
        a += p[i].x() * p[(i + 1) % n].y() - p[(i + 1) % n].x() * p[i].y();
    return a * 0.5;
}

// drop repeated points and points in the middle of a straight edge
QPolygonF tidy(QPolygonF s)
{
    if (s.size() >= 2 && QLineF(s.first(), s.last()).length() < 1e-6)
        s.removeLast();
    bool changed = true;
    while (changed && s.size() >= 3) {
        changed = false;
        for (int i = 0; i < s.size() && s.size() >= 3; ++i) {
            const QPointF a = s[(i + s.size() - 1) % s.size()], b = s[i], c = s[(i + 1) % s.size()];
            const QPointF d1 = b - a, d2 = c - b;
            const double l1 = std::hypot(d1.x(), d1.y()), l2 = std::hypot(d2.x(), d2.y());
            if (l1 < 1e-6 || l2 < 1e-6 || std::fabs(d1.x() * d2.y() - d1.y() * d2.x()) < 1e-7 * l1 * l2) {
                s.remove(i);
                changed = true;
                break;
            }
        }
    }
    return s;
}

// a point just inside a loop (next to the middle of its first edge)
QPointF insidePoint(const QPolygonF &s)
{
    const QPointF a = s[0], b = s[1 % s.size()];
    QPointF n(-(b.y() - a.y()), b.x() - a.x());
    const double l = std::hypot(n.x(), n.y());
    if (l < 1e-12)
        return a;
    n /= l;
    if (polyArea(s) < 0)
        n = -n;
    return (a + b) / 2.0 + n * std::min(1e-3, l * 1e-3);
}

// the loops of a path, as outer outlines each with its holes (islands in holes are outlines again)
QList<QList<QPolygonF>> shapesOf(const QPainterPath &path)
{
    QList<QPolygonF> subs;
    for (const QPolygonF &s0 : path.simplified().toSubpathPolygons()) {
        const QPolygonF s = tidy(s0);
        if (s.size() >= 3 && std::fabs(polyArea(s)) > 1e-3)
            subs << s;
    }
    QList<int> depth(subs.size(), 0);
    for (int i = 0; i < subs.size(); ++i) {
        const QPointF q = insidePoint(subs[i]);
        for (int j = 0; j < subs.size(); ++j)
            if (j != i && std::fabs(polyArea(subs[j])) > std::fabs(polyArea(subs[i])) &&
                subs[j].containsPoint(q, Qt::OddEvenFill))
                ++depth[i];
    }
    QList<QList<QPolygonF>> out;
    QList<int> outerOf;
    for (int i = 0; i < subs.size(); ++i)
        if (depth[i] % 2 == 0) {
            out << QList<QPolygonF>{subs[i]};
            outerOf << i;
        }
    for (int i = 0; i < subs.size(); ++i) {
        if (depth[i] % 2 == 0)
            continue;
        const QPointF q = insidePoint(subs[i]);
        int best = -1;
        for (int k = 0; k < outerOf.size(); ++k) {
            const QPolygonF &o = subs[outerOf[k]];
            if (depth[outerOf[k]] == depth[i] - 1 && o.containsPoint(q, Qt::OddEvenFill) &&
                (best < 0 || std::fabs(polyArea(o)) < std::fabs(polyArea(subs[outerOf[best]]))))
                best = k;
        }
        if (best >= 0)
            out[best] << subs[i];
    }
    return out;
}

bool axisRect(const QPolygonF &p)
{
    if (p.size() != 4)
        return false;
    for (int i = 0; i < 4; ++i) {
        const QPointF d = p[(i + 1) % 4] - p[i];
        if (std::fabs(d.x()) > 1e-6 && std::fabs(d.y()) > 1e-6)
            return false;
    }
    return true;
}

bool squareToAxes(const V3 &d)
{
    const V3 n = d.normalized();
    return std::fabs(n.x()) > 0.9999f || std::fabs(n.y()) > 0.9999f || std::fabs(n.z()) > 0.9999f;
}
} // namespace

// the board as a 2D profile (in u, v) swept between levels lo .. hi along a
bool Canvas::boolPrism(const SketchItem *it, const V3 &a, const V3 &u, const V3 &v, QPainterPath &path, double &lo,
                       double &hi) const
{
    if (it->extrude.isNull())
        return false;
    const V3 e = it->extrude;
    const double ea = V3::dotProduct(e, a);
    path = QPainterPath();
    path.setFillRule(Qt::OddEvenFill);
    if (std::fabs(std::fabs(ea) - e.length()) < 1e-3 * e.length()) {      // swept along a: its own profile
        double base = 0;
        bool first = true;
        for (const auto &s : stripsBase(it)) {
            QPolygonF poly;
            for (const V3 &q : s)
                poly << QPointF(V3::dotProduct(q, u), V3::dotProduct(q, v));
            path.addPolygon(poly);
            path.closeSubpath();
            if (first) {
                base = V3::dotProduct(s.first(), a);
                first = false;
            }
        }
        lo = std::min(base, base + ea);
        hi = std::max(base, base + ea);
        return !path.isEmpty();
    }
    // a box standing the other way: exactly its bounding box while everything is square to the axes
    if (it->type() == SketchItem::Rect && squareToAxes(e) && squareToAxes(a) && squareToAxes(u)) {
        double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
        lo = 1e300;
        hi = -1e300;
        for (const V3 &c : rectCorners(it))
            for (const V3 &q : {c, c + e}) {
                const double uu = V3::dotProduct(q, u), vv = V3::dotProduct(q, v), aa = V3::dotProduct(q, a);
                u0 = std::min(u0, uu); u1 = std::max(u1, uu);
                v0 = std::min(v0, vv); v1 = std::max(v1, vv);
                lo = std::min(lo, aa); hi = std::max(hi, aa);
            }
        path.addRect(QRectF(QPointF(u0, v0), QPointF(u1, v1)));
        return true;
    }
    return false;
}

void Canvas::booleanSelection(BoolOp op, bool keepCutters)
{
    const QString opName = op == BoolOp::Union ? "Union" : (op == BoolOp::Subtract ? "Subtract" : "Intersect");
    QList<SketchItem *> ops;
    for (SketchItem *it : std::as_const(m_selected))
        if (!it->extrude.isNull() && !it->locked)
            ops << it;
    if (ops.size() < 2) {
        emit message(opName + (op == BoolOp::Subtract
                                   ? ": select the board to cut FIRST, then Ctrl+click the boards that cut it"
                                   : ": select two or more solid boards"));
        return;
    }
    QString msg;
    QList<SketchItem *> made;
    snapshotBegin();
    if (!booleanItems(ops, op, keepCutters, msg, made)) {
        m_snapshot.clear();
        emit message(msg);
        return;
    }
    for (SketchItem *it : std::as_const(m_items))
        it->selected = false;
    m_selected.clear();
    for (SketchItem *n : std::as_const(made)) {
        n->selected = true;
        m_selected << n;
    }
    snapshotCommit(opName);
    emit selectionChanged();
    emit message(msg);
    update();
}

// the work of Union / Subtract / Intersect on ops (the first is the board kept or cut); the caller
// makes the undo step, so an extrude-cut or a joint is one step
bool Canvas::booleanItems(const QList<SketchItem *> &ops, BoolOp op, bool keepCutters, QString &msg,
                          QList<SketchItem *> &made)
{
    const QString opName = op == BoolOp::Union ? "Union" : (op == BoolOp::Subtract ? "Subtract" : "Intersect");
    made.clear();
    if (ops.size() < 2)
        return false;
    const SketchItem *A = ops.first();

    struct Slab { double s0, s1; QPainterPath path; QString key; };
    struct Plan { V3 a, u, v; int plane = -1; QList<Slab> slabs; int pieces = 0; };
    // the directions to try: the first board's own one, and for a box the other two axes as well
    QList<V3> dirs{A->extrude.normalized()};
    if (A->type() == SketchItem::Rect && squareToAxes(A->extrude))
        for (const V3 &d : {V3(0, 0, 1), V3(0, 1, 0), V3(1, 0, 0)})
            if (std::fabs(V3::dotProduct(d, dirs.first())) < 0.5f)
                dirs << d;
    const bool fine = m_fine;
    m_fine = true;
    Plan best;
    bool have = false;
    QString why;
    for (const V3 &d : std::as_const(dirs)) {
        Plan pl;
        pl.a = d;
        if (std::fabs(d.z()) > 0.9999f) pl.plane = 0;
        else if (std::fabs(d.y()) > 0.9999f) pl.plane = 1;
        else if (std::fabs(d.x()) > 0.9999f) pl.plane = 2;
        if (pl.plane >= 0) {
            const Frame fr = frameFor(pl.plane);
            pl.u = fr.U;
            pl.v = fr.V;
            pl.a = fr.N;
        } else {
            pl.u = V3::crossProduct(d, std::fabs(d.z()) < 0.9f ? V3(0, 0, 1) : V3(1, 0, 0)).normalized();
            pl.v = V3::crossProduct(d, pl.u);
        }
        struct P { QPainterPath path; double lo = 0, hi = 0; };
        QList<P> pr;
        bool ok = true;
        for (const SketchItem *it : std::as_const(ops)) {
            P p;
            if (!boolPrism(it, pl.a, pl.u, pl.v, p.path, p.lo, p.hi)) {
                ok = false;
                why = itemLabel(it);
                break;
            }
            pr << p;
        }
        if (!ok)
            continue;
        QList<double> lv;
        for (const P &p : std::as_const(pr))
            lv << p.lo << p.hi;
        std::sort(lv.begin(), lv.end());
        lv.erase(std::unique(lv.begin(), lv.end(), [](double x, double y) { return std::fabs(x - y) < 1e-4; }), lv.end());
        for (int i = 0; i + 1 < lv.size(); ++i) {
            const double s0 = lv[i], s1 = lv[i + 1], mid = (s0 + s1) / 2;
            QString key;
            QPainterPath keep, cut;
            bool all = true, haveKeep = false;
            for (int k = 0; k < pr.size(); ++k) {
                const bool in = pr[k].lo < mid && mid < pr[k].hi;
                key += in ? QChar('1') : QChar('0');
                if (!in) {
                    all = false;
                    continue;
                }
                if (op == BoolOp::Union) {
                    keep = haveKeep ? keep.united(pr[k].path) : pr[k].path;
                    haveKeep = true;
                } else if (op == BoolOp::Subtract) {
                    if (k == 0) { keep = pr[0].path; haveKeep = true; }
                    else cut = cut.isEmpty() ? pr[k].path : cut.united(pr[k].path);
                } else {
                    keep = haveKeep ? keep.intersected(pr[k].path) : pr[k].path;
                    haveKeep = true;
                }
            }
            if (!haveKeep || (op == BoolOp::Intersect && !all))
                continue;
            const QPainterPath res = (op == BoolOp::Subtract && !cut.isEmpty()) ? keep.subtracted(cut) : keep;
            if (res.isEmpty() || shapesOf(res).isEmpty())
                continue;
            if (!pl.slabs.isEmpty() && pl.slabs.last().key == key && std::fabs(pl.slabs.last().s1 - s0) < 1e-4)
                pl.slabs.last().s1 = s1;                   // the same boards present: the same profile, one piece
            else
                pl.slabs.append({s0, s1, res, key});
        }
        for (const Slab &sl : std::as_const(pl.slabs))
            pl.pieces += int(shapesOf(sl.path).size());
        if (!have || (pl.pieces > 0 && pl.pieces < best.pieces) || best.pieces == 0) {
            best = pl;
            have = true;
        }
    }
    m_fine = fine;
    if (!have) {
        msg = QString("%1: \"%2\" lies at an angle to the first board - boards must be square to each other").arg(opName, why);
        return false;
    }
    if (best.pieces == 0) {
        msg = op == BoolOp::Intersect ? QString("Intersect: the boards do not overlap")
                                      : QString("%1: nothing would be left of the board").arg(opName);
        return false;
    }

    // the new boards: rectangles where the profile is a plain rectangle, else closed polylines (with holes)
    for (const Slab &sl : std::as_const(best.slabs))
        for (const QList<QPolygonF> &shape : shapesOf(sl.path)) {
            auto to3 = [&](const QPointF &q) {
                return best.u * float(q.x()) + best.v * float(q.y()) + best.a * float(sl.s0);
            };
            SketchItem *n = nullptr;
            if (best.plane >= 0 && shape.size() == 1 && axisRect(shape.first())) {
                n = new SketchItem(SketchItem::Rect);
                const QRectF r = shape.first().boundingRect();
                n->pts = {to3(r.topLeft()), to3(r.bottomRight())};
                n->plane = best.plane;
            } else {
                n = new SketchItem(SketchItem::Polyline);
                n->plane = best.plane >= 0 ? best.plane : A->plane;
                if (shape.size() == 1) {
                    for (const QPointF &q : shape.first())
                        n->pts << to3(q);
                    n->pts << n->pts.first();
                } else {
                    for (const QPolygonF &loop : shape) {
                        for (const QPointF &q : loop)
                            n->pts << to3(q);
                        n->loops << int(loop.size());
                    }
                }
            }
            n->extrude = best.a * float(sl.s1 - sl.s0);
            n->color = A->color;
            n->fill = A->fill;
            n->width = A->width;
            n->lineStyle = A->lineStyle;
            n->name = A->name;
            n->material = A->material;
            made << n;
        }

    // pieces of one board stay one group and one part of the cut list
    int gid = A->group, pid = A->part;
    if (made.size() > 1) {
        int maxG = 0, maxP = 0;
        for (const SketchItem *it : std::as_const(m_items)) {
            maxG = std::max(maxG, it->group);
            maxP = std::max(maxP, it->part);
        }
        if (!gid) gid = maxG + 1;
        if (!pid) pid = maxP + 1;
    }
    for (SketchItem *n : std::as_const(made)) {
        n->group = gid;
        n->part = pid;
    }
    int at = int(m_items.indexOf(const_cast<SketchItem *>(A)));
    for (int k = 0; k < ops.size(); ++k) {
        if (op == BoolOp::Subtract && keepCutters && k > 0)
            continue;
        SketchItem *it = ops[k];
        const int idx = int(m_items.indexOf(it));
        if (idx >= 0 && idx < at)
            --at;
        m_items.removeAll(it);
        if (m_hover == it) m_hover = nullptr;
        if (m_editItem == it) m_editItem = nullptr;
        if (m_joinFirst == it) m_joinFirst = nullptr;
        m_selected.removeAll(it);
        delete it;
    }
    at = std::clamp(at, 0, int(m_items.size()));
    for (SketchItem *n : std::as_const(made))
        m_items.insert(at++, n);
    msg = made.size() == 1 ? QString("%1: one board").arg(opName)
                           : QString("%1: %2 pieces, grouped - one part in the cut list").arg(opName).arg(made.size());
    return true;
}

// ===========================================================================
// Extrude New / Join / Cut / Intersect (a closed shape pulled into a body meets the boards around
// it) and Cut joints (boards that go into others cut their housings)
// ===========================================================================
namespace {
double pathArea(const QPainterPath &p)
{
    double a = 0;
    for (const QPolygonF &poly : p.toFillPolygons())
        a += std::fabs(polyArea(poly));
    return a;
}
} // namespace

// 0 apart, 1 touching (a shared face), 2 overlapping in volume, -1 cannot tell (at an angle)
int Canvas::solidContact(const SketchItem *a, const SketchItem *b) const
{
    auto box = [this](const SketchItem *it, V3 &lo, V3 &hi) {
        lo = V3(1e30f, 1e30f, 1e30f);
        hi = -lo;
        for (const auto &s : stripsBase(it))
            for (const V3 &q0 : s)
                for (const V3 &q : {q0, q0 + it->extrude}) {
                    lo = V3(std::min(lo.x(), q.x()), std::min(lo.y(), q.y()), std::min(lo.z(), q.z()));
                    hi = V3(std::max(hi.x(), q.x()), std::max(hi.y(), q.y()), std::max(hi.z(), q.z()));
                }
    };
    V3 la, ha, lb, hb;
    box(a, la, ha);
    box(b, lb, hb);
    const float tol = 1e-3f;
    for (int k = 0; k < 3; ++k)
        if (la[k] > hb[k] + tol || lb[k] > ha[k] + tol)
            return 0;
    const V3 d = a->extrude.normalized();
    const V3 u = V3::crossProduct(d, std::fabs(d.z()) < 0.9f ? V3(0, 0, 1) : V3(1, 0, 0)).normalized();
    const V3 v = V3::crossProduct(d, u);
    QPainterPath pa, pb;
    double loA = 0, hiA = 0, loB = 0, hiB = 0;
    if (!boolPrism(a, d, u, v, pa, loA, hiA) || !boolPrism(b, d, u, v, pb, loB, hiB))
        return -1;
    const double along = std::min(hiA, hiB) - std::max(loA, loB);
    if (along < -1e-3)
        return 0;
    const double area = pathArea(pa.intersected(pb));
    if (along > 1e-3 && area > 1e-2)
        return 2;
    if (area > 1e-2)
        return 1;                                          // end to end
    if (along > 1e-3) {                                    // side by side: do the outlines meet?
        QPainterPathStroker st;
        st.setWidth(0.02);
        if (pathArea(pa.intersected(st.createStroke(pb))) > 1e-6)
            return 1;
    }
    return 0;
}

// Auto: Cut when the new body goes into a board, else a New body
int Canvas::extrudeAutoOp(const SketchItem *body) const
{
    for (const SketchItem *it : m_items)
        if (it != body && !it->hidden && !it->locked && !it->extrude.isNull() && solidContact(body, it) == 2)
            return 3;
    return 1;
}

QString Canvas::extrudeOpName(int op)
{
    static const char *n[] = {"Auto", "New body", "Join", "Cut", "Intersect"};
    return n[std::clamp(op, 0, 4)];
}

// op 0 Auto, 1 New, 2 Join, 3 Cut, 4 Intersect; no undo step of its own; body may be deleted
QString Canvas::extrudeCombine(SketchItem *body, int op)
{
    if (op == 0)
        op = extrudeAutoOp(body);
    if (op == 1)
        return QString("new body");
    QList<SketchItem *> targets;
    for (SketchItem *it : std::as_const(m_items)) {
        if (it == body || it->hidden || it->locked || it->extrude.isNull())
            continue;
        const int c = solidContact(body, it);
        if (c == 2 || (op == 2 && c == 1))
            targets << it;
    }
    // boards selected before the pull: only those are cut / joined, when the new body meets any of them
    QList<SketchItem *> scoped;
    for (SketchItem *t : std::as_const(targets))
        if (m_ppScope.contains(t))
            scoped << t;
    if (!scoped.isEmpty())
        targets = scoped;
    if (targets.isEmpty())
        return QString("%1: it meets no board - kept as a new body").arg(extrudeOpName(op));
    QString msg;
    QList<SketchItem *> made;
    if (op == 2) {
        QList<SketchItem *> ops = targets;
        ops << body;
        if (!booleanItems(ops, BoolOp::Union, false, msg, made))
            return msg + " - kept as a new body";
        return QString("joined to %1 board(s)").arg(targets.size());
    }
    int done = 0;
    QString err;
    for (int k = 0; k < targets.size(); ++k) {
        SketchItem *tool = body;
        if (op == 4 && k + 1 < targets.size()) {           // every board gets its own copy of the tool
            tool = body->clone();
            tool->selected = false;
            m_items.append(tool);
        }
        if (booleanItems({targets[k], tool}, op == 3 ? BoolOp::Subtract : BoolOp::Intersect, true, msg, made)) {
            ++done;
        } else {
            err = msg;
            if (tool != body) {
                m_items.removeAll(tool);
                delete tool;
            }
        }
    }
    if (op == 3 && done > 0) {                             // the cutting body goes, like Fusion's Cut
        m_items.removeAll(body);
        m_selected.removeAll(body);
        if (m_hover == body) m_hover = nullptr;
        delete body;
    }
    if (done == 0)
        return err + " - kept as a new body";
    return QString("%1 %2 board(s)%3").arg(op == 3 ? "cut into" : "intersected with").arg(done)
        .arg(err.isEmpty() ? QString() : "  (" + err + ")");
}

void Canvas::cutJointsSelection()
{
    QList<SketchItem *> cutters;
    for (SketchItem *it : std::as_const(m_selected))
        if (!it->extrude.isNull())
            cutters << it;
    if (cutters.isEmpty()) {
        emit message("Cut joints: select the boards that go INTO others (shelves, rails, tenons), then Cut joints");
        return;
    }
    snapshotBegin();
    int cuts = 0;
    QString err;
    for (SketchItem *c : std::as_const(cutters)) {
        QList<SketchItem *> targets;                       // fresh for every cutter: earlier cuts replaced boards
        for (SketchItem *it : std::as_const(m_items))
            if (it != c && !cutters.contains(it) && !it->hidden && !it->locked && !it->extrude.isNull() &&
                solidContact(c, it) == 2)
                targets << it;
        for (SketchItem *t : std::as_const(targets)) {
            QString msg;
            QList<SketchItem *> made;
            if (booleanItems({t, c}, BoolOp::Subtract, true, msg, made))
                ++cuts;
            else
                err = msg;
        }
    }
    if (cuts == 0) {
        m_snapshot.clear();
        emit message(err.isEmpty() ? QString("Cut joints: the selected boards do not go into any other board")
                                   : "Cut joints: " + err);
        return;
    }
    snapshotCommit("Cut joints");
    emit selectionChanged();
    emit message(QString("Cut joints: %1 housing(s) cut, the selected boards kept%2").arg(cuts)
                     .arg(err.isEmpty() ? QString() : "  (" + err + ")"));
    update();
}

QList<int> Canvas::selectedSolidIndices() const
{
    QList<int> out;                                        // in the order they were selected: the first is the target
    for (SketchItem *it : std::as_const(m_selected))
        if (!it->extrude.isNull())
            out << int(m_items.indexOf(it));
    return out;
}

// the Combine dialog: every target is cut by (or intersected with) all the tools, or targets and tools
// join into one body; one undo step
bool Canvas::combineBodies(const QList<int> &targets, const QList<int> &tools, BoolOp op, bool keepTools)
{
    QList<SketchItem *> T, U;
    for (int i : targets)
        if (SketchItem *it = m_items.value(i); it && !it->extrude.isNull() && !it->locked && !T.contains(it))
            T << it;
    for (int i : tools)
        if (SketchItem *it = m_items.value(i); it && !it->extrude.isNull() && !it->locked && !T.contains(it) && !U.contains(it))
            U << it;
    if (T.isEmpty() || U.isEmpty()) {
        emit message("Combine: choose at least one target body and one tool body (boards; turned parts and pipes cannot combine)");
        return false;
    }
    auto drop = [this](SketchItem *it) {
        m_items.removeAll(it);
        m_selected.removeAll(it);
        if (m_hover == it) m_hover = nullptr;
        delete it;
    };
    auto cloneOf = [this](const SketchItem *it) {
        SketchItem *c = it->clone();
        c->selected = false;
        m_items.append(c);
        return c;
    };
    snapshotBegin();
    QString msg, err;
    QList<SketchItem *> made;
    int done = 0;
    if (op == BoolOp::Union) {
        QList<SketchItem *> ops = T;
        for (SketchItem *u : std::as_const(U))
            ops << (keepTools ? cloneOf(u) : u);
        if (booleanItems(ops, BoolOp::Union, false, msg, made))
            done = 1;
        else
            err = msg;
    } else if (op == BoolOp::Subtract) {
        for (SketchItem *t : std::as_const(T)) {
            QList<SketchItem *> ops{t};
            ops += U;
            if (booleanItems(ops, BoolOp::Subtract, true, msg, made))
                ++done;
            else
                err = msg;
        }
        if (done && !keepTools)
            for (SketchItem *u : std::as_const(U))
                drop(u);
    } else {
        for (SketchItem *t : std::as_const(T)) {
            QList<SketchItem *> ops{t}, clones;
            for (SketchItem *u : std::as_const(U))
                clones << cloneOf(u);
            ops += clones;
            if (booleanItems(ops, BoolOp::Intersect, false, msg, made)) {
                ++done;
            } else {
                err = msg;
                for (SketchItem *c : std::as_const(clones))
                    drop(c);
            }
        }
        if (done && !keepTools)
            for (SketchItem *u : std::as_const(U))
                drop(u);
    }
    if (!done) {
        m_snapshot.clear();
        emit message("Combine: " + err);
        return false;
    }
    for (SketchItem *it : std::as_const(m_items))
        it->selected = false;
    m_selected.clear();
    snapshotCommit("Combine");
    static const char *opn[] = {"joined", "cut", "intersected"};
    emit message(QString("Combine: %1 target(s) %2 with %3 tool(s)%4%5").arg(done)
                     .arg(opn[op == BoolOp::Union ? 0 : (op == BoolOp::Subtract ? 1 : 2)]).arg(U.size())
                     .arg(keepTools ? ", tools kept" : "", err.isEmpty() ? QString() : "  (" + err + ")"));
    emit selectionChanged();
    update();
    return true;
}