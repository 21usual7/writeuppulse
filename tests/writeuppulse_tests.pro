QT += core gui quick quickcontrols2 network sql testlib
CONFIG += c++17 testcase
TARGET = writeuppulse_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/notes.h ../src/references.h ../src/archive.h ../src/backend.h ../src/cloudsync.h ../src/database.h ../src/extractor.h ../src/llmclient.h ../src/filepicker.h ../src/fetcher.h ../src/progress.h ../src/store.h ../src/syncer.h ../src/translator.h ../src/tutor.h
SOURCES += writeuppulse_tests.cpp ../src/notes.cpp ../src/references.cpp ../src/archive.cpp ../src/backend.cpp ../src/cloudsync.cpp ../src/database.cpp ../src/extractor.cpp ../src/llmclient.cpp ../src/fetcher.cpp ../src/progress.cpp ../src/store.cpp ../src/syncer.cpp ../src/translator.cpp ../src/tutor.cpp
LIBS += -lcrypto
RESOURCES += ../src/resources.qrc
