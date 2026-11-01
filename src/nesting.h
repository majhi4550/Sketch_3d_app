#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>

// Sheet nesting for the cut list: rectangular parts packed onto stock sheets (MaxRects, best short side
// fit, largest parts first). The saw kerf is kept between parts, the trim along every sheet edge.
struct NestPart
{
    double l = 0, w = 0;       // finished size: l along the sheet's length unless turned
    QString label;
};

struct NestPlace
{
    int part = -1;             // index into the parts list
    QRectF rect;               // on the sheet (mm, origin at the sheet corner)
    bool turned = false;
};

struct NestSheet
{
    QList<NestPlace> places;
    double partArea = 0;
};

struct NestResult
{
    QList<NestSheet> sheets;
    QList<int> tooBig;         // parts that fit no sheet
    double sheetL = 0, sheetW = 0;
    int lowerBound = 0;        // sheets needed if nothing were wasted
    double utilisation() const;   // part area / sheet area used, 0..1
};

// allowTurn = false keeps every part's length along the sheet's length (grain). Tries 6 part orders x 4 fit
// rules x best/first sheet fit and keeps the fewest sheets, then the emptiest last sheet.
NestResult nestParts(const QList<NestPart> &parts, double sheetL, double sheetW, double kerf, double trim, bool allowTurn);
// one pass only (largest area first, best short side fit): the baseline nestParts is measured against
NestResult nestPartsSinglePass(const QList<NestPart> &parts, double sheetL, double sheetW, double kerf, double trim, bool allowTurn);

// sheets for one material + thickness, packed by nestParts()
struct NestGroup
{
    QString title;
    QList<NestPart> parts;
    NestResult res;
};

// every group's sheets with the parts drawn to scale (cut list Sheets tab, PNG export)
QImage nestingImage(const QList<NestGroup> &groups, int width);