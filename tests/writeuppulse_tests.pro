QT += core gui quick quickcontrols2 network sql testlib
CONFIG += c++17 testcase
TARGET = writeuppulse_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/database.h ../src/filepicker.h ../src/fetcher.h ../src/progress.h ../src/store.h ../src/syncer.h ../src/translator.h ../src/tutor.h
SOURCES += writeuppulse_tests.cpp ../src/backend.cpp ../src/database.cpp ../src/fetcher.cpp ../src/progress.cpp ../src/store.cpp ../src/syncer.cpp ../src/translator.cpp ../src/tutor.cpp
