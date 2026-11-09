QT += core gui quick quickcontrols2 testlib
CONFIG += c++17 testcase
TARGET = writeuppulse_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/filepicker.h ../src/fetcher.h ../src/progress.h ../src/store.h ../src/translator.h
SOURCES += writeuppulse_tests.cpp ../src/backend.cpp ../src/fetcher.cpp ../src/progress.cpp ../src/store.cpp ../src/translator.cpp
