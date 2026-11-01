#include "uikit.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSet>
#include <QToolBar>
#include <QVBoxLayout>

#include <functional>

// ---------------------------------------------------------------------------
// icons
// ---------------------------------------------------------------------------
static void drawGlyph(QPainter &p, const QString &n, const QColor &c, const QColor &accent)
{
    QPen pen(c, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen acc(accent, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto dot = [&](qreal x, qreal y, qreal r = 1.9) {
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawEllipse(QPointF(x, y), r, r);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    };
    auto arrow = [&](QPointF tip, qreal ang) {
        const qreal a1 = ang + 2.6, a2 = ang - 2.6;
        p.drawLine(tip, tip + QPointF(std::cos(a1), std::sin(a1)) * 4.2);
        p.drawLine(tip, tip + QPointF(std::cos(a2), std::sin(a2)) * 4.2);
    };
    if (n == "select") {
        QPolygonF a({{6, 4}, {6, 19}, {10, 15}, {13, 21}, {15.5, 20}, {12.5, 14}, {18, 14}});
        p.setBrush(QColor(c.red(), c.green(), c.blue(), 40));
        p.drawPolygon(a);
    } else if (n == "line") {
        p.drawLine(5, 19, 19, 5);
        dot(5, 19); dot(19, 5);
    } else if (n == "polyline") {
        QPolygonF a({{4, 18}, {9, 7}, {14, 15}, {20, 5}});
        p.drawPolyline(a);
        for (const QPointF &q : a) dot(q.x(), q.y(), 1.6);
    } else if (n == "rect") {
        p.drawRoundedRect(QRectF(4.5, 6.5, 15, 11), 1.5, 1.5);
        dot(4.5, 6.5); dot(19.5, 17.5);
    } else if (n == "circle") {
        p.drawEllipse(QPointF(12, 12), 7.5, 7.5);
        dot(12, 12, 1.6);
    } else if (n == "arc") {
        QPainterPath a;
        a.moveTo(4.5, 17);
        a.quadTo(12, -2, 19.5, 17);
        p.drawPath(a);
        dot(4.5, 17); dot(19.5, 17);
    } else if (n == "curve") {
        QPainterPath a;
        a.moveTo(4, 17);
        a.cubicTo(8, 3, 14, 21, 20, 7);
        p.drawPath(a);
        dot(4, 17); dot(20, 7);
    } else if (n == "dim") {
        p.drawLine(5, 7, 5, 17); p.drawLine(19, 7, 19, 17);
        p.setPen(acc);
        p.drawLine(5, 12, 19, 12);
        arrow(QPointF(5, 12), 0); arrow(QPointF(19, 12), 3.14159);
    } else if (n == "text") {
        QFont f("Georgia");
        f.setPixelSize(19);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(0, 0, 24, 24), Qt::AlignCenter, "T");
        p.setPen(acc);
        p.drawLine(6, 20, 18, 20);
    } else if (n == "image") {
        p.drawRoundedRect(QRectF(4, 5, 16, 14), 2, 2);
        QPolygonF m({{5, 17}, {10, 11}, {13.5, 15}, {15.5, 13}, {19, 17}});
        p.drawPolyline(m);
        dot(16, 9, 1.5);
    } else if (n == "trim") {
        p.drawLine(4, 20, 20, 4);
        p.setPen(QPen(c, 1.7, Qt::DotLine, Qt::RoundCap));
        p.drawLine(4, 4, 11, 11);
        p.setPen(acc);
        p.drawLine(11, 11, 20, 20);
    } else if (n == "extend") {
        p.drawLine(19, 4, 19, 20);
        p.drawLine(4, 12, 15, 12);
        p.setPen(acc);
        arrow(QPointF(18, 12), 3.14159);
        p.setPen(QPen(accent, 1.7, Qt::DotLine));
        p.drawLine(15, 12, 18, 12);
    } else if (n == "join") {
        p.drawPolyline(QPolygonF({{5, 5}, {5, 19}, {19, 19}}));
        dot(5, 19, 2.4);
    } else if (n == "break") {
        p.drawLine(4, 12, 9, 12);
        p.drawLine(15, 12, 20, 12);
        p.setPen(acc);
        p.drawLine(11, 8, 13, 16);
    } else if (n == "offset") {
        QPainterPath a, b;
        a.moveTo(4, 18); a.quadTo(4, 6, 16, 6);
        b.moveTo(8, 21); b.quadTo(8, 12, 20, 11);
        p.drawPath(a);
        p.setPen(acc);
        p.drawPath(b);
    } else if (n == "cutter") {
        p.setPen(QPen(accent, 1.7, Qt::DashLine));
        p.drawLine(3, 20, 21, 4);
        p.setPen(pen);
        p.drawLine(5, 8, 19, 16);
        p.drawLine(7, 17, 17, 6);
    } else if (n == "mirror") {
        p.setPen(QPen(accent, 1.5, Qt::DashLine));
        p.drawLine(12, 3, 12, 21);
        p.setPen(pen);
        p.drawPolygon(QPolygonF({{9, 7}, {9, 17}, {3.5, 17}}));
        p.setBrush(QColor(c.red(), c.green(), c.blue(), 50));
        p.drawPolygon(QPolygonF({{15, 7}, {15, 17}, {20.5, 17}}));
    } else if (n == "pattern") {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                dot(6 + i * 6, 6 + j * 6, i == 0 && j == 0 ? 2.3 : 1.7);
    } else if (n == "edit") {
        p.drawPolyline(QPolygonF({{5, 17}, {10, 7}, {19, 13}}));
        for (QPointF q : {QPointF(5, 17), QPointF(10, 7), QPointF(19, 13)}) {
            p.setBrush(QColor(20, 26, 36));
            p.setPen(acc);
            p.drawRect(QRectF(q.x() - 2.2, q.y() - 2.2, 4.4, 4.4));
        }
    } else if (n == "move" || n == "drag") {
        p.drawLine(12, 3, 12, 21); p.drawLine(3, 12, 21, 12);
        arrow(QPointF(12, 3), 1.5708); arrow(QPointF(12, 21), -1.5708);
        arrow(QPointF(3, 12), 0); arrow(QPointF(21, 12), 3.14159);
        if (n == "drag") dot(12, 12, 2.4);
    } else if (n == "copy") {
        p.drawRoundedRect(QRectF(4, 4, 11, 11), 2, 2);
        p.setPen(acc);
        p.drawRoundedRect(QRectF(9, 9, 11, 11), 2, 2);
    } else if (n == "group" || n == "ungroup") {
        p.setPen(QPen(accent, 1.5, n == "group" ? Qt::DashLine : Qt::DotLine));
        p.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
        p.setPen(pen);
        p.drawRect(QRectF(6.5, 6.5, 6, 6));
        p.drawRect(QRectF(11.5, 11.5, 6, 6));
        if (n == "ungroup") { p.setPen(acc); p.drawLine(5, 19, 19, 5); }
    } else if (n == "delete") {
        p.drawLine(6, 7, 18, 7); p.drawLine(10, 7, 10, 4); p.drawLine(14, 7, 14, 4); p.drawLine(10, 4, 14, 4);
        p.drawPolyline(QPolygonF({{7, 7}, {8, 20}, {16, 20}, {17, 7}}));
        p.setPen(acc);
        p.drawLine(12, 10, 12, 17);
    } else if (n == "palette") {
        p.drawEllipse(QPointF(12, 12), 8, 8);
        dot(8, 10, 1.6); dot(12, 7, 1.6); dot(16, 10, 1.6);
    } else if (n.startsWith("align-")) {
        const QString k = n.mid(6);
        const bool vert = (k == "left" || k == "hcenter" || k == "right");
        p.setPen(acc);
        if (vert) {
            const qreal x = k == "left" ? 4 : (k == "right" ? 20 : 12);
            p.drawLine(x, 3, x, 21);
            p.setPen(pen);
            const qreal w1 = 11, w2 = 7;
            auto bar = [&](qreal y, qreal w) {
                const qreal x0 = k == "left" ? 6 : (k == "right" ? 18 - w : 12 - w / 2);
                p.setBrush(QColor(c.red(), c.green(), c.blue(), 60));
                p.drawRoundedRect(QRectF(x0, y, w, 4.5), 1.2, 1.2);
            };
            bar(6, w1); bar(13.5, w2);
        } else {
            const qreal y = k == "top" ? 4 : (k == "bottom" ? 20 : 12);
            p.drawLine(3, y, 21, y);
            p.setPen(pen);
            auto bar = [&](qreal x, qreal h) {
                const qreal y0 = k == "top" ? 6 : (k == "bottom" ? 18 - h : 12 - h / 2);
                p.setBrush(QColor(c.red(), c.green(), c.blue(), 60));
                p.drawRoundedRect(QRectF(x, y0, 4.5, h), 1.2, 1.2);
            };
            bar(6, 11); bar(13.5, 7);
        }
    } else if (n == "dist-h") {
        p.setPen(acc);
        p.drawLine(4, 3, 4, 21); p.drawLine(20, 3, 20, 21);
        p.setPen(pen);
        p.setBrush(QColor(c.red(), c.green(), c.blue(), 60));
        p.drawRoundedRect(QRectF(9.5, 7, 5, 10), 1.2, 1.2);
    } else if (n == "dist-v") {
        p.setPen(acc);
        p.drawLine(3, 4, 21, 4); p.drawLine(3, 20, 21, 20);
        p.setPen(pen);
        p.setBrush(QColor(c.red(), c.green(), c.blue(), 60));
        p.drawRoundedRect(QRectF(7, 9.5, 10, 5), 1.2, 1.2);
    } else if (n == "center-page") {
        p.drawRoundedRect(QRectF(4, 3, 16, 18), 1.5, 1.5);
        p.setPen(acc);
        p.drawLine(12, 6, 12, 18); p.drawLine(7, 12, 17, 12);
    } else if (n == "center-origin") {
        p.drawEllipse(QPointF(12, 12), 7, 7);
        p.setPen(acc);
        p.drawLine(12, 2, 12, 22); p.drawLine(2, 12, 22, 12);
    } else if (n == "flip-h") {
        p.setPen(QPen(c, 1.2, Qt::DashLine));
        p.drawLine(QPointF(12, 3), QPointF(12, 21));
        p.setPen(pen);
        p.drawPolygon(QPolygonF({{9.5, 6}, {9.5, 18}, {3, 18}}));
        p.setPen(acc);
        p.drawPolygon(QPolygonF({{14.5, 6}, {14.5, 18}, {21, 18}}));
        p.setPen(pen);
    } else if (n == "flip-v") {
        p.setPen(QPen(c, 1.2, Qt::DashLine));
        p.drawLine(QPointF(3, 12), QPointF(21, 12));
        p.setPen(pen);
        p.drawPolygon(QPolygonF({{6, 9.5}, {18, 9.5}, {18, 3}}));
        p.setPen(acc);
        p.drawPolygon(QPolygonF({{6, 14.5}, {18, 14.5}, {18, 21}}));
        p.setPen(pen);
    } else if (n == "fill") {
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 110));
        p.drawRoundedRect(QRectF(4, 6, 16, 12), 2, 2);
        p.setBrush(Qt::NoBrush);
    } else if (n == "hatch") {
        QPainterPath box;
        box.addRoundedRect(QRectF(4, 4, 16, 16), 2, 2);
        p.save();
        p.setClipPath(box);
        p.setPen(QPen(accent, 1.3));
        for (int k = -16; k <= 16; k += 4)
            p.drawLine(QPointF(4 + k, 20), QPointF(20 + k, 4));
        p.restore();
        p.drawPath(box);
    } else if (n == "front") {
        p.drawRect(QRectF(4, 4, 11, 11));
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 150));
        p.setPen(acc);
        p.drawRect(QRectF(9, 9, 11, 11));
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    } else if (n == "back") {
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 150));
        p.setPen(acc);
        p.drawRect(QRectF(4, 4, 11, 11));
        p.setBrush(QColor(c.red(), c.green(), c.blue(), 60));
        p.setPen(pen);
        p.drawRect(QRectF(9, 9, 11, 11));
        p.setBrush(Qt::NoBrush);
    } else if (n == "box") {
        QPolygonF top({{4, 8}, {12, 4}, {20, 8}, {12, 12}});
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 90));
        p.drawPolygon(top);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({{4, 8}, {4, 16}, {12, 20}, {20, 16}, {20, 8}}));
        p.drawLine(QPointF(12, 12), QPointF(12, 20));
        dot(4, 8, 1.6);
    } else if (n == "presspull") {
        p.drawPolygon(QPolygonF({{3, 17}, {12, 21}, {21, 17}, {12, 13}}));
        p.setPen(acc);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 70));
        p.drawPolygon(QPolygonF({{6, 10}, {12, 13}, {18, 10}, {12, 7}}));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(12, 10), QPointF(12, 2.5));
        arrow(QPointF(12, 2.5), -1.5708);
        p.setPen(pen);
    } else if (n == "union" || n == "subtract" || n == "intersect") {
        const QRectF a(3, 6, 11, 11), b(10, 7, 11, 11);
        QPainterPath pa, pb;
        pa.addRect(a);
        pb.addRect(b);
        const QPainterPath fillp = n == "union" ? pa.united(pb) : (n == "subtract" ? pa.subtracted(pb) : pa.intersected(pb));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 150));
        p.drawPath(fillp);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
        p.drawRect(a);
        p.setPen(QPen(c, 1.2, Qt::DashLine));
        p.drawRect(b);
        p.setPen(pen);
    } else if (n == "rotate") {
        QPainterPath a;
        a.arcMoveTo(QRectF(4, 4, 16, 16), 200);
        a.arcTo(QRectF(4, 4, 16, 16), 200, -250);
        p.drawPath(a);
        p.setPen(acc);
        arrow(a.currentPosition(), 2.2);
        p.setPen(pen);
        dot(12, 12, 1.8);
    } else if (n == "fillet" || n == "chamfer") {
        p.drawLine(QPointF(4, 20), QPointF(4, 11));
        p.drawLine(QPointF(13, 4), QPointF(20, 4));
        p.setPen(QPen(c, 1.0, Qt::DotLine));
        p.drawLine(QPointF(4, 11), QPointF(4, 4));
        p.drawLine(QPointF(4, 4), QPointF(13, 4));
        p.setPen(acc);
        if (n == "fillet") {
            QPainterPath a;
            a.moveTo(4, 11);
            a.quadTo(4, 4, 13, 4);
            p.drawPath(a);
        } else {
            p.drawLine(QPointF(4, 11), QPointF(13, 4));
        }
        p.setPen(pen);
    } else if (n == "shell") {
        p.drawPolygon(QPolygonF({{3, 9}, {12, 5}, {21, 9}, {21, 17}, {12, 21}, {3, 17}}));
        p.setPen(acc);
        p.drawPolygon(QPolygonF({{7, 10}, {12, 8}, {17, 10}, {12, 12}}));
        p.setPen(pen);
        p.drawLine(QPointF(12, 12), QPointF(12, 17));
    } else if (n == "revolve") {
        p.setPen(QPen(c, 1.0, Qt::DashDotLine));
        p.drawLine(QPointF(12, 2), QPointF(12, 22));
        p.setPen(pen);
        p.drawEllipse(QRectF(5, 15, 14, 5));
        p.drawLine(QPointF(5, 17.5), QPointF(8, 6));
        p.drawLine(QPointF(19, 17.5), QPointF(16, 6));
        p.setPen(acc);
        p.drawEllipse(QRectF(8, 4.5, 8, 3));
        p.setPen(pen);
    } else if (n == "pipe") {
        QPainterPath a;
        a.moveTo(3, 19);
        a.lineTo(9, 19);
        a.quadTo(17, 19, 17, 11);
        a.lineTo(17, 3);
        p.setPen(QPen(c, 6.0, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        p.drawPath(a);
        p.setPen(QPen(QColor(15, 20, 28), 2.6, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        p.drawPath(a);
        p.setPen(pen);
        p.setPen(acc);
        p.drawEllipse(QRectF(14, 1.5, 6, 3));
        p.setPen(pen);
    } else if (n == "joint") {
        p.drawRect(QRectF(3, 3, 5, 18));
        p.drawRect(QRectF(16, 3, 5, 18));
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 120));
        p.setPen(acc);
        p.drawRect(QRectF(6, 10, 12, 4));
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    } else if (n == "gizmo") {
        p.setPen(QPen(QColor(235, 85, 85), 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(11, 13), QPointF(20, 13));
        p.setPen(QPen(QColor(80, 200, 110), 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(11, 13), QPointF(5, 19));
        p.setPen(QPen(QColor(80, 145, 255), 2.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(11, 13), QPointF(11, 3.5));
        dot(11, 13, 1.8);
    } else if (n == "cutlist") {
        p.drawRoundedRect(QRectF(5, 3.5, 14, 17), 1.5, 1.5);
        p.setPen(acc);
        for (int k = 0; k < 4; ++k)
            p.drawLine(QPointF(8, 8 + k * 3.5), QPointF(16, 8 + k * 3.5));
        p.setPen(pen);
    } else if (n == "extrude") {
        p.drawPolygon(QPolygonF({{4, 16}, {13, 20}, {20, 16}, {11, 12}}));
        p.setPen(acc);
        p.drawLine(QPointF(11, 11), QPointF(11, 3));
        arrow(QPointF(11, 3), -1.5708);
        p.setPen(pen);
    } else if (n == "hide" || n == "show") {
        QPainterPath eye;
        eye.moveTo(3, 12);
        eye.quadTo(12, 3, 21, 12);
        eye.quadTo(12, 21, 3, 12);
        p.drawPath(eye);
        dot(12, 12, 2.6);
        if (n == "hide") {
            p.setPen(acc);
            p.drawLine(QPointF(4, 20), QPointF(20, 4));
            p.setPen(pen);
        }
    } else if (n == "explode") {
        p.drawLine(QPointF(4, 8), QPointF(10, 8));
        p.drawLine(QPointF(14, 8), QPointF(20, 8));
        p.drawLine(QPointF(4, 16), QPointF(10, 16));
        p.drawLine(QPointF(14, 16), QPointF(20, 16));
        p.setPen(acc);
        p.drawLine(QPointF(12, 3), QPointF(12, 21));
        p.setPen(pen);
    } else if (n == "weight") {
        p.setPen(QPen(c, 1.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(4, 6), QPointF(20, 6));
        p.setPen(QPen(c, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(4, 12), QPointF(20, 12));
        p.setPen(QPen(accent, 3.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(4, 18.5), QPointF(20, 18.5));
        p.setPen(pen);
    } else if (n == "linetype") {
        p.setPen(QPen(c, 1.8, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(3, 7), QPointF(21, 7));
        p.setPen(QPen(accent, 1.8, Qt::DashLine, Qt::FlatCap));
        p.drawLine(QPointF(3, 12), QPointF(21, 12));
        p.setPen(QPen(c, 1.8, Qt::DashDotLine, Qt::FlatCap));
        p.drawLine(QPointF(3, 17), QPointF(21, 17));
        p.setPen(pen);
    } else if (n == "align") {
        p.setPen(acc);
        p.drawLine(4, 3, 4, 21);
        p.setPen(pen);
        p.drawRoundedRect(QRectF(6.5, 5, 12, 5), 1.2, 1.2);
        p.drawRoundedRect(QRectF(6.5, 13, 7, 5), 1.2, 1.2);
    } else {
        p.drawEllipse(QPointF(12, 12), 6, 6);
    }
}

QIcon uiIcon(const QString &name, int sz)
{
    QIcon ic;
    const qreal dpr = 2.0;
    auto make = [&](const QColor &c, const QColor &acc) {
        QPixmap pm(int(sz * dpr), int(sz * dpr));
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(sz / 24.0, sz / 24.0);
        drawGlyph(p, name, c, acc);
        return pm;
    };
    const QColor normal(200, 216, 238), accent(53, 224, 200);
    ic.addPixmap(make(normal, accent), QIcon::Normal, QIcon::Off);
    ic.addPixmap(make(QColor(255, 255, 255), accent), QIcon::Normal, QIcon::On);
    ic.addPixmap(make(QColor(255, 255, 255), accent), QIcon::Active, QIcon::Off);
    ic.addPixmap(make(QColor(110, 120, 138), QColor(110, 120, 138)), QIcon::Disabled, QIcon::Off);
    return ic;
}

// ---------------------------------------------------------------------------
// command palette
// ---------------------------------------------------------------------------
namespace {
struct Cmd { QAction *act; QString label, path, keys; };

class Palette : public QDialog
{
public:
    Palette(QWidget *parent, QList<Cmd> cmds) : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint), m_cmds(std::move(cmds))
    {
        setStyleSheet(
            "QDialog{background:#0f141c;border:1px solid #35e0c8;border-radius:12px;}"
            "QLineEdit{background:#0a0e14;border:1px solid #2a3546;border-radius:8px;padding:9px 12px;font-size:15px;color:#e8f1ff;}"
            "QListWidget{background:transparent;border:none;font-size:13px;outline:0;}"
            "QListWidget::item{padding:7px 10px;border-radius:6px;color:#cfe0f5;}"
            "QListWidget::item:selected{background:#12384a;color:#35e0c8;}"
            "QLabel{color:#6f819e;font-size:11px;background:transparent;}");
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(12, 12, 12, 8);
        m_edit = new QLineEdit;
        m_edit->setPlaceholderText("Type a command  -  line, align left, trim, group, ortho ...");
        m_list = new QListWidget;
        auto *foot = new QLabel("Enter run   |   Up / Down move   |   Esc close");
        lay->addWidget(m_edit);
        lay->addWidget(m_list, 1);
        lay->addWidget(foot);
        resize(560, 420);
        connect(m_edit, &QLineEdit::textChanged, this, [this] { refill(); });
        connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *) { run(); });
        connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *) { run(); });
        m_edit->installEventFilter(this);
        refill();
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_edit && e->type() == QEvent::KeyPress) {
            auto *k = static_cast<QKeyEvent *>(e);
            const int n = m_list->count();
            if (k->key() == Qt::Key_Down && n) { m_list->setCurrentRow((m_list->currentRow() + 1) % n); return true; }
            if (k->key() == Qt::Key_Up && n) { m_list->setCurrentRow((m_list->currentRow() - 1 + n) % n); return true; }
            if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) { run(); return true; }
        }
        return QDialog::eventFilter(o, e);
    }

private:
    QList<Cmd> m_cmds;
    QLineEdit *m_edit;
    QListWidget *m_list;

    static int score(const QString &q, const QString &text)
    {
        if (q.isEmpty()) return 0;
        const QString t = text.toLower();
        const int idx = t.indexOf(q);
        if (idx >= 0) return idx == 0 ? 0 : (t[idx - 1] == ' ' ? 2 : 6);      // substring: best
        int pos = 0, gaps = 0;
        for (QChar ch : q) {
            const int f = t.indexOf(ch, pos);
            if (f < 0) return -1;
            gaps += f - pos;
            pos = f + 1;
        }
        return 20 + gaps;
    }

    void refill()
    {
        const QString q = m_edit->text().trimmed().toLower();
        QList<QPair<int, int>> hits;
        for (int i = 0; i < m_cmds.size(); ++i) {
            const int s = score(q, m_cmds[i].label + " " + m_cmds[i].path);
            if (s >= 0) hits.append({s, i});
        }
        std::stable_sort(hits.begin(), hits.end(), [](auto &a, auto &b) { return a.first < b.first; });
        m_list->clear();
        for (const auto &h : std::as_const(hits)) {
            const Cmd &c = m_cmds[h.second];
            auto *it = new QListWidgetItem(c.act->icon(), c.label + (c.keys.isEmpty() ? "" : "        [" + c.keys + "]") +
                                                              (c.path.isEmpty() ? "" : "     - " + c.path));
            it->setData(Qt::UserRole, h.second);
            m_list->addItem(it);
            if (m_list->count() > 60) break;
        }
        if (m_list->count()) m_list->setCurrentRow(0);
    }

    void run()
    {
        auto *it = m_list->currentItem();
        if (!it) return;
        QAction *a = m_cmds[it->data(Qt::UserRole).toInt()].act;
        close();
        if (a->isEnabled())
            QMetaObject::invokeMethod(a, [a] { a->trigger(); }, Qt::QueuedConnection);
    }
};
} // namespace

static void collect(QMenu *m, const QString &path, QList<Cmd> &out, QSet<QAction *> &seen)
{
    for (QAction *a : m->actions()) {
        if (a->isSeparator() || seen.contains(a))
            continue;
        if (a->menu()) {
            collect(a->menu(), path.isEmpty() ? a->text().remove('&') : path + " > " + a->text().remove('&'), out, seen);
            continue;
        }
        const QString t = a->text().remove('&').remove("...").trimmed();
        if (t.isEmpty())
            continue;
        seen.insert(a);
        out.append({a, t, path, a->shortcut().toString(QKeySequence::NativeText)});
    }
}

void showCommandPalette(QMainWindow *w)
{
    QList<Cmd> cmds;
    QSet<QAction *> seen;
    for (QAction *top : w->menuBar()->actions())
        if (top->menu())
            collect(top->menu(), top->text().remove('&'), cmds, seen);
    for (QToolBar *tb : w->findChildren<QToolBar *>())
        for (QAction *a : tb->actions())
            if (!a->isSeparator() && !a->menu() && !seen.contains(a) && !a->text().isEmpty()) {
                seen.insert(a);
                cmds.append({a, a->text().remove('&'), tb->windowTitle() + " toolbar", a->shortcut().toString(QKeySequence::NativeText)});
            }
    auto *p = new Palette(w, cmds);
    p->setAttribute(Qt::WA_DeleteOnClose);
    const QPoint c = w->mapToGlobal(QPoint(w->width() / 2, 70));
    p->move(c.x() - p->width() / 2, c.y());
    p->show();
    p->raise();
    p->activateWindow();
    p->findChild<QLineEdit *>()->setFocus();
}
