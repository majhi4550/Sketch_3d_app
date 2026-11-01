#include <QApplication>

#include "mainwindow.h"

#include <QDir>
#include <QFont>
#include <QSettings>

int runSelfTest(const QString &outDir);   // selftest.cpp
int runRender(const QString &file, const QString &png);
int runBench(const QString &file, const QString &outTxt);

static const char *kFusionDark = R"QSS(
QWidget {
    background-color: #12161d;
    color: #d5dcea;
    font-size: 12px;
}
QMainWindow::separator {
    background: #0d1117;
    width: 3px; height: 3px;
}
QMenuBar {
    background-color: #0d1117;
    border-bottom: 1px solid #232c3a;
    padding: 2px;
}
QMenuBar::item {
    padding: 4px 10px;
    border-radius: 4px;
    background: transparent;
}
QMenuBar::item:selected { background-color: #1c4b8c; }
QMenu {
    background-color: #151b25;
    border: 1px solid #2a3546;
}
QMenu::item { padding: 5px 26px; }
QMenu::item:selected { background-color: #1c4b8c; }
QMenu::separator { height: 1px; background: #232c3a; margin: 4px 8px; }
QToolBar {
    background-color: #0d1117;
    border: none;
    spacing: 5px;
    padding: 5px;
}
QToolButton {
    background-color: #151c28;
    color: #cfe0f5;
    border: 1px solid #263244;
    border-radius: 8px;
    padding: 5px 8px;
}
QToolButton:hover { background-color: #1c2a3c; border-color: #3a5170; }
QToolButton:pressed { background-color: #0e2f3f; }
QToolButton:checked {
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #0f3d4d, stop:1 #0a2733);
    color: #ffffff;
    border: 1px solid #35e0c8;
}
QToolButton::menu-indicator { image: none; }
QPushButton {
    background-color: #18202c;
    color: #cfe0f5;
    border: 1px solid #2a3546;
    border-radius: 6px;
    padding: 5px 12px;
}
QPushButton:hover { background-color: #223047; }
QPushButton:pressed { background-color: #0e2f3f; }
QDockWidget {
    color: #9fb3d1;
    font-weight: bold;
    titlebar-close-icon: none;
    titlebar-normal-icon: none;
}
QDockWidget::title {
    background-color: #0d1117;
    padding: 7px;
    border-bottom: 1px solid #232c3a;
}
QDoubleSpinBox, QSpinBox, QComboBox, QLineEdit {
    background-color: #0d1117;
    border: 1px solid #2a3546;
    border-radius: 5px;
    padding: 4px 6px;
    color: #e6edf7;
    selection-background-color: #1c4b8c;
}
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    width: 16px; background: #18202c; border: none;
}
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background-color: #151b25;
    border: 1px solid #2a3546;
    selection-background-color: #1c4b8c;
}
QStatusBar {
    background-color: #0d1117;
    border-top: 1px solid #232c3a;
    color: #8fa3c0;
}
QLabel { background: transparent; }
QRadioButton, QCheckBox { background: transparent; spacing: 6px; }
QDialog { background-color: #12161d; }
QMessageBox { background-color: #12161d; }
QScrollBar:vertical {
    background: #0d1117; width: 10px; border-radius: 5px;
}
QScrollBar::handle:vertical {
    background: #2a3546; min-height: 30px; border-radius: 5px;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal {
    background: #0d1117; height: 10px; border-radius: 5px;
}
QScrollBar::handle:horizontal {
    background: #2a3546; min-width: 30px; border-radius: 5px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 14px; height: 14px; border: 1px solid #3a4a63; border-radius: 3px; background: #0d1117;
}
QCheckBox::indicator:checked { background: #35e0c8; border-color: #35e0c8; }
QRadioButton::indicator { border-radius: 7px; }
QRadioButton::indicator:checked { background: #35e0c8; border-color: #35e0c8; }
QGroupBox {
    border: 1px solid #232c3a; border-radius: 8px; margin-top: 14px; padding: 10px 6px 6px 6px;
    font-weight: bold; color: #9fb3d1;
}
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QTabBar::tab {
    background: #0d1117; border: 1px solid #232c3a; padding: 6px 12px; margin-right: 2px;
    border-top-left-radius: 6px; border-top-right-radius: 6px; color: #8fa3c0;
}
QTabBar::tab:selected { background: #18202c; color: #35e0c8; }
QListWidget { background: #0d1117; border: 1px solid #232c3a; border-radius: 6px; }
QListWidget::item { padding: 3px 4px; }
QListWidget::item:selected { background: #1c4b8c; color: white; }
QToolTip { background-color: #151b25; color: #e6edf7; border: 1px solid #35e0c8; padding: 4px; }
QScrollArea { background: transparent; }
)QSS";

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Sketch3D");
    QApplication::setApplicationVersion("3.5.0");
    QApplication::setOrganizationName("Sketch3D");
    app.setStyle("Fusion");
    app.setStyleSheet(kFusionDark);
    // the offscreen platform (self-test, render, bench) has no system font; give it the one Windows uses
    if (QGuiApplication::platformName() == "offscreen")
        QApplication::setFont(QFont("Segoe UI", 9));

    // Sketch3D --selftest <folder>: builds a cabinet with the 3D tools through synthetic input and writes
    // screenshots + report.txt there (settings go to an ini file in that folder, not the registry)
    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == "--selftest") {
        const QString out = QString::fromLocal8Bit(argv[2]);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, out);
        return runSelfTest(out);
    }
    if (argc > 3 && QString::fromLocal8Bit(argv[1]) == "--bench") {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
        return runBench(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    if (argc > 3 && QString::fromLocal8Bit(argv[1]) == "--render") {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
        return runRender(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }

    // every drawing window is its own MainWindow (File > New Window); the app ends with the last one
    auto *w = new MainWindow;
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->show();
    if (argc > 1)
        w->openPath(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
