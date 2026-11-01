cmake_minimum_required(VERSION 3.16)

project(Sketch3D VERSION 3.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)

qt_add_executable(Sketch3D
    src/main.cpp
    src/sketchitem.h src/sketchitem.cpp
    src/canvas.h src/canvas_util.h
    src/canvas.cpp src/canvas_paint.cpp src/canvas_ops.cpp src/canvas_edit.cpp src/canvas_cut.cpp src/canvas_align.cpp src/canvas_hatch.cpp src/canvas_solid.cpp src/canvas_bool.cpp src/canvas_gizmo.cpp src/canvas_rotate.cpp
    src/canvas_fillet.cpp src/canvas_shell.cpp src/canvas_sweep.cpp src/canvas_snap.cpp src/canvas_print.cpp src/uikit.cpp src/uikit.h
    src/units.h src/units.cpp
    src/nesting.h src/nesting.cpp
    src/mainwindow.h src/mainwindow.cpp
    src/selftest.cpp
)
target_link_libraries(Sketch3D PRIVATE Qt6::Widgets Qt6::PrintSupport)

if (WIN32)
    target_sources(Sketch3D PRIVATE resources/Sketch3D.rc)
    set_target_properties(Sketch3D PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
