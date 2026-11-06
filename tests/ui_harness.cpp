// Runs the real orex window offscreen against the live Rex server and saves
// a screenshot every two seconds, so layout changes made from elsewhere
// (rex CLI, the Mac app) can be checked without touching the desktop.
// Usage: ui_harness <session> <out-prefix> <shots>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "../src/terminalitem.h"
#include "../src/workspace.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Material");
    qmlRegisterType<TerminalItem>("Orex", 1, 0, "TerminalItem");
    Workspace workspace;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &workspace);
    engine.load(QUrl("qrc:/Main.qml"));
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    workspace.setPaneArea(window->findChild<QQuickItem *>("paneArea"));
    workspace.start(argv[1]);
    const QString prefix = argv[2];
    const int shots = QString(argv[3]).toInt();
    for (int i = 1; i <= shots; ++i)
        QTimer::singleShot(i * 2000, [=, &workspace, &app] {
            window->grabWindow().save(QStringLiteral("%1-%2.png").arg(prefix).arg(i));
            qInfo() << "shot" << i << "windows" << workspace.windows().size() << "status" << workspace.status();
            if (i == shots) { workspace.shutdown(); QTimer::singleShot(300, &app, &QCoreApplication::quit); }
        });
    return app.exec();
}
