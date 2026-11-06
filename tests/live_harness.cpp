// Drives orex's real protocol layer against a live Rex session, offscreen:
// attach, type a command through TerminalItem key events, grab the screen.
// Usage: live_harness <session-id> <block-id> <out.png>

#include <QGuiApplication>
#include <QJsonArray>
#include <QKeyEvent>
#include <QQuickView>
#include <QTimer>

#include "../src/blockstream.h"
#include "../src/rexclient.h"
#include "../src/terminalitem.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    const QString sessionId = argv[1], blockId = argv[2], out = argv[3];
    QQuickView view;
    view.resize(900, 400);
    auto *term = new TerminalItem(view.contentItem());
    term->setSize(QSizeF(900, 400));
    term->setFocus(true);
    view.show();

    RexClient rex;
    BlockStream *stream = nullptr;
    QObject::connect(&rex, &RexClient::ready, [&] {
        rex.call("session.attach", {{"session_id", sessionId}}, [&](const QJsonObject &, const QJsonObject &err) {
            if (!err.isEmpty()) { qWarning() << "attach" << err; app.exit(1); return; }
            stream = new BlockStream(&rex, sessionId, blockId, term, &app);
            QObject::connect(stream, &BlockStream::ended, [](const QString &r) { qWarning() << "ended" << r; });
            stream->start();
        });
    });
    rex.open(RexClient::defaultSocketPath());

    // Type "echo orex-ok; tput cols lines" with real key events after the stream settles.
    QTimer::singleShot(1500, [&] {
        for (QChar c : QStringLiteral("echo orex-ok 中文; tput cols lines")) {
            QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
            QCoreApplication::sendEvent(term, &press);
        }
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, "\r");
        QCoreApplication::sendEvent(term, &enter);
    });
    QTimer::singleShot(3500, [&] {
        view.grabWindow().save(out);
        qInfo() << "grid" << term->cols() << "x" << term->rows();
        stream->stop();
        QTimer::singleShot(300, &app, &QCoreApplication::quit);
    });
    return app.exec();
}
