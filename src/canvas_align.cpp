#include "canvas.h"
#include "units.h"
#include "canvas_util.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>

using V3 = QVector3D;

// ===========================================================================
// Align / distribute and live smart guides.
// Everything works in the sketch plane's own (U, V) coordinates, and a group
// is always treated as ONE unit.
// ===========================================================================
bool Canvas::uvBounds(const SketchItem *it, const Frame &fr, QRectF &r) const
{
    QVector<V3> pts;
    if (it->type() == SketchItem::Text) {
        const V3 a = it->pts.value(0);
        const qreal w = std::max<qreal>(1, it->text.split('\n').first().size()) * it->textSize * 0.55;
        const qreal h = it->textSize * it->text.split('\n').size();
        pts << a << a + fr.U * float(w) - fr.V * float(h);
    } else if (it->type() == SketchItem::ImageItem) {
        for (const V3 &q : imageCorners(it))
            pts << q;
    } else {
        for (const auto &s : strips(it))
            pts += s;
    }
    if (pts.isEmpty())
        return false;
    qreal u0 = 1e18, u1 = -1e18, v0 = 1e18, v1 = -1e18;
    for (const V3 &q : std::as_const(pts)) {
        const qreal u = V3::dotProduct(q, fr.U), v = V3::dotProduct(q, fr.V);
        u0 = std::min(u0, u); u1 = std::max(u1, u);
        v0 = std::min(v0, v); v1 = std::max(v1, v);
    }
    r = QRectF(QPointF(u0, v0), QPointF(u1, v1));
    return true;
}

QList<Canvas::AlignUnit> Canvas::alignUnits(const QList<SketchItem *> &sel, const Frame &fr) const
{
    QList<AlignUnit> units;
    QHash<int, int> byGroup;
    for (SketchItem *it : sel) {
        QRectF r;
        if (!uvBounds(it, fr, r))
            continue;
        if (it->group && byGroup.contains(it->group)) {
            AlignUnit &u = units[byGroup[it->group]];
            u.items << it;
            u.box = QRectF(QPointF(std::min(u.box.left(), r.left()), std::min(u.box.top(), r.top())),
                           QPointF(std::max(u.box.right(), r.right()), std::max(u.box.bottom(), r.bottom())));
        } else {
            if (it->group)
                byGroup[it->group] = units.size();
            units.append({{it}, r});
        }
    }
    return units;
}

void Canvas::alignSelection(AlignMode mode, bool toPage)
{
    const Frame fr = planeFrame();
    QList<AlignUnit> units = alignUnits(m_selected, fr);
    if (units.isEmpty()) {
        emit message("Align: select the objects first");
        return;
    }
    QRectF ref;
    if (toPage || units.size() == 1) {
        ref = QRectF(-m_pageSize.width() / 2, -m_pageSize.height() / 2, m_pageSize.width(), m_pageSize.height());
        if (units.size() == 1 && !toPage)
            emit message("One object selected - aligned to the page");
    } else {
        ref = units[0].box;
        for (const AlignUnit &u : std::as_const(units))
            ref = QRectF(QPointF(std::min(ref.left(), u.box.left()), std::min(ref.top(), u.box.top())),
                         QPointF(std::max(ref.right(), u.box.right()), std::max(ref.bottom(), u.box.bottom())));
    }
    snapshotBegin();
    for (const AlignUnit &u : std::as_const(units)) {
        qreal du = 0, dv = 0;
        switch (mode) {
        case AlignMode::Left:    du = ref.left() - u.box.left(); break;
        case AlignMode::HCenter: du = ref.center().x() - u.box.center().x(); break;
        case AlignMode::Right:   du = ref.right() - u.box.right(); break;
        case AlignMode::Bottom:  dv = ref.top() - u.box.top(); break;          // V grows upward
        case AlignMode::VCenter: dv = ref.center().y() - u.box.center().y(); break;
        case AlignMode::Top:     dv = ref.bottom() - u.box.bottom(); break;
        }
        const V3 d = fr.U * float(du) + fr.V * float(dv);
        for (SketchItem *it : u.items)
            for (V3 &p : it->pts)
                p += d;
    }
    snapshotCommit("Align");
    emit message(QString("Aligned %1 object(s) to the %2").arg(units.size()).arg(toPage || units.size() == 1 ? "page" : "selection"));
    update();
}

void Canvas::distributeSelection(bool horizontal)
{
    const Frame fr = planeFrame();
    QList<AlignUnit> units = alignUnits(m_selected, fr);
    if (units.size() < 3) {
        emit message("Distribute: select three or more objects (groups count as one)");
        return;
    }
    std::sort(units.begin(), units.end(), [&](const AlignUnit &a, const AlignUnit &b) {
        return horizontal ? a.box.center().x() < b.box.center().x() : a.box.center().y() < b.box.center().y();
    });
    qreal total = 0;
    for (const AlignUnit &u : std::as_const(units))
        total += horizontal ? u.box.width() : u.box.height();
    const qreal lo = horizontal ? units.first().box.left() : units.first().box.top();
    const qreal hi = horizontal ? units.last().box.right() : units.last().box.bottom();
    const qreal gap = (hi - lo - total) / (units.size() - 1);
    snapshotBegin();
    qreal pos = lo;
    for (const AlignUnit &u : std::as_const(units)) {
        const qreal cur = horizontal ? u.box.left() : u.box.top();
        const qreal d = pos - cur;
        const V3 dv = horizontal ? fr.U * float(d) : fr.V * float(d);
        for (SketchItem *it : u.items)
            for (V3 &p : it->pts)
                p += dv;
        pos += (horizontal ? u.box.width() : u.box.height()) + gap;
    }
    snapshotCommit("Distribute");
    emit message(QString("Distributed %1 objects with equal gaps of %2").arg(units.size()).arg(Units::format(gap)));
    update();
}

void Canvas::centerSelectionOn(const QVector3D &target)
{
    if (m_selected.isEmpty())
        return;
    const Frame fr = planeFrame();
    QRectF all;
    bool first = true;
    for (SketchItem *it : std::as_const(m_selected)) {
        QRectF r;
        if (!uvBounds(it, fr, r))
            continue;
        all = first ? r : QRectF(QPointF(std::min(all.left(), r.left()), std::min(all.top(), r.top())),
                                 QPointF(std::max(all.right(), r.right()), std::max(all.bottom(), r.bottom())));
        first = false;
    }
    if (first)
        return;
    const V3 d = fr.U * float(V3::dotProduct(target, fr.U) - all.center().x()) +
                 fr.V * float(V3::dotProduct(target, fr.V) - all.center().y());
    applyMove(d, false);
}

QRectF Canvas::selectionScreenRect() const
{
    QRectF all;
    bool first = true;
    for (const SketchItem *it : m_selected) {
        const QRectF r = itemScreenBounds(it);
        if (r.isNull() && r.width() == 0)
            continue;
        all = first ? r : all.united(r);
        first = false;
    }
    return first ? QRectF() : all;
}

// ---------------------------------------------------------------------------
// smart guides while dragging: edges / centres of the moved set snap to edges / centres of other
// objects, the page centre and the origin (Figma / Illustrator style)
// ---------------------------------------------------------------------------
bool Canvas::smartAlign(QVector3D &delta, const QVector3D &applied)
{
    m_smart.clear();
    const Frame fr = planeFrame();
    if (!m_boxesValid) {
        m_staticBoxes.clear();
        for (const SketchItem *it : std::as_const(m_items)) {
            if (m_dragData.contains(const_cast<SketchItem *>(it)))
                continue;
            QRectF r;
            if (uvBounds(it, fr, r))
                m_staticBoxes << r;
            if (m_staticBoxes.size() > 2500)
                break;
        }
        m_staticBoxes << QRectF(0, 0, 0, 0);                       // origin / page centre
        m_boxesValid = true;
    }
    QRectF mv;
    bool first = true;
    for (auto it = m_dragData.constBegin(); it != m_dragData.constEnd(); ++it) {
        QRectF r;
        if (!uvBounds(it.key(), fr, r))
            continue;
        mv = first ? r : QRectF(QPointF(std::min(mv.left(), r.left()), std::min(mv.top(), r.top())),
                                QPointF(std::max(mv.right(), r.right()), std::max(mv.bottom(), r.bottom())));
        first = false;
    }
    if (first)
        return false;
    // the moved items already carry 'applied'; work in "original + new delta" terms
    const qreal au = V3::dotProduct(applied, fr.U), av = V3::dotProduct(applied, fr.V);
    const qreal nu = V3::dotProduct(delta, fr.U), nv = V3::dotProduct(delta, fr.V);
    mv.translate(nu - au, nv - av);
    const qreal tol = 7.0 / std::max(1e-6, scaleAt(m_dragStart));
    const qreal mu[3] = {mv.left(), mv.center().x(), mv.right()};
    const qreal mvv[3] = {mv.top(), mv.center().y(), mv.bottom()};
    qreal bu = tol, bv = tol, fixU = 0, fixV = 0;
    int iu = -1, iv = -1;
    QRectF ru, rv;
    for (const QRectF &s : std::as_const(m_staticBoxes)) {
        const qreal su[3] = {s.left(), s.center().x(), s.right()};
        const qreal sv[3] = {s.top(), s.center().y(), s.bottom()};
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) {
                if (std::fabs(mu[a] - su[b]) < bu) { bu = std::fabs(mu[a] - su[b]); fixU = su[b] - mu[a]; iu = a; ru = s; }
                if (std::fabs(mvv[a] - sv[b]) < bv) { bv = std::fabs(mvv[a] - sv[b]); fixV = sv[b] - mvv[a]; iv = a; rv = s; }
            }
    }
    if (iu < 0 && iv < 0)
        return false;
    delta += fr.U * float(fixU) + fr.V * float(fixV);
    mv.translate(fixU, fixV);
    const qreal n0 = V3::dotProduct(m_dragStart, fr.N);
    auto P = [&](qreal u, qreal v) { return fr.U * float(u) + fr.V * float(v) + fr.N * float(n0); };
    static const char *nm[3] = {"edge", "centre", "edge"};
    if (iu >= 0) {
        const qreal u = (iu == 0 ? mv.left() : (iu == 1 ? mv.center().x() : mv.right()));
        const qreal v0 = std::min(mv.top(), ru.top()) - 6, v1 = std::max(mv.bottom(), ru.bottom()) + 6;
        m_smart.append({P(u, v0), P(u, v1), QString("align %1").arg(nm[iu])});
    }
    if (iv >= 0) {
        const qreal v = (iv == 0 ? mv.top() : (iv == 1 ? mv.center().y() : mv.bottom()));
        const qreal u0 = std::min(mv.left(), rv.left()) - 6, u1 = std::max(mv.right(), rv.right()) + 6;
        m_smart.append({P(u0, v), P(u1, v), QString("align %1").arg(nm[iv])});
    }
    return true;
}

bool Canvas::selectionBarAllowed() const
{
    if (m_dragging || m_resizing || m_rubberActive || m_editor || !m_pending.isEmpty() || m_orbiting || m_panning)
        return false;
    return m_tool == Tool::Select || m_tool == Tool::FreeMove || m_tool == Tool::DynMove;
}

// ===========================================================================
// Transform size: bounding box with 8 grips + numeric resize
// ===========================================================================
bool Canvas::selectionBoxUV(QRectF &r) const
{
    const Frame fr = planeFrame();
    bool any = false;
    for (const SketchItem *it : m_selected) {
        QRectF b;
        if (!uvBounds(it, fr, b))
            continue;
        r = any ? r.united(b) : b;
        any = true;
    }
    return any;
}

bool Canvas::selectionSizeUV(QSizeF &size) const
{
    QRectF r;
    if (!selectionBoxUV(r))
        return false;
    size = r.size();
    return true;
}

// 0 TL, 1 T, 2 TR, 3 R, 4 BR, 5 B, 6 BL, 7 L   (v grows upwards in the sketch plane)
QPointF Canvas::boxHandleUV(const QRectF &r, int i) const
{
    const qreal u0 = r.left(), u1 = r.right(), v0 = r.top(), v1 = r.bottom();   // QRectF(top=min v)
    const qreal um = (u0 + u1) / 2, vm = (v0 + v1) / 2;
    switch (i) {
    case 0: return {u0, v1};
    case 1: return {um, v1};
    case 2: return {u1, v1};
    case 3: return {u1, vm};
    case 4: return {u1, v0};
    case 5: return {um, v0};
    case 6: return {u0, v0};
    default: return {u0, vm};
    }
}

int Canvas::handleAt(const QPoint &s) const
{
    if (m_tool != Tool::Select || m_selected.isEmpty() || m_dragging || gizmoVisible())
        return -1;                                     // in the 3D view the gizmo replaces the 2D grips
    QRectF r;
    if (!selectionBoxUV(r))
        return -1;
    const Frame fr = planeFrame();
    const qreal n0 = V3::dotProduct(selectionCenter(), fr.N);
    int best = -1;
    qreal bd = 9.0;
    for (int i = 0; i < 8; ++i) {
        const QPointF h = boxHandleUV(r, i);
        QPointF sp;
        if (!projectD(fr.U * float(h.x()) + fr.V * float(h.y()) + fr.N * float(n0), sp))
            continue;
        const qreal d = QLineF(sp, QPointF(s)).length();
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) {
        QPointF g;
        if (rotGripScreen(r, g) && QLineF(g, QPointF(s)).length() < 10.0)
            best = 8;                                   // the round rotation grip
    }
    return best;
}

void Canvas::beginResize(int handle)
{
    if (!selectionBoxUV(m_rsBox))
        return;
    m_rsHandle = handle;
    m_rsPlanePt = selectionCenter();
    m_rsOrig.clear();
    m_rsExtra.clear();
    for (SketchItem *it : std::as_const(m_selected)) {
        m_rsOrig[it] = it->pts;
        m_rsExtra[it] = {it->textSize, it->imageSizeMm};
    }
    m_rsSu = m_rsSv = 1;
    snapshotBegin();
    m_resizing = true;
}

void Canvas::applyScaleUV(const QPointF &anchor, qreal su, qreal sv)
{
    const Frame fr = planeFrame();
    for (auto it = m_rsOrig.constBegin(); it != m_rsOrig.constEnd(); ++it) {
        SketchItem *item = it.key();
        const QList<V3> &orig = it.value();
        for (int i = 0; i < orig.size() && i < item->pts.size(); ++i) {
            const V3 &q = orig[i];
            const qreal u = V3::dotProduct(q, fr.U), v = V3::dotProduct(q, fr.V);
            item->pts[i] = q + fr.U * float(anchor.x() + (u - anchor.x()) * su - u)
                             + fr.V * float(anchor.y() + (v - anchor.y()) * sv - v);
        }
        const auto ex = m_rsExtra.value(item);
        if (item->type() == SketchItem::Text)
            item->textSize = ex.first * (su + sv) / 2.0;
        if (item->type() == SketchItem::ImageItem)
            item->imageSizeMm = QSizeF(ex.second.width() * su, ex.second.height() * sv);
    }
    m_rsSu = su;
    m_rsSv = sv;
}

void Canvas::doResize(bool freeScale)
{
    if (!m_resizing || !m_cur.valid)
        return;
    const Frame fr = planeFrame();
    const QPointF hp = boxHandleUV(m_rsBox, m_rsHandle), an = boxHandleUV(m_rsBox, (m_rsHandle + 4) % 8);
    const qreal cu = V3::dotProduct(m_cur.pt, fr.U), cv = V3::dotProduct(m_cur.pt, fr.V);
    const bool corner = m_rsHandle % 2 == 0;
    const bool moveU = corner || m_rsHandle == 3 || m_rsHandle == 7;
    const bool moveV = corner || m_rsHandle == 1 || m_rsHandle == 5;
    qreal su = 1, sv = 1;
    if (moveU && std::fabs(hp.x() - an.x()) > 1e-9) su = (cu - an.x()) / (hp.x() - an.x());
    if (moveV && std::fabs(hp.y() - an.y()) > 1e-9) sv = (cv - an.y()) / (hp.y() - an.y());
    if (corner && !freeScale) {                       // corners keep the proportions (hold Shift for free)
        const qreal s = std::max(std::fabs(su), std::fabs(sv));
        su = sv = s;
    }
    su = std::max(0.01, su);
    sv = std::max(0.01, sv);
    applyScaleUV(an, su, sv);
    update();
}

void Canvas::resizeSelection(double w, double h, int anchor)
{
    QRectF r;
    if (m_selected.isEmpty() || !selectionBoxUV(r)) {
        emit message("Resize: select the objects first");
        return;
    }
    const qreal su = r.width() > 1e-9 && w > 0 ? w / r.width() : 1.0;
    const qreal sv = r.height() > 1e-9 && h > 0 ? h / r.height() : 1.0;
    QPointF an = r.center();
    const qreal u0 = r.left(), u1 = r.right(), v0 = r.top(), v1 = r.bottom();
    switch (anchor) {
    case 1: an = {u0, v0}; break;
    case 2: an = {u1, v0}; break;
    case 3: an = {u0, v1}; break;
    case 4: an = {u1, v1}; break;
    default: break;
    }
    snapshotBegin();
    m_rsOrig.clear();
    m_rsExtra.clear();
    for (SketchItem *it : std::as_const(m_selected)) {
        m_rsOrig[it] = it->pts;
        m_rsExtra[it] = {it->textSize, it->imageSizeMm};
    }
    applyScaleUV(an, su, sv);
    m_rsOrig.clear();
    snapshotCommit("Resize");
    emit message(QString("Resized to %1 x %2").arg(Units::format(r.width() * su), Units::format(r.height() * sv)));
    emit selectionChanged();
    update();
}

void Canvas::drawTransformBox(QPainter &p) const
{
    QRectF r;
    if (m_tool != Tool::Select || m_selected.isEmpty() || m_dragging || m_rubberActive || gizmoVisible() ||
        m_gzActive >= 0 || !selectionBoxUV(r))
        return;
    const Frame fr = planeFrame();
    const qreal n0 = V3::dotProduct(selectionCenter(), fr.N);
    auto P = [&](qreal u, qreal v) { return fr.U * float(u) + fr.V * float(v) + fr.N * float(n0); };
    QPolygonF quad;
    for (const QPointF &c : {QPointF(r.left(), r.top()), QPointF(r.right(), r.top()),
                             QPointF(r.right(), r.bottom()), QPointF(r.left(), r.bottom())}) {
        QPointF s;
        if (!projectD(P(c.x(), c.y()), s))
            return;
        quad << s;
    }
    if (QLineF(quad[0], quad[2]).length() < 14)
        return;
    const QColor col(110, 214, 255);
    p.setPen(QPen(col, 1.2, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(quad);
    p.setBrush(QColor(10, 20, 30));
    p.setPen(QPen(col, 1.6));
    for (int i = 0; i < 8; ++i) {
        const QPointF h = boxHandleUV(r, i);
        QPointF s;
        if (!projectD(P(h.x(), h.y()), s))
            continue;
        p.drawRect(QRectF(s.x() - 4.5, s.y() - 4.5, 9, 9));
    }
    p.setBrush(Qt::NoBrush);
    QPointF top, grip;
    QPointF dir(0, -1);
    if (rotGripScreen(r, grip) && projectD(P((r.left() + r.right()) / 2, r.bottom()), top)) {
        dir = (grip - top) / 28.0;
        p.setPen(QPen(col, 1.2));
        p.drawLine(top, grip);
        p.setBrush(m_rotHover || m_rotating ? col : QColor(10, 20, 30));
        p.drawEllipse(grip, 6.0, 6.0);
        p.setBrush(Qt::NoBrush);
        if (m_rotating) {                                  // centre mark while rotating
            QPointF c;
            if (projectD(m_rotCenter, c)) {
                p.drawLine(c - QPointF(6, 0), c + QPointF(6, 0));
                p.drawLine(c - QPointF(0, 6), c + QPointF(0, 6));
            }
        }
    }
    if (projectD(P((r.left() + r.right()) / 2, r.bottom()), top)) {
        top += dir * 26.0;                                 // keep the size tag clear of the rotation grip
        QFont f = p.font();
        f.setPixelSize(11);
        f.setBold(true);
        p.setFont(f);
        const QString t = m_rotating ? QString("Rotate %1\u00B0   (Shift = 15\u00B0 steps)").arg(m_rotAngle, 0, 'f', 1)
                                     : QString("%1 x %2%3").arg(Units::format(r.width()), Units::format(r.height()))
                              .arg(m_resizing ? QString("   (%1%)").arg(std::sqrt(m_rsSu * m_rsSv) * 100.0, 0, 'f', 1) : QString());
        const qreal tw = m_rotating ? 200 : std::max<qreal>(124, QFontMetricsF(f).horizontalAdvance(t) + 16);
        const QRectF tr(top.x() - tw / 2, top.y() - 30, tw, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(14, 20, 30, 230));
        p.drawRoundedRect(tr, 5, 5);
        p.setPen(col);
        p.drawText(tr, Qt::AlignCenter, t);
        p.setBrush(Qt::NoBrush);
    }
}

// Puts every point of the selection (or of the whole drawing if nothing is selected) exactly on the
// current sketch plane. Repairs drawings whose lines were slanted in depth, e.g. by 3D snapping.
void Canvas::flattenToPlane()
{
    const Frame fr = planeFrame();
    const qreal nPlane = V3::dotProduct(planeOrigin(), fr.N);
    const QList<SketchItem *> targets = m_selected.isEmpty() ? m_items : m_selected;
    qreal maxDev = 0;
    int moved = 0;
    for (const SketchItem *it : targets)
        for (const V3 &q : it->pts) {
            const qreal d = std::fabs(V3::dotProduct(q, fr.N) - nPlane);
            if (d > 1e-4) { ++moved; maxDev = std::max(maxDev, d); }
        }
    if (!moved) {
        emit message("Already flat: everything lies on the " + planeName(m_plane) + " plane");
        return;
    }
    snapshotBegin();
    for (SketchItem *it : targets) {
        for (V3 &q : it->pts)
            q -= fr.N * float(V3::dotProduct(q, fr.N) - nPlane);
        it->plane = int(m_plane);
    }
    snapshotCommit("Flatten to plane");
    emit message(QString("Flattened %1 points onto the %2 plane (largest slant %3)")
                     .arg(moved).arg(planeName(m_plane), Units::format(maxDev)));
    update();
}

// ---------------------------------------------------------------------------
// Free rotation grip, flip, fill and draw order
// ---------------------------------------------------------------------------
bool Canvas::rotGripScreen(const QRectF &r, QPointF &out) const
{
    const Frame fr = planeFrame();
    const qreal n0 = V3::dotProduct(selectionCenter(), fr.N);
    auto P = [&](qreal u, qreal v) { return fr.U * float(u) + fr.V * float(v) + fr.N * float(n0); };
    QPointF top, ctr;
    if (!projectD(P(r.center().x(), r.bottom()), top) || !projectD(P(r.center().x(), r.center().y()), ctr))
        return false;
    QPointF d = top - ctr;
    const qreal L = std::hypot(d.x(), d.y());
    d = L > 1e-6 ? d / L : QPointF(0, -1);
    out = top + d * 28.0;
    return true;
}

bool Canvas::rotAngleAt(const QPoint &s, qreal &deg) const
{
    QVector3D ro, rd, hit;
    screenRay(QPointF(s), ro, rd);
    const Frame fr = planeFrame();
    if (!rayPlane(ro, rd, m_rotCenter, fr.N, hit))
        return false;
    const V3 d = hit - m_rotCenter;
    const qreal u = V3::dotProduct(d, fr.U), v = V3::dotProduct(d, fr.V);
    if (std::hypot(u, v) < 1e-9)
        return false;
    deg = std::atan2(v, u) * 180.0 / PI;
    return true;
}

bool Canvas::closedShape(const SketchItem *it) const
{
    switch (it->type()) {
    case SketchItem::Rect:
        return it->pts.size() >= 2;
    case SketchItem::Circle:
        return it->pts.size() >= 2;
    case SketchItem::Polyline:
        return !it->loops.isEmpty() || (it->pts.size() >= 4 && (it->pts.first() - it->pts.last()).length() < 1e-3f);
    default:
        return false;
    }
}

// A rectangle is stored as two corners aligned to its plane, so any rotation that is not a multiple
// of 90 degrees (or happens around another plane) turns it into a closed polyline with real corners.
SketchItem *Canvas::rectToPolyline(SketchItem *old, const QList<QVector3D> &origPts)
{
    const QList<QVector3D> keep = old->pts;
    old->pts = origPts;
    const QVector<QVector3D> c = rectCorners(old);
    old->pts = keep;
    if (c.size() != 4)
        return old;
    auto *pl = new SketchItem(SketchItem::Polyline);
    pl->color = old->color;
    pl->fill = old->fill;
    pl->width = old->width;
    pl->plane = old->plane;
    pl->lineStyle = old->lineStyle;
    pl->group = old->group;
    pl->selected = old->selected;
    pl->hidden = old->hidden;
    pl->locked = old->locked;
    pl->extrude = old->extrude;
    pl->pts = {c[0], c[1], c[2], c[3], c[0]};
    const int idx = m_items.indexOf(old);
    if (idx >= 0) m_items[idx] = pl;
    const int si = m_selected.indexOf(old);
    if (si >= 0) m_selected[si] = pl;
    if (m_hover == old) m_hover = nullptr;
    if (m_editItem == old) m_editItem = nullptr;
    if (m_joinFirst == old) m_joinFirst = nullptr;
    if (m_editorItem == old) m_editorItem = nullptr;
    delete old;
    return pl;
}

void Canvas::beginRotate(const QPoint &s)
{
    QRectF r;
    if (m_selected.isEmpty() || !selectionBoxUV(r))
        return;
    const Frame fr = planeFrame();
    const qreal n0 = V3::dotProduct(selectionCenter(), fr.N);
    m_rotCenter = fr.U * float(r.center().x()) + fr.V * float(r.center().y()) + fr.N * float(n0);
    if (!rotAngleAt(s, m_rotStart))
        return;
    m_rsOrig.clear();
    m_rotTxt.clear();
    for (SketchItem *it : std::as_const(m_selected)) {
        const Frame f = frameFor(std::max(0, it->plane));
        if (std::fabs(V3::dotProduct(f.N, fr.N)) < 0.99f &&
            (it->type() == SketchItem::Circle || it->type() == SketchItem::ImageItem))
            continue;                                   // cannot turn these out of their own plane
        m_rsOrig[it] = it->pts;
        m_rotTxt[it] = it->textAngle;
        if (!it->extrude.isNull())
            m_rotExt[it] = it->extrude;
    }
    m_rotAngle = 0;
    snapshotBegin();
    m_rotating = true;
    emit message("Rotate: drag around the centre - hold Shift for 15° steps, release to finish");
    update();
}

void Canvas::doRotate(const QPoint &s, bool snap15)
{
    qreal a;
    if (!m_rotating || !rotAngleAt(s, a))
        return;
    qreal d = std::remainder(a - m_rotStart, 360.0);
    d = snap15 ? std::round(d / 15.0) * 15.0 : std::round(d * 10.0) / 10.0;
    m_rotAngle = d;
    const Frame fr = planeFrame();
    const bool right = std::fabs(std::remainder(d, 90.0)) < 1e-6;

    // rectangles that can no longer be described by two corners become closed polylines
    const QList<SketchItem *> keys = m_rsOrig.keys();
    for (SketchItem *it : keys) {
        if (it->type() != SketchItem::Rect || std::fabs(d) < 1e-6)
            continue;
        const Frame f = frameFor(std::max(0, it->plane));
        const bool offPlane = std::fabs(V3::dotProduct(f.N, fr.N)) < 0.99f;
        if (!right || offPlane) {
            const QList<QVector3D> orig = m_rsOrig.take(it);
            const double ta = m_rotTxt.take(it);
            const bool hasExt = m_rotExt.contains(it);
            const V3 ext = m_rotExt.take(it);
            SketchItem *pl = rectToPolyline(it, orig);
            m_rsOrig[pl] = pl->pts;
            m_rotTxt[pl] = ta;
            if (hasExt)
                m_rotExt[pl] = ext;
        }
    }

    // text turns the same way on screen as the geometry does
    qreal sign = 1;
    {
        QPointF s0, s1, s2;
        if (projectD(m_rotCenter, s0) && projectD(m_rotCenter + fr.U, s1) && projectD(m_rotCenter + fr.V, s2)) {
            const QPointF a1 = s1 - s0, a2 = s2 - s0;
            sign = (a1.x() * a2.y() - a1.y() * a2.x()) < 0 ? 1 : -1;
        }
    }
    const qreal rad = d * PI / 180.0, cs = std::cos(rad), sn = std::sin(rad);
    for (auto it = m_rsOrig.constBegin(); it != m_rsOrig.constEnd(); ++it) {
        SketchItem *item = it.key();
        const QList<QVector3D> &orig = it.value();
        for (int i = 0; i < orig.size() && i < item->pts.size(); ++i) {
            const V3 rel = orig[i] - m_rotCenter;
            const qreal u = V3::dotProduct(rel, fr.U), v = V3::dotProduct(rel, fr.V);
            const qreal u2 = u * cs - v * sn, v2 = u * sn + v * cs;
            item->pts[i] = orig[i] + fr.U * float(u2 - u) + fr.V * float(v2 - v);
        }
        if (item->type() == SketchItem::Text)
            item->textAngle = m_rotTxt.value(item) + d * sign;
        if (m_rotExt.contains(item)) {
            const V3 e = m_rotExt.value(item);
            const qreal u = V3::dotProduct(e, fr.U), v = V3::dotProduct(e, fr.V);
            item->extrude = e + fr.U * float(u * cs - v * sn - u) + fr.V * float(u * sn + v * cs - v);
        }
    }
    update();
}

void Canvas::finishRotate()
{
    m_rotating = false;
    m_rsOrig.clear();
    m_rotTxt.clear();
    m_rotExt.clear();
    snapshotCommit("Rotate");
    if (std::fabs(m_rotAngle) > 1e-6)
        emit message(QString("Rotated %1°").arg(m_rotAngle, 0, 'f', 1));
    emit selectionChanged();
    update();
}

void Canvas::flipSelection(bool horizontal)
{
    QRectF r;
    if (m_selected.isEmpty() || !selectionBoxUV(r)) {
        emit message("Flip: select the objects first");
        return;
    }
    snapshotBegin();
    m_rsOrig.clear();
    m_rsExtra.clear();
    for (SketchItem *it : std::as_const(m_selected)) {
        m_rsOrig[it] = it->pts;
        m_rsExtra[it] = {it->textSize, it->imageSizeMm};
    }
    applyScaleUV(r.center(), horizontal ? -1.0 : 1.0, horizontal ? 1.0 : -1.0);
    for (SketchItem *it : std::as_const(m_selected)) {
        const auto ex = m_rsExtra.value(it);
        if (it->type() == SketchItem::Text) {
            it->textSize = ex.first;
            it->textAngle = -it->textAngle;             // text stays readable
        }
        if (it->type() == SketchItem::ImageItem)
            it->imageSizeMm = ex.second;
    }
    m_rsOrig.clear();
    m_rsExtra.clear();
    m_rsSu = m_rsSv = 1;
    snapshotCommit(horizontal ? "Flip Horizontal" : "Flip Vertical");
    emit selectionChanged();
    update();
}

void Canvas::setSelectedFill(const QColor &c)
{
    if (m_selected.isEmpty()) {
        emit message("Fill: select a closed shape (rectangle, circle or closed polyline)");
        return;
    }
    snapshotBegin();
    int n = 0;
    for (SketchItem *it : std::as_const(m_selected))
        if (closedShape(it)) {
            it->fill = c;
            ++n;
        }
    snapshotCommit(c.isValid() ? "Fill" : "Remove Fill");
    if (!n)
        emit message("Fill works on closed shapes: rectangles, circles and closed polylines");
    update();
}

void Canvas::arrangeSelection(bool toFront)
{
    if (m_selected.isEmpty())
        return;
    QList<SketchItem *> sel, rest;
    for (SketchItem *it : std::as_const(m_items))
        (m_selected.contains(it) ? sel : rest).append(it);
    snapshotBegin();
    m_items = toFront ? rest + sel : sel + rest;
    snapshotCommit(toFront ? "Bring to Front" : "Send to Back");
    update();
}

// ---------------------------------------------------------------------------
// Visibility and the Fix constraint
// ---------------------------------------------------------------------------
void Canvas::hideSelection()
{
    if (m_selected.isEmpty()) {
        emit message("Hide: select objects first");
        return;
    }
    snapshotBegin();
    const int n = m_selected.size();
    for (SketchItem *it : std::as_const(m_selected)) {
        it->hidden = true;
        it->selected = false;
    }
    m_selected.clear();
    m_hover = nullptr;
    snapshotCommit("Hide");
    emit message(QString("%1 object(s) hidden - Model > Show all brings them back").arg(n));
    emit selectionChanged();
    update();
}

void Canvas::showAll()
{
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_items))
        it->hidden = false;
    snapshotCommit("Show All");
    update();
}

void Canvas::isolateSelection()
{
    if (m_selected.isEmpty()) {
        emit message("Isolate: select the objects to keep visible");
        return;
    }
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_items))
        if (!m_selected.contains(it))
            it->hidden = true;
    m_hover = nullptr;
    snapshotCommit("Isolate");
    emit message("Only the selection is visible - Model > Show all to see everything again");
    update();
}

void Canvas::fixSelection()
{
    if (m_selected.isEmpty()) {
        emit message("Fix: select objects first");
        return;
    }
    snapshotBegin();
    const int n = m_selected.size();
    for (SketchItem *it : std::as_const(m_selected)) {
        it->locked = true;
        it->selected = false;
    }
    m_selected.clear();
    m_hover = nullptr;
    snapshotCommit("Fix");
    emit message(QString("%1 object(s) fixed: they still snap but cannot be selected, moved or cut "
                         "(Model > Unfix all)").arg(n));
    emit selectionChanged();
    update();
}

void Canvas::unfixAll()
{
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_items))
        it->locked = false;
    snapshotCommit("Unfix All");
    emit message("All objects are free again");
    update();
}

// ---------------------------------------------------------------------------
// Solid bodies
// ---------------------------------------------------------------------------
void Canvas::extrudeSolid(double dist)
{
    if (m_selected.isEmpty() || std::fabs(dist) < 1e-9) {
        emit message("Extrude: select closed shapes (rectangle, circle, closed polyline) and give a distance");
        return;
    }
    snapshotBegin();
    int n = 0, skipped = 0;
    for (SketchItem *it : std::as_const(m_selected)) {
        if (!closedShape(it)) {
            ++skipped;
            continue;
        }
        it->extrude = frameFor(std::max(0, it->plane)).N * float(dist);
        ++n;
    }
    snapshotCommit("Extrude Solid");
    QString msg = QString("%1 solid body(ies) %2 thick").arg(n).arg(Units::format(dist));
    if (skipped)
        msg += QString(" - %1 open shape(s) skipped: close them first (or use Edit > Extrude wireframe)").arg(skipped);
    emit message(msg);
    update();
}

void Canvas::removeExtrude()
{
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected))
        it->extrude = QVector3D();
    snapshotCommit("Flatten Body");
    update();
}

void Canvas::sketchOnBodyFace(bool top)
{
    SketchItem *b = nullptr;
    for (SketchItem *it : std::as_const(m_selected))
        if (!it->extrude.isNull()) { b = it; break; }
    if (!b || b->pts.isEmpty()) {
        emit message("Sketch on face: select a solid body first");
        return;
    }
    const QVector3D en = b->extrude.normalized();
    int best = -1;
    for (int p = 0; p < 4; ++p)
        if (std::fabs(V3::dotProduct(frameFor(p).N, en)) > 0.999f) {
            best = p;
            if (p == int(m_plane))
                break;
        }
    if (best < 0) {
        emit message("Sketch on face: that face is not parallel to a sketch plane");
        return;
    }
    const QVector3D pt = b->pts[0] + (top ? b->extrude : QVector3D());
    setPlane(Plane(best));
    setPlaneOffset(V3::dotProduct(pt, frameFor(best).N));
    emit message(QString("Sketch plane set on the %1 face of the body").arg(top ? "top" : "bottom"));
    update();
}

void Canvas::splitBodyAtPlane()
{
    const Frame fr = planeFrame();
    const qreal n0 = V3::dotProduct(planeOrigin(), fr.N);
    const QList<SketchItem *> sel = m_selected;
    snapshotBegin();
    int n = 0;
    for (SketchItem *it : sel) {
        if (it->extrude.isNull() || it->pts.isEmpty())
            continue;
        if (std::fabs(V3::dotProduct(it->extrude.normalized(), fr.N)) < 0.999f)
            continue;                              // the plane must be parallel to the body's faces
        const qreal a = V3::dotProduct(it->pts[0], fr.N), b = V3::dotProduct(it->pts[0] + it->extrude, fr.N);
        const qreal t = (n0 - a) / (b - a);
        if (t <= 1e-4 || t >= 1.0 - 1e-4)
            continue;
        const QVector3D cut = it->extrude * float(t);
        SketchItem *c = it->clone();
        for (QVector3D &q : c->pts)
            q += cut;
        c->extrude = it->extrude - cut;
        it->extrude = cut;
        m_items.insert(m_items.indexOf(it) + 1, c);
        ++n;
    }
    snapshotCommit("Split Body");
    emit message(n ? QString("Split %1 body(ies) at the sketch plane").arg(n)
                   : QString("Split: select solid bodies crossed by the current sketch plane (set its offset / Tab to change plane)"));
    update();
}

// ---------------------------------------------------------------------------
// One-shot constraints on selected lines (the first selected line is the reference)
// ---------------------------------------------------------------------------
void Canvas::constrainSelection(int kind)
{
    QList<SketchItem *> lines;
    for (SketchItem *it : std::as_const(m_selected))
        if (it->type() == SketchItem::Line && it->pts.size() >= 2)
            lines << it;
    static const char *names[] = {"Horizontal", "Vertical", "Parallel", "Perpendicular", "Equal length", "Coincident"};
    kind = std::clamp(kind, 0, 5);
    if (lines.isEmpty() || (kind >= 2 && lines.size() < 2)) {
        emit message(QString("%1: select %2 line(s) - the FIRST one selected is the reference")
                         .arg(names[kind]).arg(kind >= 2 ? "two or more" : "one or more"));
        return;
    }
    const Frame fr = planeFrame();
    const SketchItem *ref = lines.first();
    snapshotBegin();
    for (int i = kind >= 2 ? 1 : 0; i < lines.size(); ++i) {
        SketchItem *l = lines[i];
        const QVector3D a = l->pts[0], d = l->pts[1] - a;
        const qreal du = V3::dotProduct(d, fr.U), dv = V3::dotProduct(d, fr.V), dn = V3::dotProduct(d, fr.N);
        const qreal L = std::hypot(du, dv);
        switch (kind) {
        case 0:
            l->pts[1] = a + fr.U * float(du >= 0 ? L : -L) + fr.N * float(dn);
            break;
        case 1:
            l->pts[1] = a + fr.V * float(dv >= 0 ? L : -L) + fr.N * float(dn);
            break;
        case 2:
        case 3: {
            const QVector3D rd = ref->pts[1] - ref->pts[0];
            qreal ru = V3::dotProduct(rd, fr.U), rv = V3::dotProduct(rd, fr.V);
            const qreal rl = std::hypot(ru, rv);
            if (rl < 1e-9)
                break;
            ru /= rl;
            rv /= rl;
            if (kind == 3) { const qreal t = ru; ru = -rv; rv = t; }
            if (du * ru + dv * rv < 0) { ru = -ru; rv = -rv; }
            l->pts[1] = a + fr.U * float(ru * L) + fr.V * float(rv * L) + fr.N * float(dn);
            break;
        }
        case 4: {
            const float Lr = (ref->pts[1] - ref->pts[0]).length();
            if (d.length() > 1e-9f)
                l->pts[1] = a + d.normalized() * Lr;
            break;
        }
        default: {                                 // coincident: nearest ends meet at the reference end
            int bi = 0, bj = 0;
            qreal bd = 1e18;
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < 2; ++y) {
                    const qreal dd = (ref->pts[x] - l->pts[y]).length();
                    if (dd < bd) { bd = dd; bi = x; bj = y; }
                }
            l->pts[bj] = ref->pts[bi];
            break;
        }
        }
    }
    snapshotCommit(names[kind]);
    emit message(QString("%1 applied").arg(names[kind]));
    update();
}

// Two selected lines: trim/extend them to their corner and round it (fillet) or bevel it (chamfer).
// Radius / distance 0 just makes a sharp corner.
void Canvas::filletLines(double r, bool chamfer)
{
    QList<SketchItem *> lines;
    for (SketchItem *it : std::as_const(m_selected))
        if (it->type() == SketchItem::Line && it->pts.size() >= 2)
            lines << it;
    if (lines.size() != 2) {
        emit message(QString("%1: select exactly two lines").arg(chamfer ? "Chamfer" : "Fillet"));
        return;
    }
    const Frame fr = planeFrame();
    SketchItem *A = lines[0], *B = lines[1];
    auto uv = [&](const QVector3D &q) { return QPointF(V3::dotProduct(q, fr.U), V3::dotProduct(q, fr.V)); };
    const qreal nn = V3::dotProduct(A->pts[0], fr.N);
    auto to3 = [&](const QPointF &q) { return fr.U * float(q.x()) + fr.V * float(q.y()) + fr.N * float(nn); };
    const QLineF la(uv(A->pts[0]), uv(A->pts[1])), lb(uv(B->pts[0]), uv(B->pts[1]));
    QPointF P;
    if (la.intersects(lb, &P) == QLineF::NoIntersection) {
        emit message("Fillet / chamfer: the lines are parallel");
        return;
    }
    // the end of each line nearer the corner is the one that moves
    const int ia = QLineF(la.p1(), P).length() < QLineF(la.p2(), P).length() ? 0 : 1;
    const int ib = QLineF(lb.p1(), P).length() < QLineF(lb.p2(), P).length() ? 0 : 1;
    const QPointF Fa = ia == 0 ? la.p2() : la.p1(), Fb = ib == 0 ? lb.p2() : lb.p1();
    const qreal da = QLineF(P, Fa).length(), db = QLineF(P, Fb).length();
    if (da < 1e-9 || db < 1e-9)
        return;
    const QPointF ua = (Fa - P) / da, ub = (Fb - P) / db;
    QPointF Ta = P, Tb = P;
    SketchItem *made = nullptr;
    if (r > 1e-9) {
        if (chamfer) {
            Ta = P + ua * r;
            Tb = P + ub * r;
            if (r >= da || r >= db) {
                emit message("Chamfer: distance is longer than a line");
                return;
            }
            made = new SketchItem(SketchItem::Line);
            made->pts = {to3(Ta), to3(Tb)};
        } else {
            const qreal cosT = std::clamp(ua.x() * ub.x() + ua.y() * ub.y(), -1.0, 1.0);
            const qreal half = std::acos(cosT) / 2.0;
            if (half < 1e-6 || half > PI / 2 - 1e-6) {
                emit message("Fillet: the lines are (almost) in line - nothing to round");
                return;
            }
            const qreal t = r / std::tan(half);
            if (t >= da || t >= db) {
                emit message("Fillet: radius too large for these lines");
                return;
            }
            Ta = P + ua * t;
            Tb = P + ub * t;
            QPointF bis = ua + ub;
            bis /= std::hypot(bis.x(), bis.y());
            const QPointF C = P + bis * (r / std::sin(half));
            const QPointF M = C - bis * r;
            made = new SketchItem(SketchItem::Arc);
            made->pts = {to3(Ta), to3(M), to3(Tb)};
        }
    }
    snapshotBegin();
    A->pts[ia] = to3(Ta) + fr.N * float(V3::dotProduct(A->pts[ia], fr.N) - nn);
    B->pts[ib] = to3(Tb) + fr.N * float(V3::dotProduct(B->pts[ib], fr.N) - nn);
    if (made) {
        made->plane = int(m_plane);
        made->color = A->color;
        made->width = A->width;
        made->lineStyle = A->lineStyle;
        made->group = A->group;
        m_items.append(made);
    }
    snapshotCommit(chamfer ? "Chamfer" : (r > 1e-9 ? "Fillet" : "Corner"));
    update();
}

// Explode: rectangles and polylines fall apart into single lines at every joint; groups are dissolved.
void Canvas::explodeSelection()
{
    if (m_selected.isEmpty()) {
        emit message("Explode: select rectangles, polylines or groups");
        return;
    }
    const QList<SketchItem *> sel = m_selected;
    clearSelection();
    snapshotBegin();
    int made = 0, opened = 0;
    for (SketchItem *it : sel) {
        if (it->group) { it->group = 0; ++opened; }
        if (!it->extrude.isNull())
            continue;                                  // solid bodies stay whole
        QVector<QVector3D> chain;
        if (it->type() == SketchItem::Rect) {
            const QVector<QVector3D> c = rectCorners(it);
            if (c.size() == 4)
                chain = {c[0], c[1], c[2], c[3], c[0]};
        } else if (it->type() == SketchItem::Polyline && it->pts.size() >= 3) {
            chain = QVector<QVector3D>(it->pts.begin(), it->pts.end());
        }
        if (chain.size() < 2)
            continue;
        int at = m_items.indexOf(it);
        for (int i = 0; i + 1 < chain.size(); ++i) {
            if ((chain[i + 1] - chain[i]).length() < 1e-6f)
                continue;
            auto *l = new SketchItem(SketchItem::Line);
            l->pts = {chain[i], chain[i + 1]};
            l->color = it->color;
            l->width = it->width;
            l->lineStyle = it->lineStyle;
            l->plane = it->plane;
            m_items.insert(++at, l);
            ++made;
        }
        m_items.removeOne(it);
        if (m_hover == it) m_hover = nullptr;
        if (m_editItem == it) m_editItem = nullptr;
        delete it;
    }
    snapshotCommit("Explode");
    emit message(QString("Exploded into %1 line(s)%2").arg(made)
                     .arg(opened ? QString(", %1 group member(s) released").arg(opened) : QString()));
    emit selectionChanged();
    update();
}
