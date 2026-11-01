#include "canvas.h"
#include "canvas_util.h"
#include "units.h"

#include <QApplication>
#include <QGuiApplication>
#include <memory>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

#include <algorithm>
#include <cmath>

struct Canvas::Theme
{
    QColor bgTop, bgBottom, minor, major, paper, paperEdge, hudBg, hudText;
    bool light;
};

const Canvas::Theme &Canvas::theme() const
{
    static const Theme T[4] = {
        {QColor(14, 19, 27), QColor(6, 9, 14), QColor(82, 106, 136, 75), QColor(128, 158, 196, 140),
         QColor(18, 25, 35), QColor(48, 62, 82), QColor(8, 12, 17, 205), QColor(146, 166, 196), false},
        {QColor(58, 61, 66), QColor(34, 36, 40), QColor(140, 146, 154, 60), QColor(180, 186, 195, 120),
         QColor(66, 69, 74), QColor(110, 116, 124), QColor(20, 22, 25, 210), QColor(190, 196, 204), false},
        {QColor(20, 62, 116), QColor(8, 30, 66), QColor(130, 180, 240, 60), QColor(180, 215, 255, 135),
         QColor(24, 70, 128), QColor(120, 170, 230), QColor(6, 24, 52, 215), QColor(190, 215, 245), false},
        {QColor(240, 243, 248), QColor(214, 220, 230), QColor(110, 125, 148, 70), QColor(80, 95, 122, 140),
         QColor(255, 255, 255), QColor(160, 170, 188), QColor(255, 255, 255, 225), QColor(60, 72, 92), true},
    };
    return T[std::clamp(m_set.theme, 0, 3)];
}

QColor Canvas::adapt(const QColor &c) const
{
    const Theme &t = theme();
    const qreal l = c.lightnessF();
    if ((t.light && l > 0.72) || (!t.light && l < 0.18)) {
        QColor h = c.toHsl();
        return QColor::fromHslF(std::max<qreal>(0.0, h.hslHueF()), h.hslSaturationF(), 1.0 - l, c.alphaF());
    }
    return c;
}

static QColor axisColor(const QVector3D &d)
{
    if (std::fabs(d.x()) > 0.5f) return QColor(255, 107, 107);
    if (std::fabs(d.y()) > 0.5f) return QColor(88, 216, 133);
    return QColor(94, 164, 255);
}

// ---------------------------------------------------------------------------
void Canvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    drawScene(p);
    drawWelcome(p);
}

// a friendly empty-canvas card with the handful of things worth knowing first
void Canvas::drawWelcome(QPainter &p) const
{
    if (!m_items.isEmpty() || !m_pending.isEmpty() || m_tool != Tool::Select || width() < 520)
        return;
    const QRectF r(width() / 2.0 - 230, height() / 2.0 - 120, 460, 240);
    p.setPen(QPen(QColor(53, 224, 200, 120), 1.2));
    p.setBrush(QColor(12, 18, 28, 205));
    p.drawRoundedRect(r, 16, 16);
    QFont f = QApplication::font();
    f.setPixelSize(22);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(53, 224, 200));
    p.drawText(QRectF(r.left(), r.top() + 14, r.width(), 36), Qt::AlignCenter, "Start sketching");
    f.setPixelSize(13);
    f.setBold(false);
    p.setFont(f);
    const char *rows[][2] = {{"L  R  C  P", "line, rectangle, circle, polyline"},
                             {"Type while drawing", "exact size - Tab switches length / angle"},
                             {"F8 / Shift   F10", "ortho  /  polar angle tracking"},
                             {"M", "precise move, with copy (K)"},
                             {"Ctrl+G", "group  -  select a group as one"},
                             {"Ctrl+K", "search every command"}};
    qreal y = r.top() + 62;
    for (const auto &row : rows) {
        p.setPen(QColor(255, 209, 102));
        p.drawText(QRectF(r.left() + 24, y, 170, 24), Qt::AlignLeft | Qt::AlignVCenter, row[0]);
        p.setPen(QColor(190, 205, 228));
        p.drawText(QRectF(r.left() + 200, y, 250, 24), Qt::AlignLeft | Qt::AlignVCenter, row[1]);
        y += 28;
    }
}

void Canvas::drawBackground(QPainter &p)
{
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0, theme().bgTop);
    g.setColorAt(1, theme().bgBottom);
    p.fillRect(rect(), g);
}

void Canvas::drawScene(QPainter &p)
{
    drawBackground(p);
    const Frame fr = planeFrame();
    const Theme &th = theme();

    // ---- paper sheet on the XY plane ----
    auto quad = [this](const QVector3D &o, const QVector3D &U, const QVector3D &V, qreal hw, qreal hh,
                       QPolygonF &poly) {
        const QVector3D c[4] = {o - U * float(hw) - V * float(hh), o + U * float(hw) - V * float(hh),
                                o + U * float(hw) + V * float(hh), o - U * float(hw) + V * float(hh)};
        poly.clear();
        for (const QVector3D &q : c) {
            QPointF s;
            if (!projectD(q, s))
                return false;
            poly << s;
        }
        return true;
    };
    QPolygonF poly;
    if (m_set.showPaper && quad(QVector3D(), QVector3D(1, 0, 0), QVector3D(0, 1, 0),
                                m_pageSize.width() / 2, m_pageSize.height() / 2, poly)) {
        p.setPen(QPen(th.paperEdge, 1.0));
        p.setBrush(th.paper);
        p.drawPolygon(poly);
    }

    // ---- active sketch plane sheet ----
    if (m_set.showPlaneSheet && quad(planeOrigin(), fr.U, fr.V, m_set.gridExtent, m_set.gridExtent, poly)) {
        p.setPen(QPen(QColor(53, 224, 200, 110), 1.0));
        p.setBrush(QColor(53, 224, 200, th.light ? 22 : 14));
        p.drawPolygon(poly);
    }
    p.setBrush(Qt::NoBrush);

    drawGrid(p);
    drawAxes(p);

    // ---- entities: images first, then hatches, then linework ----
    for (const SketchItem *it : std::as_const(m_items))
        if (!it->hidden && it->type() == SketchItem::ImageItem)
            drawItem(p, it);
    for (const SketchItem *it : std::as_const(m_items))
        if (!it->hidden && it->type() == SketchItem::Hatch)
            drawItem(p, it);
    renderSolids(p);                                   // solid boards, depth-buffered (canvas_solid.cpp)
    for (const SketchItem *it : std::as_const(m_items))
        if (!it->hidden && it->type() != SketchItem::ImageItem && it->type() != SketchItem::Hatch && !isSolid(it))
            drawItem(p, it);

    // ---- Hatch tool: the region under the cursor, in the current style ----
    if (!m_exporting && m_tool == Tool::Hatch && !m_hatchPreview.isEmpty()) {
        SketchItem tmp(SketchItem::Hatch);
        tmp.plane = int(m_plane);
        tmp.color = m_defColor;
        tmp.width = 0.18;
        tmp.hatch = m_hatchStyle.pattern;
        tmp.hatchScale = m_hatchPreviewScale;
        tmp.hatchAngle = m_hatchStyle.angle;
        tmp.fill = m_hatchStyle.fill;
        p.save();
        p.setOpacity(0.75);
        drawHatch(p, &tmp, m_hatchPreview);
        p.restore();
        p.setPen(QPen(accent(), 1.6, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        for (const auto &l : std::as_const(m_hatchPreview))
            drawStrip(p, l);
    }

    if (!m_exporting) {
        drawGuides(p);
        drawGhost(p);
        drawPending(p);
        drawSolidOverlay(p);
        drawBlade(p);
        drawFence(p);
        drawHandles(p);
        drawTransformBox(p);
        drawGizmo3D(p);
        drawRotateTool(p);
        drawFilletTool(p);
        drawSweepTool(p);
        drawSnapMarker(p);
        if (m_rubberActive) {
            const bool window = m_lastMouse.x() >= m_pressScreen.x();
            p.setPen(QPen(accent(), 1, window ? Qt::SolidLine : Qt::DashLine));
            p.setBrush(QColor(53, 224, 200, window ? 30 : 18));
            p.drawRect(m_rubberBand);
            p.setBrush(Qt::NoBrush);
        }
        if (m_set.showGizmo)
            drawGizmo(p);
        if (m_set.showHud)
            drawHud(p);
    }
}

// ---------------------------------------------------------------------------
// grid + axes
// ---------------------------------------------------------------------------
void Canvas::drawGrid(QPainter &p)
{
    if (!m_set.showGrid || m_orbiting)                     // turning the view: no grid until the mouse is let go
        return;
    const Frame fr = planeFrame();
    const QVector3D o = planeOrigin();
    const qreal E = m_set.gridExtent;
    const Theme &th = theme();

    // only the part of the sheet that is on screen, so a big sheet keeps a fine grid
    qreal u0 = -E, u1 = E, v0 = -E, v1 = E;
    {
        qreal a0 = 1e300, a1 = -1e300, b0 = 1e300, b1 = -1e300;
        bool all = true;
        for (const QPointF &c : {QPointF(0, 0), QPointF(width(), 0), QPointF(0, height()), QPointF(width(), height())}) {
            QVector3D ro, rd, hit;
            screenRay(c, ro, rd);
            if (!rayPlane(ro, rd, o, fr.N, hit) ||
                (perspectiveActive() && QVector3D::dotProduct(hit - ro, m_bf) <= 0)) {
                all = false;                               // horizon in view: keep the whole sheet
                break;
            }
            const qreal u = QVector3D::dotProduct(hit - o, fr.U), v = QVector3D::dotProduct(hit - o, fr.V);
            a0 = std::min(a0, u); a1 = std::max(a1, u);
            b0 = std::min(b0, v); b1 = std::max(b1, v);
        }
        if (all) {
            u0 = std::max(u0, a0); u1 = std::min(u1, a1);
            v0 = std::max(v0, b0); v1 = std::min(v1, b1);
        }
    }
    qreal step = m_gridSize;
    while (step * m_zoom < 7.0)
        step *= 2.0;
    while ((u1 - u0) / step + (v1 - v0) / step > 1040)
        step *= 2.0;
    const int every = Units::isImperial() ? 12 : 10;     // a major line every foot / every 10 cells

    QVector<QLineF> minor, major;
    QLineF l;
    if (u1 > u0 && v1 > v0) {
        for (long long i = (long long)std::ceil(u0 / step); i * step <= u1; ++i) {
            if (i == 0)
                continue;
            const qreal t = i * step;
            if (projectSeg(o + fr.U * float(t) + fr.V * float(v0), o + fr.U * float(t) + fr.V * float(v1), l))
                ((i % every == 0) ? major : minor).append(l);
        }
        for (long long i = (long long)std::ceil(v0 / step); i * step <= v1; ++i) {
            if (i == 0)
                continue;
            const qreal t = i * step;
            if (projectSeg(o + fr.U * float(u0) + fr.V * float(t), o + fr.U * float(u1) + fr.V * float(t), l))
                ((i % every == 0) ? major : minor).append(l);
        }
    }
    p.setPen(QPen(th.minor, 1.0));
    p.drawLines(minor);
    p.setPen(QPen(th.major, 1.0));
    p.drawLines(major);

    // the two in-plane axis lines through the plane origin
    const qreal ext = E;
    if (projectSeg(o - fr.U * float(ext), o + fr.U * float(ext), l)) {
        QColor c = axisColor(fr.U); c.setAlpha(190);
        p.setPen(QPen(c, 1.4));
        p.drawLine(l);
    }
    if (projectSeg(o - fr.V * float(ext), o + fr.V * float(ext), l)) {
        QColor c = axisColor(fr.V); c.setAlpha(190);
        p.setPen(QPen(c, 1.4));
        p.drawLine(l);
    }
}

void Canvas::drawAxes(QPainter &p)
{
    if (!m_set.showAxes)
        return;
    const QVector3D dirs[3] = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};
    const char *names[3] = {"X", "Y", "Z"};
    const qreal L = 70.0 / m_zoom;               // origin triad: constant 70 px
    const qreal far = m_set.gridExtent * 1.5;
    QFont f = QApplication::font();
    f.setPixelSize(11);
    f.setBold(true);
    p.setFont(f);
    for (int i = 0; i < 3; ++i) {
        QColor c = axisColor(dirs[i]);
        QLineF l;
        c.setAlpha(60);
        p.setPen(QPen(c, 1.0));
        if (projectSeg(QVector3D(), dirs[i] * float(far), l))
            p.drawLine(l);
        c.setAlpha(235);
        p.setPen(QPen(c, 2.2, Qt::SolidLine, Qt::RoundCap));
        if (projectSeg(QVector3D(), dirs[i] * float(L), l)) {
            p.drawLine(l);
            p.drawText(l.p2() + QPointF(5, 4), names[i]);
        }
    }
    QPointF o;
    if (projectD(QVector3D(), o)) {
        p.setPen(Qt::NoPen);
        p.setBrush(accent());
        p.drawEllipse(o, 3.0, 3.0);
        p.setBrush(Qt::NoBrush);
    }
}

// ---------------------------------------------------------------------------
// entities
// ---------------------------------------------------------------------------
void Canvas::drawStrip(QPainter &p, const QVector<QVector3D> &s) const
{
    // one continuous path so dash patterns flow around corners and along curves
    QPainterPath path;
    QLineF l;
    QPointF last;
    bool open = false;
    for (int i = 0; i + 1 < s.size(); ++i) {
        if (!projectSeg(s[i], s[i + 1], l)) {
            open = false;
            continue;
        }
        if (!open || QLineF(last, l.p1()).length() > 0.5)
            path.moveTo(l.p1());
        path.lineTo(l.p2());
        last = l.p2();
        open = true;
    }
    p.drawPath(path);
}

qreal Canvas::annotPx(qreal screenPx) const
{
    return m_plot ? screenPx * 0.25 * m_plot->pxPerMm : screenPx;
}

qreal Canvas::itemPenPx(const SketchItem *it, qreal sc) const
{
    if (m_plot)                                     // on paper a line weight is paper mm (as DXF lineweights)
        return std::max(0.05, it->width) * m_plot->pxPerMm;
    return std::clamp(it->width * sc * m_set.lineScale, 1.0, 400.0);   // true mm thickness
}

void Canvas::applyLineStyle(QPen &pen, const SketchItem *it, qreal lw, qreal sc) const
{
    if (it->lineStyle <= 0)
        return;
    // patterns are defined in mm (so they scale with the drawing, on paper: paper mm) and expressed in pen widths
    static const QVector<qreal> mm[] = {{}, {6, 3}, {0.01, 3}, {8, 2.5, 0.01, 2.5}, {14, 3, 3, 3}, {4, 2.5}};
    QVector<qreal> pat = mm[std::clamp(it->lineStyle, 0, 5)];
    qreal minDash = 1e9;
    for (int i = 0; i < pat.size(); i += 2)
        if (pat[i] > 0.05) minDash = std::min(minDash, pat[i]);
    const qreal k = m_plot ? m_plot->pxPerMm : std::max(sc, 5.0 / std::max(minDash, 0.01));   // readable when zoomed out
    for (qreal &v : pat)
        v = std::max(0.01, v * k / lw);
    if (it->lineStyle == 2)                                         // dots: round cap makes them circular
        for (int i = 0; i < pat.size(); i += 2) pat[i] = 0.01;
    pen.setCapStyle(it->lineStyle == 2 ? Qt::RoundCap : Qt::FlatCap);
    pen.setDashPattern(pat);
}

void Canvas::drawItem(QPainter &p, const SketchItem *it, bool preview) const
{
    const bool sel = it->selected && !preview && !m_plot;
    const bool hov = (it == m_hover) && !preview && !sel;
    const QColor base = preview ? accent() : (m_plot && m_plot->mono ? QColor(Qt::black) : adapt(it->color));
    const qreal sc = scaleAt(it->pts.value(0));
    const qreal lw = itemPenPx(it, sc);

    if (it->type() == SketchItem::Text) {
        QPointF a;
        if (!projectD(it->pts.value(0), a))
            return;
        p.setFont(textFont(it));
        const QRectF r = textRect(it);
        p.save();
        p.setTransform(textXf(it), true);
        if (sel || hov) {
            p.setPen(QPen(sel ? accent() : QColor(255, 209, 102), 1, Qt::DashLine));
            p.drawRect(r.adjusted(-3, -2, 3, 2));
        }
        p.setPen(base);
        p.drawText(r, Qt::AlignLeft | Qt::AlignTop, it->text);
        p.restore();
        return;
    }

    if (it->type() == SketchItem::ImageItem) {
        const QVector<QVector3D> c = imageCorners(it);
        QPolygonF dst;
        bool ok = c.size() == 4 && !it->image.isNull();
        for (const QVector3D &q : c) {
            QPointF s;
            ok = ok && projectD(q, s);
            dst << s;
        }
        if (ok) {
            const qreal w = it->image.width(), h = it->image.height();
            QTransform tr;
            if (QTransform::quadToQuad(QPolygonF({QPointF(0, 0), QPointF(w, 0), QPointF(w, h), QPointF(0, h)}),
                                       dst, tr)) {
                p.save();
                p.setTransform(tr, true);
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                p.drawImage(QPointF(0, 0), it->image);
                p.restore();
            }
            if (sel || hov) {
                p.setPen(QPen(sel ? accent() : QColor(255, 209, 102), 1.2, Qt::DashLine));
                p.drawPolygon(dst);
            }
        }
        return;
    }

    if (it->type() == SketchItem::Dimension) {
        QPen pen(base, std::max(1.0, lw * 0.7));
        drawDimension(p, it, pen);
        if (sel || hov) {
            p.setPen(QPen(sel ? accent() : QColor(255, 209, 102), 1.2, Qt::DashLine));
            for (const auto &s : strips(it))
                drawStrip(p, s);
        }
        return;
    }

    if (it->type() == SketchItem::Hatch) {
        const auto loops = hatchLoops(it);
        drawHatch(p, it, loops);
        if (sel || hov) {
            p.setPen(QPen(sel ? accent() : QColor(255, 209, 102), 1.6, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            for (const auto &l : loops)
                drawStrip(p, l);
        }
        return;
    }

    const auto ss = strips(it);
    if (!it->extrude.isNull() && !ss.isEmpty()) {
        drawSolid(p, it, ss.first());
    } else if (!preview && hasHatch(it) && !ss.isEmpty()) {
        drawHatch(p, it, {ss.first()});
    } else if (!preview && it->fill.isValid() && it->fill.alpha() > 0 && closedShape(it) && !ss.isEmpty()) {
        QPolygonF poly;
        bool ok = true;
        for (const QVector3D &q : ss.first()) {
            QPointF s;
            if (!projectD(q, s)) { ok = false; break; }
            poly << s;
        }
        if (ok && poly.size() >= 3) {
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(it->fill);
            p.drawPolygon(poly);
            p.restore();
        }
    }
    if (sel || hov) {
        QColor glow = sel ? accent() : QColor(255, 209, 102);
        glow.setAlpha(sel ? 70 : 90);
        p.setPen(QPen(glow, lw + 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        for (const auto &s : ss)
            drawStrip(p, s);
    }
    QPen pen(base, lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (preview)
        pen.setStyle(Qt::DashLine);
    else
        applyLineStyle(pen, it, lw, sc);
    p.setPen(pen);
    for (const auto &s : ss)
        drawStrip(p, s);
}


void Canvas::drawDimension(QPainter &p, const SketchItem *it, const QPen &pen) const
{
    const DimGeom g = dimGeom(it);
    if (!g.ok || it->pts.size() < 3)
        return;
    QPointF a, b, s1, s2;
    if (!projectD(it->pts[0], a) || !projectD(it->pts[1], b) || !projectD(g.d1, s1) || !projectD(g.d2, s2))
        return;
    p.setPen(pen);
    // extension lines: small gap at the object, short overshoot past the dimension line
    // (sizes in window pixels; on paper annotPx makes them paper millimetres)
    auto ext = [&](const QPointF &from, const QPointF &to) {
        const QPointF d = to - from;
        const qreal l = std::hypot(d.x(), d.y());
        if (l < 1e-3) return;
        const QPointF u = d / l;
        p.drawLine(from + u * std::min(annotPx(3.0), l * 0.3), to + u * annotPx(5.0));
    };
    ext(a, s1);
    ext(b, s2);
    QPointF dir = s2 - s1;
    const qreal len = std::hypot(dir.x(), dir.y());
    p.drawLine(s1, s2);
    if (len < 1e-3)
        return;
    dir /= len;
    const QPointF n(-dir.y(), dir.x());
    const qreal as = std::clamp(len / 3.0, annotPx(3.0), annotPx(11.0));
    auto arrow = [&](QPointF tip, QPointF d) {
        const QPolygonF tri{tip, tip - d * as + n * as * 0.28, tip - d * as - n * as * 0.28};
        p.setBrush(pen.color());
        p.setPen(Qt::NoPen);
        p.drawPolygon(tri);
        p.setBrush(Qt::NoBrush);
    };
    arrow(s1, -dir);
    arrow(s2, dir);

    const QString txt = Units::formatDim(g.value);
    QFont f = QApplication::font();
    f.setPixelSize(std::max(1, qRound(annotPx(12))));
    f.setBold(true);
    p.setFont(f);
    const QFontMetricsF fm(f);
    p.save();
    p.translate((s1 + s2) / 2.0);
    qreal ang = std::atan2(dir.y(), dir.x()) * 180.0 / PI;
    if (ang > 90 || ang < -90)
        ang += 180;
    p.rotate(ang);
    const qreal pad = annotPx(4), lift = annotPx(1);
    const QRectF tr(-fm.horizontalAdvance(txt) / 2 - pad, -fm.height() - lift, fm.horizontalAdvance(txt) + 2 * pad, fm.height() + lift);
    p.fillRect(tr, m_plot ? QColor(Qt::white)
                          : QColor(theme().bgBottom.red(), theme().bgBottom.green(), theme().bgBottom.blue(), 215));
    p.setPen(pen.color());
    p.drawText(tr, Qt::AlignCenter, txt);
    p.restore();
}

// ---------------------------------------------------------------------------
// interaction overlays
// ---------------------------------------------------------------------------
static void label(QPainter &p, const QPointF &at, const QString &text, const QColor &fg,
                  const QColor &bg)
{
    QFont f = p.font();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);
    const QFontMetricsF fm(f);
    const QRectF r(at.x(), at.y(), fm.horizontalAdvance(text) + 14, fm.height() + 6);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, 5, 5);
    p.setBrush(Qt::NoBrush);
    p.setPen(fg);
    p.drawText(r, Qt::AlignCenter, text);
}

void Canvas::drawPending(QPainter &p) const
{
    if (m_pending.isEmpty() || !m_cur.valid)
        return;
    const QVector3D cur = previewPoint();
    if (m_tool == Tool::Move) {
        const QVector3D a = m_pending[0], d = cur - a;
        for (const SketchItem *it : m_selected) {
            std::unique_ptr<SketchItem> g(xformClone(it, [&](const QVector3D &q) { return q + d; }));
            drawItem(p, g.get(), true);
        }
        QPointF sa, sc;
        if (projectD(a, sa) && projectD(cur, sc)) {
            p.setPen(QPen(QColor(255, 209, 102), 1.4, Qt::DashLine));
            p.drawLine(sa, sc);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 209, 102));
            p.drawEllipse(sa, 4.0, 4.0);
            p.setBrush(Qt::NoBrush);
            label(p, sc + QPointF(14, 14), QString::fromUtf8("\xCE\x94 %1, %2, %3   |   %4")
                      .arg(Units::formatDim(d.x()), Units::formatDim(d.y()), Units::formatDim(d.z()), Units::format(d.length())),
                  QColor(20, 20, 20), QColor(255, 209, 102));
        }
        if (m_set.dynInput && dynActive())
            drawDynFields(p, a, cur);
        return;
    }
    if (m_tool == Tool::Mirror) {
        const QVector3D a = m_pending[0];
        QLineF l;
        QVector3D dd = cur - a;
        if (dd.length() > 1e-6f && projectSeg(a - dd * 400.0f, a + dd * 400.0f, l)) {
            p.setPen(QPen(QColor(255, 209, 102), 1.3, Qt::DashDotLine));
            p.drawLine(l);
        }
        const Frame fr = planeFrame();
        QVector3D d = cur - a;
        d -= fr.N * QVector3D::dotProduct(d, fr.N);
        if (d.length() > 1e-6f) {
            d.normalize();
            const QVector3D m = QVector3D::crossProduct(fr.N, d).normalized();
            for (const SketchItem *it : m_selected) {
                std::unique_ptr<SketchItem> g(xformClone(it, [&](const QVector3D &q) {
                    return q - m * float(2.0 * QVector3D::dotProduct(q - a, m));
                }));
                drawItem(p, g.get(), true);
            }
        }
        QPointF sp;
        if (projectD(a, sp)) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 209, 102));
            p.drawEllipse(sp, 4.0, 4.0);
            p.setBrush(Qt::NoBrush);
        }
        return;
    }
    const int n = m_pending.size();
    SketchItem tmp(SketchItem::Line);
    tmp.plane = int(m_plane);
    switch (m_tool) {
    case Tool::Line:
        tmp.pts = {m_pending[0], cur};
        break;
    case Tool::Curve3:
    case Tool::Arc:
        if (n == 1) tmp.pts = {m_pending[0], cur};
        else { tmp = SketchItem(m_tool == Tool::Arc ? SketchItem::Arc : SketchItem::Curve);
               tmp.plane = int(m_plane); tmp.pts = {m_pending[0], m_pending[1], cur}; }
        break;
    case Tool::Circle:
        tmp = SketchItem(SketchItem::Circle); tmp.plane = int(m_plane); tmp.pts = {m_pending[0], cur};
        break;
    case Tool::Rect:
        tmp = SketchItem(SketchItem::Rect); tmp.plane = int(m_plane); tmp.pts = {m_pending[0], cur};
        break;
    case Tool::Polyline:
        tmp = SketchItem(SketchItem::Polyline); tmp.plane = int(m_plane);
        tmp.pts = m_pending; tmp.pts.append(cur);
        break;
    case Tool::Box: {
        tmp = SketchItem(SketchItem::Rect);
        tmp.plane = int(m_plane);
        if (n == 1) {
            tmp.pts = {m_pending[0], cur};
        } else {
            const Frame fr = planeFrame();
            const QVector3D d = m_pending[1] - m_pending[0];
            tmp.pts = {m_pending[0], m_pending[0] + fr.U * QVector3D::dotProduct(d, fr.U) + fr.V * QVector3D::dotProduct(d, fr.V)};
            double h = m_boxCurH, v = 0;
            if (dynTyping() && dynValue(0, v))
                h = v * m_boxOutSign;
            if (std::fabs(h) > 1e-6)
                tmp.extrude = fr.N * float(h);
            tmp.fill = QColor(53, 224, 200, 140);           // see-through while it is a preview
        }
        break;
    }
    case Tool::Dimension:
        if (n == 1) tmp.pts = {m_pending[0], cur};
        else { tmp = SketchItem(SketchItem::Dimension); tmp.plane = int(m_plane);
               tmp.dimMode = m_dimMode; tmp.pts = {m_pending[0], m_pending[1], cur}; }
        break;
    default:
        return;
    }
    drawItem(p, &tmp, true);

    p.setPen(Qt::NoPen);
    p.setBrush(accent());
    for (const QVector3D &q : m_pending) {
        QPointF s;
        if (projectD(q, s))
            p.drawEllipse(s, 3.5, 3.5);
    }
    p.setBrush(Qt::NoBrush);

    if (m_set.dynInput && dynActive())
        drawDynFields(p, m_pending.last(), cur);
}

// AutoCAD-style dynamic input: editable size fields drawn in the viewport next to the geometry.
void Canvas::drawDynFields(QPainter &p, const QVector3D &anchor, const QVector3D &cur) const
{
    const Frame fr = planeFrame();
    QVector3D d = cur - anchor;
    d -= fr.N * QVector3D::dotProduct(d, fr.N);
    const double du = QVector3D::dotProduct(d, fr.U), dv = QVector3D::dotProduct(d, fr.V);
    QPointF sa, sc;
    if (!projectD(anchor, sa) || !projectD(cur, sc))
        return;
    QFont f = QApplication::font();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);
    const QFontMetricsF fm(f);

    auto field = [&](const QPointF &c, const QString &prefix, double value, int idx, int dec) {
        const bool active = (idx == m_dynIdx) && dynTyping();
        const bool typed = !m_dyn[idx].text.isEmpty();
        QString txt = typed ? dynShown(idx, active) : (dec < 0 ? Units::formatDim(value) : QString::number(value, 'f', dec));
        const QString full = prefix + " " + txt;
        const QRectF r(c.x() - (fm.horizontalAdvance(full) + 14) / 2, c.y() - 11, fm.horizontalAdvance(full) + 14, 22);
        p.setPen(QPen(active ? accent() : (typed ? QColor(255, 209, 102) : QColor(70, 90, 120)), active ? 2 : 1));
        p.setBrush(typed ? QColor(66, 52, 18, 235) : QColor(12, 18, 28, 225));
        p.drawRoundedRect(r, 5, 5);
        p.setPen(typed ? QColor(255, 224, 150) : QColor(215, 230, 250));
        p.drawText(r, Qt::AlignCenter, full);
        p.setBrush(Qt::NoBrush);
    };
    auto perp = [](QPointF a, QPointF b, qreal dist) {
        QPointF dir = b - a;
        const qreal l = std::hypot(dir.x(), dir.y());
        if (l < 1e-3) return QPointF(0, -dist);
        dir /= l;
        QPointF n(-dir.y(), dir.x());
        if (n.y() > 0) n = -n;                      // keep boxes above the geometry
        return n * dist;
    };

    switch (m_tool) {
    case Tool::Box:
        if (m_pending.size() >= 2) {                       // height, + = out of the face it started on
            double h = m_boxCurH * m_boxOutSign;
            field(QPointF(m_lastMouse) + QPointF(46, -26), "H", h, 0, -1);
            if (!m_boxNote.isEmpty() && !dynTyping()) {
                p.setPen(QColor(255, 209, 102));
                p.drawText(QPointF(m_lastMouse) + QPointF(22, 4), m_boxNote);
            }
            break;
        }
        [[fallthrough]];
    case Tool::Rect: {
        QPointF c1, c2;
        const QVector3D k1 = anchor + fr.U * float(du), k2 = anchor + fr.U * float(du) + fr.V * float(dv);
        if (projectD(k1, c1) && projectD(k2, c2)) {
            QPointF mid = (sa + c1) / 2.0;
            field(mid + perp(sa, c1, 20) * (dv < 0 ? -1 : 1), "W", std::fabs(du), 0, -1);
            mid = (c1 + c2) / 2.0;
            field(mid + QPointF(34, 0), "H", std::fabs(dv), 1, -1);
        }
        break;
    }
    case Tool::Circle:
        field((sa + sc) / 2.0 + perp(sa, sc, 18), "R", d.length(), 0, -1);
        break;
    default: {
        const double ang = std::atan2(dv, du) * 180.0 / PI;
        field((sa + sc) / 2.0 + perp(sa, sc, 20), "L", d.length(), 0, -1);
        if (!m_axisLock && d.length() > 1e-6f) {
            // angle arc from the plane's U axis
            const qreal rr = 34.0 / std::max(1e-6, scaleAt(anchor));
            QPolygonF arc;
            const int steps = 24;
            for (int i = 0; i <= steps; ++i) {
                const qreal t = ang * PI / 180.0 * i / steps;
                QPointF sp;
                if (projectD(anchor + (fr.U * float(std::cos(t)) + fr.V * float(std::sin(t))) * float(rr), sp))
                    arc << sp;
            }
            p.setPen(QPen(QColor(255, 209, 102, 200), 1.2));
            p.drawPolyline(arc);
            QPointF ref;
            if (projectD(anchor + fr.U * float(rr * 1.4), ref)) {
                p.setPen(QPen(QColor(255, 209, 102, 110), 1, Qt::DashLine));
                p.drawLine(sa, ref);
            }
            field(sc + QPointF(18, 22), QString::fromUtf8("\xE2\x88\xA0"), norm360(ang), 1, 1);
        }
        break;
    }
    }
}

void Canvas::drawGhost(QPainter &p) const
{
    const QColor col = m_ghostExtend ? QColor(88, 216, 133) : QColor(255, 107, 107);
    if (m_tool == Tool::Cutter && m_bladeAction == 1 && m_pending.size() >= 2)
        ;                                         // red = what will be removed
    for (const auto &g : m_ghosts) {
        if (g.size() < 2)
            continue;
        p.setPen(QPen(col, 3.4, m_ghostExtend ? Qt::SolidLine : Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
        drawStrip(p, g);
    }
    for (const QVector3D &m : m_marks) {
        QPointF s;
        if (!projectD(m, s))
            continue;
        p.setPen(QPen(QColor(255, 209, 102), 1.6));
        p.setBrush(QColor(20, 24, 32, 230));
        p.drawPolygon(QPolygonF({s + QPointF(0, -5), s + QPointF(5, 0), s + QPointF(0, 5), s + QPointF(-5, 0)}));
        p.setBrush(Qt::NoBrush);
    }
    if (m_tool == Tool::Offset && offsetSourceValid() && !m_lastMouse.isNull()) {
        QString t;
        if (!m_offsetTyped.isEmpty())
            t = QString("Offset: %1_  (Enter)").arg(m_offsetTyped);
        else if (!m_ghosts.isEmpty())
            t = QString("%1 %2").arg(m_offsetThrough ? "Through" : "Offset", Units::format(m_offsetShown));
        if (!t.isEmpty())
            label(p, QPointF(m_lastMouse) + QPointF(16, -26), t, QColor(20, 20, 20), QColor(88, 216, 133));
    }
}

void Canvas::drawBlade(QPainter &p) const
{
    if (m_tool != Tool::Cutter || m_pending.isEmpty() || !m_cur.valid)
        return;
    const QVector3D A = m_pending[0];
    const QVector3D B = m_pending.size() >= 2 ? m_pending[1] : m_cur.pt;
    if ((B - A).length() < 1e-6f)
        return;
    const QColor amber(255, 209, 102);
    QLineF l;
    const bool infinite = m_bladeAction == 1;
    QVector3D d = (B - A).normalized();
    if (m_cutMode == 1) {                         // True 3D: show the knife wall
        const Frame fr = planeFrame();
        QVector3D dd = B - A;
        dd -= fr.N * QVector3D::dotProduct(dd, fr.N);
        const float ext = float(std::max(60.0, m_gridSize * 12));
        const QVector3D a0 = infinite ? A - dd.normalized() * 4000.0f : A;
        const QVector3D b0 = infinite ? A + dd.normalized() * 4000.0f : A + dd;
        QPointF q[4];
        if (projectD(a0 - fr.N * ext, q[0]) && projectD(b0 - fr.N * ext, q[1]) &&
            projectD(b0 + fr.N * ext, q[2]) && projectD(a0 + fr.N * ext, q[3])) {
            p.setPen(QPen(QColor(255, 209, 102, 90), 1));
            p.setBrush(QColor(255, 209, 102, 28));
            p.drawPolygon(QPolygonF({q[0], q[1], q[2], q[3]}));
            p.setBrush(Qt::NoBrush);
        }
    }
    const QVector3D a1 = infinite ? A - d * 4000.0f : A, b1 = infinite ? A + d * 4000.0f : B;
    if (projectSeg(a1, b1, l)) {
        p.setPen(QPen(amber, infinite ? 1.6 : 2.6, infinite ? Qt::DashDotLine : Qt::SolidLine, Qt::RoundCap));
        p.drawLine(l);
    }
    QPointF sa, sb;
    p.setPen(Qt::NoPen);
    p.setBrush(amber);
    if (projectD(A, sa)) p.drawEllipse(sa, 4.5, 4.5);
    if (projectD(B, sb)) p.drawEllipse(sb, 4.5, 4.5);
    p.setBrush(Qt::NoBrush);
    if (projectD(B, sb)) {
        const QString t = (m_pending.size() >= 2 && infinite) ? "click the side to REMOVE"
                          : QString("blade %1").arg(Units::format((B - A).length()));
        QFont f = QApplication::font();
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        const QFontMetricsF fm(f);
        const QRectF r(sb.x() + 14, sb.y() + 12, fm.horizontalAdvance(t) + 14, 22);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 48, 12, 235));
        p.drawRoundedRect(r, 5, 5);
        p.setBrush(Qt::NoBrush);
        p.setPen(QColor(255, 224, 150));
        p.drawText(r, Qt::AlignCenter, t);
    }
}

void Canvas::drawFence(QPainter &p) const
{
    if (!m_fenceDrag)
        return;
    const bool ext = (m_tool == Tool::Extend) != bool(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
    const QColor col = ext ? QColor(88, 216, 133) : QColor(255, 107, 107);
    p.setPen(QPen(col, 1.8, Qt::DashLine, Qt::RoundCap));
    p.drawLine(m_fenceA, m_fenceB);
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawEllipse(QPointF(m_fenceA), 3.5, 3.5);
    p.drawEllipse(QPointF(m_fenceB), 3.5, 3.5);
    p.setBrush(Qt::NoBrush);
}

void Canvas::drawGuides(QPainter &p) const
{
    QLineF l;
    for (const SmartGuide &g : std::as_const(m_smart)) {
        QLineF sl;
        if (projectSeg(g.a, g.b, sl)) {
            p.setPen(QPen(QColor(255, 72, 208), 1.3, Qt::DashLine));
            p.drawLine(sl);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 72, 208));
            for (const QPointF &q : {sl.p1(), sl.p2()})
                p.drawEllipse(q, 3.0, 3.0);
            p.setBrush(Qt::NoBrush);
            label(p, (sl.p1() + sl.p2()) / 2.0 + QPointF(8, -8), g.label, QColor(255, 255, 255), QColor(190, 40, 150));
        }
    }
    if (m_magnetOn && m_dragging) {
        QPointF s;
        if (projectD(m_magnet, s)) {
            p.setPen(QPen(QColor(255, 209, 102), 2));
            p.drawRect(QRectF(s.x() - 6, s.y() - 6, 12, 12));
            label(p, s + QPointF(12, -26), "Snap to vertex", QColor(20, 20, 20), QColor(255, 209, 102));
        }
    }
    if (m_trackOn && projectSeg(m_trackLine.first, m_trackLine.second, l)) {
        p.setPen(QPen(QColor(110, 214, 255, 215), 1.2, Qt::DashDotLine));
        p.drawLine(l);
    }
    if (m_guides.isEmpty())
        return;
    for (const auto &g : m_guides) {
        const bool lockLine = m_axisLock != 0;
        QColor c = lockLine ? axisColor(g.second - g.first) : QColor(255, 209, 102);
        c.setAlpha(lockLine ? 230 : 190);
        p.setPen(QPen(c, 1.2, Qt::DashLine));
        if (projectSeg(g.first, g.second, l))
            p.drawLine(l);
    }
}

void Canvas::drawSnapMarker(QPainter &p) const
{
    if (!m_cur.valid || m_overGizmo || m_panning || m_orbiting)
        return;
    const bool active = drawingTool() || m_dragging || m_tool == Tool::Text || m_tool == Tool::InsertImage;
    if (!active)
        return;
    QPointF s;
    if (!projectD(m_cur.pt, s))
        return;
    const QColor amber(255, 209, 102);
    switch (m_cur.kind) {
    case 2:
        p.setPen(QPen(amber, 2));
        p.drawRect(QRectF(s.x() - 6, s.y() - 6, 12, 12));
        label(p, s + QPointF(12, -26), QString("Endpoint") + (m_cur.note.isEmpty() ? QString() : " (" + m_cur.note + ")"), QColor(20, 20, 20), amber);
        break;
    case 3: {
        p.setPen(QPen(amber, 2));
        p.drawPolygon(QPolygonF({s + QPointF(0, -7), s + QPointF(7, 6), s + QPointF(-7, 6)}));
        label(p, s + QPointF(12, -26), QString("Midpoint") + (m_cur.note.isEmpty() ? QString() : " (" + m_cur.note + ")"), QColor(20, 20, 20), amber);
        break;
    }
    case 4:
        p.setPen(QPen(amber, 2));
        p.drawEllipse(s, 7, 7);
        label(p, s + QPointF(12, -26), QString("Centre") + (m_cur.note.isEmpty() ? QString() : " (" + m_cur.note + ")"), QColor(20, 20, 20), amber);
        break;
    case 6:
        p.setPen(QPen(amber, 2));
        p.drawLine(s + QPointF(-6, -6), s + QPointF(6, 6));
        p.drawLine(s + QPointF(-6, 6), s + QPointF(6, -6));
        label(p, s + QPointF(12, -26), QString("Intersection") + (m_cur.note.isEmpty() ? QString() : " (" + m_cur.note + ")"), QColor(20, 20, 20), amber);
        break;
    case 7:
        p.setPen(QPen(amber, 2));
        p.drawPolyline(QPolygonF({s + QPointF(-6, -6), s + QPointF(-6, 6), s + QPointF(6, 6)}));
        label(p, s + QPointF(12, -26), QString("Perpendicular") + (m_cur.note.isEmpty() ? QString() : " (" + m_cur.note + ")"), QColor(20, 20, 20), amber);
        break;
    case 10:                                           // face centre: a square with its centre
        p.setPen(QPen(amber, 2));
        p.drawRect(QRectF(s.x() - 7, s.y() - 7, 14, 14));
        p.setBrush(amber);
        p.drawEllipse(s, 2.2, 2.2);
        p.setBrush(Qt::NoBrush);
        label(p, s + QPointF(12, -26), QString("Face centre") + (m_cur.note.isEmpty() ? QString() : " (" + m_cur.note + ")"), QColor(20, 20, 20), amber);
        break;
    case 11:                                           // body centre: a circle with a cross
        p.setPen(QPen(amber, 2));
        p.drawEllipse(s, 8, 8);
        p.drawLine(s + QPointF(-11, 0), s + QPointF(11, 0));
        p.drawLine(s + QPointF(0, -11), s + QPointF(0, 11));
        label(p, s + QPointF(12, -26), QString("Body centre") + (m_cur.note.isEmpty() ? QString() : " (" + m_cur.note + ")"), QColor(20, 20, 20), amber);
        break;
    case 12:                                           // a point on a face
        p.setPen(QPen(amber, 1.8));
        p.drawPolygon(QPolygonF({s + QPointF(0, -6), s + QPointF(6, 0), s + QPointF(0, 6), s + QPointF(-6, 0)}));
        label(p, s + QPointF(12, -26), QString("On face"), QColor(20, 20, 20), amber);
        break;
    case 13: {                                         // Move up to a face
        const QColor green(80, 220, 120);
        if (m_moveToValid)
            drawFaceHighlight(p, m_moveTo.v, green, m_moveTo.loops);
        p.setPen(QPen(green, 2));
        p.drawEllipse(s, 5, 5);
        label(p, s + QPointF(12, -26), QString("Up to face") + (m_cur.note.isEmpty() ? QString() : " (" + m_cur.note + ")"), QColor(10, 30, 15), green);
        break;
    }
    case 9:
        p.setPen(QPen(amber, 2));
        p.drawLine(s + QPointF(-6, 6), s + QPointF(6, -6));
        p.drawEllipse(s, 3, 3);
        label(p, s + QPointF(12, -26), QString("On line") + (m_cur.note.isEmpty() ? QString() : " (" + m_cur.note + ")"), QColor(20, 20, 20), amber);
        break;
    default:
        p.setPen(QPen(accent(), 1.4));
        p.drawLine(s + QPointF(-8, 0), s + QPointF(8, 0));
        p.drawLine(s + QPointF(0, -8), s + QPointF(0, 8));
        if (!m_cur.note.isEmpty())
            label(p, s + QPointF(14, -30), m_cur.note, QColor(10, 30, 40), QColor(110, 214, 255));
        break;
    }
}

void Canvas::drawHandles(QPainter &p) const
{
    QList<const SketchItem *> targets;
    if (m_editItem)
        targets.append(m_editItem);
    else
        for (const SketchItem *it : m_selected)
            targets.append(it);
    if (targets.isEmpty())
        return;
    p.setPen(QPen(accent(), 1.4));
    p.setBrush(theme().bgBottom);
    const qreal h = (m_tool == Tool::Edit) ? 5.0 : 3.5;
    for (const SketchItem *it : targets)
        for (const QVector3D &pt : it->pts) {
            QPointF s;
            if (projectD(pt, s))
                p.drawRect(QRectF(s.x() - h, s.y() - h, 2 * h, 2 * h));
        }
    p.setBrush(Qt::NoBrush);

    if (m_dragging && m_dragMoved && m_tool == Tool::DynMove && !m_dragData.isEmpty()) {
        const QVector3D orig = m_dragData.constBegin().value().value(0);
        QPointF s0, s1;
        if (projectD(orig, s0) && projectD(orig + m_dynDelta, s1)) {
            p.setPen(QPen(QColor(255, 209, 102), 1.4, Qt::DashLine));
            p.drawLine(s0, s1);
            label(p, s1 + QPointF(12, -28), Units::format(m_dynDelta.length()),
                  QColor(20, 20, 20), QColor(255, 209, 102));
        }
    }
}

// ---------------------------------------------------------------------------
// view cube (top-right): 6 faces, 12 edges and 8 corners are clickable,
// each one looks from that direction; the house button returns to the home ISO view
// ---------------------------------------------------------------------------
namespace {
const qreal kCubeHalf = 27.0;     // half edge length in pixels
const qreal kCubeZone = 0.26;     // edge / corner band as a fraction of the face width

struct CubeFace {
    QVector3D n, right, up;       // outward normal, label right, label up (right x up = n)
    const char *label;
};
const CubeFace kCubeFaces[6] = {
    {QVector3D(0, 0, 1),  QVector3D(1, 0, 0),  QVector3D(0, 1, 0),  "TOP"},
    {QVector3D(0, 0, -1), QVector3D(1, 0, 0),  QVector3D(0, -1, 0), "BOTTOM"},
    {QVector3D(0, -1, 0), QVector3D(1, 0, 0),  QVector3D(0, 0, 1),  "FRONT"},
    {QVector3D(0, 1, 0),  QVector3D(-1, 0, 0), QVector3D(0, 0, 1),  "BACK"},
    {QVector3D(1, 0, 0),  QVector3D(0, 1, 0),  QVector3D(0, 0, 1),  "RIGHT"},
    {QVector3D(-1, 0, 0), QVector3D(0, -1, 0), QVector3D(0, 0, 1),  "LEFT"},
};

// direction (components -1/0/1) <-> hotspot index 0..25
int dirIndex(int x, int y, int z)
{
    const int k = (x + 1) * 9 + (y + 1) * 3 + (z + 1);   // 0..26, 13 = centre
    return k < 13 ? k : k - 1;
}

struct CubeCell {
    QPolygonF poly;
    int hit;
};
} // namespace

QVector3D Canvas::gizmoDirection(int hit)
{
    if (hit < 0 || hit >= 26)
        return QVector3D();
    const int k = hit < 13 ? hit : hit + 1;
    return QVector3D(k / 9 - 1, (k / 3) % 3 - 1, k % 3 - 1);
}

QRectF Canvas::gizmoRect() const
{
    return QRectF(width() - 140, 12, 128, 146);
}

static QPointF cubeCentre(const QRectF &r)
{
    return QPointF(r.center().x(), r.top() + 62);
}

static QRectF homeRect(const QRectF &r)
{
    return QRectF(r.right() - 26, r.bottom() - 24, 20, 20);
}

static QRectF rollRect(const QRectF &r, bool left)
{
    return left ? QRectF(r.left() + 2, r.top() + 2, 20, 20) : QRectF(r.right() - 22, r.top() + 2, 20, 20);
}

// visible cube faces split into a 3x3 grid of clickable cells, back-to-front
static QVector<CubeCell> cubeCells(const QRectF &r, const QVector3D &br, const QVector3D &bu, const QVector3D &bf)
{
    const QPointF c = cubeCentre(r);
    auto proj = [&](const QVector3D &v) {
        return c + QPointF(QVector3D::dotProduct(v, br), -QVector3D::dotProduct(v, bu)) * kCubeHalf;
    };
    const qreal edges[4] = {-1.0, -1.0 + 2 * kCubeZone, 1.0 - 2 * kCubeZone, 1.0};
    QVector<CubeCell> out;
    for (const CubeFace &f : kCubeFaces) {
        if (QVector3D::dotProduct(f.n, -bf) < 0.03)
            continue;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                QPolygonF q;
                const qreal u0 = edges[i], u1 = edges[i + 1], v0 = edges[j], v1 = edges[j + 1];
                q << proj(f.n + f.right * u0 + f.up * v0) << proj(f.n + f.right * u1 + f.up * v0)
                  << proj(f.n + f.right * u1 + f.up * v1) << proj(f.n + f.right * u0 + f.up * v1);
                const QVector3D d = f.n + f.right * float(i - 1) + f.up * float(j - 1);
                out.push_back({q, dirIndex(int(d.x()), int(d.y()), int(d.z()))});
            }
    }
    return out;
}

int Canvas::gizmoHit(const QPoint &pos) const
{
    const QRectF r = gizmoRect();
    if (!r.contains(QPointF(pos)))
        return -1;
    if (homeRect(r).adjusted(-3, -3, 3, 3).contains(QPointF(pos)))
        return kGizmoHome;
    if (rollRect(r, true).adjusted(-2, -2, 2, 2).contains(QPointF(pos)))
        return kGizmoRollL;
    if (rollRect(r, false).adjusted(-2, -2, 2, 2).contains(QPointF(pos)))
        return kGizmoRollR;
    const QQuaternion inv = m_frame.conjugated();     // the cube lives in view-orientation space
    for (const CubeCell &cell : cubeCells(r, inv.rotatedVector(m_br), inv.rotatedVector(m_bu), inv.rotatedVector(m_bf)))
        if (cell.poly.containsPoint(QPointF(pos), Qt::OddEvenFill))
            return cell.hit;
    return -1;
}

void Canvas::drawGizmo(QPainter &p)
{
    const QRectF r = gizmoRect();
    const Theme &th = theme();
    const QPointF c = cubeCentre(r);
    const int hover = m_overGizmo ? m_gizmoHover : -1;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(th.hudBg.red(), th.hudBg.green(), th.hudBg.blue(), 110));
    p.drawEllipse(c, 58.0, 58.0);

    // camera basis in view-cube space (differs from world after "Set current view as ...")
    const QQuaternion inv = m_frame.conjugated();
    const QVector3D cbr = inv.rotatedVector(m_br), cbu = inv.rotatedVector(m_bu), cbf = inv.rotatedVector(m_bf);
    auto proj = [&](const QVector3D &v) {
        return c + QPointF(QVector3D::dotProduct(v, cbr), -QVector3D::dotProduct(v, cbu)) * kCubeHalf;
    };
    const QColor accent(80, 170, 255);

    // faces with shading, cells, labels
    for (const CubeFace &f : kCubeFaces) {
        const qreal facing = QVector3D::dotProduct(f.n, -cbf);
        if (facing < 0.03)
            continue;
        QPolygonF face;
        face << proj(f.n - f.right - f.up) << proj(f.n + f.right - f.up)
             << proj(f.n + f.right + f.up) << proj(f.n - f.right + f.up);
        const int shade = int(150 + 80 * facing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(shade - 20, shade - 8, shade, 235));
        p.drawPolygon(face);

        // label mapped onto the face (orthographic projection of a plane is affine)
        if (facing > 0.25) {
            const QPointF o = proj(f.n), rx = proj(f.n + f.right) - o, dy = proj(f.n - f.up) - o;
            p.save();
            p.setTransform(QTransform(rx.x() / 50, rx.y() / 50, dy.x() / 50, dy.y() / 50, o.x(), o.y()), true);
            QFont fl = QApplication::font();
            fl.setPixelSize(17);
            fl.setBold(true);
            p.setFont(fl);
            p.setPen(QColor(30, 40, 55, int(90 + 165 * facing)));
            p.drawText(QRectF(-48, -14, 96, 28), Qt::AlignCenter, f.label);
            p.restore();
        }
    }
    // hovered hotspot (a corner lights up on all three faces, an edge on two)
    p.setPen(Qt::NoPen);
    for (const CubeCell &cell : cubeCells(r, cbr, cbu, cbf))
        if (cell.hit == hover) {
            p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 200));
            p.drawPolygon(cell.poly);
        }
    // outlines and the edge / corner band lines
    for (const CubeFace &f : kCubeFaces) {
        if (QVector3D::dotProduct(f.n, -cbf) < 0.03)
            continue;
        const qreal a = 1.0 - 2 * kCubeZone;
        p.setPen(QPen(QColor(40, 50, 65, 90), 0.8));
        for (qreal t : {-a, a}) {
            p.drawLine(proj(f.n + f.right * float(t) - f.up), proj(f.n + f.right * float(t) + f.up));
            p.drawLine(proj(f.n - f.right + f.up * float(t)), proj(f.n + f.right + f.up * float(t)));
        }
        QPolygonF face;
        face << proj(f.n - f.right - f.up) << proj(f.n + f.right - f.up)
             << proj(f.n + f.right + f.up) << proj(f.n - f.right + f.up);
        p.setPen(QPen(QColor(25, 32, 44), 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(face);
    }

    // axis triad (bottom-left)
    const QPointF to(r.left() + 18, r.bottom() - 16);
    const QVector3D ax[3] = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};
    QFont fa = QApplication::font();
    fa.setPixelSize(9);
    fa.setBold(true);
    p.setFont(fa);
    for (int i = 0; i < 3; ++i) {
        const QPointF d(QVector3D::dotProduct(ax[i], m_br), -QVector3D::dotProduct(ax[i], m_bu));
        const QColor col = axisColor(ax[i]);
        p.setPen(QPen(col, 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(to, to + d * 13);
        p.drawText(QRectF(to.x() + d.x() * 19 - 5, to.y() + d.y() * 19 - 6, 10, 12), Qt::AlignCenter,
                   QString(QChar("XYZ"[i])));
    }

    // home button
    const QRectF hr = homeRect(r);
    p.setPen(Qt::NoPen);
    p.setBrush(hover == kGizmoHome ? accent : QColor(th.hudBg.red(), th.hudBg.green(), th.hudBg.blue(), 200));
    p.drawRoundedRect(hr, 4, 4);
    QPolygonF house;
    const QPointF hc = hr.center();
    house << hc + QPointF(0, -6) << hc + QPointF(6, 0) << hc + QPointF(4, 0) << hc + QPointF(4, 6)
          << hc + QPointF(-4, 6) << hc + QPointF(-4, 0) << hc + QPointF(-6, 0);
    p.setBrush(hover == kGizmoHome ? QColor(15, 18, 24) : QColor(220, 228, 240));
    p.drawPolygon(house);

    // roll buttons: turn the picture 90 deg about the viewing axis
    for (bool left : {true, false}) {
        const QRectF rr = rollRect(r, left);
        const bool hot = hover == (left ? kGizmoRollL : kGizmoRollR);
        p.setPen(Qt::NoPen);
        p.setBrush(hot ? accent : QColor(th.hudBg.red(), th.hudBg.green(), th.hudBg.blue(), 170));
        p.drawRoundedRect(rr, 4, 4);
        const QColor ink = hot ? QColor(15, 18, 24) : QColor(220, 228, 240);
        p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        const QRectF arcR = rr.adjusted(5, 5, -5, -5);
        p.drawArc(arcR, left ? 30 * 16 : 150 * 16, left ? 240 * 16 : -240 * 16);
        // arrow head at the arc end
        const qreal endDeg = left ? 270.0 : -90.0;
        const qreal ang = endDeg * PI / 180.0;
        const QPointF tip = arcR.center() + QPointF(std::cos(ang), -std::sin(ang)) * (arcR.width() / 2);
        const qreal dir = left ? 1.0 : -1.0;     // tangent direction (screen x) at the bottom of the circle
        p.setBrush(ink);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({tip + QPointF(dir * 3.5, 0), tip + QPointF(-dir * 1.5, -3), tip + QPointF(-dir * 1.5, 3)}));
    }
    p.restore();
}

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------
void Canvas::drawHud(QPainter &p) const
{
    const Theme &th = theme();
    p.save();
    QFont f = QApplication::font();
    f.setPixelSize(12);
    f.setFamily("Consolas");
    f.setStyleHint(QFont::Monospace);
    p.setFont(f);
    const QFontMetricsF fm(f);

    const QString l1 = QString("VIEW   %1%2  \xC2\xB7  %3").arg(viewName(m_view),
                                                              customOrientation() ? " (user)" : "",
                                                            perspectiveActive() ? "PERSPECTIVE" : "ORTHOGRAPHIC");
    QString l2 = QString("PLANE  %1").arg(planeName(m_plane));
    if (std::fabs(m_planeOffset) > 1e-9)
        l2 += QString("  offset %1").arg(Units::format(m_planeOffset));
    if (m_axisLock)
        l2 += QString("  \xC2\xB7  LOCK %1").arg(QChar("XYZ"[m_axisLock - 1]));
    l2 += "  \xC2\xB7  UNITS " + Units::unitLabel().toUpper();
    const qreal w = std::max(fm.horizontalAdvance(l1), fm.horizontalAdvance(l2)) + 28;
    const QRectF box(12, 12, w, 46);
    p.setPen(Qt::NoPen);
    p.setBrush(th.hudBg);
    p.drawRoundedRect(box, 8, 8);
    p.setPen(accent());
    p.drawText(box.adjusted(13, 6, -4, 0), Qt::AlignLeft | Qt::AlignTop, l1);
    p.setPen(th.hudText);
    p.drawText(box.adjusted(13, 24, -4, 0), Qt::AlignLeft | Qt::AlignTop, l2);

    const QString hint = "RMB drag orbit  \xC2\xB7  MMB / Space pan  \xC2\xB7  Shift+click picks several  \xC2\xB7  Wheel zoom  \xC2\xB7  "
                         "Tab plane  \xC2\xB7  X/Y/Z axis lock  \xC2\xB7  F fit  \xC2\xB7  F1 help";
    const QRectF hb(12, height() - 32, fm.horizontalAdvance(hint) + 24, 22);
    p.setBrush(QColor(th.hudBg.red(), th.hudBg.green(), th.hudBg.blue(), 150));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(hb, 6, 6);
    p.setPen(th.hudText);
    p.drawText(hb, Qt::AlignCenter, hint);
    p.restore();
}

// Solid body: shaded faces (back faces culled, far faces first), the edges are drawn afterwards.
void Canvas::drawSolid(QPainter &p, const SketchItem *it, const QVector<QVector3D> &ring) const
{
    const QVector3D e = it->extrude;
    const int n = ring.size() - 1;                 // the ring is closed (last == first)
    if (n < 3 || e.isNull())
        return;
    struct Face { QVector<QVector3D> v; QVector3D nrm; qreal depth; };
    QList<Face> faces;
    QVector3D cen;
    for (int i = 0; i < n; ++i)
        cen += ring[i];
    cen /= float(n);
    const QVector3D en = e.normalized();
    auto add = [&](const QVector<QVector3D> &v, const QVector3D &nrm) {
        if (QVector3D::dotProduct(nrm, m_bf) > 1e-4f)
            return;                                // faces turned away from the camera
        QVector3D c;
        for (const QVector3D &q : v)
            c += q;
        c /= float(v.size());
        faces.append({v, nrm, QVector3D::dotProduct(c, m_bf)});
    };
    QVector<QVector3D> bot, top;
    for (int i = 0; i < n; ++i) {
        bot.append(ring[i]);
        top.append(ring[i] + e);
    }
    add(bot, -en);
    add(top, en);
    for (int i = 0; i < n; ++i) {
        const QVector3D a = ring[i], b = ring[i + 1];
        QVector3D nrm = QVector3D::crossProduct(b - a, en);
        if (nrm.lengthSquared() < 1e-12f)
            continue;
        nrm.normalize();
        if (QVector3D::dotProduct(nrm, (a + b) * 0.5f - cen) < 0)
            nrm = -nrm;
        add({a, b, b + e, a + e}, nrm);
    }
    std::sort(faces.begin(), faces.end(), [](const Face &x, const Face &y) { return x.depth > y.depth; });
    const QColor base = it->fill.isValid() ? it->fill : QColor(196, 205, 220, 240);
    p.save();
    for (const Face &f : faces) {
        QPolygonF poly;
        bool ok = true;
        for (const QVector3D &q : f.v) {
            QPointF s;
            if (!projectD(q, s)) { ok = false; break; }
            poly << s;
        }
        if (!ok)
            continue;
        const float lit = float(0.55 + 0.45 * std::fabs(QVector3D::dotProduct(f.nrm, m_bf)));
        QColor c;
        c.setRgbF(base.redF() * lit, base.greenF() * lit, base.blueF() * lit, base.alphaF());
        p.setPen(QPen(c, 0.8));                     // hairline in the face colour hides the seams
        p.setBrush(c);
        p.drawPolygon(poly);
    }
    p.restore();
}
