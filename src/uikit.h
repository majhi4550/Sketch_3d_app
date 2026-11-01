#pragma once

#include <QIcon>
#include <QString>

class QMainWindow;

// Procedurally drawn vector icons (crisp at any DPI, no resource files needed).
// Names: select line polyline rect circle arc curve dim text image trim join break offset extend
//        cutter mirror pattern edit move drag copy group ungroup delete palette
//        align-left align-hcenter align-right align-top align-vcenter align-bottom
//        dist-h dist-v center-page center-origin align
QIcon uiIcon(const QString &name, int logicalSize = 22);

// Ctrl+K command palette: fuzzy-search every menu / toolbar command and run it.
void showCommandPalette(QMainWindow *w);
