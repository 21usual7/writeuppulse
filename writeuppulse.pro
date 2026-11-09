QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = writeuppulse
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/fetcher.h \
    src/progress.h \
    src/store.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/fetcher.cpp \
    src/progress.cpp \
    src/store.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
