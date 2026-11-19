QT += core gui qml quick quickcontrols2 sql dbus
CONFIG += c++17 release
TARGET = cell
TEMPLATE = app
HEADERS += src/backend.h src/theme.h src/translator.h src/telemetry.h src/analysis.h
SOURCES += src/main.cpp src/backend.cpp src/theme.cpp src/translator.cpp src/telemetry.cpp src/analysis.cpp
RESOURCES += src/resources.qrc
