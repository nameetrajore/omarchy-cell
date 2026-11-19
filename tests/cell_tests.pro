QT += core gui qml quick quickcontrols2 testlib sql dbus
CONFIG += c++17 testcase
TARGET = cell_tests
TEMPLATE = app
INCLUDEPATH += ../src
HEADERS += ../src/backend.h ../src/theme.h ../src/translator.h ../src/telemetry.h ../src/analysis.h
SOURCES += cell_tests.cpp ../src/backend.cpp ../src/theme.cpp ../src/translator.cpp ../src/telemetry.cpp ../src/analysis.cpp
RESOURCES += ../src/resources.qrc
