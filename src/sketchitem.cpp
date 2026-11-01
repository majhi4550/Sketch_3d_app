#include "sketchitem.h"

#include <QBuffer>
#include <QJsonArray>
#include <QUuid>

static QString typeName(SketchItem::Type t)
{
    switch (t) {
    case SketchItem::Line:      return "line";
    case SketchItem::Curve:     return "curve";
    case SketchItem::Arc:       return "arc";
    case SketchItem::Circle:    return "circle";
    case SketchItem::Polyline:  return "polyline";
    case SketchItem::Rect:      return "rect";
    case SketchItem::Dimension: return "dimension";
    case SketchItem::Text:      return "text";
    case SketchItem::Hatch:     return "hatch";
    case SketchItem::Revolve:   return "revolve";
    case SketchItem::Pipe:      return "pipe";
    default:                    return "image";
    }
}

static SketchItem::Type typeFromName(const QString &s)
{
    if (s == "curve")     return SketchItem::Curve;
    if (s == "arc")       return SketchItem::Arc;
    if (s == "circle")    return SketchItem::Circle;
    if (s == "polyline")  return SketchItem::Polyline;
    if (s == "rect")      return SketchItem::Rect;
    if (s == "dimension") return SketchItem::Dimension;
    if (s == "text")      return SketchItem::Text;
    if (s == "image")     return SketchItem::ImageItem;
    if (s == "hatch")     return SketchItem::Hatch;
    if (s == "revolve")   return SketchItem::Revolve;
    if (s == "pipe")      return SketchItem::Pipe;
    return SketchItem::Line;
}

QJsonObject SketchItem::toJson() const
{
    QJsonObject o;
    o["type"] = typeName(m_type);
    o["color"] = color.name();
    o["width"] = width;
    o["plane"] = plane;
    o["dimMode"] = dimMode;
    o["lineStyle"] = lineStyle;
    o["group"] = group;
    o["font"] = fontFamily;
    o["bold"] = bold;
    o["italic"] = italic;
    o["textAngle"] = textAngle;
    if (fill.isValid())
        o["fill"] = fill.name(QColor::HexArgb);
    if (hidden) o["hidden"] = true;
    if (locked) o["locked"] = true;
    if (!name.isEmpty()) o["name"] = name;
    if (part) o["part"] = part;
    if (!material.isEmpty()) o["mat"] = material;
    if (hatch) o["hatch"] = hatch;
    if (hatchScale > 0) o["hatchScale"] = hatchScale;
    if (hatchAngle != 0) o["hatchAngle"] = hatchAngle;
    if (!loops.isEmpty()) {
        QJsonArray la;
        for (int n : loops)
            la.append(n);
        o["loops"] = la;
    }
    if (m_type == Revolve)
        o["sweep"] = sweep;
    if (m_type == Pipe) {
        o["pipeD"] = pipeD;
        o["pipeWall"] = pipeWall;
        o["bendR"] = bendR;
    }
    if (!extrude.isNull()) {
        o["ex"] = extrude.x();
        o["ey"] = extrude.y();
        o["ez"] = extrude.z();
    }

    QJsonArray a;
    for (const QVector3D &p : pts) {
        QJsonObject v;
        v["x"] = p.x();
        v["y"] = p.y();
        v["z"] = p.z();
        a.append(v);
    }
    o["pts"] = a;

    o["text"] = text;
    o["textSize"] = textSize;

    if (!image.isNull()) {
        QByteArray ba;
        QBuffer buf(&ba);
        buf.open(QIODevice::WriteOnly);
        image.save(&buf, "PNG");
        o["image"] = QString::fromLatin1(ba.toBase64());
    }
    o["imageW"] = imageSizeMm.width();
    o["imageH"] = imageSizeMm.height();
    return o;
}

void SketchItem::fromJson(const QJsonObject &o)
{
    m_type = typeFromName(o["type"].toString());
    color = QColor(o["color"].toString("#1e1e1e"));
    width = o["width"].toDouble(0.35);
    plane = o["plane"].toInt(-1);
    dimMode = o["dimMode"].toInt(0);
    lineStyle = o["lineStyle"].toInt(0);
    group = o["group"].toInt(0);
    fontFamily = o["font"].toString();
    bold = o["bold"].toBool(false);
    italic = o["italic"].toBool(false);
    textAngle = o["textAngle"].toDouble(0);
    fill = o.contains("fill") ? QColor(o["fill"].toString()) : QColor();
    hidden = o["hidden"].toBool(false);
    locked = o["locked"].toBool(false);
    name = o["name"].toString();
    part = o["part"].toInt(0);
    material = o["mat"].toString();
    hatch = o["hatch"].toInt(0);
    hatchScale = o["hatchScale"].toDouble(0);
    hatchAngle = o["hatchAngle"].toDouble(0);
    loops.clear();
    for (const QJsonValue &v : o["loops"].toArray())
        loops.append(v.toInt());
    extrude = QVector3D(float(o["ex"].toDouble(0)), float(o["ey"].toDouble(0)), float(o["ez"].toDouble(0)));
    sweep = o["sweep"].toDouble(360);
    pipeD = o["pipeD"].toDouble(25);
    pipeWall = o["pipeWall"].toDouble(0);
    bendR = o["bendR"].toDouble(0);

    pts.clear();
    const QJsonArray a = o["pts"].toArray();
    for (const QJsonValue &v : a) {
        const QJsonObject p = v.toObject();
        pts.append(QVector3D(p["x"].toDouble(), p["y"].toDouble(), p["z"].toDouble()));
    }

    text = o["text"].toString();
    textSize = o["textSize"].toDouble(3.5);

    const QString b64 = o["image"].toString();
    if (!b64.isEmpty())
        image.loadFromData(QByteArray::fromBase64(b64.toLatin1()), "PNG");
    imageSizeMm = QSizeF(o["imageW"].toDouble(60), o["imageH"].toDouble(40));
    selected = false;
}

SketchItem *SketchItem::clone() const
{
    auto *it = new SketchItem(m_type);
    it->fromJson(toJson());
    return it;
}
