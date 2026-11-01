#include "canvas.h"
#include "canvas_util.h"
#include "units.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QQuaternion>

#include <algorithm>
#include <cmath>
#include <memory>

using V3 = QVector3D;

// ===========================================================================
// Move / rotate gizmo on the selection (Select tool), like AutoCAD's 3D gizmos:
// arrows move along X / Y / Z, squares move in a plane, rings turn about an axis.
// Click a handle, move, click (or press - drag - release); type a distance / angle + Enter.
// Handles: 0..2 = X / Y / Z arrow or ring, 3..5 = the XY, YZ, ZX squares.
// ===========================================================================
namespace {
const V3 kAxis[3] = {V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1)};
const QColor kAxisCol[3] = {QColor(235, 85, 85), QColor(80, 200, 110), QColor(80, 145, 255)};

// the two axes that span the ring of axis i (right-handed with it)
void ringBasis(int i, V3 &b1, V3 &b2)
{
    b1 = kAxis[(i + 1) % 3];
    b2 = kAxis[(i + 2) % 3];
}
} // namespace

void Canvas::setGizmoMode(GizmoMode m)
{
    if (m_gzActive >= 0)
        gzCancel();
    m_gizmoMode = m;
    m_gzHover = -1;
    emit gizmoModeChanged(int(m));
    emitHint();
    update();
}

bool Canvas::gizmoVisible() const
{
    // the 3D view only: Top / Front / Side keep the 2D grips (resize, turn) for flat work
    return m_gizmoMode != GizmoMode::Off && m_view == View::Iso && m_tool == Tool::Select && !m_selected.isEmpty() &&
           !m_exporting && !m_dragging && !m_rubberActive && !m_resizing && !m_rotating;
}

double Canvas::gizmoWorldSize(const V3 &c) const
{
    return 80.0 / std::max(1e-6, double(scaleAt(c)));
}

QPointF Canvas::gizmoGripScreen(int h) const
{
    const V3 c = selectionCenter();
    const double L = gizmoWorldSize(c);
    if (h >= 3) {
        const int i = h - 3, j = (i + 1) % 3;
        return project(c + (kAxis[i] + kAxis[j]) * float(0.375 * L));
    }
    if (m_gizmoMode == GizmoMode::Rotate) {
        V3 b1, b2;
        ringBasis(h, b1, b2);
        return project(c + (b1 + b2).normalized() * float(0.9 * L));
    }
    return project(c + kAxis[h] * float(0.7 * L));
}

int Canvas::gzHit(const QPoint &s) const
{
    if (!gizmoVisible())
        return -1;
    const V3 c = selectionCenter();
    const double L = gizmoWorldSize(c);
    const QPointF sp(s);
    if (m_gizmoMode == GizmoMode::Move) {
        for (int k = 0; k < 3; ++k) {                      // plane squares sit between two arrows
            const V3 a = kAxis[k], b = kAxis[(k + 1) % 3];
            const QPolygonF sq({project(c + (a * 0.25f + b * 0.25f) * float(L)), project(c + (a * 0.5f + b * 0.25f) * float(L)),
                                project(c + (a * 0.5f + b * 0.5f) * float(L)), project(c + (a * 0.25f + b * 0.5f) * float(L))});
            if (sq.containsPoint(sp, Qt::OddEvenFill))
                return 3 + k;
        }
    }
    double best = 9.0;
    int hit = -1;
    for (int i = 0; i < 3; ++i) {
        if (m_gizmoMode == GizmoMode::Move) {
            const qreal d = distToSegment(sp, QLineF(project(c + kAxis[i] * float(0.15 * L)), project(c + kAxis[i] * float(1.08 * L))));
            if (d < best) { best = d; hit = i; }
        } else {
            V3 b1, b2;
            ringBasis(i, b1, b2);
            QPointF prev;
            for (int k = 0; k <= 48; ++k) {
                const double t = 2 * PI * k / 48;
                const QPointF q = project(c + (b1 * float(std::cos(t)) + b2 * float(std::sin(t))) * float(0.9 * L));
                if (k > 0) {
                    const qreal d = distToSegment(sp, QLineF(prev, q));
                    if (d < best) { best = d; hit = i; }
                }
                prev = q;
            }
        }
    }
    return hit;
}

// point of the line c + dir t closest to the cursor ray
static double rayLineParam(const V3 &ro, const V3 &rd, const V3 &c, const V3 &dir, bool &ok)
{
    const V3 w0 = c - ro;
    const double b = V3::dotProduct(dir, rd), d0 = V3::dotProduct(dir, w0), e = V3::dotProduct(rd, w0);
    const double den = 1.0 - b * b;
    ok = den > 1e-6;
    return ok ? (b * e - d0) / den : 0.0;
}

double Canvas::gzRingAngle(const QPoint &s) const
{
    V3 b1, b2, hit, ro, rd;
    ringBasis(m_gzActive, b1, b2);
    screenRay(QPointF(s), ro, rd);
    if (rayPlane(ro, rd, m_gzCenter, kAxis[m_gzActive], hit)) {
        const V3 w = hit - m_gzCenter;
        return std::atan2(V3::dotProduct(w, b2), V3::dotProduct(w, b1)) * 180.0 / PI;
    }
    // ring seen edge-on: the angle around its centre on screen
    const QPointF c = project(m_gzCenter), d = QPointF(s) - c;
    const double sgn = V3::dotProduct(kAxis[m_gzActive], m_bf) < 0 ? 1.0 : -1.0;
    return sgn * std::atan2(-d.y(), d.x()) * 180.0 / PI;
}

void Canvas::gzBegin(int h, const QPoint &s)
{
    m_gzActive = h;
    m_gzCenter = selectionCenter();
    m_gzDelta = V3();
    m_gzAngle = 0;
    m_gzNote.clear();
    dynClear();
    V3 ro, rd;
    screenRay(QPointF(s), ro, rd);
    if (m_gizmoMode == GizmoMode::Rotate) {
        m_gzA0 = gzRingAngle(s);
    } else if (h < 3) {
        bool ok = false;
        m_gzT0 = rayLineParam(ro, rd, m_gzCenter, kAxis[h], ok);
    } else {
        if (!rayPlane(ro, rd, m_gzCenter, kAxis[(h - 3 + 2) % 3], m_gzStart))
            m_gzStart = m_gzCenter;
    }
    emitHint();
    update();
}

void Canvas::gzUpdate(const QPoint &s)
{
    if (m_gzActive < 0)
        return;
    m_gzNote.clear();
    V3 ro, rd;
    screenRay(QPointF(s), ro, rd);
    const double g = m_set.snapGrid ? m_gridSize : 0.0;
    if (m_gizmoMode == GizmoMode::Rotate) {
        double a = std::remainder(gzRingAngle(s) - m_gzA0, 360.0);
        const bool shift = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
        a = shift ? std::round(a / 15.0) * 15.0 : std::round(a);
        m_gzAngle = a;
        return;
    }
    if (m_gzActive < 3) {
        const V3 ax = kAxis[m_gzActive];
        bool ok = false;
        const double t = rayLineParam(ro, rd, m_gzCenter, ax, ok);
        if (!ok)
            return;
        double d = t - m_gzT0;
        if (g > 0)
            d = std::round(d / g) * g;
        // an object snap point under the cursor: the selection's near or far side stops on it
        if (m_set.snapObject) {
            QList<SnapCand> cands;
            for (const SketchItem *it : std::as_const(m_items)) {
                if (it->hidden || it->selected)
                    continue;
                allSnapPoints(it, cands);
            }
            qreal best = 12.0;
            bool hit = false;
            double target = 0;
            for (const SnapCand &cd : std::as_const(cands)) {
                QPointF sp;
                if (!projectD(cd.p, sp))
                    continue;
                const qreal px = QLineF(sp, QPointF(s)).length();
                if (px < best) { best = px; target = V3::dotProduct(cd.p, ax); hit = true; }
            }
            if (hit) {
                double lo = 1e300, hi = -1e300;
                for (const SketchItem *it : std::as_const(m_selected))
                    for (const auto &st : strips(it))
                        for (const V3 &q : st) {
                            lo = std::min(lo, double(V3::dotProduct(q, ax)));
                            hi = std::max(hi, double(V3::dotProduct(q, ax)));
                        }
                const double free = t - m_gzT0, d1 = target - hi, d2 = target - lo;
                d = std::fabs(d1 - free) < std::fabs(d2 - free) ? d1 : d2;
                m_gzNote = "snap";
            }
        }
        m_gzDelta = ax * float(d);
    } else {
        V3 hit;
        const V3 nrm = kAxis[(m_gzActive - 3 + 2) % 3];
        if (!rayPlane(ro, rd, m_gzCenter, nrm, hit))
            return;
        V3 d = hit - m_gzStart;
        d -= nrm * V3::dotProduct(d, nrm);
        if (g > 0)
            d = V3(float(std::round(d.x() / g) * g), float(std::round(d.y() / g) * g), float(std::round(d.z() / g) * g));
        m_gzDelta = d;
    }
}

std::function<V3(const V3 &)> Canvas::gzTransform() const
{
    if (m_gizmoMode == GizmoMode::Rotate && m_gzActive >= 0 && m_gzActive < 3) {
        const QQuaternion q = QQuaternion::fromAxisAndAngle(kAxis[m_gzActive], float(m_gzAngle));
        const V3 c = m_gzCenter;
        return [q, c](const V3 &p) { return c + q.rotatedVector(p - c); };
    }
    const V3 d = m_gzDelta;
    return [d](const V3 &p) { return p + d; };
}

void Canvas::gzCommit()
{
    if (m_gzActive < 0)
        return;
    const bool rot = m_gizmoMode == GizmoMode::Rotate;
    const auto f = gzTransform();
    const double amount = rot ? std::fabs(m_gzAngle) : double(m_gzDelta.length());
    const QString what = rot ? QString("Rotated %1°").arg(m_gzAngle, 0, 'g', 6)
                             : QString("Moved %1").arg(Units::format(m_gzDelta.length()));
    m_gzActive = -1;
    m_gzNote.clear();
    dynClear();
    if (amount > 1e-9) {
        applyXformSelection(f, rot ? "Rotate" : "Move");
        emit message(what);
    }
    emitHint();
    update();
}

void Canvas::gzCancel()
{
    m_gzActive = -1;
    m_gzNote.clear();
    dynClear();
    emit message("Gizmo: cancelled");
    emitHint();
    update();
}

// every selected object is replaced by its transformed copy (bodies keep their thickness direction,
// rectangles turned off their axes become closed polylines)
void Canvas::applyXformSelection(const std::function<V3(const V3 &)> &f, const QString &name,
                                 const std::function<void(SketchItem *)> &post)
{
    if (m_selected.isEmpty())
        return;
    snapshotBegin();
    QList<SketchItem *> made;
    for (SketchItem *it : QList<SketchItem *>(m_selected)) {
        SketchItem *n = xformClone(it, f);
        if (post)
            post(n);
        n->selected = true;
        const int idx = int(m_items.indexOf(it));
        if (idx >= 0)
            m_items[idx] = n;
        else
            m_items.append(n);
        if (m_hover == it) m_hover = nullptr;
        if (m_editItem == it) m_editItem = nullptr;
        if (m_joinFirst == it) m_joinFirst = nullptr;
        delete it;
        made << n;
    }
    m_selected = made;
    snapshotCommit(name);
    emit selectionChanged();
    update();
}

void Canvas::drawGizmo3D(QPainter &p) const
{
    if (!gizmoVisible() && m_gzActive < 0)
        return;
    // the moved / turned selection, as a preview
    if (m_gzActive >= 0) {
        const auto f = gzTransform();
        for (const SketchItem *it : m_selected) {
            std::unique_ptr<SketchItem> g(xformClone(it, f));
            drawItem(p, g.get(), true);
        }
    }
    const V3 c = m_gzActive >= 0 ? m_gzCenter + m_gzDelta : selectionCenter();
    const double L = gizmoWorldSize(c);
    QPointF sc;
    if (!projectD(c, sc))
        return;
    const QColor hot(255, 209, 102);
    p.save();
    auto col = [&](int h, const QColor &base) { return (h == m_gzActive || (m_gzActive < 0 && h == m_gzHover)) ? hot : base; };
    if (m_gizmoMode == GizmoMode::Move) {
        for (int k = 0; k < 3; ++k) {
            const V3 a = kAxis[k], b = kAxis[(k + 1) % 3];
            const QPolygonF sq({project(c + (a * 0.25f + b * 0.25f) * float(L)), project(c + (a * 0.5f + b * 0.25f) * float(L)),
                                project(c + (a * 0.5f + b * 0.5f) * float(L)), project(c + (a * 0.25f + b * 0.5f) * float(L))});
            QColor fc = col(3 + k, kAxisCol[(k + 2) % 3]);
            p.setPen(QPen(fc, 1.2));
            fc.setAlpha(90);
            p.setBrush(fc);
            p.drawPolygon(sq);
        }
        for (int i = 0; i < 3; ++i) {
            const QPointF a = project(c + kAxis[i] * float(0.15 * L)), b = project(c + kAxis[i] * float(L));
            const QColor cc = col(i, kAxisCol[i]);
            p.setPen(QPen(cc, 3.0, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(a, b);
            QPointF d = b - a;
            const qreal l = std::hypot(d.x(), d.y());
            if (l > 1e-3) {
                d /= l;
                const QPointF n(-d.y(), d.x());
                p.setPen(Qt::NoPen);
                p.setBrush(cc);
                p.drawPolygon(QPolygonF({b + d * 12.0, b + n * 5.5, b - n * 5.5}));
            }
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            V3 b1, b2;
            ringBasis(i, b1, b2);
            QPolygonF ring;
            for (int k = 0; k <= 64; ++k) {
                const double t = 2 * PI * k / 64;
                ring << project(c + (b1 * float(std::cos(t)) + b2 * float(std::sin(t))) * float(0.9 * L));
            }
            p.setPen(QPen(col(i, kAxisCol[i]), i == m_gzActive || (m_gzActive < 0 && i == m_gzHover) ? 3.5 : 2.4));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(ring);
        }
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(240, 244, 250));
    p.drawEllipse(sc, 4.0, 4.0);

    // the amount next to the cursor while a handle is held
    if (m_gzActive >= 0) {
        QFont fnt = QApplication::font();
        fnt.setPixelSize(12);
        fnt.setBold(true);
        p.setFont(fnt);
        const QFontMetricsF fm(fnt);
        const bool typed = !m_dyn[0].text.isEmpty();
        const bool rot = m_gizmoMode == GizmoMode::Rotate;
        QString txt;
        if (typed)
            txt = (rot ? QString::fromUtf8("\xE2\x88\xA0 ") : QString("D ")) + dynShown(0, true);
        else if (rot)
            txt = QString::fromUtf8("\xE2\x88\xA0 %1\xC2\xB0").arg(m_gzAngle, 0, 'g', 6);
        else
            txt = QString::fromUtf8("\xCE\x94 %1").arg(Units::format(m_gzDelta.length()));
        if (!m_gzNote.isEmpty() && !typed)
            txt += "  " + m_gzNote;
        const QPointF at = QPointF(m_lastMouse) + QPointF(22, -26);
        const QRectF r(at.x(), at.y() - 11, fm.horizontalAdvance(txt) + 14, 22);
        p.setPen(QPen(typed ? hot : accent(), typed ? 2 : 1));
        p.setBrush(typed ? QColor(66, 52, 18, 235) : QColor(12, 18, 28, 225));
        p.drawRoundedRect(r, 5, 5);
        p.setPen(typed ? QColor(255, 224, 150) : QColor(215, 230, 250));
        p.drawText(r, Qt::AlignCenter, txt);
    }
    p.restore();
}
