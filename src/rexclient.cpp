#include "rexclient.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <unistd.h>

// The API revision this client was written against. The server translates
// older and newer shapes, so this only moves when orex uses something new.
static constexpr int kApiRevision = 10;

RexClient::RexClient(QObject *parent) : QObject(parent) {
    connect(&m_ws, &WsClient::opened, this, [this] {
        const QJsonObject hello{
            {"type", "hello"},
            {"api_revision", kApiRevision},
            {"client", QJsonObject{{"kind", "app"},
                                   {"platform", "linux"},
                                   {"name", "orex"},
                                   {"version", QCoreApplication::applicationVersion()}}},
        };
        m_ws.sendText(QJsonDocument(hello).toJson(QJsonDocument::Compact));
    });
    connect(&m_ws, &WsClient::textMessage, this, &RexClient::onText);
    connect(&m_ws, &WsClient::closed, this, [this](int status, const QString &reason) {
        emit closed(QStringLiteral("%1 %2").arg(status).arg(reason));
    });
}

QString RexClient::defaultSocketPath() {
    const QString fromEnv = qEnvironmentVariable("REX_SERVER");
    if (fromEnv.startsWith('/'))
        return fromEnv;
    if (fromEnv.startsWith("unix://"))
        return fromEnv.mid(7);
    return QStringLiteral("/run/user/%1/rex/server.sock").arg(getuid());
}

void RexClient::open(const QString &socketPath) {
    m_socketPath = socketPath;
    m_ws.open(socketPath, "/api/control", "rex.control.v1");
}

void RexClient::call(const QString &method, const QJsonObject &payload, Callback done) {
    const QString id = QString::number(m_nextId++);
    if (done)
        m_pending.insert(id, std::move(done));
    const QJsonObject request{{"type", "request"}, {"id", id}, {"method", method}, {"payload", payload}};
    m_ws.sendText(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void RexClient::onText(const QByteArray &utf8) {
    const QJsonObject message = QJsonDocument::fromJson(utf8).object();
    const QString type = message.value("type").toString();
    if (type == "welcome") {
        emit ready();
    } else if (type == "response") {
        const Callback done = m_pending.take(message.value("id").toString());
        if (done)
            done(message.value("payload").toObject(), message.value("error").toObject());
    } else if (type == "event") {
        emit serverEvent(message.value("payload").toObject());
    }
    // Other types come from newer servers; the protocol says to skip them.
}
