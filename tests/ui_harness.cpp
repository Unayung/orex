// Runs the real orex window offscreen against the live Rex server and saves
// a screenshot every two seconds, so layout changes made from elsewhere
// (rex CLI, the Mac app) can be checked without touching the desktop.
// Usage: ui_harness <session> <out-prefix> <shots> [omarchy-current-dir]

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "../src/terminalitem.h"
#include "../src/theme.h"
#include "../src/workspace.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Material");
    qmlRegisterType<TerminalItem>("Orex", 1, 0, "TerminalItem");
    // A missing directory gives the default accent, for README screenshots.
    Theme theme(argc > 4 ? argv[4] : QString());
    Workspace workspace(&theme);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &workspace);
    engine.load(QUrl("qrc:/Main.qml"));
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    workspace.setPaneArea(window->findChild<QQuickItem *>("paneArea"));
    workspace.start(argv[1]);
    const QString prefix = argv[2];
    const int shots = QString(argv[3]).toInt();
    // Simulates coming back to orex: focus moves away from the pane and back.
    if (qEnvironmentVariableIsSet("OREX_REFOCUS_AT"))
        QTimer::singleShot(qEnvironmentVariableIntValue("OREX_REFOCUS_AT"), [window] {
            const auto terms = window->findChildren<TerminalItem *>();
            if (terms.isEmpty()) return;
            window->requestActivate();
            window->contentItem()->forceActiveFocus();
            terms.first()->forceActiveFocus();
            qInfo() << "refocused, window active:" << window->isActive();
        });
    // Runs actions on a schedule, "ms:action[=arg],ms:action", to drive
    // layout features without a keyboard.
    for (const QString &step : qEnvironmentVariable("OREX_ACTIONS").split(',', Qt::SkipEmptyParts)) {
        const int at = step.section(':', 0, 0).toInt();
        const QString action = step.section(':', 1).section('=', 0, 0);
        const QString arg = step.section('=', 1);
        QTimer::singleShot(at, [&workspace, action, arg] {
            qInfo() << "action" << action << arg;
            workspace.perform(action, arg.isEmpty() ? QVariant() : QVariant(arg));
        });
    }
    // Runs QML-side actions (palette, help, dialogs) the same way: "ms:action".
    for (const QString &step : qEnvironmentVariable("OREX_QML").split(',', Qt::SkipEmptyParts))
        QTimer::singleShot(step.section(':', 0, 0).toInt(), [window, action = step.section(':', 1)] {
            QMetaObject::invokeMethod(window, "run", Q_ARG(QVariant, action));
        });
    for (int i = 1; i <= shots; ++i)
        QTimer::singleShot(i * 2000, [=, &workspace, &app] {
            window->grabWindow().save(QStringLiteral("%1-%2.png").arg(prefix).arg(i));
            qInfo() << "shot" << i << "windows" << workspace.windows().size() << "status" << workspace.status();
            if (i == shots) { workspace.shutdown(); QTimer::singleShot(300, &app, &QCoreApplication::quit); }
        });
    return app.exec();
}
