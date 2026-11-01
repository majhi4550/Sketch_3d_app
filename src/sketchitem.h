#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector3D>

// One sketch entity. Geometry is stored as true 3D control points so the
// drawing can be viewed from any projection plane (Top/XY, XZ, ZY, YX).
class SketchItem
{
public:
    enum Type { Line, Curve, Arc, Circle, Polyline, Rect, Dimension, Text, ImageItem, Hatch, Revolve, Pipe };

    explicit SketchItem(Type t = Line) : m_type(t) {}
    ~SketchItem() = default;

    Type type() const { return m_type; }

    QList<QVector3D> pts;   // control points; meaning depends on type:
                            // Line 2pts | Curve 3pts (start,mid,end) |
                            // Arc 3pts (start,mid,end) | Circle center+edge |
                            // Rect 2 corners | Dimension 2 pts + offset pt |
                            // Text/Image 1 anchor pt | Hatch boundary loops (see loops) |
                            // Revolve: axis point, a second axis point, then the closed profile |
                            // Pipe: the centre line (path) points
    QList<int> loops;       // Hatch / Polyline with holes: point count of each loop in pts (outer first, then holes)
    int hatch = 0;          // hatch pattern (Canvas::hatchPatternNames); 0 = solid fill
    double hatchScale = 0;  // pattern spacing in mm, 0 = automatic
    double hatchAngle = 0;  // pattern rotation, degrees
    int dimMode = 0;       // Dimension: 0 aligned | 1 horizontal (along U) | 2 vertical (along V)
    int plane = 0;          // sketch plane the entity was drawn on:
                            // 0 Top(XY) | 1 XZ | 2 ZY | 3 YX (-1 = unknown/legacy)
    QColor color = QColor(30, 30, 30);
    QColor fill;            // fill of closed shapes (invalid = no fill), alpha = opacity
    bool hidden = false;    // invisible: not drawn, not picked, not snapped
    bool locked = false;    // "Fix" constraint: cannot be selected, moved, cut or edited
    QVector3D extrude;      // non-zero: the closed profile is a SOLID body swept along this vector
    double width = 0.35;    // line width in mm
    bool selected = false;
    int lineStyle = 0;      // 0 solid | 1 dash | 2 dot | 3 dash-dot | 4 centre | 5 hidden
    int group = 0;          // 0 = ungrouped, otherwise the group id
    QString name;           // part name (cut list), empty = unnamed
    int part = 0;           // solids made of several pieces by Union / Subtract share an id (one cut-list part)
    QString material;       // board material for the cut list and sheet nesting, e.g. "18 mm ply"
    double sweep = 360;     // Revolve: angle turned about the axis, degrees (right hand about pts[0] -> pts[1])
    double pipeD = 25;      // Pipe: outside diameter
    double pipeWall = 0;    // Pipe: wall thickness, 0 = a solid rod
    double bendR = 0;       // Pipe: centre-line bend radius at the corners, 0 = 1.5 x diameter

    QString text;           // Text
    double textSize = 3.5;  // Text height in mm
    QString fontFamily;     // empty = application font
    bool bold = false, italic = false;
    double textAngle = 0;   // degrees, counter-clockwise on screen

    QImage image;           // ImageItem
    QSizeF imageSizeMm = QSizeF(60, 40);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);
    SketchItem *clone() const;

private:
    Type m_type = Line;
};
