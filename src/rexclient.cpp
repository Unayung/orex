#include "rexclient.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <unistd.h>

// The API revision this client was written against. The server translates
// older and newer shapes, so this only moves when orex uses something new.
static constexpr int kApiRevision = 10;

RexClient::RexClient(QObject *parent) : QObject(parent) {
    connect(&m_ws, &WsClient::opened, this, [this] {
        m_opened = true;
        m_candidates.clear();
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
        if (m_opened) {
            emit closed(QStringLiteral("%1 %2").arg(status).arg(reason));
            return;
        }
        m_failures << reason;
        if (m_candidates.isEmpty()) {
            emit closed(m_failures.join(QStringLiteral("; ")));
            return;
        }
        // The socket that failed may still be inside its own signal, so the
        // next attempt waits for the event loop instead of reusing it here.
        QMetaObject::invokeMethod(this, [this, generation = m_generation] {
            if (generation == m_generation)
                tryNext();
        }, Qt::QueuedConnection);
    });
}

QString RexClient::defaultServer() {
    const QString fromEnv = qEnvironmentVariable("REX_SERVER");
    if (!fromEnv.isEmpty())
        return fromEnv;
    return QStringLiteral("/run/user/%1/rex/server.sock").arg(getuid());
}

void RexClient::open(const QString &address) {
    ++m_generation;
    m_opened = false;
    m_failures.clear();
    QString error;
    m_candidates = parseEndpoint(address, &error);
    if (m_candidates.isEmpty()) {
        // Like a failed connection, this is reported after open() returns.
        QMetaObject::invokeMethod(this, [this, error] { emit closed(error); }, Qt::QueuedConnection);
        return;
    }
    tryNext();
}

void RexClient::tryNext() {
    m_endpoint = m_candidates.takeFirst();
    m_ws.open(m_endpoint, "/api/control", "rex.control.v1");
}

void RexClient::openData(WsClient &ws, const QString &path, const QString &subprotocol) const {
    ws.open(m_endpoint, path, subprotocol);
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
