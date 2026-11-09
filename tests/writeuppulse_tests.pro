QT += core gui quick quickcontrols2 testlib
CONFIG += c++17 testcase
TARGET = writeuppulse_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/fetcher.h ../src/store.h ../src/theme.h ../src/translator.h
SOURCES += writeuppulse_tests.cpp ../src/backend.cpp ../src/fetcher.cpp ../src/store.cpp ../src/theme.cpp ../src/translator.cpp
