// orex — a Rex client for Omarchy: shows a Rex server's terminal sessions.

#include <QGuiApplication>
#include <QJsonArray>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QUrl>

#include "blockstream.h"
#include "rexclient.h"
#include "terminalitem.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("orex");
    app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("orex");
    QQuickStyle::setStyle("Material");

    qmlRegisterType<TerminalItem>("Orex", 1, 0, "TerminalItem");

    QQmlApplicationEngine engine;
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    QObject *window = engine.rootObjects().first();
    auto *terminal = window->findChild<TerminalItem *>("terminal");
    auto setStatus = [window](const QString &text) { window->setProperty("status", text); };

    // ponytail: M1 shows one block, the focused pane of the chosen session's
    // active tab. M2 replaces this with a session list, tabs, and splits.
    const QString wanted = app.arguments().value(1);
    RexClient rex;
    BlockStream *stream = nullptr;

    QObject::connect(&rex, &RexClient::closed, &app, [&](const QString &reason) {
        setStatus(QObject::tr("Disconnected from Rex: %1").arg(reason));
    });
    QObject::connect(&rex, &RexClient::ready, &app, [&] {
        rex.call("session.list", {}, [&](const QJsonObject &list, const QJsonObject &error) {
            if (!error.isEmpty())
                return setStatus(error.value("message").toString());
            QJsonObject session;
            for (const QJsonValue &value : list.value("sessions").toArray()) {
                const QJsonObject s = value.toObject();
                if (wanted.isEmpty() || s.value("label").toString() == wanted ||
                    s.value("session_id").toString().endsWith(wanted)) {
                    session = s;
                    break;
                }
            }
            if (session.isEmpty())
                return setStatus(wanted.isEmpty() ? QObject::tr("The Rex server has no sessions.")
                                                  : QObject::tr("No Rex session matches \"%1\".").arg(wanted));
            const QString sessionId = session.value("session_id").toString();
            window->setProperty("title", QStringLiteral("orex — %1").arg(session.value("label").toString()));
            rex.call("session.attach", {{"session_id", sessionId}},
                     [&, sessionId](const QJsonObject &view, const QJsonObject &error) {
                if (!error.isEmpty())
                    return setStatus(error.value("message").toString());
                const QJsonObject v = view.contains("view") ? view.value("view").toObject() : view;
                const QString activeWindow = v.value("active_window_id").toString();
                QString blockId;
                for (const QJsonValue &w : v.value("windows").toArray())
                    if (w.toObject().value("window_id").toString() == activeWindow)
                        blockId = w.toObject().value("focused_block_id").toString();
                if (blockId.isEmpty())
                    return setStatus(QObject::tr("The session has no focused pane."));
                stream = new BlockStream(&rex, sessionId, blockId, terminal, &app);
                QObject::connect(stream, &BlockStream::ended, &app, [&](const QString &reason) {
                    setStatus(QObject::tr("The pane's stream ended: %1").arg(reason));
                });
                stream->start();
                setStatus(QString());
            });
        });
    });
    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&] {
        if (stream)
            stream->stop();
    });

    rex.open(RexClient::defaultSocketPath());
    return app.exec();
}
