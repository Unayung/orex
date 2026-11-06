#pragma once

// The control connection to a Rex server: one WebSocket that carries every
// method call and event, as JSON text messages (protocol rex.control.v1).

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <functional>

#include "wsclient.h"

class RexClient : public QObject {
    Q_OBJECT

public:
    // A response's payload, or its error object with code, message, status.
    using Callback = std::function<void(const QJsonObject &payload, const QJsonObject &error)>;

    explicit RexClient(QObject *parent = nullptr);

    // The server's address: $REX_SERVER in any form parseEndpoint() takes,
    // else the local server's socket, /run/user/<uid>/rex/server.sock.
    static QString defaultServer();
    // The old name of defaultServer(), from when only Unix sockets worked.
    static QString defaultSocketPath() { return defaultServer(); }

    // Connects to the first endpoint of the address whose WebSocket upgrade
    // succeeds, trying a bare host name's candidates in Rex's order. If none
    // does, or the address doesn't parse, emits closed() with the reasons.
    void open(const QString &address);
    void call(const QString &method, const QJsonObject &payload, Callback done = {});

    // The endpoint the control connection reached, valid after ready().
    Endpoint endpoint() const { return m_endpoint; }
    // Opens a data connection to the same server, and the same account.
    void openData(WsClient &ws, const QString &path, const QString &subprotocol) const;
    // The reached endpoint written as an address, which WsClient::open()
    // also takes. Kept for callers written before openData().
    QString socketPath() const { return m_endpoint.toString(); }

signals:
    void ready();
    // The event inside each event message: {"type": ..., "payload": {...}}.
    void serverEvent(const QJsonObject &event);
    void closed(const QString &reason);

private:
    void onText(const QByteArray &utf8);
    void tryNext();

    WsClient m_ws;
    QList<Endpoint> m_candidates;
    QStringList m_failures;
    Endpoint m_endpoint;
    bool m_opened = false;
    // Counts open() calls, so a retry queued for an older one is dropped.
    quint64 m_generation = 0;
    QHash<QString, Callback> m_pending;
    quint64 m_nextId = 1;
};
