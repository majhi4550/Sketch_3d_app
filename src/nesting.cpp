#include "nesting.h"
#include "units.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {
// one sheet's free space as maximal rectangles (Jukka Jylanki, "A thousand ways to pack the bin")
struct Bin
{
    QList<QRectF> free;
    NestSheet sheet;

    // rule 0 best short side fit (the smallest leftover on the shorter side), 1 best long side fit,
    // 2 best area fit, 3 bottom-left (lowest, then leftmost)
    bool score(double w, double h, int rule, double &bestShort, double &bestLong, QRectF &spot) const
    {
        bool found = false;
        for (const QRectF &f : free) {
            if (w > f.width() + 1e-9 || h > f.height() + 1e-9)
                continue;
            const double lw = f.width() - w, lh = f.height() - h;
            double s = std::min(lw, lh), l = std::max(lw, lh);
            if (rule == 1)
                std::swap(s, l);
            else if (rule == 2)
                l = s, s = f.width() * f.height() - w * h;
            else if (rule == 3)
                s = f.y() + h, l = f.x();
            if (s < bestShort - 1e-9 || (std::fabs(s - bestShort) < 1e-9 && l < bestLong)) {
                bestShort = s;
                bestLong = l;
                spot = QRectF(f.x(), f.y(), w, h);
                found = true;
            }
        }
        return found;
    }

    void place(const QRectF &used)
    {
        QList<QRectF> next;
        for (const QRectF &f : std::as_const(free)) {
            if (!f.intersects(used)) {
                next << f;
                continue;
            }
            // the free rectangle minus the used one: up to four maximal pieces
            if (used.left() > f.left())
                next << QRectF(f.left(), f.top(), used.left() - f.left(), f.height());
            if (used.right() < f.right())
                next << QRectF(used.right(), f.top(), f.right() - used.right(), f.height());
            if (used.top() > f.top())
                next << QRectF(f.left(), f.top(), f.width(), used.top() - f.top());
            if (used.bottom() < f.bottom())
                next << QRectF(f.left(), used.bottom(), f.width(), f.bottom() - used.bottom());
        }
        // drop pieces that lie inside another one
        free.clear();
        for (int i = 0; i < next.size(); ++i) {
            if (next[i].width() < 1e-6 || next[i].height() < 1e-6)
                continue;
            bool inside = false;
            for (int j = 0; j < next.size() && !inside; ++j)
                if (i != j && next[j].contains(next[i]) && !(next[j] == next[i] && j > i))
                    inside = true;
            if (!inside)
                free << next[i];
        }
    }
};
} // namespace

double NestResult::utilisation() const
{
    if (sheets.isEmpty() || sheetL <= 0 || sheetW <= 0)
        return 0;
    double a = 0;
    for (const NestSheet &s : sheets)
        a += s.partArea;
    return a / (sheets.size() * sheetL * sheetW);
}

namespace {
// one packing pass: parts in the given order, every spot chosen by one rule
NestResult nestOnce(const QList<NestPart> &parts, const QList<int> &order, int rule, bool firstFit, double sheetL,
                    double sheetW, double kerf, double trim, bool allowTurn)
{
    NestResult res;
    res.sheetL = sheetL;
    res.sheetW = sheetW;
    // every part grows by one kerf; the usable sheet grows by one kerf too, so the last part on a row
    // needs no cut after it
    const double usableL = sheetL - 2 * trim + kerf, usableW = sheetW - 2 * trim + kerf;
    QList<Bin> bins;
    for (int idx : std::as_const(order)) {
        const double w = parts[idx].l + kerf, h = parts[idx].w + kerf;
        const bool fitsStraight = w <= usableL + 1e-9 && h <= usableW + 1e-9;
        const bool fitsTurned = allowTurn && h <= usableL + 1e-9 && w <= usableW + 1e-9;
        if (!fitsStraight && !fitsTurned) {
            res.tooBig << idx;
            continue;
        }
        int bestBin = -1;
        bool bestTurn = false;
        QRectF bestSpot;
        double bs = std::numeric_limits<double>::max(), bl = bs;
        for (int b = 0; b < bins.size(); ++b) {
            QRectF spot;
            if (fitsStraight && bins[b].score(w, h, rule, bs, bl, spot)) { bestBin = b; bestSpot = spot; bestTurn = false; }
            if (fitsTurned && bins[b].score(h, w, rule, bs, bl, spot)) { bestBin = b; bestSpot = spot; bestTurn = true; }
            if (firstFit && bestBin >= 0)                 // first fit: fill earlier sheets before later ones
                break;
        }
        if (bestBin < 0) {                                // a new sheet
            Bin nb;
            nb.free << QRectF(0, 0, usableL, usableW);
            bins << nb;
            bestBin = int(bins.size()) - 1;
            bestTurn = !fitsStraight;
            bestSpot = bestTurn ? QRectF(0, 0, h, w) : QRectF(0, 0, w, h);
        }
        bins[bestBin].place(bestSpot);
        NestPlace pl;
        pl.part = idx;
        pl.turned = bestTurn;
        pl.rect = QRectF(bestSpot.x() + trim, bestSpot.y() + trim, bestSpot.width() - kerf, bestSpot.height() - kerf);
        bins[bestBin].sheet.places << pl;
        bins[bestBin].sheet.partArea += parts[idx].l * parts[idx].w;
    }
    for (const Bin &b : std::as_const(bins))
        res.sheets << b.sheet;
    return res;
}
} // namespace

NestResult nestParts(const QList<NestPart> &parts, double sheetL, double sheetW, double kerf, double trim, bool allowTurn)
{
    NestResult best;
    best.sheetL = sheetL;
    best.sheetW = sheetW;
    if (sheetL - 2 * trim <= 0 || sheetW - 2 * trim <= 0)
        return best;
    double total = 0;
    for (const NestPart &p : parts)
        total += p.l * p.w;
    const int lowerBound = int(std::ceil(total / ((sheetL - 2 * trim) * (sheetW - 2 * trim)) - 1e-9));

    // no single order or rule wins on every cut list, so try them all and keep the best: fewest sheets,
    // then the emptiest last sheet (a bigger usable offcut)
    using Key = std::function<double(const NestPart &)>;
    const QList<Key> keys = {
        [](const NestPart &p) { return p.l * p.w; },
        [](const NestPart &p) { return std::max(p.l, p.w); },
        [](const NestPart &p) { return p.l + p.w; },
        [](const NestPart &p) { return std::min(p.l, p.w); },
        [](const NestPart &p) { return p.l; },
        [](const NestPart &p) { return p.w; },
    };
    bool have = false;
    double bestLast = 0;
    for (const Key &key : keys) {
        QList<int> order;
        for (int i = 0; i < parts.size(); ++i)
            order << i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const double ka = key(parts[a]), kb = key(parts[b]);
            if (std::fabs(ka - kb) > 1e-9) return ka > kb;
            const double aa = parts[a].l * parts[a].w, ab = parts[b].l * parts[b].w;
            if (std::fabs(aa - ab) > 1e-9) return aa > ab;
            return std::max(parts[a].l, parts[a].w) > std::max(parts[b].l, parts[b].w);
        });
        for (int pass = 0; pass < 8; ++pass) {
            NestResult r = nestOnce(parts, order, pass % 4, pass >= 4, sheetL, sheetW, kerf, trim, allowTurn);
            const double last = r.sheets.isEmpty() ? 0 : r.sheets.last().partArea;
            if (!have || r.sheets.size() < best.sheets.size() || (r.sheets.size() == best.sheets.size() && last < bestLast - 1e-6)) {
                best = r;
                bestLast = last;
                have = true;
            }
        }
    }
    best.lowerBound = lowerBound;
    return best;
}

NestResult nestPartsSinglePass(const QList<NestPart> &parts, double sheetL, double sheetW, double kerf, double trim, bool allowTurn)
{
    QList<int> order;
    double total = 0;
    for (int i = 0; i < parts.size(); ++i) {
        order << i;
        total += parts[i].l * parts[i].w;
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const double aa = parts[a].l * parts[a].w, ab = parts[b].l * parts[b].w;
        if (std::fabs(aa - ab) > 1e-9) return aa > ab;
        return std::max(parts[a].l, parts[a].w) > std::max(parts[b].l, parts[b].w);
    });
    NestResult r;
    if (sheetL - 2 * trim > 0 && sheetW - 2 * trim > 0) {
        r = nestOnce(parts, order, 0, false, sheetL, sheetW, kerf, trim, allowTurn);
        r.lowerBound = int(std::ceil(total / ((sheetL - 2 * trim) * (sheetW - 2 * trim)) - 1e-9));
    }
    r.sheetL = sheetL;
    r.sheetW = sheetW;
    return r;
}

// sheets for one material + thickness, packed by nestParts()
// every group's sheets with the parts drawn to scale, for the Sheets tab and the PNG
QImage nestingImage(const QList<NestGroup> &groups, int width)
{
    const int margin = 14;
    int h = margin;
    for (const NestGroup &g : groups) {
        const double sc = g.res.sheetL > 0 ? (width - 2.0 * margin) / g.res.sheetL : 1;
        h += 28 + int(g.res.sheets.size()) * (int(g.res.sheetW * sc) + 30) + (g.res.tooBig.isEmpty() ? 0 : 22) + 8;
    }
    QImage img(width, std::max(h, 80), QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(246, 247, 249));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QFont title = QApplication::font(), small = QApplication::font();
    title.setPixelSize(15);
    title.setBold(true);
    small.setPixelSize(11);
    int y = margin;
    for (const NestGroup &g : groups) {
        const double sc = g.res.sheetL > 0 ? (width - 2.0 * margin) / g.res.sheetL : 1;
        p.setFont(title);
        p.setPen(QColor(30, 34, 40));
        p.drawText(QRectF(margin, y, width - 2 * margin, 22), Qt::AlignLeft | Qt::AlignVCenter, g.title);
        y += 28;
        for (int s = 0; s < g.res.sheets.size(); ++s) {
            const NestSheet &sh = g.res.sheets[s];
            p.setFont(small);
            p.setPen(QColor(70, 78, 90));
            p.drawText(QRectF(margin, y, width - 2 * margin, 18), Qt::AlignLeft | Qt::AlignVCenter,
                       QString("Sheet %1 of %2  -  %3 parts, %4% used").arg(s + 1).arg(g.res.sheets.size())
                           .arg(sh.places.size()).arg(int(std::round(100.0 * sh.partArea / (g.res.sheetL * g.res.sheetW)))));
            y += 20;
            const QRectF sheetR(margin, y, g.res.sheetL * sc, g.res.sheetW * sc);
            p.setPen(QPen(QColor(150, 130, 100), 1.2));
            p.setBrush(QColor(233, 222, 200));
            p.drawRect(sheetR);
            for (const NestPlace &pl : sh.places) {
                const NestPart &part = g.parts[pl.part];
                const QRectF r(sheetR.left() + pl.rect.x() * sc, sheetR.top() + pl.rect.y() * sc, pl.rect.width() * sc,
                               pl.rect.height() * sc);
                const int hue = int(qHash(part.label) % 360u);
                p.setPen(QPen(QColor(60, 50, 40), 1.0));
                p.setBrush(QColor::fromHsv(hue, 55, 238));
                p.drawRect(r);
                const QString lab = QString("%1  %2 x %3").arg(part.label, Units::formatDim(part.l), Units::formatDim(part.w));
                const QFontMetricsF fm(small);
                p.setPen(QColor(25, 25, 25));
                if (fm.horizontalAdvance(lab) + 6 < r.width() && r.height() > 14)
                    p.drawText(r, Qt::AlignCenter, lab);
                else if (fm.horizontalAdvance(part.label) + 4 < r.width() && r.height() > 12)
                    p.drawText(r, Qt::AlignCenter, part.label);
            }
            y += int(g.res.sheetW * sc) + 10;
        }
        if (!g.res.tooBig.isEmpty()) {
            p.setFont(small);
            p.setPen(QColor(200, 60, 50));
            QStringList big;
            for (int i : g.res.tooBig)
                big << QString("%1 %2 x %3").arg(g.parts[i].label, Units::formatDim(g.parts[i].l), Units::formatDim(g.parts[i].w));
            p.drawText(QRectF(margin, y, width - 2 * margin, 18), Qt::AlignLeft | Qt::AlignVCenter,
                       "Larger than the sheet: " + big.join(", "));
            y += 22;
        }
        y += 8;
    }
    return img;
}

