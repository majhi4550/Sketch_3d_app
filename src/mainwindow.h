#pragma once

#include <QMainWindow>
#include <QFrame>
#include <QToolButton>
#include <QPlainTextEdit>
#include <QMap>

#include <functional>
#include <vector>

#include "canvas.h"

class QAction;
class QCheckBox;
class QDialog;
class QComboBox;
class QDoubleSpinBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPushButton;
class QSpinBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    bool openPath(const QString &path);
    // separate drawing windows: each MainWindow is one drawing
    MainWindow *newWindow();
    static QList<MainWindow *> windows();
    static MainWindow *windowFor(const QString &path);   // the window that already has this file open
    Canvas *canvas() const { return m_canvas; }

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void newFile();
    bool openFile();
    bool saveFile();
    bool saveFileAs();
    void exportPng();
    void exportJpg();
    void exportPdf();
    void exportDxf();
    void insertImage();
    void showHelp();
    void openPattern();
    void about();

private:
    Canvas *m_canvas;
    QString m_filePath;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QDialog *m_patternDlg = nullptr;
    QComboBox *m_dimCombo = nullptr;
    QComboBox *m_cutCombo = nullptr;
    QComboBox *m_polarCombo = nullptr;
    QComboBox *m_bladeCombo = nullptr;

    QMap<Canvas::Tool, QAction *> m_toolActs;
    QMap<Canvas::View, QAction *> m_viewActs;
    QMap<Canvas::Plane, QAction *> m_planeActs;
    std::vector<std::function<void(const ViewSettings &)>> m_syncers;

    QToolButton *m_palBtn = nullptr;
    QDoubleSpinBox *m_sizeW = nullptr, *m_sizeH = nullptr;
    QCheckBox *m_sizeLock = nullptr;
    QComboBox *m_sizeAnchor = nullptr;
    double m_ratio = 1.0;
    QComboBox *m_styleCombo = nullptr, *m_widthCombo = nullptr, *m_lineStyleCombo = nullptr, *m_fontCombo = nullptr;
    QPlainTextEdit *m_textEdit = nullptr;
    QCheckBox *m_boldChk = nullptr, *m_italicChk = nullptr;
    QDoubleSpinBox *m_textAngle = nullptr;
    QAction *m_copyAct = nullptr;
    QFrame *m_selBar = nullptr;
    QToolButton *m_sbGroup = nullptr, *m_sbUngroup = nullptr;
    bool m_alignToPage = false;
    void buildSelBar();
    void updateSelBar();
    QMenu *buildAlignMenu(QWidget *parent);
    void buildStyleToolBar();
    QPushButton *m_colorBtn = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr, *m_textSpin = nullptr, *m_angleSpin = nullptr,
                   *m_scaleSpin = nullptr, *m_offsetSpin = nullptr, *m_extrudeSpin = nullptr;
    QDoubleSpinBox *m_vx = nullptr, *m_vy = nullptr, *m_vz = nullptr, *m_planeOffsetSpin = nullptr;
    QSpinBox *m_arrayCount = nullptr;
    QCheckBox *m_keepMirror = nullptr;
    QComboBox *m_gridCombo = nullptr, *m_planeCombo = nullptr;
    QLabel *m_selInfo = nullptr, *m_coordLabel = nullptr, *m_viewLabel = nullptr,
           *m_snapLabel = nullptr, *m_hintLabel = nullptr;
    QListWidget *m_objects = nullptr;
    bool m_syncing = false;
    // hatch / fill panel
    QComboBox *m_hatchCombo = nullptr;
    QDoubleSpinBox *m_hatchScale = nullptr, *m_hatchAngle = nullptr;
    QPushButton *m_hatchFillBtn = nullptr;
    QColor m_hatchFill;
    void pushHatchStyle();
    void updateHatchFillBtn();
    // units
    int m_unitsKey = -1;
    void unitsChanged();
    void rebuildGridCombo();
    void updateCoordLabel(const QVector3D &p);
    QVector3D m_lastCursor;

    void buildMenus();
    void buildToolBars();
    void buildDocks();
    QWidget *buildPropertiesPanel();
    QWidget *buildViewportPanel();
    QWidget *buildObjectsPanel();

    void mutate(const std::function<void(ViewSettings &)> &fn);
    void syncControls();
    void syncSelection();
    void pickFill();
    void openCutList();
    void pipeSettings();
    void osnapSettings();
    void runBoolean(Canvas::BoolOp op);       // Union / Subtract / Intersect, asking whether to keep the tools
    QCheckBox *m_askKeepChk = nullptr;
    void openCombine();                       // targets / tools / operation, non-modal
    QDialog *m_combineDlg = nullptr;
    void buildToolsDock();                    // the 3D Tools side panel (the modelling tools added by the upgrades)
    QLineEdit *m_nameEdit = nullptr;          // part name of the selection (cut list)
    QDoubleSpinBox *m_boardSpin = nullptr;          // Box tool: default board thickness
    QComboBox *m_matCombo = nullptr;                // material of the selection
    void refreshObjects();
    void updateStatusLabels();

    bool maybeSave();
    void updateTitle();
    void applyColor();
    void updateSwatch();
    void addRecent(const QString &path);
    void rebuildRecentMenu();
    bool openInThisOrOtherWindow(const QString &path);
    void openInNewWindow();
    void rebuildWindowMenu();
    void tileWindows();
    QString documentName() const;
    QMenu *m_windowMenu = nullptr;
    QAction *m_newWinAct = nullptr, *m_closeWinAct = nullptr;
    void loadSettings();
    void saveSettings();
};
