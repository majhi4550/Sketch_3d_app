#include "canvas.h"
#include "units.h"
#include <QInputDialog>
#include "canvas_util.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QMimeData>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QQuaternion>
#include <QTextStream>

#include <algorithm>
#include <functional>
#include <cmath>

// ---------------------------------------------------------------------------
// undo: whole-document snapshots
// ---------------------------------------------------------------------------
class SnapshotCommand : public QUndoCommand
{
public:
    SnapshotCommand(Canvas *c, const QByteArray &before, const QByteArray &after, const QString &name)
        : QUndoCommand(name), m_canvas(c), m_before(before), m_after(after) {}
    void redo() override { m_canvas->deserialize(m_after, false); }
    void undo() override { m_canvas->deserialize(m_before, false); }
private:
    Canvas *m_canvas;
    QByteArray m_before, m_after;
};

void Canvas::snapshotCommit(const QString &name)
{
    const QByteArray after = serialize();
    if (after == m_snapshot) {
        m_snapshot.clear();
        return;
    }
    m_undo.push(new SnapshotCommand(this, m_snapshot, after, name));
    m_snapshot.clear();
    emit contentChanged();
}

// ---------------------------------------------------------------------------
// selection
// ---------------------------------------------------------------------------
void Canvas::deleteItems()
{
    qDeleteAll(m_items);
    m_items.clear();
    m_selected.clear();
    m_hover = nullptr;
    m_editItem = nullptr;
    m_joinFirst = nullptr;
    m_dragData.clear();
}

void Canvas::setSelected(SketchItem *it, bool sel)
{
    QList<SketchItem *> grp{it};
    if (it->group)                                   // a group is always selected as a whole
        for (SketchItem *o : std::as_const(m_items))
            if (o != it && o->group == it->group)
                grp << o;
    for (SketchItem *o : std::as_const(grp)) {
        o->selected = sel;
        if (sel && !m_selected.contains(o))
            m_selected.append(o);
        if (!sel)
            m_selected.removeAll(o);
    }
    emit selectionChanged();
}

bool Canvas::selectionHasGroup() const
{
    for (const SketchItem *it : m_selected)
        if (it->group)
            return true;
    return false;
}

void Canvas::groupSelection()
{
    if (m_selected.size() < 2) {
        emit message("Group: select two or more objects first");
        return;
    }
    int id = 0;
    for (const SketchItem *it : std::as_const(m_items))
        id = std::max(id, it->group);
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected))
        it->group = id + 1;
    snapshotCommit("Group");
    emit message(QString("Grouped %1 objects - they now select and move together (Ctrl+Shift+G to ungroup)")
                     .arg(m_selected.size()));
    emit selectionChanged();
    update();
}

void Canvas::ungroupSelection()
{
    if (!selectionHasGroup()) {
        emit message("Ungroup: the selection contains no group");
        return;
    }
    snapshotBegin();
    QSet<int> ids;
    for (const SketchItem *it : std::as_const(m_selected))
        if (it->group)
            ids.insert(it->group);
    for (SketchItem *it : std::as_const(m_items))
        if (ids.contains(it->group))
            it->group = 0;
    snapshotCommit("Ungroup");
    emit message("Ungrouped");
    emit selectionChanged();
    update();
}

// clones made from grouped originals get their own fresh group ids
void Canvas::regroup(const QList<SketchItem *> &made)
{
    int id = 0;
    for (const SketchItem *it : std::as_const(m_items))
        id = std::max(id, it->group);
    QHash<int, int> map;
    for (SketchItem *it : made)
        if (it->group) {
            if (!map.contains(it->group))
                map[it->group] = ++id;
            it->group = map[it->group];
        }
}

void Canvas::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    for (SketchItem *it : std::as_const(m_selected))
        it->selected = false;
    m_selected.clear();
    emit selectionChanged();
}

void Canvas::selectAll()
{
    for (SketchItem *it : std::as_const(m_items)) {
        if (it->hidden || it->locked)
            continue;
        it->selected = true;
        if (!m_selected.contains(it))
            m_selected.append(it);
    }
    emit selectionChanged();
    update();
}

void Canvas::selectIndices(const QList<int> &idx)
{
    for (SketchItem *it : std::as_const(m_selected))
        it->selected = false;
    m_selected.clear();
    for (int i : idx)
        if (i >= 0 && i < m_items.size()) {
            m_items[i]->selected = true;
            m_selected.append(m_items[i]);
        }
    emit selectionChanged();
    update();
}

QString Canvas::itemLabel(const SketchItem *it)
{
    const QString g = it->group ? QString("  [G%1]").arg(it->group) : QString();
    QString s;
    switch (it->type()) {
    case SketchItem::Line:      s = "Line"; break;
    case SketchItem::Curve:     s = "Curve"; break;
    case SketchItem::Arc:       s = "Arc"; break;
    case SketchItem::Circle:    s = "Circle"; break;
    case SketchItem::Polyline:  s = QString("Polyline (%1 pts)").arg(it->pts.size()); break;
    case SketchItem::Rect:      s = "Rectangle"; break;
    case SketchItem::Dimension: s = "Dimension"; break;
    case SketchItem::Text:      s = "Text: " + it->text.left(18); break;
    case SketchItem::Hatch:     s = "Hatch: " + hatchPatternNames().value(it->hatch).section("  (", 0, 0); break;
    default:                    s = "Image"; break;
    }
    if (it->type() == SketchItem::Revolve)
        s = "Revolved body";
    else if (it->type() == SketchItem::Pipe)
        s = "Pipe: " + pipeLabel(it->pipeD, it->pipeWall);
    else if (!it->extrude.isNull())
        s = "Board (" + s.section(" (", 0, 0).toLower() + ")";
    if (!it->name.isEmpty())
        s = it->name + "  -  " + s;
    return s + g;
}

QString Canvas::selectionInfo() const
{
    if (m_selected.isEmpty())
        return "Nothing selected";
    if (m_selected.size() > 1) {
        QSet<int> gs;
        for (const SketchItem *it : m_selected)
            if (it->group) gs.insert(it->group);
        double len = 0;
        for (const SketchItem *it : m_selected)
            len += itemLength(it);
        return QString("%1 objects%2  |  total length %3").arg(m_selected.size())
            .arg(gs.isEmpty() ? QString() : QString(" in %1 group(s)").arg(gs.size()), Units::format(len));
    }
    const SketchItem *it = m_selected[0];
    QString s = itemLabel(it);
    if (isSolid(it)) {                                 // a board: its size the way the cut list reads it
        const auto rows = cutList(true);
        if (rows.size() == 1)
            s += QString("\nBoard  %1 x %2 x %3").arg(Units::format(rows[0].l), Units::format(rows[0].w),
                                                      Units::format(rows[0].t));
    }
    static const char *styleNames[] = {"Solid", "Dash", "Dot", "Dash-dot", "Centre", "Hidden"};
    s += QString("\nPen  %1 mm  %2").arg(it->width, 0, 'f', 2).arg(styleNames[std::clamp(it->lineStyle, 0, 5)]);
    const auto &p = it->pts;
    switch (it->type()) {
    case SketchItem::Line:
        s += QString("\nLength  %1").arg(Units::format(itemLength(it)));
        s += QString("\nFrom  %1, %2, %3").arg(Units::formatDim(p[0].x()), Units::formatDim(p[0].y()), Units::formatDim(p[0].z()));
        s += QString("\nTo    %1, %2, %3").arg(Units::formatDim(p[1].x()), Units::formatDim(p[1].y()), Units::formatDim(p[1].z()));
        break;
    case SketchItem::Circle: {
        const double r = (p[1] - p[0]).length();
        s += QString("\nRadius  %1\nDiameter  %2\nCircumference  %3\nArea  %4")
                 .arg(Units::format(r), Units::format(2 * r), Units::format(2 * PI * r), Units::formatArea(PI * r * r));
        break;
    }
    case SketchItem::Rect: {
        const QVector<QVector3D> c = rectCorners(it);
        if (c.size() == 4) {
            const double w = (c[1] - c[0]).length(), h = (c[3] - c[0]).length();
            s += QString("\nWidth  %1\nHeight  %2\nArea  %3").arg(Units::format(w), Units::format(h), Units::formatArea(w * h));
        }
        break;
    }
    case SketchItem::Text:
        s += QString("\nHeight  %1\nAngle  %2\xC2\xB0").arg(Units::format(it->textSize)).arg(it->textAngle, 0, 'f', 1);
        break;
    case SketchItem::ImageItem:
        break;
    case SketchItem::Hatch: {
        const Frame fr = frameFor(std::max(0, it->plane));
        QList<QVector<QPointF>> uv;
        for (const auto &l : hatchLoops(it)) {
            QVector<QPointF> l2;
            for (const QVector3D &q : l)
                l2 << QPointF(QVector3D::dotProduct(q, fr.U), QVector3D::dotProduct(q, fr.V));
            uv << l2;
        }
        s += QString("\nArea  %1").arg(Units::formatArea(loopsArea(uv)));
        if (uv.size() > 1)
            s += QString("\nIslands  %1").arg(uv.size() - 1);
        s += QString("\nSpacing  %1%2  angle %3\xC2\xB0").arg(Units::format(hatchSpacing(it, hatchLoops(it))),
                                                              it->hatchScale > 0 ? "" : " (auto)").arg(it->hatchAngle, 0, 'g', 4);
        break;
    }
    default:
        s += QString("\nLength  %1").arg(Units::format(itemLength(it)));
        break;
    }
    return s;
}

// ---------------------------------------------------------------------------
// document-level operations
// ---------------------------------------------------------------------------
void Canvas::clearAll()
{
    snapshotBegin();
    deleteItems();
    m_pending.clear();
    m_guides.clear();
    snapshotCommit("Clear Canvas");
    emit selectionChanged();
    update();
}

void Canvas::deleteSelection()
{
    if (m_selected.isEmpty())
        return;
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected)) {
        m_items.removeAll(it);
        if (m_hover == it) m_hover = nullptr;
        if (m_editItem == it) m_editItem = nullptr;
        delete it;
    }
    m_selected.clear();
    snapshotCommit("Delete");
    emit selectionChanged();
    update();
}

void Canvas::setSelectedColor(const QColor &c)
{
    setDefaultColor(c);
    if (m_selected.isEmpty())
        return;
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected))
        it->color = c;
    snapshotCommit("Change Colour");
    update();
}

void Canvas::setSelectedWidth(double w)
{
    setDefaultWidth(w);
    if (m_selected.isEmpty())
        return;
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected))
        it->width = w;
    snapshotCommit("Change Line Width");
    update();
}

void Canvas::setSelectedLineStyle(int s)
{
    m_defStyle = s;
    if (m_selected.isEmpty())
        return;
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected))
        it->lineStyle = s;
    snapshotCommit("Change Line Type");
    update();
}

SketchItem *Canvas::selectedText() const
{
    for (SketchItem *it : m_selected)
        if (it->type() == SketchItem::Text)
            return it;
    return nullptr;
}

void Canvas::setSelectedTextContent(const QString &t)
{
    SketchItem *it = selectedText();
    if (!it || it->text == t)
        return;
    snapshotBegin();
    it->text = t;
    snapshotCommit("Edit Text");
    emit selectionChanged();
    update();
}

void Canvas::setSelectedFont(const QString &family, bool bold, bool italic)
{
    bool any = false;
    for (const SketchItem *it : std::as_const(m_selected))
        any = any || it->type() == SketchItem::Text;
    if (!any)
        return;
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected))
        if (it->type() == SketchItem::Text) {
            it->fontFamily = family;
            it->bold = bold;
            it->italic = italic;
        }
    snapshotCommit("Change Font");
    update();
}

void Canvas::setSelectedTextAngle(double deg)
{
    bool any = false;
    for (const SketchItem *it : std::as_const(m_selected))
        any = any || it->type() == SketchItem::Text;
    if (!any)
        return;
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected))
        if (it->type() == SketchItem::Text)
            it->textAngle = deg;
    snapshotCommit("Rotate Text");
    update();
}

void Canvas::editSelectedText()
{
    if (SketchItem *it = selectedText())
        startInlineEdit(it);
    else
        emit message("Select a text object first (or double-click it)");
}

// move or copy the selection by an exact vector
void Canvas::applyMove(const QVector3D &delta, bool copy)
{
    if (m_selected.isEmpty() || delta.isNull())
        return;
    snapshotBegin();
    if (!copy) {
        for (SketchItem *it : std::as_const(m_selected))
            for (QVector3D &pt : it->pts)
                pt += delta;
    } else {
        const QList<SketchItem *> src = m_selected;
        QList<SketchItem *> made;
        clearSelection();
        for (const SketchItem *it : src) {
            SketchItem *t = it->clone();
            for (QVector3D &pt : t->pts)
                pt += delta;
            m_items.append(t);
            made << t;
        }
        regroup(made);
        for (SketchItem *t : std::as_const(made)) {
            t->selected = true;
            m_selected.append(t);
        }
    }
    snapshotCommit(copy ? "Copy" : "Move");
    emit message(QString("%1 by %2, %3, %4  (distance %5)").arg(copy ? "Copied" : "Moved")
                     .arg(Units::formatDim(delta.x()), Units::formatDim(delta.y()), Units::formatDim(delta.z()),
                          Units::format(delta.length())));
    emit selectionChanged();
    update();
}

void Canvas::setSelectedTextSize(double mm)
{
    setDefaultTextSize(mm);
    bool any = false;
    for (const SketchItem *it : std::as_const(m_selected))
        any = any || it->type() == SketchItem::Text;
    if (!any)
        return;
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected))
        if (it->type() == SketchItem::Text)
            it->textSize = mm;
    snapshotCommit("Change Text Size");
    update();
}

void Canvas::beginDrag(const QVector3D &start)
{
    m_dragging = true;
    m_dragMoved = false;
    m_dragStart = start;
    m_dragData.clear();
    m_dynDelta = QVector3D();
    snapshotBegin();

    if (m_tool == Tool::Edit && m_editItem) {
        int idx = -1;
        qreal bd = 12.0;
        for (int i = 0; i < m_editItem->pts.size(); ++i) {
            QPointF sp;
            if (!projectD(m_editItem->pts[i], sp))
                continue;
            const qreal d = QLineF(sp, QPointF(m_pressScreen)).length();
            if (d < bd) {
                bd = d;
                idx = i;
            }
        }
        m_editIndex = idx;
        if (idx >= 0) {
            m_dragData.insert(m_editItem, m_editItem->pts);
            m_dragStart = m_editItem->pts[idx];     // drag within the vertex's own plane layer
        } else {
            m_dragging = false;
            m_snapshot.clear();
        }
        return;
    }
    for (SketchItem *it : std::as_const(m_selected))
        m_dragData.insert(it, it->pts);
    if (m_dragData.isEmpty()) {
        m_dragging = false;
        m_snapshot.clear();
    }
}

void Canvas::finishPending()
{
    SketchItem *it = nullptr;
    const int n = m_pending.size();
    const Frame fr = planeFrame();
    switch (m_tool) {
    case Tool::Line:
        if (n >= 2) { it = new SketchItem(SketchItem::Line); it->pts = m_pending.mid(0, 2); }
        break;
    case Tool::Curve3:
        if (n >= 3) { it = new SketchItem(SketchItem::Curve); it->pts = m_pending.mid(0, 3); }
        break;
    case Tool::Arc:
        if (n >= 3) { it = new SketchItem(SketchItem::Arc); it->pts = m_pending.mid(0, 3); }
        break;
    case Tool::Circle:
        if (n >= 2) { it = new SketchItem(SketchItem::Circle); it->pts = m_pending.mid(0, 2); }
        break;
    case Tool::Rect:
        if (n >= 2) {
            it = new SketchItem(SketchItem::Rect);
            const QVector3D d = m_pending[1] - m_pending[0];
            it->pts = {m_pending[0], m_pending[0] + fr.U * QVector3D::dotProduct(d, fr.U) +
                                         fr.V * QVector3D::dotProduct(d, fr.V)};
        }
        break;
    case Tool::Dimension:
        if (n >= 3) { it = new SketchItem(SketchItem::Dimension); it->pts = m_pending.mid(0, 3); it->dimMode = m_dimMode; }
        break;
    case Tool::Polyline:
        if (n >= 2) { it = new SketchItem(SketchItem::Polyline); it->pts = m_pending; }
        break;
    default:
        break;
    }
    m_pending.clear();
    dynClear();
    m_axisLock = 0;
    m_guides.clear();
    emitSnapState();
    if (it) {
        it->plane = int(m_plane);
        it->color = m_defColor;
        it->width = m_defWidth;
        it->lineStyle = m_defStyle;
        snapshotBegin();
        m_items.append(it);
        snapshotCommit("Create " + toolName(m_tool));
    }
    if (m_tool != Tool::Box)
        boxRestorePlane();                        // a shape drawn on a face: back to the sketch plane
    emitHint();
    update();
}

// ---------------------------------------------------------------------------
// transforms
// ---------------------------------------------------------------------------
void Canvas::rotatePoint(QVector3D &p, const QVector3D &c, const QVector3D &axis, qreal deg) const
{
    const QQuaternion q = QQuaternion::fromAxisAndAngle(axis, float(deg));
    p = c + q.rotatedVector(p - c);
}

void Canvas::rotateSelection(double deg)
{
    if (m_selected.isEmpty()) {
        emit message("Rotate: nothing selected");
        return;
    }
    const QVector3D c = selectionCenter();
    const Frame fr = planeFrame();
    int skipped = 0;
    snapshotBegin();
    const QList<SketchItem *> sel = m_selected;
    for (SketchItem *it : sel) {
        const Frame f = frameFor(std::max(0, it->plane));
        if (std::fabs(QVector3D::dotProduct(f.N, fr.N)) < 0.99f &&
            (it->type() == SketchItem::Circle || it->type() == SketchItem::Rect ||
             it->type() == SketchItem::ImageItem)) {
            ++skipped;
            continue;
        }
        if (it->type() == SketchItem::Rect && std::fabs(std::remainder(deg, 90.0)) > 1e-6)
            it = rectToPolyline(it, it->pts);           // a turned rectangle keeps its true corners
        for (QVector3D &pt : it->pts)
            rotatePoint(pt, c, fr.N, deg);
        if (!it->extrude.isNull())
            rotatePoint(it->extrude, QVector3D(), fr.N, deg);
    }
    snapshotCommit("Rotate");
    if (skipped)
        emit message(QString("%1 object(s) on another plane were not rotated - switch to their plane first").arg(skipped));
    update();
}

void Canvas::scaleSelection(double factor)
{
    if (m_selected.isEmpty() || qFuzzyIsNull(factor)) {
        emit message("Scale: nothing selected");
        return;
    }
    const QVector3D c = selectionCenter();
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected)) {
        for (QVector3D &pt : it->pts)
            pt = c + (pt - c) * float(factor);
        it->extrude *= float(factor);
        if (it->type() == SketchItem::Text)
            it->textSize *= factor;
        if (it->type() == SketchItem::ImageItem)
            it->imageSizeMm *= factor;
    }
    snapshotCommit("Scale");
    update();
}

void Canvas::moveSelectionBy(const QVector3D &delta)
{
    if (m_selected.isEmpty() || delta.isNull())
        return;
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected))
        for (QVector3D &pt : it->pts)
            pt += delta;
    snapshotCommit("Move");
    update();
}

void Canvas::mirrorSelection(bool alongU, bool keepOriginal)
{
    if (m_selected.isEmpty()) {
        emit message("Mirror: nothing selected");
        return;
    }
    const Frame fr = planeFrame();
    const QVector3D axis = alongU ? fr.U : fr.V;
    const QVector3D c = selectionCenter();
    const QList<SketchItem *> src = m_selected;
    QList<SketchItem *> made;
    snapshotBegin();
    for (SketchItem *it : src) {
        SketchItem *t = keepOriginal ? it->clone() : it;
        for (QVector3D &pt : t->pts)
            pt -= axis * (2.0f * QVector3D::dotProduct(pt - c, axis));
        t->extrude -= axis * (2.0f * QVector3D::dotProduct(t->extrude, axis));
        if (keepOriginal) {
            m_items.append(t);
            made << t;
        }
    }
    if (keepOriginal) {
        for (SketchItem *it : src) { it->selected = false; }
        m_selected.clear();
        for (SketchItem *t : std::as_const(made)) { t->selected = true; m_selected.append(t); }
    }
    regroup(made);
    snapshotCommit("Mirror");
    update();
}

void Canvas::arraySelection(int count, const QVector3D &step)
{
    if (m_selected.isEmpty() || count < 2 || step.isNull()) {
        emit message("Array: select objects and give a count >= 2 and a non-zero step");
        return;
    }
    const QList<SketchItem *> src = m_selected;
    QList<SketchItem *> made;
    snapshotBegin();
    for (int k = 1; k < count; ++k)
        for (SketchItem *it : src) {
            SketchItem *t = it->clone();
            for (QVector3D &pt : t->pts)
                pt += step * float(k);
            m_items.append(t);
            made << t;
        }
    for (SketchItem *it : src) it->selected = false;
    m_selected.clear();
    for (SketchItem *t : std::as_const(made)) { t->selected = true; m_selected.append(t); }
    regroup(made);
    snapshotCommit("Array");
    emit message(QString("Array: %1 copies created").arg(made.size()));
    update();
}

void Canvas::extrudeSelection(double dist)
{
    if (m_selected.isEmpty() || qFuzzyIsNull(dist)) {
        emit message("Extrude: select closed shapes first and give a non-zero distance");
        return;
    }
    const QList<SketchItem *> src = m_selected;
    QList<SketchItem *> made;
    snapshotBegin();
    auto addLine = [&](const QVector3D &a, const QVector3D &b, const SketchItem *like) {
        auto *l = new SketchItem(SketchItem::Line);
        l->pts = {a, b};
        l->plane = like->plane;
        l->color = like->color;
        l->width = like->width;
        l->lineStyle = like->lineStyle;
        l->group = like->group;
        m_items.append(l);
        made << l;
    };
    for (const SketchItem *it : src) {
        const Frame f = frameFor(std::max(0, it->plane));
        const QVector3D d = f.N * float(dist);
        auto copyUp = [&]() {
            SketchItem *t = it->clone();
            for (QVector3D &pt : t->pts)
                pt += d;
            m_items.append(t);
            made << t;
        };
        switch (it->type()) {
        case SketchItem::Rect: {
            const QVector<QVector3D> c = rectCorners(it);
            if (c.size() != 4) break;
            copyUp();
            for (const QVector3D &q : c)
                addLine(q, q + d, it);
            break;
        }
        case SketchItem::Circle: {
            copyUp();
            const QVector3D c0 = it->pts[0];
            const float r = (it->pts[1] - c0).length();
            const QVector3D e1 = (it->pts[1] - c0).normalized();
            const QVector3D e2 = QVector3D::crossProduct(f.N, e1);
            for (const QVector3D &q : {c0 + e1 * r, c0 - e1 * r, c0 + e2 * r, c0 - e2 * r})
                addLine(q, q + d, it);
            break;
        }
        case SketchItem::Polyline:
            copyUp();
            for (const QVector3D &q : it->pts)
                addLine(q, q + d, it);
            break;
        case SketchItem::Arc:
        case SketchItem::Curve:
            copyUp();
            addLine(it->pts[0], it->pts[0] + d, it);
            addLine(it->pts[2], it->pts[2] + d, it);
            break;
        case SketchItem::Line: {
            auto *pl = new SketchItem(SketchItem::Polyline);
            pl->pts = {it->pts[0], it->pts[1], it->pts[1] + d, it->pts[0] + d, it->pts[0]};
            pl->plane = it->plane; pl->color = it->color; pl->width = it->width;
            pl->lineStyle = it->lineStyle; pl->group = it->group;
            m_items.append(pl);
            made << pl;
            break;
        }
        default:
            break;
        }
    }
    if (made.isEmpty()) {
        m_snapshot.clear();
        emit message("Extrude works on rectangles, circles, polylines, arcs, curves and lines");
        return;
    }
    for (SketchItem *it : src) it->selected = false;
    m_selected.clear();
    for (SketchItem *t : std::as_const(made)) { t->selected = true; m_selected.append(t); }
    regroup(made);
    snapshotCommit("Extrude");
    emit message(QString("Extruded %1 along the shape's plane normal").arg(Units::format(dist)));
    update();
}

// The clipboard is the system one, so objects copy between drawing windows (and between
// two running copies of Sketch3D). The source tag tells a paste whether it came from this canvas.
static int s_pasteCount = 0;
static const char *kClipItems = "application/x-sketch3d-items";
static const char *kClipSource = "application/x-sketch3d-source";

static QByteArray clipTag(const Canvas *c)
{
    return QByteArray::number(QCoreApplication::applicationPid()) + ':' + QByteArray::number(quintptr(c));
}

bool Canvas::hasClipboard() const
{
    const QMimeData *md = QGuiApplication::clipboard()->mimeData();
    return md && md->hasFormat(kClipItems);
}

void Canvas::copySelection()
{
    if (m_selected.isEmpty())
        return;
    QJsonArray a;
    for (const SketchItem *it : std::as_const(m_selected))
        a.append(it->toJson());
    auto *md = new QMimeData;
    md->setData(kClipItems, QJsonDocument(a).toJson(QJsonDocument::Compact));
    md->setData(kClipSource, clipTag(this));
    QGuiApplication::clipboard()->setMimeData(md);
    s_pasteCount = 0;
    emit message(QString("Copied %1 object(s)").arg(m_selected.size()));
}

void Canvas::pasteClipboard()
{
    const QMimeData *md = QGuiApplication::clipboard()->mimeData();
    if (!md || !md->hasFormat(kClipItems))
        return;
    const QJsonArray a = QJsonDocument::fromJson(md->data(kClipItems)).array();
    const Frame fr = planeFrame();
    // a paste in the window it was copied from steps aside; the first paste in another window lands in place
    const int step = md->data(kClipSource) == clipTag(this) ? ++s_pasteCount : s_pasteCount++;
    const QVector3D off = (fr.U - fr.V) * float(m_gridSize * 2.0 * step);
    snapshotBegin();
    for (SketchItem *it : std::as_const(m_selected)) it->selected = false;
    m_selected.clear();
    QList<SketchItem *> pasted;
    for (const QJsonValue &v : a) {
        auto *it = new SketchItem(SketchItem::Line);
        it->fromJson(v.toObject());
        pasted << it;
        for (QVector3D &pt : it->pts)
            pt += off;
        if (it->plane < 0)
            it->plane = int(m_plane);
        it->selected = true;
        m_items.append(it);
        m_selected.append(it);
    }
    for (SketchItem *p : std::as_const(pasted))
        m_items.removeOne(p);
    regroup(pasted);
    for (SketchItem *p : std::as_const(pasted))
        m_items.append(p);
    snapshotCommit("Paste");
    emit selectionChanged();
    update();
}

void Canvas::duplicateSelection()
{
    if (m_selected.isEmpty())
        return;
    copySelection();
    pasteClipboard();
}

// ---------------------------------------------------------------------------
// text / image
// ---------------------------------------------------------------------------
void Canvas::createTextAt(const QVector3D &p)
{
    snapshotBegin();
    auto *it = new SketchItem(SketchItem::Text);
    it->pts = {p};
    it->plane = int(m_plane);
    it->text = "Text";
    it->textSize = m_defTextSize;
    it->color = m_defColor;
    m_items.append(it);
    clearSelection();
    setSelected(it, true);
    startInlineEdit(it);
    update();
}

void Canvas::insertPendingImageAt(const QVector3D &p)
{
    if (m_pendingImage.isNull()) {
        emit message("Insert Image: choose a JPG/PNG file first (File > Insert Image)");
        return;
    }
    snapshotBegin();
    auto *it = new SketchItem(SketchItem::ImageItem);
    it->pts = {p};
    it->plane = int(m_plane);
    it->image = m_pendingImage;
    const qreal iw = it->image.width(), ih = it->image.height();
    it->imageSizeMm = iw > ih ? QSizeF(80, 80 * ih / iw) : QSizeF(80 * iw / ih, 80);
    m_items.append(it);
    m_pendingImage = QImage();
    snapshotCommit("Insert Image");
    update();
}

void Canvas::startInlineEdit(SketchItem *it)
{
    if (m_snapshot.isEmpty())
        snapshotBegin();
    if (m_editor) {
        m_editor->deleteLater();
        m_editor = nullptr;
    }
    if (it->text.contains('\n')) {                       // multi-line text: use a proper editor dialog
        bool ok = false;
        const QString t = QInputDialog::getMultiLineText(this, "Edit text", "Text:", it->text, &ok);
        if (ok)
            it->text = t;
        snapshotCommit("Edit Text");
        emit selectionChanged();
        update();
        return;
    }
    m_editorItem = it;
    m_editor = new QLineEdit(this);
    m_editor->setText(it->text);
    m_editor->setFont(textFont(it));
    m_editor->setToolTip("Type \\n for a new line");
    const QRectF r = textRect(it);
    m_editor->move(int(r.left() - 2), int(r.top() - 4));
    m_editor->setMinimumWidth(180);
    m_editor->show();
    m_editor->setFocus();
    m_editor->selectAll();
    connect(m_editor, &QLineEdit::editingFinished, this, &Canvas::applyInlineEdit);
}

void Canvas::applyInlineEdit()
{
    if (!m_editor || !m_editorItem)
        return;
    QLineEdit *ed = m_editor;
    SketchItem *it = m_editorItem;
    m_editor = nullptr;
    m_editorItem = nullptr;
    it->text = ed->text().replace("\\n", "\n");
    ed->deleteLater();
    snapshotCommit("Edit Text");
    setFocus();
    update();
}

// ---------------------------------------------------------------------------
// Trim / Break / Join / Offset - all done in the entity's own plane
// ---------------------------------------------------------------------------
void Canvas::trimAt(const QPoint &s)
{
    SketchItem *it = pick(s, 9.0);
    if (!it || (it->type() != SketchItem::Line && it->type() != SketchItem::Polyline)) {
        emit message("Trim: click a line/polyline that crosses another entity");
        return;
    }
    const Frame fr = frameFor(std::max(0, it->plane));
    QVector3D ro, rd, click;
    screenRay(s, ro, rd);
    if (!rayPlane(ro, rd, it->pts[0], fr.N, click)) {
        emit message("Trim: look at the entity's plane more directly");
        return;
    }
    auto uv = [&](const QVector3D &q) { return QPointF(QVector3D::dotProduct(q, fr.U), QVector3D::dotProduct(q, fr.V)); };
    const qreal nref = QVector3D::dotProduct(it->pts[0], fr.N);
    const QPointF cuv = uv(click);
    const QList<QVector3D> orig = it->pts;

    int bestSeg = -1;
    qreal bestT = 0, bestDist = 1e18;
    for (int i = 0; i + 1 < orig.size(); ++i) {
        const QLineF seg(uv(orig[i]), uv(orig[i + 1]));
        if (seg.length() < 1e-9)
            continue;
        for (const SketchItem *o : std::as_const(m_items)) {
            if (o == it)
                continue;
            for (const auto &st : strips(o)) {
                bool coplanar = true;
                for (const QVector3D &q : st)
                    coplanar = coplanar && std::fabs(QVector3D::dotProduct(q, fr.N) - nref) < 0.01;
                if (!coplanar)
                    continue;
                for (int k = 0; k + 1 < st.size(); ++k) {
                    qreal t;
                    if (segIntersect(seg, QLineF(uv(st[k]), uv(st[k + 1])), &t)) {
                        const QPointF ip = seg.p1() + t * (seg.p2() - seg.p1());
                        const qreal d = QLineF(ip, cuv).length();
                        if (d < bestDist) {
                            bestDist = d;
                            bestSeg = i;
                            bestT = t;
                        }
                    }
                }
            }
        }
    }
    if (bestSeg < 0) {
        emit message("Trim: no crossing entity found in the same plane");
        return;
    }
    const QVector3D A = orig[bestSeg], B = orig[bestSeg + 1];
    const QVector3D P = A + (B - A) * float(bestT);
    // parameter of the click along the clicked segment
    qreal tc;
    distToSegment(cuv, QLineF(uv(A), uv(B)), &tc);

    QList<QVector3D> keep;
    if (tc < bestT) {                              // clicked the start side -> remove it
        keep.append(P);
        for (int i = bestSeg + 1; i < orig.size(); ++i)
            keep.append(orig[i]);
    } else {                                       // clicked the end side -> remove it
        for (int i = 0; i <= bestSeg; ++i)
            keep.append(orig[i]);
        keep.append(P);
    }
    if (keep.size() < 2 || (keep.size() == 2 && (keep[0] - keep[1]).length() < 1e-6))
        return;

    const QColor col = it->color;
    const double lw = it->width;
    const int ls = it->lineStyle, grp = it->group;
    const int pl = it->plane;
    if (!m_snapshotHeld)
        snapshotBegin();
    m_snapshotHeld = false;
    m_items.removeOne(it);
    m_hover = nullptr;
    delete it;
    clearSelection();
    auto *n = new SketchItem(keep.size() == 2 ? SketchItem::Line : SketchItem::Polyline);
    n->pts = keep;
    n->color = col;
    n->width = lw;
    n->lineStyle = ls;
    n->group = grp;
    n->plane = pl;
    m_items.append(n);
    snapshotCommit("Trim");
    update();
}

void Canvas::breakAt(const QPoint &s)
{
    SketchItem *it = pick(s, 9.0);
    if (it && it->type() == SketchItem::Rect && it->extrude.isNull()) {
        const QList<QVector3D> keep = it->pts;
        snapshotBegin();                               // undo restores the rectangle
        it = rectToPolyline(it, keep);
        m_snapshotHeld = true;
    }
    if (!it || (it->type() != SketchItem::Line && it->type() != SketchItem::Polyline)) {
        emit message("Break: click a line, polyline or rectangle");
        return;
    }
    QVector3D ro, rd;
    screenRay(s, ro, rd);
    const QList<QVector3D> orig = it->pts;
    int seg = -1;
    qreal bt = 0, bd = 1e18;
    for (int i = 0; i + 1 < orig.size(); ++i) {
        qreal t;
        const qreal d = segRayClosest(orig[i], orig[i + 1], ro, rd, &t);
        if (d < bd) {
            bd = d;
            seg = i;
            bt = t;
        }
    }
    if (seg < 0 || bt < 1e-3 || bt > 1.0 - 1e-3) {
        if (m_snapshotHeld) {
            m_snapshotHeld = false;
            snapshotCommit("Rectangle to Polyline");
        }
        emit message("Break: click in the middle of a segment");
        return;
    }
    const QVector3D P = orig[seg] + (orig[seg + 1] - orig[seg]) * float(bt);
    QList<QVector3D> c1, c2;
    const bool closed = it->type() == SketchItem::Polyline && orig.size() >= 4 &&
                        (orig.first() - orig.last()).length() < 1e-3f;
    if (closed) {                                      // a closed shape opens at the click: one open chain
        c1.append(P);
        for (int i = seg + 1; i < orig.size() - 1; ++i) c1.append(orig[i]);
        for (int i = 0; i <= seg; ++i) c1.append(orig[i]);
        c1.append(P);
    } else {
        for (int i = 0; i <= seg; ++i) c1.append(orig[i]);
        c1.append(P);
        c2.append(P);
        for (int i = seg + 1; i < orig.size(); ++i) c2.append(orig[i]);
    }

    const QColor col = it->color;
    const double lw = it->width;
    const int ls = it->lineStyle, grp = it->group;
    const int pl = it->plane;
    snapshotBegin();
    m_items.removeOne(it);
    m_hover = nullptr;
    delete it;
    clearSelection();
    for (const QList<QVector3D> &chain : {c1, c2}) {
        if (chain.size() < 2)
            continue;
        auto *n = new SketchItem(chain.size() == 2 ? SketchItem::Line : SketchItem::Polyline);
        n->pts = chain;
        n->color = col;
        n->width = lw;
        n->lineStyle = ls;
        n->group = grp;
        n->plane = pl;
        m_items.append(n);
    }
    snapshotCommit("Break");
    update();
}

void Canvas::joinClick(SketchItem *it)
{
    if (!it || it->type() != SketchItem::Line) {
        emit message("Join: pick a straight line");
        return;
    }
    if (!m_joinFirst) {
        m_joinFirst = it;
        emit message("Join: now pick the second line");
        return;
    }
    if (m_joinFirst == it)
        return;
    SketchItem *f = m_joinFirst;
    m_joinFirst = nullptr;
    const Frame fr = frameFor(std::max(0, f->plane));
    const qreal nref = QVector3D::dotProduct(f->pts[0], fr.N);
    for (const QVector3D &q : {f->pts[1], it->pts[0], it->pts[1]})
        if (std::fabs(QVector3D::dotProduct(q, fr.N) - nref) > 0.01) {
            emit message("Join: the two lines are not in the same plane");
            return;
        }
    auto uv = [&](const QVector3D &q) { return QPointF(QVector3D::dotProduct(q, fr.U), QVector3D::dotProduct(q, fr.V)); };
    const QPointF a1 = uv(f->pts[0]), a2 = uv(f->pts[1]), b1 = uv(it->pts[0]), b2 = uv(it->pts[1]);
    const QPointF r = a2 - a1, sdir = b2 - b1;
    const qreal den = r.x() * sdir.y() - r.y() * sdir.x();
    if (std::fabs(den) < 1e-9) {
        emit message("Join: lines are parallel");
        return;
    }
    const QPointF dp = b1 - a1;
    const qreal t = (dp.x() * sdir.y() - dp.y() * sdir.x()) / den;
    const QPointF ip = a1 + t * r;
    const QPointF u0 = uv(f->pts[0]);
    const QVector3D ip3 = f->pts[0] + fr.U * float(ip.x() - u0.x()) + fr.V * float(ip.y() - u0.y());
    auto far = [&](const QVector3D &a, const QVector3D &b) {
        return (QLineF(uv(a), ip).length() > QLineF(uv(b), ip).length()) ? a : b;
    };
    const QVector3D aFar = far(f->pts[0], f->pts[1]);
    const QVector3D bFar = far(it->pts[0], it->pts[1]);
    const QColor col = f->color;
    const double lw = f->width;
    const int ls = f->lineStyle, grp = f->group;
    const int pl = f->plane;

    snapshotBegin();
    m_items.removeOne(f);
    m_items.removeOne(it);
    m_hover = nullptr;
    delete f;
    delete it;
    clearSelection();
    auto *n = new SketchItem(SketchItem::Polyline);
    n->pts = {aFar, ip3, bFar};
    n->color = col;
    n->width = lw;
    n->lineStyle = ls;
    n->group = grp;
    n->plane = pl;
    m_items.append(n);
    snapshotCommit("Join");
    update();
}

// ---------------------------------------------------------------------------
// Offset: pick an entity, then the cursor picks the side (live preview).
// Distance = the Offset distance (type a number + Enter to change it) or, with T, through the cursor.
// Polylines get true mitred corners at exactly the offset distance; closed shapes stay closed.
// ---------------------------------------------------------------------------
void Canvas::setOffsetDistance(double d)
{
    if (d <= 0 || std::fabs(d - m_offsetDist) < 1e-12)
        return;
    m_offsetDist = d;
    emit offsetDistanceChanged(d);
    if (m_tool == Tool::Offset) {
        updateOffsetPreview();
        emitHint();
        update();
    }
}

SketchItem *Canvas::offsetShape(const SketchItem *it, const QPoint &s, double d, QString *why,
                                double *usedDist) const
{
    auto fail = [&](const QString &m) -> SketchItem * {
        if (why)
            *why = m;
        return nullptr;
    };
    if (!it || it->pts.isEmpty())
        return fail("Offset: click a line, polyline, rectangle, circle, arc or curve");
    const Frame fr = frameFor(std::max(0, it->plane));
    QVector3D ro, rd, click;
    screenRay(s, ro, rd);
    if (!rayPlane(ro, rd, it->pts[0], fr.N, click))
        return fail("Offset: look at the entity's plane more directly");

    const QVector3D ref = it->pts[0];
    auto uv = [&](const QVector3D &q) { return QPointF(QVector3D::dotProduct(q, fr.U), QVector3D::dotProduct(q, fr.V)); };
    const QPointF u0 = uv(ref);
    auto from = [&](const QPointF &q) { return ref + fr.U * float(q.x() - u0.x()) + fr.V * float(q.y() - u0.y()); };
    auto nrm = [](QPointF v) { const qreal l = std::hypot(v.x(), v.y()); return l > 1e-12 ? v / l : QPointF(); };
    auto cross = [](const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); };
    auto segDist = [](const QPointF &p, const QPointF &a, const QPointF &b) {
        const QPointF ab = b - a;
        const qreal L2 = QPointF::dotProduct(ab, ab);
        const qreal t = L2 > 1e-18 ? std::clamp(QPointF::dotProduct(p - a, ab) / L2, 0.0, 1.0) : 0.0;
        const QPointF q = a + ab * t;
        return std::hypot(p.x() - q.x(), p.y() - q.y());
    };
    const QPointF w = uv(click);
    const bool through = d < 0;
    auto tooSmall = [&] { return fail("Offset: move the cursor off the entity to choose the side"); };

    SketchItem *n = nullptr;
    // open / closed point chain in plane coordinates -> mitred offset chain
    auto offsetChain = [&](QVector<QPointF> P, bool closed) -> QVector<QPointF> {
        QVector<QPointF> Q;
        for (const QPointF &q : P)                // drop repeated points
            if (Q.isEmpty() || std::hypot(q.x() - Q.last().x(), q.y() - Q.last().y()) > 1e-9)
                Q.append(q);
        if (closed && Q.size() > 1 && std::hypot(Q.first().x() - Q.last().x(), Q.first().y() - Q.last().y()) < 1e-6)
            Q.removeLast();
        P = Q;
        const int m = P.size();
        if (m < 2 || (closed && m < 3))
            return {};
        const int segs = closed ? m : m - 1;
        auto A = [&](int i) { return P[i % m]; };
        auto B = [&](int i) { return P[(i + 1) % m]; };
        // side and (through) distance from the nearest segment
        int best = 0;
        qreal bd = 1e300;
        for (int i = 0; i < segs; ++i) {
            const qreal dd = segDist(w, A(i), B(i));
            if (dd < bd) {
                bd = dd;
                best = i;
            }
        }
        const qreal side = cross(B(best) - A(best), w - A(best)) >= 0 ? 1.0 : -1.0;
        if (through)
            d = bd;
        if (d < 1e-6)
            return {};
        const qreal sd = side * d;
        auto lft = [&](int i) { const QPointF t = nrm(B(i) - A(i)); return QPointF(-t.y(), t.x()); };
        auto vtx = [&](int i0, int i1, const QPointF &corner) {   // corner between segments i0 -> i1
            const QPointF p1 = A(i0) + lft(i0) * sd, d1 = B(i0) - A(i0);
            const QPointF p2 = A(i1) + lft(i1) * sd, d2 = B(i1) - A(i1);
            const qreal den = cross(d1, d2);
            if (std::fabs(den) < 1e-12 * std::hypot(d1.x(), d1.y()) * std::hypot(d2.x(), d2.y()))
                return corner + lft(i1) * sd;     // straight through
            const QPointF x = p1 + d1 * (cross(p2 - p1, d2) / den);
            const QPointF mv = x - corner;
            if (std::hypot(mv.x(), mv.y()) > 25.0 * std::fabs(d))   // needle-sharp spike: bevel
                return corner + nrm(lft(i0) + lft(i1)) * sd;
            return x;
        };
        QVector<QPointF> out;
        if (closed) {
            for (int j = 0; j < m; ++j)
                out.append(vtx((j - 1 + segs) % segs, j, P[j]));
            out.append(out.first());
        } else {
            out.append(P[0] + lft(0) * sd);
            for (int j = 1; j < m - 1; ++j)
                out.append(vtx(j - 1, j, P[j]));
            out.append(P[m - 1] + lft(segs - 1) * sd);
        }
        return out;
    };

    switch (it->type()) {
    case SketchItem::Line: {
        if (it->pts.size() < 2)
            return fail("Offset: invalid line");
        const QPointF a = uv(it->pts[0]), b = uv(it->pts[1]);
        const QPointF dir = nrm(b - a);
        if (dir.isNull())
            return fail("Offset: zero-length line");
        QPointF nm(-dir.y(), dir.x());
        const qreal sgn = cross(dir, w - a);
        if (sgn < 0)
            nm = -nm;
        if (through)
            d = std::fabs(sgn);
        if (d < 1e-6)
            return tooSmall();
        n = new SketchItem(SketchItem::Line);
        n->pts = {from(a + nm * d), from(b + nm * d)};
        break;
    }
    case SketchItem::Circle:
    case SketchItem::Arc: {
        QVector3D cen3;
        qreal r = 0;
        if (it->type() == SketchItem::Circle) {
            if (it->pts.size() < 2)
                return fail("Offset: invalid circle");
            cen3 = it->pts[0];
            r = (it->pts[1] - it->pts[0]).length();
        } else {
            qreal a0, sw;
            QVector3D e1, e2;
            if (it->pts.size() < 3 || !arcData(it->pts[0], it->pts[1], it->pts[2], cen3, r, a0, sw, e1, e2))
                return fail("Offset: invalid arc");
        }
        const QPointF c = uv(cen3);
        const qreal dc = std::hypot(w.x() - c.x(), w.y() - c.y());
        const bool inside = dc < r;
        if (through)
            d = std::fabs(dc - r);
        if (d < 1e-6)
            return tooSmall();
        const qreal nr = r + (inside ? -d : d);
        if (nr <= 1e-6)
            return fail(QString("Offset: %1 is too large for this radius (%2)").arg(Units::format(d), Units::format(r)));
        n = new SketchItem(it->type());
        for (const QVector3D &q : it->pts.mid(it->type() == SketchItem::Circle ? 1 : 0)) {
            const QVector3D v = q - cen3;
            n->pts.append(cen3 + (v.length() > 1e-9 ? v.normalized() : fr.U) * float(nr));
        }
        if (it->type() == SketchItem::Circle)
            n->pts.prepend(it->pts[0]);
        break;
    }
    case SketchItem::Rect: {
        const QVector<QVector3D> c = rectCorners(it);
        if (c.size() < 4)
            return fail("Offset: invalid rectangle");
        const QRectF rr = QRectF(uv(c[0]), uv(c[2])).normalized();
        const bool inside = rr.contains(w);
        if (through) {
            if (inside)
                d = std::min({w.x() - rr.left(), rr.right() - w.x(), w.y() - rr.top(), rr.bottom() - w.y()});
            else
                d = std::max({rr.left() - w.x(), w.x() - rr.right(), rr.top() - w.y(), w.y() - rr.bottom()});
        }
        if (d < 1e-6)
            return tooSmall();
        const QRectF r2 = inside ? rr.adjusted(d, d, -d, -d) : rr.adjusted(-d, -d, d, d);
        if (r2.width() <= 1e-6 || r2.height() <= 1e-6)
            return fail(QString("Offset: %1 is too large for this rectangle").arg(Units::format(d)));
        n = new SketchItem(SketchItem::Rect);
        n->pts = {from(r2.topLeft()), from(r2.bottomRight())};
        break;
    }
    case SketchItem::Polyline:
    case SketchItem::Curve: {
        QVector<QPointF> P;
        bool closed = false;
        if (it->type() == SketchItem::Polyline) {
            for (const QVector3D &q : it->pts)
                P.append(uv(q));
            closed = closedShape(it);
        } else {                                   // curve: offset its fine tessellation
            const bool fine = m_fine;
            m_fine = true;
            const QList<QVector<QVector3D>> st = stripsBase(it);
            m_fine = fine;
            if (st.isEmpty())
                return fail("Offset: invalid curve");
            for (const QVector3D &q : st.first())
                P.append(uv(q));
        }
        const QVector<QPointF> out = offsetChain(P, closed);
        if (out.size() < 2)
            return d < 1e-6 ? tooSmall() : fail("Offset: this polyline has no length");
        n = new SketchItem(SketchItem::Polyline);
        for (const QPointF &q : out)
            n->pts.append(from(q));
        break;
    }
    default:
        return fail("Offset supports line / polyline / rectangle / circle / arc / curve");
    }
    n->color = it->color;
    n->width = it->width;
    n->lineStyle = it->lineStyle;
    n->plane = it->plane;
    if (usedDist)
        *usedDist = d;
    return n;
}

void Canvas::updateOffsetPreview()
{
    m_ghosts.clear();
    m_marks.clear();
    if (m_tool != Tool::Offset || !offsetSourceValid() || m_lastMouse.isNull())
        return;
    double d = m_offsetThrough ? -1.0 : m_offsetDist;
    double typed = 0;
    const bool ok = Units::parse(m_offsetTyped, typed);
    if (!m_offsetThrough && ok && typed > 0)
        d = typed;
    double used = 0;
    if (SketchItem *n = offsetShape(m_offsetSrc, m_lastMouse, d, nullptr, &used)) {
        m_ghosts = strips(n);
        m_ghostExtend = true;
        m_offsetShown = used;
        delete n;
    }
}

void Canvas::offsetCommit(const QPoint &s, double d)
{
    if (!offsetSourceValid())
        return;
    QString why;
    double used = 0;
    SketchItem *n = offsetShape(m_offsetSrc, s, d, &why, &used);
    if (!n) {
        emit message(why);
        return;
    }
    clearSelection();
    snapshotBegin();
    m_items.append(n);
    snapshotCommit("Offset");
    m_offsetSrc = nullptr;
    m_offsetTyped.clear();
    m_ghosts.clear();
    emit message(QString("Offset copy made at %1  -  click the next object (Enter repeats on the new copy)")
                     .arg(Units::format(used)));
    // the undo command rebuilds the item list, so the copy is the last item now (not n)
    m_lastOffsetMade = m_items.isEmpty() ? nullptr : m_items.last();
    emitHint();
    update();
}

void Canvas::offsetClick(SketchItem *it, const QPoint &s)
{
    if (!offsetSourceValid()) {
        m_offsetSrc = nullptr;
        if (!it) {
            emit message("Offset: click a line, polyline, rectangle, circle, arc or curve");
            return;
        }
        if (it->type() == SketchItem::Text || it->type() == SketchItem::Dimension ||
            it->type() == SketchItem::ImageItem || it->type() == SketchItem::Hatch || !it->loops.isEmpty()) {
            emit message("Offset supports line / polyline / rectangle / circle / arc / curve");
            return;
        }
        m_offsetSrc = it;
        clearSelection();
        setSelected(it, true);
        updateOffsetPreview();
        emitHint();
        update();
        return;
    }
    // second click = the side (and, in through mode, the distance)
    double d = m_offsetThrough ? -1.0 : m_offsetDist;
    double typed = 0;
    const bool ok = Units::parse(m_offsetTyped, typed);
    if (!m_offsetThrough && ok && typed > 0) {
        setOffsetDistance(typed);
        d = typed;
    }
    offsetCommit(s, d);
}

// ---------------------------------------------------------------------------
// serialization
// ---------------------------------------------------------------------------
QByteArray Canvas::serialize() const
{
    QJsonObject root;
    root["app"] = "Sketch3D";
    root["version"] = 3;
    root["pageW"] = m_pageSize.width();
    root["pageH"] = m_pageSize.height();
    root["gridSize"] = m_gridSize;
    root["plane"] = int(m_plane);
    root["planeOffset"] = m_planeOffset;
    root["view"] = int(m_view);
    {
        auto quat = [](const QQuaternion &q) { return QJsonArray{q.scalar(), q.x(), q.y(), q.z()}; };
        const QVector3D eye = m_isoQ.rotatedVector(QVector3D(0, 0, 1));    // yaw / pitch for older versions
        root["yaw"] = std::atan2(eye.y(), eye.x()) * 180.0 / PI;
        root["pitch"] = std::asin(std::clamp<qreal>(eye.z(), -1, 1)) * 180.0 / PI;
        root["camQ"] = quat(m_isoQ);
        if (customOrientation())
            root["viewFrame"] = quat(m_frame);
        if (m_hasHome)
            root["homeQ"] = quat(m_homeQ);
    }
    root["zoom"] = m_zoom;
    root["tx"] = m_target.x();
    root["ty"] = m_target.y();
    root["tz"] = m_target.z();
    root["defColor"] = m_defColor.name();
    root["defWidth"] = m_defWidth;
    root["defTextSize"] = m_defTextSize;
    root["units"] = m_set.units;

    QJsonArray arr;
    for (const SketchItem *it : m_items)
        arr.append(it->toJson());
    root["items"] = arr;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool Canvas::deserialize(const QByteArray &data, bool restoreCamera)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();

    // remember selection (by index) so undo/redo/commit keep what you were working on
    QList<int> selIdx;
    int editIdx = m_editItem ? int(m_items.indexOf(m_editItem)) : -1;
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i]->selected)
            selIdx << i;

    deleteItems();
    m_pageSize = QSizeF(root["pageW"].toDouble(210), root["pageH"].toDouble(297));
    const int docPlane = std::clamp(root["plane"].toInt(0), 0, 3);

    if (restoreCamera) {
        m_gridSize = root["gridSize"].toDouble(5.0);
        m_plane = Plane(docPlane);
        m_planeOffset = root["planeOffset"].toDouble(0.0);
        m_view = View(std::clamp(root["view"].toInt(0), 0, 6));
        auto quat = [&root](const char *key, QQuaternion &out) {
            const QJsonArray a = root[key].toArray();
            if (a.size() != 4)
                return false;
            const QQuaternion q(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()), float(a[3].toDouble()));
            if (q.length() < 0.5f)
                return false;
            out = q.normalized();
            return true;
        };
        if (!quat("camQ", m_isoQ)) {        // older files: yaw / pitch turntable camera
            const qreal y = root["yaw"].toDouble(45.0) * PI / 180.0, p = root["pitch"].toDouble(35.264) * PI / 180.0;
            m_isoQ = camFromDirUp(QVector3D(std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)),
                                  QVector3D(0, 0, 1), QQuaternion());
        }
        if (!quat("viewFrame", m_frame))
            m_frame = QQuaternion();
        m_hasHome = quat("homeQ", m_homeQ);
        m_zoom = root["zoom"].toDouble(3.0);
        m_target = QVector3D(root["tx"].toDouble(0), root["ty"].toDouble(0), root["tz"].toDouble(0));
        m_defColor = QColor(root["defColor"].toString("#d8e2f0"));
        m_defWidth = root["defWidth"].toDouble(0.35);
        m_defTextSize = root["defTextSize"].toDouble(3.5);
        if (root.contains("units") && root["units"].toInt() != m_set.units) {   // the drawing's own units
            m_set.units = std::clamp(root["units"].toInt(), 0, int(Units::SystemCount) - 1);
            Units::setOptions({m_set.units, m_set.unitDecimals, m_set.unitFrac});
            emit settingsChanged();
        }
        updateCamera();
    }

    m_ppSet.clear();                       // chosen faces are item positions: not valid in another drawing state
    const QJsonArray arr = root["items"].toArray();
    for (const QJsonValue &v : arr) {
        auto *it = new SketchItem(SketchItem::Line);
        it->fromJson(v.toObject());
        if (it->plane < 0)
            it->plane = docPlane;          // legacy files: fall back to the document plane
        m_items.append(it);
    }
    m_pending.clear();
    m_guides.clear();
    if (selIdx.size() && selIdx.last() < m_items.size() && !restoreCamera) {
        for (int i : selIdx) {
            m_items[i]->selected = true;
            m_selected.append(m_items[i]);
        }
    }
    if (editIdx >= 0 && editIdx < m_items.size() && !restoreCamera)
        m_editItem = m_items[editIdx];
    if (restoreCamera) {
        emit viewChanged();
        emit planeChanged();
    }
    emit selectionChanged();
    emit contentChanged();
    update();
    return true;
}

bool Canvas::saveFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(serialize());
    return true;
}

bool Canvas::loadFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return deserialize(f.readAll(), true);
}

// ---------------------------------------------------------------------------
// export
// ---------------------------------------------------------------------------
static QPixmap renderScaled(Canvas *c, int scale, const std::function<void(QPainter &)> &draw)
{
    QPixmap pm(c->size() * scale);
    pm.fill(Qt::black);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(scale, scale);
    draw(p);
    return pm;
}

bool Canvas::exportImage(const QString &path, const char *format)
{
    m_exporting = true;
    const QPixmap pm = renderScaled(this, 2, [this](QPainter &p) { drawScene(p); });
    m_exporting = false;
    update();
    return pm.save(path, format);
}

// (PDF: a vector drawing to scale, paintPlot() in canvas_print.cpp and the pages in printing.cpp)

bool Canvas::exportDxf(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream o(&f);
    o.setRealNumberNotation(QTextStream::FixedNotation);
    o.setRealNumberPrecision(5);
    o << "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n";
    o << "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLTYPE\n70\n6\n"
         "0\nLTYPE\n2\nCONTINUOUS\n70\n0\n3\nSolid line\n72\n65\n73\n0\n40\n0.0\n"
         "0\nLTYPE\n2\nDASHED\n70\n0\n3\nDashed\n72\n65\n73\n2\n40\n9.0\n49\n6.0\n49\n-3.0\n"
         "0\nLTYPE\n2\nDOT\n70\n0\n3\nDot\n72\n65\n73\n2\n40\n3.0\n49\n0.0\n49\n-3.0\n"
         "0\nLTYPE\n2\nDASHDOT\n70\n0\n3\nDash dot\n72\n65\n73\n4\n40\n12.0\n49\n6.0\n49\n-2.0\n49\n0.0\n49\n-4.0\n"
         "0\nLTYPE\n2\nCENTER\n70\n0\n3\nCenter\n72\n65\n73\n4\n40\n20.0\n49\n14.0\n49\n-3.0\n49\n3.0\n49\n-0.0\n"
         "0\nLTYPE\n2\nHIDDEN\n70\n0\n3\nHidden\n72\n65\n73\n2\n40\n6.0\n49\n3.0\n49\n-3.0\n"
         "0\nENDTAB\n0\nENDSEC\n";
    o << "0\nSECTION\n2\nENTITIES\n";
    static const char *ltNames[] = {"CONTINUOUS", "DASHED", "DOT", "DASHDOT", "CENTER", "HIDDEN"};
    QString props;
    auto line = [&](const QVector3D &a, const QVector3D &b) {
        o << "0\nLINE\n8\nSKETCH\n" << props << "10\n" << a.x() << "\n20\n" << a.y() << "\n30\n" << a.z()
          << "\n11\n" << b.x() << "\n21\n" << b.y() << "\n31\n" << b.z() << "\n";
    };
    auto text = [&](const QVector3D &p, double h, const QString &t) {
        o << "0\nTEXT\n8\nSKETCH\n10\n" << p.x() << "\n20\n" << p.y() << "\n30\n" << p.z()
          << "\n40\n" << h << "\n1\n" << t << "\n";
    };
    for (const SketchItem *it : std::as_const(m_items)) {
        props = QString("6\n%1\n370\n%2\n420\n%3\n").arg(ltNames[std::clamp(it->lineStyle, 0, 5)])
                    .arg(std::clamp(int(it->width * 100 + 0.5), 0, 211))
                    .arg(it->color.rgb() & 0xFFFFFF);
        if (it->type() == SketchItem::Text) {
            text(it->pts.value(0), it->textSize, it->text);
            continue;
        }
        if (it->type() == SketchItem::ImageItem)
            continue;
        if (it->type() != SketchItem::Hatch)                // a hatch boundary is other objects' linework
            for (const auto &s : strips(it))
                for (int i = 0; i + 1 < s.size(); ++i)
                    line(s[i], s[i + 1]);
        if (hasHatch(it)) {                                  // pattern lines, exploded, on their own layer
            const auto loops = hatchLoops(it);
            bool dense = false;
            const auto segs = hatchSegments(it, loops, hatchSpacing(it, loops), 200000, &dense);
            props = QString("6\nCONTINUOUS\n370\n%1\n420\n%2\n").arg(std::clamp(int(it->width * 100 + 0.5), 0, 211))
                        .arg(it->color.rgb() & 0xFFFFFF);
            for (const auto &sg : segs) {
                if (sg.first == sg.second)
                    o << "0\nPOINT\n8\nHATCH\n" << props << "10\n" << sg.first.x() << "\n20\n" << sg.first.y()
                      << "\n30\n" << sg.first.z() << "\n";
                else
                    o << "0\nLINE\n8\nHATCH\n" << props << "10\n" << sg.first.x() << "\n20\n" << sg.first.y() << "\n30\n"
                      << sg.first.z() << "\n11\n" << sg.second.x() << "\n21\n" << sg.second.y() << "\n31\n"
                      << sg.second.z() << "\n";
            }
        }
        if (it->type() == SketchItem::Dimension) {
            const DimGeom g = dimGeom(it);
            if (g.ok)
                text((g.d1 + g.d2) * 0.5f, 2.5, Units::formatDim(g.value));
        }
    }
    o << "0\nENDSEC\n0\nEOF\n";
    return true;
}
