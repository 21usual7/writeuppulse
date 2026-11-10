QT += core gui qml quick quickcontrols2 dbus network sql

CONFIG += c++17 release
TARGET = writeuppulse
TEMPLATE = app
# Keep in step with pkgver in pkgbuild/PKGBUILD.
VERSION = 0.3.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

HEADERS += \
    src/archive.h \
    src/backend.h \
    src/cloudsync.h \
    src/cli.h \
    src/database.h \
    src/extractor.h \
    src/fetcher.h \
    src/llmclient.h \
    src/notes.h \
    src/filepicker.h \
    src/portalfilepicker.h \
    src/progress.h \
    src/references.h \
    src/store.h \
    src/syncer.h \
    src/tutor.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/archive.cpp \
    src/backend.cpp \
    src/cloudsync.cpp \
    src/cli.cpp \
    src/database.cpp \
    src/extractor.cpp \
    src/fetcher.cpp \
    src/llmclient.cpp \
    src/notes.cpp \
    src/portalfilepicker.cpp \
    src/progress.cpp \
    src/references.cpp \
    src/store.cpp \
    src/syncer.cpp \
    src/tutor.cpp \
    src/translator.cpp

# AES-GCM for the sync rows (CloudSync).
LIBS += -lcrypto

RESOURCES += src/resources.qrc
