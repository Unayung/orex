// Runs the real orex window offscreen against the live Rex server and saves
// a screenshot every two seconds, so layout changes made from elsewhere
// (rex CLI, the Mac app) can be checked without touching the desktop.
// Usage: ui_harness <session> <out-prefix> <shots> [omarchy-current-dir]

#include <QClipboard>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QTest>
#include <QTimer>

#include "../src/terminalitem.h"
#include "../src/theme.h"
#include "../src/workspace.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("orex-harness"); // keeps test settings apart from the real app's
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
    if (qEnvironmentVariableIsSet("OREX_SIZE")) {
        const QStringList wh = qEnvironmentVariable("OREX_SIZE").split('x');
        window->resize(wh.at(0).toInt(), wh.at(1).toInt());
    }
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
            // Never act on sessions the tester listed as off limits, such as
            // the user's own, even if an action switched to one of them.
            const QStringList protectedIds = qEnvironmentVariable("OREX_PROTECT").split(',', Qt::SkipEmptyParts);
            if (protectedIds.contains(workspace.currentSession())) {
                qInfo() << "skip (protected session)" << action;
                return;
            }
            qInfo() << "action" << action << arg;
            workspace.perform(action, arg.isEmpty() ? QVariant() : QVariant(arg));
        });
    }
    // Runs QML-side actions (palette, help, dialogs) the same way: "ms:action".
    for (const QString &step : qEnvironmentVariable("OREX_QML").split(',', Qt::SkipEmptyParts))
        QTimer::singleShot(step.section(':', 0, 0).toInt(), [window, action = step.section(':', 1)] {
            QMetaObject::invokeMethod(window, "run", Q_ARG(QVariant, action));
        });
    // Selects a session by "ms:host label/session label", to open a session
    // on a particular host without stepping through others.
    for (const QString &step : qEnvironmentVariable("OREX_SELECT").split(',', Qt::SkipEmptyParts))
        QTimer::singleShot(step.section(':', 0, 0).toInt(), [&workspace, what = step.section(':', 1)] {
            for (const QVariant &v : workspace.sessions()) {
                const QVariantMap s = v.toMap();
                if (s.value("host_label").toString() + "/" + s.value("label").toString() == what) {
                    qInfo() << "select" << what;
                    workspace.selectSession(s.value("session_id").toString());
                }
            }
        });
    // Drags with the mouse, "ms:x1,y1>x2,y2:png", and saves a screenshot
    // before releasing, to see what's drawn mid-drag.
    if (!qEnvironmentVariable("OREX_DRAG").isEmpty()) {
        const QStringList d = qEnvironmentVariable("OREX_DRAG").split(':');
        const QStringList ends = d.at(1).split('>');
        const QPointF from(ends[0].section(',', 0, 0).toDouble(), ends[0].section(',', 1).toDouble());
        const QPointF to(ends[1].section(',', 0, 0).toDouble(), ends[1].section(',', 1).toDouble());
        QTimer::singleShot(d.at(0).toInt(), [window, from, to, png = d.at(2)] {
            // QTest goes through the window system queue, with timestamps,
            // which pointer handlers need to see a drag.
            QTest::mousePress(window, Qt::LeftButton, {}, from.toPoint());
            for (int i = 1; i <= 20; ++i)
                QTest::mouseMove(window, (from + (to - from) * (i / 20.0)).toPoint(), 16);
            // Let a frame render, as it would mid-drag, before the shot.
            QTest::qWait(250);
            window->grabWindow().save(png);
            qInfo() << "dragged, mid-drag shot" << png;
            QTest::mouseRelease(window, Qt::LeftButton, {}, to.toPoint(), 16);
        });
    }
    // Moves the pointer with no button held, "ms:x,y", after making the
    // window active, as for focus that follows the mouse.
    for (const QString &step : qEnvironmentVariable("OREX_HOVER").split(';', Qt::SkipEmptyParts))
        QTimer::singleShot(step.section(':', 0, 0).toInt(), [window, at = step.section(':', 1)] {
            window->requestActivate();
            // "x1,y1>x2,y2" glides from one point to the other, crossing
            // whatever lies between, as a hand on a mouse would.
            const QStringList ends = at.split('>');
            const QPoint from(ends[0].section(',', 0, 0).toInt(), ends[0].section(',', 1).toInt());
            const QPoint to(ends.last().section(',', 0, 0).toInt(), ends.last().section(',', 1).toInt());
            for (int i = 0; i <= 10; ++i)
                QTest::mouseMove(window, from + (to - from) * i / 10, 10);
            qInfo() << "hovered" << from << "->" << to << "active" << window->isActive();
            // Grab right away: the offscreen platform drops the window's
            // activation after a moment, which ends the hover.
            QCoreApplication::processEvents();
            static int n = 0;
            window->grabWindow().save(QStringLiteral("%1/hover-%2.png").arg(qEnvironmentVariable("OREX_HOVER_DIR", "/tmp")).arg(++n));
        });
    // Adds remote hosts on a schedule, "ms:label|address;…", printing the result.
    QObject::connect(&workspace, &Workspace::hostAddFinished, [](bool ok, const QString &message) {
        qInfo() << "hostAddFinished" << ok << message;
    });
    for (const QString &step : qEnvironmentVariable("OREX_ADDHOST").split(';', Qt::SkipEmptyParts))
        QTimer::singleShot(step.section(':', 0, 0).toInt(), [&workspace, what = step.section(':', 1)] {
            qInfo() << "addRemoteHost" << what;
            workspace.addRemoteHost(what.section('|', 0, 0), what.section('|', 1));
        });
    // Right-clicks at "ms:x,y" and grabs the menu that opens; then types
    // OREX_KEYS "ms:Key,Key,…" (Qt key names) into the window.
    if (!qEnvironmentVariable("OREX_RCLICK").isEmpty()) {
        const QString v = qEnvironmentVariable("OREX_RCLICK");
        QTimer::singleShot(v.section(':', 0, 0).toInt(), [window, at = v.section(':', 1)] {
            window->requestActivate();
            const QPoint p(at.section(',', 0, 0).toInt(), at.section(',', 1).toInt());
            QTest::mouseClick(window, Qt::RightButton, {}, p);
            QTimer::singleShot(400, [window] {
                window->grabWindow().save(qEnvironmentVariable("OREX_HOVER_DIR", "/tmp") + "/rclick.png");
                qInfo() << "right-clicked, menu grabbed";
                // Picks a menu item right away, before the offscreen window
                // loses activation and the menu closes: OREX_MENUCLICK "x,y".
                if (qEnvironmentVariableIsSet("OREX_MENUCLICK")) {
                    const QString c = qEnvironmentVariable("OREX_MENUCLICK");
                    QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(c.section(',', 0, 0).toInt(), c.section(',', 1).toInt()));
                    qInfo() << "clicked menu item at" << c;
                }
            });
        });
    }
    if (!qEnvironmentVariable("OREX_KEYS").isEmpty()) {
        const QString v = qEnvironmentVariable("OREX_KEYS");
        QTimer::singleShot(v.section(':', 0, 0).toInt(), [window, keys = v.section(':', 1).split(',')] {
            for (const QString &k : keys) {
                const QKeySequence seq(k);
                QTest::keyClick(window, Qt::Key(seq[0].key()), seq[0].keyboardModifiers(), 60);
            }
            qInfo() << "typed" << keys;
        });
    }
    for (int i = 1; i <= shots; ++i)
        QTimer::singleShot(i * 2000, [=, &workspace, &app] {
            window->grabWindow().save(QStringLiteral("%1-%2.png").arg(prefix).arg(i));
            if (qEnvironmentVariableIsSet("OREX_GEOMETRY"))
                for (TerminalItem *t : window->findChildren<TerminalItem *>())
                    if (t->isVisible())
                        qInfo() << "terminal at" << t->x() << t->y() << t->width() << t->height()
                                << "scene" << t->mapToScene(QPointF(0, 0));
            if (qEnvironmentVariableIsSet("OREX_CLIPBOARD")) {
                const QString clip = QGuiApplication::clipboard()->text();
                qInfo() << "clipboard" << clip.size() << "chars, starts" << clip.left(24) << "ends" << clip.right(12);
            }
            qInfo() << "shot" << i << "windows" << workspace.windows().size() << "status" << workspace.status();
            if (i == shots) { workspace.shutdown(); QTimer::singleShot(300, &app, &QCoreApplication::quit); }
        });
    return app.exec();
}
