// orex — a Rex client for Omarchy: shows a Rex server's terminal sessions.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QUrl>

#include "terminalitem.h"
#include "theme.h"
#include "workspace.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("orex");
    app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("orex");
    QQuickStyle::setStyle("Material");

    qmlRegisterType<TerminalItem>("Orex", 1, 0, "TerminalItem");

    Theme theme;
    Workspace workspace(&theme);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &workspace);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    workspace.setPaneArea(engine.rootObjects().first()->findChild<QQuickItem *>("paneArea"));

    // Hand the panes back before the event loop stops, so the disown
    // requests still go out.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &workspace, &Workspace::shutdown);

    workspace.start(app.arguments().value(1));
    return app.exec();
}
