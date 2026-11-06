#pragma once

// A small WebSocket client over a Unix socket, TCP, or TLS. Rex's local
// server listens only on a Unix socket, and QWebSocket can't open one (nor is
// qt6-websockets installed), so this speaks just enough of RFC 6455 for Rex:
// one subprotocol, text and binary messages, pings, and close. No
// compression, which the server allows.

#include <QByteArray>
#include <QLocalSocket>
#include <QObject>
#include <QSslSocket>
#include <QTimer>

#include "endpoint.h"

class WsClient : public QObject {
    Q_OBJECT

public:
    explicit WsClient(QObject *parent = nullptr);
    ~WsClient() override;

    void open(const Endpoint &endpoint, const QString &path, const QString &subprotocol);
    // The same, for an address that names exactly one endpoint, such as a
    // socket path or RexClient::socketPath(). A bare host name opens its
    // first candidate only; RexClient::open() is what probes the others.
    void open(const QString &address, const QString &path, const QString &subprotocol);
    void sendText(const QByteArray &utf8);
    void sendBinary(const QByteArray &bytes);
    void close();

signals:
    void opened();
    void textMessage(const QByteArray &utf8);
    void binaryMessage(const QByteArray &bytes);
    // Also emitted when the connection or the upgrade fails, before opened().
    void closed(int status, const QString &reason);

private:
    void onReadyRead();
    void fail(int status, const QString &reason);
    bool readHandshake();
    bool readFrame();
    void sendFrame(int opcode, const QByteArray &payload);
    void disconnectSocket();

    // One of these carries the connection, and m_io points at it.
    QLocalSocket m_local;
    QSslSocket m_net;
    QIODevice *m_io = &m_local;
    QTimer m_connectTimer;
    QString m_peer;
    QByteArray m_request;
    QByteArray m_expectedAccept;
    QByteArray m_buffer;
    QByteArray m_message;
    int m_messageOpcode = 0;
    bool m_upgraded = false;
    bool m_closed = false;
};
