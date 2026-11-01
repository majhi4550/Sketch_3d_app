// Sketch3D --selftest <folder>
// Builds a small cabinet carcass the way the 3D tools are meant to be used (Box on faces, typed sizes,
// Mirror, Press/Pull, names, cut list), with synthetic mouse / key events sent to the canvas only.
// Writes PASS / FAIL lines and the cut list to report.txt, plus screenshots, into the folder.
#include "mainwindow.h"
#include "nesting.h"
#include "units.h"
#include "canvas_util.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStatusBar>
#include <QAction>
#include <QAbstractButton>
#include <QTimer>
#include <QMessageBox>
#include <QToolButton>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTextStream>

#include <algorithm>
#include <cmath>

int runSelfTest(const QString &outDir)
{
    QDir().mkpath(outDir);
    auto *w = new MainWindow;
    w->resize(1500, 950);
    w->show();
    Canvas *c = w->canvas();
    QStringList log;
    int fails = 0;
    auto check = [&](bool ok, const QString &what) {
        log << QString(ok ? "PASS  " : "FAIL  ") + what;
        if (!ok)
            ++fails;
    };
    auto pump = [] { QApplication::processEvents(); };
    auto mouse = [&](QEvent::Type t, const QPointF &p, Qt::MouseButton b, Qt::MouseButtons bs) {
        QMouseEvent ev(t, p, c->mapToGlobal(p), b, bs, Qt::NoModifier);
        QApplication::sendEvent(c, &ev);
    };
    auto move = [&](const QPointF &p) { mouse(QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton); pump(); };
    auto click = [&](const QPointF &p) {
        move(p);
        mouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
        pump();
    };
    auto key = [&](int k, const QString &txt = QString()) {
        QKeyEvent pr(QEvent::KeyPress, k, Qt::NoModifier, txt);
        QApplication::sendEvent(c, &pr);
        QKeyEvent rl(QEvent::KeyRelease, k, Qt::NoModifier, txt);
        QApplication::sendEvent(c, &rl);
        pump();
    };
    auto type = [&](const QString &s) {
        for (const QChar ch : s) {
            int k = Qt::Key_Period;
            if (ch.isDigit()) k = Qt::Key_0 + ch.digitValue();
            else if (ch == QLatin1Char('+')) k = Qt::Key_Plus;
            else if (ch == QLatin1Char('-')) k = Qt::Key_Minus;
            else if (ch == QLatin1Char('*')) k = Qt::Key_Asterisk;
            else if (ch == QLatin1Char('/')) k = Qt::Key_Slash;
            else if (ch == QLatin1Char('(')) k = Qt::Key_ParenLeft;
            else if (ch == QLatin1Char(')')) k = Qt::Key_ParenRight;
            key(k, QString(ch));
        }
    };
    auto shot = [&](const QString &name) {
        c->update();
        // let the 60 ms timers (selection bar) catch up, as they would between a user's clicks
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 80)
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        c->grab().save(outDir + "/" + name + ".png");
    };
    // a point slightly inside a face, next to one of its corners (so the corner snaps)
    auto nearCorner = [&](const QVector3D &corner, const QVector3D &towards) {
        const QPointF a = c->screenOf(corner), b = c->screenOf(towards);
        QPointF d = b - a;
        const qreal l = std::hypot(d.x(), d.y());
        return l > 1e-6 ? a + d / l * 4.0 : a;
    };
    auto bounds = [](const SketchItem *it, QVector3D &mn, QVector3D &mx) {
        mn = QVector3D(1e9f, 1e9f, 1e9f);
        mx = -mn;
        for (const QVector3D &p : it->pts)
            for (const QVector3D &q : {p, p + it->extrude}) {
                mn = QVector3D(std::min(mn.x(), q.x()), std::min(mn.y(), q.y()), std::min(mn.z(), q.z()));
                mx = QVector3D(std::max(mx.x(), q.x()), std::max(mx.y(), q.y()), std::max(mx.z(), q.z()));
            }
    };
    auto boxIs = [&](const SketchItem *it, QVector3D lo, QVector3D hi, const QString &what) {
        if (!it || it->extrude.isNull()) {
            check(false, what + ": no board");
            return;
        }
        QVector3D mn, mx;
        bounds(it, mn, mx);
        const bool ok = (mn - lo).length() < 0.05f && (mx - hi).length() < 0.05f;
        check(ok, QString("%1: (%2, %3, %4) .. (%5, %6, %7)").arg(what).arg(mn.x()).arg(mn.y()).arg(mn.z())
                      .arg(mx.x()).arg(mx.y()).arg(mx.z()));
    };
    auto last = [&]() -> const SketchItem * { return c->items().isEmpty() ? nullptr : c->items().last(); };

    c->setViewDirection(QVector3D(1, -1, 0.8f), false);
    c->zoomFit();

    // 1 left side: corner on the origin, type 18 Tab 400 Enter, height 750 Enter
    c->setTool(Canvas::Tool::Box);
    click(c->screenOf(QVector3D(0, 0, 0)));
    type("18"); key(Qt::Key_Tab); type("400"); key(Qt::Key_Return);
    type("750"); key(Qt::Key_Return);
    boxIs(last(), QVector3D(0, 0, 0), QVector3D(18, 400, 750), "Box typed: left side 18 x 400 x 750");
    c->zoomFit();
    shot("01_side");

    // 2 top: starts on the side's top face (the box grows out of that face), 800 x 400, Enter = 18 board
    c->setTool(Canvas::Tool::Box);
    move(nearCorner(QVector3D(0, 0, 750), QVector3D(9, 200, 750)));
    shot("02a_box_face_hover");
    click(nearCorner(QVector3D(0, 0, 750), QVector3D(9, 200, 750)));
    type("800"); key(Qt::Key_Tab); type("400"); key(Qt::Key_Return);
    move(c->screenOf(QVector3D(800, 400, 900)));
    shot("02b_box_height_preview");
    key(Qt::Key_Return);
    boxIs(last(), QVector3D(0, 0, 750), QVector3D(800, 400, 768), "Box on a face + Enter = 18 mm: top");
    const SketchItem *top = last();
    check(c->plane() == Canvas::Plane::TopXY && std::fabs(c->planeOffset()) < 1e-6,
          "sketch plane is back to XY 0 after the box");
    c->zoomFit();
    shot("02_top");

    // 3 select the side by clicking its front face, mirror it about x = 400
    c->setTool(Canvas::Tool::Select);
    const SketchItem *left = c->items().value(0);
    click(c->screenOf(QVector3D(9, 0, 300)));
    check(left && left->selected && c->selectedCount() == 1, "clicking a board's face selects it");
    c->mirrorAcross(QVector3D(400, 0, 0), QVector3D(400, 100, 0), true);
    boxIs(last(), QVector3D(782, 0, 0), QVector3D(800, 400, 750), "Mirror keeps the board's thickness inside");
    const SketchItem *right = last();

    // 4 bottom: Box on the inside face of the left side, from its lower front corner
    c->setTool(Canvas::Tool::Box);
    click(nearCorner(QVector3D(18, 0, 0), QVector3D(18, 200, 375)));
    type("18"); key(Qt::Key_Tab); type("400"); key(Qt::Key_Return);
    type("764"); key(Qt::Key_Return);
    boxIs(last(), QVector3D(18, 0, 0), QVector3D(782, 400, 18), "Box on a side face (ZY plane): bottom 764 long");
    const SketchItem *bottom = last();

    // 5 Press/Pull: the top's upper face up by a typed 7 mm
    c->setTool(Canvas::Tool::PressPull);
    click(c->screenOf(QVector3D(400, 200, 768)));
    type("7"); key(Qt::Key_Return);
    top = c->items().value(1);
    boxIs(top, QVector3D(0, 0, 750), QVector3D(800, 400, 775), "Press/Pull typed: top 25 thick");

    // 6 Press/Pull with the mouse: the bottom's front face out 30 mm, then undo
    click(c->screenOf(QVector3D(400, 0, 9)));
    move(c->screenOf(QVector3D(400, -40, 9)));
    shot("06_pull_live");
    click(c->screenOf(QVector3D(400, -40, 9)));
    bottom = c->items().value(3);
    QVector3D mn, mx;
    if (bottom)
        bounds(bottom, mn, mx);
    check(bottom && std::fabs(mn.y() + 40) <= 5.01f, QString("Press/Pull by mouse (grid 5): bottom front at y = %1, aimed at -40").arg(mn.y()));
    c->undoStack()->undo();
    pump();
    bottom = c->items().value(3);
    if (bottom)
        bounds(bottom, mn, mx);
    check(bottom && std::fabs(mn.y()) < 0.05f, "undo puts the face back");

    // 7 names and the cut list
    c->setTool(Canvas::Tool::Select);
    c->selectIndices({0, 2});                              // (renaming rebuilds the item list: use positions)
    c->setSelectedName("Side");
    c->selectIndices({1});
    c->setSelectedName("Top");
    c->selectIndices({});
    const auto rows = c->cutList(false);
    auto has = [&](const QString &n, double l, double wd, double t, int q) {
        for (const auto &r : rows)
            if (r.name == n && std::fabs(r.l - l) < 0.05 && std::fabs(r.w - wd) < 0.05 && std::fabs(r.t - t) < 0.05 && r.qty == q)
                return true;
        return false;
    };
    check(rows.size() == 3, QString("cut list has 3 rows (%1)").arg(rows.size()));
    check(has("Side", 750, 400, 18, 2), "cut list: Side 750 x 400 x 18, 2 off");
    check(has("Top", 800, 400, 25, 1), "cut list: Top 800 x 400 x 25");
    check(has("Panel", 764, 400, 18, 1), "cut list: unnamed bottom 764 x 400 x 18");

    // boards found by their bounding box (the item list is rebuilt by every command)
    auto findBox = [&](QVector3D lo, QVector3D hi) -> int {
        for (int i = 0; i < c->items().size(); ++i) {
            const SketchItem *it = c->items()[i];
            if (it->extrude.isNull())
                continue;
            QVector3D a, b;
            bounds(it, a, b);
            if ((a - lo).length() < 0.05f && (b - hi).length() < 0.05f)
                return i;
        }
        return -1;
    };

    // 9 Subtract: a toe-kick notch 50 deep x 100 high out of the left side's front bottom corner.
    //   The cutter starts on the side's front face (so it is drawn in that face's plane) and goes 50 in.
    c->setTool(Canvas::Tool::Box);
    // a few pixels diagonally into the 18 mm face from its corner: on the face, and the corner snaps
    click(nearCorner(QVector3D(0, 0, 0), QVector3D(9, 0, 9)));
    const int planeAfterClick = int(c->plane());
    type("18"); key(Qt::Key_Tab); type("100");
    const int planeAfterType = int(c->plane());
    key(Qt::Key_Return);
    key(Qt::Key_Minus, "-"); type("50"); key(Qt::Key_Return);
    const int cutter = findBox(QVector3D(0, 0, 0), QVector3D(18, 50, 100));
    {
        QVector3D a0, a1;
        if (last()) bounds(last(), a0, a1);
        check(cutter >= 0, QString("Box on a front face, typed -50 goes into the board: notch cutter 18 x 50 x 100 (last box %1,%2,%3 .. %4,%5,%6; plane %7 / %8)")
                               .arg(a0.x()).arg(a0.y()).arg(a0.z()).arg(a1.x()).arg(a1.y()).arg(a1.z()).arg(planeAfterClick).arg(planeAfterType));
    }
    const int side = findBox(QVector3D(0, 0, 0), QVector3D(18, 400, 750));
    c->setTool(Canvas::Tool::Select);
    c->selectIndices({side, cutter});
    c->booleanSelection(Canvas::BoolOp::Subtract);
    const int notched = findBox(QVector3D(0, 0, 0), QVector3D(18, 400, 750));
    const SketchItem *ns = c->items().value(notched);
    check(ns && ns->type() == SketchItem::Polyline && ns->pts.size() == 7 && std::fabs(ns->extrude.length() - 18) < 0.05f,
          QString("Subtract: the side is one L-shaped board (%1 points, %2 thick)")
              .arg(ns ? ns->pts.size() : 0).arg(ns ? ns->extrude.length() : 0));
    check(findBox(QVector3D(0, 0, 0), QVector3D(18, 50, 100)) < 0, "Subtract removed the cutter");
    c->selectIndices({});
    c->setViewDirection(QVector3D(1, -1, 0.8f), false);
    c->zoomFit();
    shot("20_notch");

    // 10 Union: a 100 x 400 x 18 board set on the top, joined to it - one stepped board
    c->setTool(Canvas::Tool::Box);
    click(nearCorner(QVector3D(0, 0, 775), QVector3D(400, 200, 775)));
    type("100"); key(Qt::Key_Tab); type("400"); key(Qt::Key_Return);
    key(Qt::Key_Return);
    const int riser = findBox(QVector3D(0, 0, 775), QVector3D(100, 400, 793));
    const int topI = findBox(QVector3D(0, 0, 750), QVector3D(800, 400, 775));
    check(riser >= 0 && topI >= 0, "a board stacked on the top (Box on its face, Enter = 18)");
    c->setTool(Canvas::Tool::Select);
    c->selectIndices({topI, riser});
    c->booleanSelection(Canvas::BoolOp::Union);
    const int joined = findBox(QVector3D(0, 0, 750), QVector3D(800, 400, 793));
    check(joined >= 0 && c->selectedCount() == 1, "Union: one stepped board 800 x 400, 750 .. 793 high");

    // 11 move gizmo: the right side 50 along +X by a typed distance, then undo
    const int rs = findBox(QVector3D(782, 0, 0), QVector3D(800, 400, 750));
    c->selectIndices({rs});
    c->setGizmoMode(Canvas::GizmoMode::Move);
    move(c->gizmoGripScreen(0));
    shot("21_gizmo_move");
    click(c->gizmoGripScreen(0));
    type("50"); key(Qt::Key_Return);
    check(findBox(QVector3D(832, 0, 0), QVector3D(850, 400, 750)) >= 0, "Move gizmo, X arrow, typed 50");
    c->undoStack()->undo();
    pump();
    check(findBox(QVector3D(782, 0, 0), QVector3D(800, 400, 750)) >= 0, "undo moves it back");

    // 12 rotate gizmo: the bottom 90 degrees about Z (around its centre), stays a rectangle board
    const int bt = findBox(QVector3D(18, 0, 0), QVector3D(782, 400, 18));
    c->selectIndices({bt});
    c->setGizmoMode(Canvas::GizmoMode::Rotate);
    move(c->gizmoGripScreen(2));
    shot("22_gizmo_rotate");
    click(c->gizmoGripScreen(2));
    type("90"); key(Qt::Key_Return);
    const int rot = findBox(QVector3D(200, -182, 0), QVector3D(600, 582, 18));
    check(rot >= 0 && c->items()[rot]->type() == SketchItem::Rect, "Rotate gizmo, Z ring, typed 90: bottom turned, still a rectangle");
    c->undoStack()->undo();
    pump();
    c->setGizmoMode(Canvas::GizmoMode::Move);
    c->selectIndices({});
    const auto rows2 = c->cutList(false);
    bool sideStill = false;
    for (const auto &r : rows2)
        if (r.name == "Side" && r.qty == 2 && std::fabs(r.l - 750) < 0.05 && std::fabs(r.w - 400) < 0.05)
            sideStill = true;
    check(sideStill, "cut list after the notch: Side 750 x 400 x 18 still 2 off");

    // 13 Rotate tool: the right side 90 degrees about Z round its front corner (typed)
    {
        const int r = findBox(QVector3D(782, 0, 0), QVector3D(800, 400, 750));
        c->selectIndices({r});
        c->setTool(Canvas::Tool::Rotate);
        click(c->screenOf(QVector3D(782, 0, 0)));
        move(c->screenOf(QVector3D(600, 300, 0)));
        shot("23_rotate_tool");
        type("90"); key(Qt::Key_Return);
        const int t90 = findBox(QVector3D(382, 0, 0), QVector3D(782, 18, 750));
        check(t90 >= 0 && c->items()[t90]->type() == SketchItem::Rect, "Rotate tool: pivot on a corner, typed 90 about Z");
        check(c->tool() == Canvas::Tool::Select, "Rotate tool ends after the turn (no copy)");
        c->undoStack()->undo();
        pump();
    }
    // 14 Rotate tool about X with K (copy): the bottom stands up at its front-left corner, the original stays
    {
        const int b = findBox(QVector3D(18, 0, 0), QVector3D(782, 400, 18));
        c->selectIndices({b});
        c->setTool(Canvas::Tool::Rotate);
        click(c->screenOf(QVector3D(18, 0, 0)));
        key(Qt::Key_X, "x");
        key(Qt::Key_K, "k");
        type("90"); key(Qt::Key_Return);
        check(findBox(QVector3D(18, -18, 0), QVector3D(782, 0, 400)) >= 0 && findBox(QVector3D(18, 0, 0), QVector3D(782, 400, 18)) >= 0,
              "Rotate tool, X key + K: a copy turned 90 about X, the original kept");
        c->setMoveCopy(false);
        c->setTool(Canvas::Tool::Select);
        c->undoStack()->undo();
        pump();
    }
    // 15 quick turn: 90 about X through the selection centre (a board in the ZY plane stays a rectangle)
    {
        const int b = findBox(QVector3D(18, 0, 0), QVector3D(782, 400, 18));
        c->selectIndices({b});
        c->rotateSelection3D(90, 0);
        const int q = findBox(QVector3D(18, 191, -191), QVector3D(782, 209, 209));
        check(q >= 0 && c->items()[q]->type() == SketchItem::Rect, "Turn 90 about X (Properties / Model menu)");
        c->undoStack()->undo();
        pump();
        c->selectIndices({});
    }
    // 16 materials: the sides become "18 mm ply", the cut list carries it
    {
        c->selectIndices({findBox(QVector3D(782, 0, 0), QVector3D(800, 400, 750)), findBox(QVector3D(0, 0, 0), QVector3D(18, 400, 750))});
        c->setSelectedMaterial("18 mm ply");
        c->selectIndices({});
        bool ok = false;
        for (const auto &r : c->cutList(false))
            if (r.name == "Side" && r.material == "18 mm ply" && r.qty == 2)
                ok = true;
        check(ok, "material on two boards: cut list row Side, 18 mm ply, 2 off");
    }
    // 17 save / load round trip with names, materials, parts, a board with a notch
    {
        const QByteArray before = c->serialize();
        c->saveFile(outDir + "/roundtrip.sk3");
        c->loadFile(outDir + "/roundtrip.sk3");
        check(c->serialize() == before, "save / load round trip: identical");
    }
    // 18 undo / redo of a Union (items are rebuilt by every command)
    {
        const int a = findBox(QVector3D(782, 0, 0), QVector3D(800, 400, 750));
        const int b = findBox(QVector3D(18, 0, 0), QVector3D(782, 400, 18));
        c->selectIndices({a, b});
        const int before = int(c->items().size());
        c->booleanSelection(Canvas::BoolOp::Union);
        const int joined = int(c->items().size());
        c->undoStack()->undo();
        pump();
        const int undone = int(c->items().size());
        c->undoStack()->redo();
        pump();
        check(joined < before && undone == before && int(c->items().size()) == joined,
              QString("undo / redo of Union: %1 -> %2 -> %3 -> %4 objects").arg(before).arg(joined).arg(undone).arg(c->items().size()));
        c->undoStack()->undo();
        pump();
        c->selectIndices({});
    }
    // 19 sheet nesting (MaxRects): known answers, no overlaps, kerf and trim kept
    {
        auto noOverlap = [](const NestResult &r, double kerf, double trim) {
            for (const NestSheet &s : r.sheets) {
                for (int i = 0; i < s.places.size(); ++i) {
                    const QRectF a = s.places[i].rect;
                    if (a.left() < trim - 1e-6 || a.top() < trim - 1e-6 || a.right() > r.sheetL - trim + 1e-6 ||
                        a.bottom() > r.sheetW - trim + 1e-6)
                        return false;
                    for (int j = i + 1; j < s.places.size(); ++j)
                        if (a.adjusted(0, 0, kerf - 1e-6, kerf - 1e-6).intersects(s.places[j].rect) ||
                            s.places[j].rect.adjusted(0, 0, kerf - 1e-6, kerf - 1e-6).intersects(a))
                            return false;
                }
            }
            return true;
        };
        QList<NestPart> eight;
        for (int k = 0; k < 8; ++k)
            eight << NestPart{600, 400, "A"};
        const NestResult r8 = nestParts(eight, 2440, 1220, 4, 10, true);
        check(r8.sheets.size() == 1 && noOverlap(r8, 4, 10), QString("nesting: 8 x 600x400 on 2440x1220 -> %1 sheet").arg(r8.sheets.size()));
        QList<NestPart> nine = eight, ten, eleven;
        nine << NestPart{600, 400, "A"};
        ten = nine;
        ten << NestPart{600, 400, "A"};
        eleven = ten;
        eleven << NestPart{600, 400, "A"};
        // with grain (no turning) 4 x 2 fit; turned, a row of 4 + 6 standing in the 800 left = 10 (the best)
        const NestResult r9g = nestParts(nine, 2440, 1220, 4, 10, false);
        check(r9g.sheets.size() == 2, QString("nesting with grain: 9 x 600x400 -> 2 sheets (%1)").arg(r9g.sheets.size()));
        const NestResult r10 = nestParts(ten, 2440, 1220, 4, 10, true);
        check(r10.sheets.size() == 1 && noOverlap(r10, 4, 10), QString("nesting turned: 10 x 600x400 fit one sheet (%1)").arg(r10.sheets.size()));
        const NestResult r11 = nestParts(eleven, 2440, 1220, 4, 10, true);
        check(r11.sheets.size() == 2, QString("nesting turned: the 11th needs a 2nd sheet (%1)").arg(r11.sheets.size()));
        NestGroup g10{"Self-test: 10 x 600 x 400, parts may turn", ten, r10};
        const NestResult big = nestParts({NestPart{2500, 100, "B"}}, 2440, 1220, 4, 10, true);
        check(big.tooBig.size() == 1 && big.sheets.isEmpty(), "nesting: a 2500 long part is reported as too big");
        quint32 seed = 12345;
        auto rnd = [&seed](int lo, int hi) { seed = seed * 1664525u + 1013904223u; return lo + int((seed >> 8) % quint32(hi - lo + 1)); };
        QList<NestPart> mix;
        double area = 0;
        for (int k = 0; k < 60; ++k) {
            mix << NestPart{double(rnd(100, 1200)), double(rnd(80, 600)), QString::number(k)};
            area += mix.last().l * mix.last().w;
        }
        const NestResult rm = nestParts(mix, 2440, 1220, 4, 10, true);
        int placed = 0;
        for (const NestSheet &s : rm.sheets) placed += int(s.places.size());
        check(placed == 60 && noOverlap(rm, 4, 10) && rm.sheets.size() >= rm.lowerBound,
              QString("nesting: 60 random parts, %1 sheets (at least %2), %3% used, no overlaps")
                  .arg(rm.sheets.size()).arg(rm.lowerBound).arg(int(std::round(100 * rm.utilisation()))));
        NestGroup g{"Self-test: 60 random parts", mix, rm};
        nestingImage({g10, g}, 760).save(outDir + "/30_nesting.png");
        // the multi-pass nesting against one pass, over 300 random cut lists (cabinet parts and big panels)
        {
            int baseSheets = 0, multiSheets = 0, better = 0, worse = 0, atBound = 0, baseAtBound = 0;
            bool allOk = true;
            QElapsedTimer tm;
            qint64 multiNs = 0;
            for (int trial = 0; trial < 300; ++trial) {
                QList<NestPart> parts;
                const int n = rnd(15, 80);
                const bool panels = trial % 3 == 2;
                for (int k = 0; k < n; ++k)
                    parts << (panels ? NestPart{double(rnd(300, 2400)), double(rnd(250, 1200)), QString::number(k)}
                                     : NestPart{double(rnd(100, 1200)), double(rnd(80, 600)), QString::number(k)});
                const bool turn = trial % 4 != 3;
                const NestResult a = nestPartsSinglePass(parts, 2440, 1220, 4, 10, turn);
                tm.start();
                const NestResult b = nestParts(parts, 2440, 1220, 4, 10, turn);
                multiNs += tm.nsecsElapsed();
                int placedB = 0;
                for (const NestSheet &s : b.sheets) placedB += int(s.places.size());
                allOk = allOk && noOverlap(b, 4, 10) && placedB + int(b.tooBig.size()) == n;
                baseSheets += int(a.sheets.size());
                multiSheets += int(b.sheets.size());
                better += b.sheets.size() < a.sheets.size();
                worse += b.sheets.size() > a.sheets.size();
                atBound += b.sheets.size() <= b.lowerBound;
                baseAtBound += a.sheets.size() <= a.lowerBound;
            }
            const double ms = multiNs / 1e6 / 300;
            check(allOk && worse == 0 && multiSheets <= baseSheets,
                  QString("nesting study, 300 random cut lists: one pass %1 sheets, multi-pass %2 (%3 lists better, %4 worse); "
                          "at the area lower bound %5 -> %6 lists; %7 ms per cut list")
                      .arg(baseSheets).arg(multiSheets).arg(better).arg(worse).arg(baseAtBound).arg(atBound).arg(ms, 0, 'f', 1));
        }
        const NestResult grain = nestParts({NestPart{1000, 300, "G"}}, 2440, 1220, 0, 0, false);
        check(!grain.sheets.isEmpty() && !grain.sheets[0].places[0].turned, "nesting with grain: parts are not turned");
    }

    // 8 pictures from several sides (the depth buffer must hide what is behind)
    c->zoomFit();
    shot("10_iso_front_right");
    c->setViewDirection(QVector3D(-1, 1, 0.8f), false);
    c->zoomFit();
    shot("11_iso_back_left");
    c->setView(Canvas::View::Front);
    c->zoomFit();
    shot("12_front");
    c->setView(Canvas::View::Top);
    c->zoomFit();
    shot("13_top");

    c->saveFile(outDir + "/selftest.sk3");

    // ===== 3.3: calculator, extrude New / Join / Cut, cut joints, fillet / chamfer =====
    auto nearV = [](double a, double b, double tol) { return std::fabs(a - b) <= tol; };
    auto rectJson = [](QVector3D a, QVector3D b, QVector3D ext) {   // a box: rectangle a..b swept along ext
        QJsonObject o;
        o["type"] = "rect";
        o["plane"] = std::fabs(ext.x()) > 0 ? 2 : (std::fabs(ext.y()) > 0 ? 1 : 0);
        QJsonArray pts;
        for (const QVector3D &v : {a, b}) {
            QJsonObject p;
            p["x"] = v.x(); p["y"] = v.y(); p["z"] = v.z();
            pts.append(p);
        }
        o["pts"] = pts;
        if (!ext.isNull()) {
            o["ex"] = ext.x(); o["ey"] = ext.y(); o["ez"] = ext.z();
        }
        return o;
    };
    auto loadItems = [&](const QJsonArray &items) {
        QJsonObject root;
        root["items"] = items;
        const QString path = outDir + "/t33.sk3";
        QFile tf(path);
        if (tf.open(QIODevice::WriteOnly))
            tf.write(QJsonDocument(root).toJson());
        tf.close();
        const bool ok = c->loadFile(path);
        c->setViewDirection(QVector3D(1, -1, 0.8f), false);
        c->zoomFit();
        pump();
        return ok;
    };
    // exact volume of a board: profile area (holes taken off) x thickness
    auto solidVolume = [](const SketchItem *it) -> double {
        if (it->extrude.isNull())
            return 0;
        const QVector3D en = it->extrude.normalized();
        double area = 0;
        if (it->type() == SketchItem::Rect && it->pts.size() >= 2) {
            const QVector3D d = it->pts[1] - it->pts[0];
            const double cc[3] = {std::fabs(d.x()), std::fabs(d.y()), std::fabs(d.z())};
            const double nn[3] = {std::fabs(en.x()), std::fabs(en.y()), std::fabs(en.z())};
            const int k = int(std::max_element(nn, nn + 3) - nn);
            area = 1;
            for (int i = 0; i < 3; ++i)
                if (i != k) area *= cc[i];
        } else if (it->type() == SketchItem::Circle && it->pts.size() >= 2) {
            area = M_PI * double((it->pts[1] - it->pts[0]).lengthSquared());
        } else {
            QList<QList<QVector3D>> rings;
            if (it->loops.isEmpty()) {
                QList<QVector3D> r = it->pts;
                if (r.size() > 1 && (r.first() - r.last()).length() < 1e-3f)
                    r.removeLast();
                rings << r;
            } else {
                int k = 0;
                for (int n0 : it->loops) { rings << it->pts.mid(k, n0); k += n0; }
            }
            for (int l = 0; l < rings.size(); ++l) {
                double sx = 0, sy = 0, sz = 0;
                const QList<QVector3D> &r = rings[l];
                for (int i = 0; i < r.size(); ++i) {
                    const QVector3D &p = r[i], &q = r[(i + 1) % r.size()];
                    sx += double(p.y()) * q.z() - double(p.z()) * q.y();
                    sy += double(p.z()) * q.x() - double(p.x()) * q.z();
                    sz += double(p.x()) * q.y() - double(p.y()) * q.x();
                }
                const double a = std::fabs(sx * en.x() + sy * en.y() + sz * en.z()) / 2;
                area += l == 0 ? a : -a;
            }
        }
        return area * double(it->extrude.length());
    };
    auto dump = [&] {                                     // every solid: type, bounds (for failure messages)
        QStringList s;
        for (const SketchItem *it : c->items()) {
            QVector3D a0, a1;
            bounds(it, a0, a1);
            s << QString("%1[%2,%3,%4..%5,%6,%7]").arg(int(it->type())).arg(a0.x()).arg(a0.y()).arg(a0.z())
                     .arg(a1.x()).arg(a1.y()).arg(a1.z());
        }
        return s.join(" ");
    };
    auto totalVolume = [&] {
        double v = 0;
        for (const SketchItem *it : c->items())
            v += solidVolume(it);
        return v;
    };

    // 20 the calculator in typed values
    {
        auto calc = [&](const QString &s, double want, bool number = false) {
            double v = 0;
            const bool ok = number ? Units::parseNumber(s, v) : Units::parse(s, v);
            check(ok && nearV(v, want, 1e-6), QString("calculator: %1 = %2 (want %3)").arg(s).arg(v, 0, 'g', 10).arg(want, 0, 'g', 10));
        };
        calc("800-2*18", 764);
        calc("(2400-3*18)/4", 586.5);
        calc("1.2m-18", 1182);
        calc("2*(300+50)", 700);
        calc("-50", -50);
        calc("4'-6\"", 1371.6);
        calc("360/7", 360.0 / 7, true);
        double v = 0;
        check(!Units::parse("10/0", v) && !Units::parse("18+", v) && !Units::parse("(3", v), "calculator: rejects 10/0, 18+, (3");
        const Units::Options keep = Units::options();
        Units::setOptions({Units::Architectural, 2, 16});
        calc("6-1/2", 165.1);                              // feet-inch units: 6-1/2 is six and a half inches
        calc("4'-6\"+3/4\"", 1390.65);
        calc("2*18", 914.4);
        Units::setOptions(keep);
    }
    // 21 a board; a box typed with a sum; a rectangle drawn on the board's face, extruded into it = Cut
    {
        QJsonArray a;
        a.append(rectJson({0, 0, 0}, {600, 300, 0}, {0, 0, 18}));
        check(loadItems(a), "test document loaded");
        c->setTool(Canvas::Tool::Box);
        click(c->screenOf(QVector3D(0, 400, 0)));
        type("800-2*18"); key(Qt::Key_Tab); type("100"); key(Qt::Key_Return);
        type("18"); key(Qt::Key_Return);
        boxIs(last(), QVector3D(0, 400, 0), QVector3D(764, 500, 18), "Box typed with a sum: 800-2*18 = 764 long");
        c->undoStack()->undo();
        pump();

        c->setTool(Canvas::Tool::Rect);
        click(c->screenOf(QVector3D(100, 100, 18)));
        type("200"); key(Qt::Key_Tab); type("50"); key(Qt::Key_Return);
        const SketchItem *r = last();
        check(r && r->type() == SketchItem::Rect && r->extrude.isNull() && r->pts.size() == 2 &&
                  std::fabs(r->pts[0].z() - 18) < 0.01f && std::fabs(r->pts[1].z() - 18) < 0.01f,
              "Rectangle started on a board's face is drawn in that face (z = 18)");
        check(c->plane() == Canvas::Plane::TopXY && std::fabs(c->planeOffset()) < 1e-6, "sketch plane back to XY 0 after it");
        c->setTool(Canvas::Tool::PressPull);
        click(c->screenOf(QVector3D(200, 125, 18)));
        key(Qt::Key_Minus, "-"); type("6");
        shot("40_extrude_cut_live");
        key(Qt::Key_Return);
        const double want = 600.0 * 300 * 18 - 200.0 * 50 * 6;
        check(nearV(totalVolume(), want, 1.0), QString("Extrude into the board (Auto = Cut): a 200 x 50 x 6 pocket, volume %1 (want %2)")
                                                   .arg(totalVolume(), 0, 'f', 0).arg(want, 0, 'f', 0));
        const auto rows = c->cutList(false);
        check(rows.size() == 1 && rows.first().qty == 1 && nearV(rows.first().l, 600, 0.05) && nearV(rows.first().t, 18, 0.05),
              QString("cut list: still one board 600 x 300 x 18 (%1 rows)").arg(rows.size()));

        // the same on another spot, but J = Join: a 100 x 100 x 20 block grows on the board
        c->setTool(Canvas::Tool::Rect);
        click(c->screenOf(QVector3D(400, 100, 18)));
        type("100"); key(Qt::Key_Tab); type("100"); key(Qt::Key_Return);
        c->setTool(Canvas::Tool::PressPull);
        click(c->screenOf(QVector3D(450, 150, 18)));
        key(Qt::Key_J, "j"); type("20"); key(Qt::Key_Return);
        const double want2 = want + 100.0 * 100 * 20;
        const auto rows2 = c->cutList(false);
        check(nearV(totalVolume(), want2, 1.0) && rows2.size() == 1,
              QString("Extrude + J = Join: the block is part of the board (volume %1, want %2; %3 cut-list rows)")
                  .arg(totalVolume(), 0, 'f', 0).arg(want2, 0, 'f', 0).arg(rows2.size()));
        c->setTool(Canvas::Tool::Select);
        c->zoomFit();
        shot("41_extrude_cut_join");
    }

    // 22 Cut joints: a shelf sunk 6 mm into two sides cuts both housings and stays whole
    {
        QJsonArray a;
        a.append(rectJson({0, 0, 0}, {18, 300, 0}, {0, 0, 400}));
        a.append(rectJson({300, 0, 0}, {318, 300, 0}, {0, 0, 400}));
        a.append(rectJson({12, 0, 200}, {306, 300, 200}, {0, 0, 18}));
        loadItems(a);
        c->selectIndices({2});
        c->cutJointsSelection();
        const double want = 2 * (18.0 * 300 * 400 - 6.0 * 300 * 18) + 294.0 * 300 * 18;
        int sides = 0;
        for (const SketchItem *it : c->items())
            if (it->type() == SketchItem::Polyline && !it->extrude.isNull())
                ++sides;
        check(c->items().size() == 3 && sides == 2 && nearV(totalVolume(), want, 1.0),
              QString("Cut joints: 2 sides with an 18 x 6 housing each, shelf kept (%1 items, volume %2, want %3)")
                  .arg(c->items().size()).arg(totalVolume(), 0, 'f', 0).arg(want, 0, 'f', 0));
        c->selectIndices({});
        shot("42_cut_joints");
    }

    // 23 Fillet a board's corner along the thickness (R 50), Chamfer a box's top front edge (5 mm), fillet a 2D corner
    {
        QJsonArray a;
        a.append(rectJson({0, 0, 0}, {600, 300, 0}, {0, 0, 18}));
        a.append(rectJson({0, 400, 0}, {600, 700, 0}, {0, 0, 18}));
        a.append(rectJson({0, 800, 0}, {200, 900, 0}, {}));
        loadItems(a);
        const double board = 600.0 * 300 * 18;
        c->setTool(Canvas::Tool::Fillet);
        type("50"); key(Qt::Key_Return);
        check(nearV(c->filletSize(false), 50, 1e-9), "Fillet: typed radius 50");
        move(c->screenOf(QVector3D(595, 0, 9)));
        shot("43_fillet_hover");
        click(c->screenOf(QVector3D(595, 0, 9)));
        const SketchItem *b1 = c->items().value(0);
        const double want1 = board - (1 - M_PI / 4) * 50.0 * 50.0 * 18;
        // the arc is kept as straight pieces with at most 0.05 mm chord error: allow that sliver along the arc
        check(b1 && b1->type() == SketchItem::Polyline && nearV(solidVolume(b1), want1, 0.05 * (M_PI / 2 * 50) * 18),
              QString("Fillet R50 on the edge along the thickness: volume %1 (want %2)")
                  .arg(b1 ? solidVolume(b1) : 0, 0, 'f', 0).arg(want1, 0, 'f', 0));
        click(c->screenOf(QVector3D(200, 805, 0)));        // the 2D rectangle's corner (200, 800)
        move(c->screenOf(QVector3D(200, 800, 0)));
        click(c->screenOf(QVector3D(200, 800, 0)));
        const SketchItem *f2 = c->items().value(2);
        check(f2 && f2->type() == SketchItem::Polyline && f2->extrude.isNull() && f2->pts.size() > 6,
              QString("Fillet a 2D rectangle's corner: closed polyline with the arc (%1 points)").arg(f2 ? f2->pts.size() : 0));
        c->setTool(Canvas::Tool::Chamfer);
        type("5"); key(Qt::Key_Return);
        move(c->screenOf(QVector3D(300, 400, 16)));
        click(c->screenOf(QVector3D(300, 400, 16)));
        const SketchItem *b2 = c->items().value(1);
        const double want2 = board - 600.0 * 5 * 5 / 2;
        check(b2 && nearV(solidVolume(b2), want2, 1.0) && std::fabs(b2->extrude.x()) > 599,
              QString("Chamfer 5 on a box's top front edge: re-made along X, volume %1 (want %2)")
                  .arg(b2 ? solidVolume(b2) : 0, 0, 'f', 0).arg(want2, 0, 'f', 0));
        const auto rows = c->cutList(false);
        const bool ok600 = rows.size() == 1 && rows.first().qty == 2 && nearV(rows.first().l, 600, 0.05) &&
                           nearV(rows.first().w, 300, 0.05) && nearV(rows.first().t, 18, 0.05);
        check(ok600, QString("cut list: both boards still 600 x 300 x 18 - one row, 2 off (%1 rows)").arg(rows.size()));
        c->setTool(Canvas::Tool::Select);
        c->zoomFit();
        shot("44_fillet_chamfer");
    }

    // 24 Shell: a 800 x 400 x 750 blank, open at the front, 18 walls = a carcass of named boards
    {
        QJsonArray a;
        a.append(rectJson({0, 0, 0}, {800, 400, 0}, {0, 0, 750}));
        loadItems(a);
        c->setTool(Canvas::Tool::Shell);
        type("18"); key(Qt::Key_Return);
        move(c->screenOf(QVector3D(400, 0, 375)));
        shot("45_shell_hover");
        click(c->screenOf(QVector3D(400, 0, 375)));
        const bool panels = c->items().size() == 5 && findBox({0, 0, 0}, {18, 400, 750}) >= 0 &&
                            findBox({782, 0, 0}, {800, 400, 750}) >= 0 && findBox({18, 0, 0}, {782, 400, 18}) >= 0 &&
                            findBox({18, 0, 732}, {782, 400, 750}) >= 0 && findBox({18, 382, 18}, {782, 400, 732}) >= 0;
        check(panels, QString("Shell, front open: 2 full sides, top + bottom between, back inside (%1 boards)").arg(c->items().size()));
        const auto rows = c->cutList(false);
        auto row = [&](const QString &n, double l, double w, double t, int q) {
            for (const auto &r : rows)
                if (r.name == n && nearV(r.l, l, 0.05) && nearV(r.w, w, 0.05) && nearV(r.t, t, 0.05) && r.qty == q)
                    return true;
            return false;
        };
        check(row("Side", 750, 400, 18, 2) && row("Top", 764, 400, 18, 1) && row("Bottom", 764, 400, 18, 1) &&
                  row("Back", 764, 714, 18, 1),
              "Shell cut list: Side 750x400 x2, Top / Bottom 764x400, Back 764x714");
        c->setTool(Canvas::Tool::Select);
        shot("46_shell_carcass");
    }

    // 25 Revolve: a 20 x 300 profile next to an axis line -> a round leg 40 x 300 (the tool: profile, axis, Enter)
    {
        QJsonArray a;
        QJsonObject prof = rectJson({0, 0, 0}, {20, 0, 300}, {});
        prof["plane"] = 1;
        a.append(prof);
        QJsonObject axis;
        axis["type"] = "line";
        axis["plane"] = 1;
        QJsonArray ap;
        for (const QVector3D &v : {QVector3D(0, 0, -20), QVector3D(0, 0, 320)}) {
            QJsonObject p;
            p["x"] = v.x(); p["y"] = v.y(); p["z"] = v.z();
            ap.append(p);
        }
        axis["pts"] = ap;
        a.append(axis);
        loadItems(a);
        c->setTool(Canvas::Tool::Revolve);
        click(c->screenOf(QVector3D(20, 0, 150)));
        move(c->screenOf(QVector3D(0, 0, -20)));
        click(c->screenOf(QVector3D(0, 0, -20)));
        move(c->screenOf(QVector3D(0, 0, 320)));
        click(c->screenOf(QVector3D(0, 0, 320)));
        int rv = -1;
        for (int i = 0; i < c->items().size(); ++i)
            if (c->items()[i]->type() == SketchItem::Revolve)
                rv = i;
        check(rv >= 0, "Revolve tool: profile, two axis points -> a revolved body");
        const auto rows = c->cutList(false);
        check(rows.size() == 1 && rows.first().name == "Turned part" && nearV(rows.first().l, 300, 0.05) &&
                  nearV(rows.first().w, 40, 0.05) && nearV(rows.first().t, 40, 0.05) && rows.first().linear,
              QString("Revolve cut list: Turned part 300 x 40 x 40 blank (%1 rows)").arg(rows.size()));
        c->setTool(Canvas::Tool::Select);
        c->zoomFit();
        shot("47_revolve");
    }

    // 26 Pipe: 25 mm tube, wall 2, bend R 50 along an L of 500 + 400: developed length 900 - 100 + 50 pi / 2
    {
        QJsonArray a;
        QJsonObject pl;
        pl["type"] = "polyline";
        pl["plane"] = 0;
        QJsonArray pp;
        for (const QVector3D &v : {QVector3D(0, 0, 0), QVector3D(500, 0, 0), QVector3D(500, 400, 0)}) {
            QJsonObject p;
            p["x"] = v.x(); p["y"] = v.y(); p["z"] = v.z();
            pp.append(p);
        }
        pl["pts"] = pp;
        a.append(pl);
        loadItems(a);
        c->setPipeParams(25, 2, 50);
        c->setTool(Canvas::Tool::Pipe);
        click(c->screenOf(QVector3D(250, 0, 0)));
        const double want = 900 - 100 + 50 * M_PI / 2;
        auto rows = c->cutList(false);
        check(c->items().size() == 1 && c->items().first()->type() == SketchItem::Pipe && rows.size() == 1 &&
                  nearV(rows.first().l, std::round(want * 10) / 10, 0.051) && nearV(rows.first().w, 25, 0.05) &&
                  nearV(rows.first().t, 2, 0.05),
              QString("Pipe tool: tube 25 x 2, developed length %1 (want %2)").arg(rows.value(0).l).arg(want, 0, 'f', 2));
        c->setTool(Canvas::Tool::Select);
        c->zoomFit();
        shot("48_pipe");
        // both kinds survive a save and load
        QJsonArray b;
        QJsonObject prof = rectJson({800, 0, 0}, {830, 0, 200}, {});
        prof["plane"] = 1;
        b.append(prof);
        c->saveFile(outDir + "/t33_pipe.sk3");
        c->loadFile(outDir + "/t33_pipe.sk3");
        rows = c->cutList(false);
        check(rows.size() == 1 && nearV(rows.first().l, std::round(want * 10) / 10, 0.051) && c->items().first()->pipeWall == 2,
              "Pipe: save / load keeps diameter, wall, bend and length");
    }

    // ===== 3.4: Box with ortho / polar on, Shift multi-pick, multi-face Press/Pull, up to a face, scoped extrude, Combine =====
    auto clickMod = [&](const QPointF &p, Qt::KeyboardModifiers mods) {
        move(p);
        QMouseEvent pr(QEvent::MouseButtonPress, p, c->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, mods);
        QApplication::sendEvent(c, &pr);
        QMouseEvent rl(QEvent::MouseButtonRelease, p, c->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, mods);
        QApplication::sendEvent(c, &rl);
        pump();
    };
    // 27 the Box's opposite corner follows the mouse with Ortho and Polar on (it used to stay on an axis = 0 wide)
    {
        loadItems(QJsonArray());
        const ViewSettings keepSet = c->settings();
        ViewSettings vs = keepSet;
        vs.ortho = true;
        vs.polar = true;
        vs.snapGrid = false;
        c->setSettings(vs);
        c->setTool(Canvas::Tool::Box);
        click(c->screenOf(QVector3D(0, 0, 0)));
        move(c->screenOf(QVector3D(300, 200, 0)));
        click(c->screenOf(QVector3D(300, 200, 0)));
        type("18"); key(Qt::Key_Return);
        QVector3D mn, mx;
        const SketchItem *b = last();
        if (b) bounds(b, mn, mx);
        check(b && !b->extrude.isNull() && std::fabs(mx.x() - mn.x() - 300) < 2 && std::fabs(mx.y() - mn.y() - 200) < 2,
              QString("Box with Ortho + Polar on: the corner follows the mouse - %1 x %2 (want 300 x 200)")
                  .arg(b ? mx.x() - mn.x() : 0).arg(b ? mx.y() - mn.y() : 0));
        // in the Top view the height follows the mouse moving up the screen (it used to stay 0)
        c->setViewDirection(QVector3D(0, 0, 1), false);
        // let the window finish laying out after the settings / view change (the canvas size must not move
        // between working out a screen point and clicking it)
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 150)
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        c->zoomFit();
        pump();
        // (inside the sketch sheet: outside it the cursor stops at the sheet's edge). Items are counted, not
        // compared by address: every command rebuilds the list and a new item may reuse an old address
        const int itemsBefore = int(c->items().size());
        click(c->screenOf(QVector3D(-250, -250, 0)));
        click(c->screenOf(QVector3D(-50, -150, 0)));
        const QPointF up = c->screenOf(QVector3D(-50, -150, 0)) - QPointF(0, 60);
        move(up);
        shot("55_topview_height");
        click(up);
        const bool made = int(c->items().size()) == itemsBefore + 1;
        const SketchItem *tb = last();
        if (tb) bounds(tb, mn, mx);
        const bool baseOk = made && tb && std::fabs(mx.x() - mn.x() - 200) < 2 && std::fabs(mx.y() - mn.y() - 100) < 2;
        check(baseOk && mx.z() - mn.z() > 5,
              QString("Box in the Top view: base 200 x 100, moving the mouse up gives the height (%1)  ")
                  .arg(made ? mx.z() - mn.z() : 0) + dump());
        c->setSettings(keepSet);
        c->setViewDirection(QVector3D(1, -1, 0.8f), false);
    }

    // 28 Shift+click adds and removes bodies in Select
    {
        QJsonArray a;
        a.append(rectJson({0, 0, 0}, {300, 200, 0}, {0, 0, 18}));
        a.append(rectJson({400, 0, 0}, {700, 200, 0}, {0, 0, 18}));
        a.append(rectJson({0, 300, 0}, {300, 500, 0}, {0, 0, 18}));
        loadItems(a);
        c->setTool(Canvas::Tool::Select);
        clickMod(c->screenOf(QVector3D(150, 100, 18)), Qt::ShiftModifier);
        clickMod(c->screenOf(QVector3D(550, 100, 18)), Qt::ShiftModifier);
        clickMod(c->screenOf(QVector3D(150, 400, 18)), Qt::ShiftModifier);
        const int three = c->selectedCount();
        clickMod(c->screenOf(QVector3D(550, 100, 18)), Qt::ShiftModifier);
        check(three == 3 && c->selectedCount() == 2, QString("Shift+click picks 3 bodies, again takes one off (%1, then %2)")
                                                         .arg(three).arg(c->selectedCount()));
        c->selectIndices({});

        // 29 Press/Pull: Shift+click a face, click another, pull 12 - both boards grow
        c->setTool(Canvas::Tool::PressPull);
        clickMod(c->screenOf(QVector3D(150, 100, 18)), Qt::ShiftModifier);
        check(c->pullSetSize() == 1, "Press/Pull: Shift+click chooses a face");
        click(c->screenOf(QVector3D(550, 100, 18)));
        type("12"); key(Qt::Key_Return);
        check(findBox({0, 0, 0}, {300, 200, 30}) >= 0 && findBox({400, 0, 0}, {700, 200, 30}) >= 0 &&
                  findBox({0, 300, 0}, {300, 500, 18}) >= 0,
              "Press/Pull of 2 faces at once: both boards 30 thick, the third untouched");
    }

    // 30 up to a face: two shelf tops pulled over a taller board's top land on its plane (z = 300)
    {
        // laid out so nothing hides the faces clicked in this view (the camera is at +X, -Y, +Z)
        QJsonArray a;
        a.append(rectJson({100, 0, 0}, {400, 200, 0}, {0, 0, 18}));
        a.append(rectJson({100, -400, 0}, {400, -200, 0}, {0, 0, 40}));
        a.append(rectJson({500, 300, 0}, {800, 500, 0}, {0, 0, 300}));
        loadItems(a);
        c->setTool(Canvas::Tool::PressPull);
        clickMod(c->screenOf(QVector3D(250, -300, 40)), Qt::ShiftModifier);
        click(c->screenOf(QVector3D(250, 100, 18)));
        move(c->screenOf(QVector3D(650, 400, 300)));
        shot("49_pull_up_to_face");
        click(c->screenOf(QVector3D(650, 400, 300)));
        check(findBox({100, 0, 0}, {400, 200, 300}) >= 0 && findBox({100, -400, 0}, {400, -200, 300}) >= 0,
              "Press/Pull up to a face: both shelves (18 and 40 high) end exactly at its plane, 300  " + dump());
    }

    // 31 a shape pulled through two stacked boards cuts only the selected one
    {
        QJsonArray a;
        a.append(rectJson({0, 0, 0}, {600, 300, 0}, {0, 0, 18}));
        a.append(rectJson({0, 0, -18}, {600, 300, -18}, {0, 0, 18}));
        loadItems(a);
        c->selectIndices({1});
        c->setTool(Canvas::Tool::Rect);
        click(c->screenOf(QVector3D(100, 100, 18)));
        type("200"); key(Qt::Key_Tab); type("50"); key(Qt::Key_Return);
        const QString afterRect = dump();
        c->selectIndices({1});
        c->setTool(Canvas::Tool::PressPull);
        click(c->screenOf(QVector3D(200, 125, 18)));
        key(Qt::Key_Minus, "-"); type("30");
        const QString whilePull = dump();
        key(Qt::Key_Return);
        log << "DEBUG scoped extrude: after rect " + afterRect + " | while pulling " + whilePull;
        const double want = 2 * 600.0 * 300 * 18 - 200.0 * 50 * 12;
        check(nearV(totalVolume(), want, 1.0) && findBox({0, 0, 0}, {600, 300, 18}) >= 0,
              QString("Extrude through two boards with the lower one selected: only it is cut (volume %1, want %2)")
                  .arg(totalVolume(), 0, 'f', 0).arg(want, 0, 'f', 0) + "  " + dump());
    }

    // 32 Combine: a board cut by two cross bars (tools gone), then (undone) intersected keeping the tools
    {
        QJsonArray a;
        a.append(rectJson({0, 0, 0}, {600, 300, 0}, {0, 0, 18}));
        a.append(rectJson({100, 0, -5}, {150, 300, -5}, {0, 0, 30}));
        a.append(rectJson({400, 0, -5}, {450, 300, -5}, {0, 0, 30}));
        loadItems(a);
        c->combineBodies({0}, {1, 2}, Canvas::BoolOp::Subtract, false);
        const double cutV = 600.0 * 300 * 18 - 2 * 50.0 * 300 * 18;
        check(nearV(totalVolume(), cutV, 1.0), QString("Combine Cut, tools removed: volume %1 (want %2)")
                                                   .arg(totalVolume(), 0, 'f', 0).arg(cutV, 0, 'f', 0));
        c->undoStack()->undo();
        pump();
        c->combineBodies({0}, {1}, Canvas::BoolOp::Intersect, true);
        const double intV = 50.0 * 300 * 18 + 2 * 50.0 * 300 * 30;
        check(nearV(totalVolume(), intV, 1.0), QString("Combine Intersect, tools kept: volume %1 (want %2)")
                                                   .arg(totalVolume(), 0, 'f', 0).arg(intV, 0, 'f', 0));
    }

// ===== 3.5: object snap remade (3D points of bodies), Mirror about a point + X / Y / Z, Move point to point / up to a face =====
    // 34 Mirror the left side across the plane through the top's BODY CENTRE, square to X (the X key)
    {
        QJsonArray a;
        a.append(rectJson({0, 0, 0}, {18, 400, 0}, {0, 0, 750}));
        a.append(rectJson({0, 0, 750}, {800, 400, 750}, {0, 0, 18}));
        loadItems(a);
        c->selectIndices({0});
        c->setTool(Canvas::Tool::Mirror);
        move(c->screenOf(QVector3D(400, 200, 759)));
        shot("51_mirror_body_centre");
        key(Qt::Key_X, "x");
        check(findBox({782, 0, 0}, {800, 400, 750}) >= 0,
              "Mirror: snap the top's body centre, X = the plane through it square to X - the side lands at 782..800  " + dump());
    }
    // 35 Move point to point in 3D: a block's corner onto the centre of a tall board's top face
    {
        QJsonArray a;
        a.append(rectJson({0, 0, 0}, {100, 100, 0}, {0, 0, 18}));
        a.append(rectJson({500, 0, 0}, {700, 200, 0}, {0, 0, 300}));
        loadItems(a);
        c->selectIndices({0});
        c->setTool(Canvas::Tool::Move);
        click(c->screenOf(QVector3D(100, 0, 18)));
        move(c->screenOf(QVector3D(600, 100, 300)));
        click(c->screenOf(QVector3D(600, 100, 300)));
        check(findBox({500, 100, 282}, {600, 200, 300}) >= 0,
              "Move point to point: the block's corner (100, 0, 18) onto the face centre (600, 100, 300)  " + dump());
        c->undoStack()->undo();
        pump();
        // 36 up to a face: base on the block's top face, then over the tall board's top (away from its points)
        c->selectIndices({0});
        c->setTool(Canvas::Tool::Move);
        click(c->screenOf(QVector3D(50, 50, 18)));
        move(c->screenOf(QVector3D(545, 45, 300)));
        shot("52_move_up_to_face");
        click(c->screenOf(QVector3D(545, 45, 300)));
        check(findBox({0, 0, 282}, {100, 100, 300}) >= 0,
              "Move up to a face: the block's top lands on the tall board's top plane (300), straight up  " + dump());
    }

    // 37 Subtract from the menu asks whether to keep the tool bodies: once "Keep", once "Remove"
    {
        QAction *sub = nullptr;
        for (QAction *a : w->findChildren<QAction *>())
            if (a->text().startsWith("&Subtract"))
                sub = a;
        auto answer = [](const QString &button) {
            QTimer::singleShot(150, [button] {
                if (auto *mb = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    for (QAbstractButton *b : mb->buttons())
                        if (b->text() == button)
                            b->click();
            });
        };
        QJsonArray a;
        a.append(rectJson({0, 0, 0}, {600, 300, 0}, {0, 0, 18}));
        a.append(rectJson({100, 0, -5}, {150, 300, -5}, {0, 0, 30}));
        loadItems(a);
        c->selectIndices({0, 1});
        answer("Keep the tools");
        if (sub) sub->trigger();
        pump();
        const double cut = 600.0 * 300 * 18 - 50.0 * 300 * 18, tool = 50.0 * 300 * 30;
        check(sub && nearV(totalVolume(), cut + tool, 1.0),
              QString("Subtract asks - Keep the tools: board cut, tool still there (volume %1, want %2)")
                  .arg(totalVolume(), 0, 'f', 0).arg(cut + tool, 0, 'f', 0));
        c->undoStack()->undo();
        pump();
        c->selectIndices({0, 1});
        answer("Remove the tools");
        if (sub) sub->trigger();
        pump();
        check(nearV(totalVolume(), cut, 1.0), QString("Subtract asks - Remove the tools: only the cut board (volume %1, want %2)")
                                                  .arg(totalVolume(), 0, 'f', 0).arg(cut, 0, 'f', 0));
    }    // the grid goes while the view is turned with the right mouse button, and comes back on release
    {
        c->setTool(Canvas::Tool::Select);
        const QPointF a = QPointF(c->width() / 2.0, c->height() / 2.0), b = a + QPointF(60, 20);
        QMouseEvent pr(QEvent::MouseButtonPress, a, c->mapToGlobal(a), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(c, &pr);
        QMouseEvent mv(QEvent::MouseMove, b, c->mapToGlobal(b), Qt::NoButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(c, &mv);
        pump();
        shot("53_orbit_no_grid");
        QMouseEvent rl(QEvent::MouseButtonRelease, b, c->mapToGlobal(b), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(c, &rl);
        pump();
        shot("54_after_orbit_grid");
    }
    // 33 the 3D Tools panel is there with its tools
    {
        auto *dock = w->findChild<QDockWidget *>("dock3DTools");
        const int buttons = dock ? int(dock->findChildren<QToolButton *>().size()) : 0;
        check(dock && buttons >= 14, QString("3D Tools side panel: %1 buttons").arg(buttons));
        c->setTool(Canvas::Tool::Select);
        c->zoomFit();
        w->grab().save(outDir + "/50_window_3d_tools.png");
    }

    QFile f(outDir + "/report.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream o(&f);
        o << log.join('\n') << "\n\n" << Canvas::cutListText(c->cutList(false)) << "\n\n"
          << (fails ? QString("%1 FAILED").arg(fails) : QString("ALL PASSED")) << "\n";
    }
    c->undoStack()->setClean();
    w->close();
    return fails ? 1 : 0;
}

// Sketch3D --bench <file.sk3> <out.txt> : frame times of the canvas from 24 directions (fresh picture each),
// a repaint with nothing changed, and a save / load round trip check of every object
int runBench(const QString &file, const QString &outTxt)
{
    auto *w = new MainWindow;
    w->resize(1500, 950);
    w->show();
    if (!w->openPath(file))
        return 2;
    Canvas *c = w->canvas();
    QApplication::processEvents();
    QStringList out;
    int solids = 0;
    for (const SketchItem *it : c->items())
        solids += it->extrude.isNull() ? 0 : 1;
    out << QString("objects %1, solid boards %2, canvas %3 x %4").arg(c->items().size()).arg(solids).arg(c->width()).arg(c->height());
    QElapsedTimer t;
    QList<double> ms;
    for (int k = 0; k < 24; ++k) {
        const double a = k * 15.0 * PI / 180.0;
        c->setViewDirection(QVector3D(float(std::cos(a)), float(std::sin(a)), 0.7f), false);
        t.start();
        c->grab();
        ms << t.nsecsElapsed() / 1e6;
    }
    std::sort(ms.begin(), ms.end());
    double sum = 0;
    for (double m : std::as_const(ms)) sum += m;
    out << QString("new view: mean %1 ms, median %2 ms, worst %3 ms").arg(sum / ms.size(), 0, 'f', 1)
               .arg(ms[ms.size() / 2], 0, 'f', 1).arg(ms.last(), 0, 'f', 1);
    t.start();
    for (int k = 0; k < 10; ++k)
        c->grab();
    out << QString("same view again (cached solids): %1 ms").arg(t.nsecsElapsed() / 1e6 / 10, 0, 'f', 1);
    // object snap: one mouse move with a point tool (Mirror) searches every snap point of every body
    c->selectIndices({0});
    c->setTool(Canvas::Tool::Mirror);
    t.start();
    for (int k = 0; k < 100; ++k) {
        const QPointF p(40 + (k * 37) % std::max(1, c->width() - 80), 40 + (k * 53) % std::max(1, c->height() - 80));
        QMouseEvent ev(QEvent::MouseMove, p, c->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(c, &ev);
    }
    out << QString("object snap per mouse move (Mirror tool): %1 ms").arg(t.nsecsElapsed() / 1e6 / 100, 0, 'f', 2);
    c->setTool(Canvas::Tool::Select);
    c->selectIndices({});
    // save / load round trip: every object must come back identical
    const QByteArray before = c->serialize();
    const QString tmp = outTxt + ".roundtrip.sk3";
    c->saveFile(tmp);
    c->loadFile(tmp);
    const QByteArray after = c->serialize();
    out << QString("save / load round trip: %1").arg(before == after ? "identical" : "DIFFERENT");
    QFile f(outTxt);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        QTextStream(&f) << out.join('\n') << '\n';
    c->undoStack()->setClean();
    w->close();
    return 0;
}

// Sketch3D --render <file.sk3> <out.png> : open a drawing off-screen and save what the canvas shows
int runRender(const QString &file, const QString &png)
{
    auto *w = new MainWindow;
    w->resize(1500, 950);
    w->show();
    if (!w->openPath(file))
        return 2;
    QApplication::processEvents();
    const bool ok = w->canvas()->grab().save(png);
    w->canvas()->undoStack()->setClean();
    w->close();
    return ok ? 0 : 3;
}