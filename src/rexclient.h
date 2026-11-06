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

    // The server's Unix socket: $REX_SERVER, else /run/user/<uid>/rex/server.sock.
    static QString defaultSocketPath();

    void open(const QString &socketPath);
    void call(const QString &method, const QJsonObject &payload, Callback done = {});

    QString socketPath() const { return m_socketPath; }

signals:
    void ready();
    // The event inside each event message: {"type": ..., "payload": {...}}.
    void serverEvent(const QJsonObject &event);
    void closed(const QString &reason);

private:
    void onText(const QByteArray &utf8);

    WsClient m_ws;
    QString m_socketPath;
    QHash<QString, Callback> m_pending;
    quint64 m_nextId = 1;
};
