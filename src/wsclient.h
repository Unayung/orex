#pragma once

// A small WebSocket client over a Unix socket. Rex's local server listens
// only on a Unix socket, and QWebSocket can't open one, so this speaks just
// enough of RFC 6455 for Rex: one subprotocol, text and binary messages,
// pings, and close. No compression, which the server allows.

#include <QByteArray>
#include <QLocalSocket>
#include <QObject>

class WsClient : public QObject {
    Q_OBJECT

public:
    explicit WsClient(QObject *parent = nullptr);

    void open(const QString &socketPath, const QString &path, const QString &subprotocol);
    void sendText(const QByteArray &utf8);
    void sendBinary(const QByteArray &bytes);
    void close();

signals:
    void opened();
    void textMessage(const QByteArray &utf8);
    void binaryMessage(const QByteArray &bytes);
    void closed(int status, const QString &reason);

private:
    void onReadyRead();
    bool readHandshake();
    bool readFrame();
    void sendFrame(int opcode, const QByteArray &payload);

    QLocalSocket m_socket;
    QByteArray m_request;
    QByteArray m_buffer;
    QByteArray m_message;
    int m_messageOpcode = 0;
    bool m_upgraded = false;
    bool m_closed = false;
};
