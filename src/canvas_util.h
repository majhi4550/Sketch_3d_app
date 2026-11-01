#pragma once

// Small geometry helpers shared by the canvas_*.cpp translation units.

#include <QLineF>
#include <QPointF>
#include <QVector3D>

#include <algorithm>
#include <cmath>

static constexpr qreal PI = 3.14159265358979323846;

inline qreal norm360(qreal a)
{
    a = std::fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

inline bool segIntersect(const QLineF &p, const QLineF &q, qreal *tOut = nullptr)
{
    const QPointF r = p.p2() - p.p1();
    const QPointF s = q.p2() - q.p1();
    const qreal den = r.x() * s.y() - r.y() * s.x();
    if (std::fabs(den) < 1e-12)
        return false;
    const QPointF dp = q.p1() - p.p1();
    const qreal t = (dp.x() * s.y() - dp.y() * s.x()) / den;
    const qreal u = (dp.x() * r.y() - dp.y() * r.x()) / den;
    const qreal eps = 1e-9;
    if (t < -eps || t > 1.0 + eps || u < -eps || u > 1.0 + eps)
        return false;
    if (tOut)
        *tOut = t;
    return true;
}

inline qreal distToSegment(const QPointF &p, const QLineF &l, qreal *tOut = nullptr)
{
    const QPointF d = l.p2() - l.p1();
    const qreal len2 = d.x() * d.x() + d.y() * d.y();
    qreal t = 0.0;
    if (len2 > 1e-12) {
        t = ((p.x() - l.p1().x()) * d.x() + (p.y() - l.p1().y()) * d.y()) / len2;
        t = std::clamp(t, 0.0, 1.0);
    }
    if (tOut)
        *tOut = t;
    return QLineF(p, l.p1() + t * d).length();
}

// Closest approach between segment a-b and the ray (ro, rd). Returns the 3D
// distance and the parameter t in [0,1] along the segment.
inline qreal segRayClosest(const QVector3D &a, const QVector3D &b, const QVector3D &ro,
                           const QVector3D &rd, qreal *tOut)
{
    const QVector3D u = b - a;
    const QVector3D w0 = a - ro;
    const qreal A = QVector3D::dotProduct(u, u);
    const qreal B = QVector3D::dotProduct(u, rd);
    const qreal C = QVector3D::dotProduct(rd, rd);
    const qreal D = QVector3D::dotProduct(u, w0);
    const qreal E = QVector3D::dotProduct(rd, w0);
    const qreal den = A * C - B * B;
    qreal s = 0.0;
    if (A > 1e-12) {
        s = (std::fabs(den) < 1e-12) ? 0.0 : (B * E - C * D) / den;
        s = std::clamp(s, 0.0, 1.0);
    }
    const QVector3D p = a + u * s;
    const qreal t2 = QVector3D::dotProduct(p - ro, rd) / std::max(C, 1e-12);
    const QVector3D q = ro + rd * t2;
    if (tOut)
        *tOut = s;
    return (p - q).length();
}

// Closest approach of segments p1-q1 and p2-q2 (s, t in [0,1]). Parallel segments
// return a huge distance so collinear overlaps never count as a crossing.
inline qreal segSegClosest(const QVector3D &p1, const QVector3D &q1, const QVector3D &p2,
                           const QVector3D &q2, qreal &s, qreal &t)
{
    const QVector3D d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    const qreal a = QVector3D::dotProduct(d1, d1), e = QVector3D::dotProduct(d2, d2);
    const qreal f = QVector3D::dotProduct(d2, r);
    s = t = 0;
    if (a < 1e-12 || e < 1e-12)
        return 1e18;
    const qreal c = QVector3D::dotProduct(d1, r), b = QVector3D::dotProduct(d1, d2);
    const qreal den = a * e - b * b;
    if (den <= 1e-9 * a * e)
        return 1e18;
    s = std::clamp((b * f - c * e) / den, 0.0, 1.0);
    t = (b * s + f) / e;
    if (t < 0) { t = 0; s = std::clamp(-c / a, 0.0, 1.0); }
    else if (t > 1) { t = 1; s = std::clamp((b - c) / a, 0.0, 1.0); }
    return ((p1 + d1 * float(s)) - (p2 + d2 * float(t))).length();
}
