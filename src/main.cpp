// writeuppulse — a daily digest of popular technical writeups, found and
// summarized by Claude Code.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <cstring>

#include "backend.h"
#include "cli.h"
#include "portalfilepicker.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Every command but `open` runs headless, from the timer or an agent.
    if (argc > 1 && std::strcmp(argv[1], "open") != 0)
        return runCli(argc, argv);

    QGuiApplication app(argc, argv);
    app.setApplicationName("writeuppulse");
    app.setApplicationVersion("0.1.0");

    // Matches the window to writeuppulse.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("writeuppulse");
    app.setWindowIcon(QIcon::fromTheme("writeuppulse"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Backend backend;
    PortalFilePicker picker;
    backend.setFilePicker(&picker);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    // The language picked in the window wins over Omarchy's, live.
    translator.setOverride(backend.language());
    QObject::connect(&backend, &Backend::languageChanged, &translator,
                     [&] { translator.setOverride(backend.language()); });
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
