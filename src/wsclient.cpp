#include "wsclient.h"

#include <QRandomGenerator>
#include <QtEndian>

WsClient::WsClient(QObject *parent) : QObject(parent) {
    connect(&m_socket, &QLocalSocket::connected, this, [this] { m_socket.write(m_request); });
    connect(&m_socket, &QLocalSocket::readyRead, this, &WsClient::onReadyRead);
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        if (!m_closed) {
            m_closed = true;
            emit closed(1006, QStringLiteral("connection lost"));
        }
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (!m_closed) {
            m_closed = true;
            emit closed(1006, m_socket.errorString());
        }
    });
}

void WsClient::open(const QString &socketPath, const QString &path, const QString &subprotocol) {
    QByteArray key(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(key.data()), 4);
    m_request = "GET " + path.toUtf8() + " HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "Sec-WebSocket-Key: " + key.toBase64() + "\r\n"
                "Sec-WebSocket-Protocol: " + subprotocol.toUtf8() + "\r\n\r\n";
    m_socket.connectToServer(socketPath);
}

void WsClient::sendText(const QByteArray &utf8) { sendFrame(0x1, utf8); }
void WsClient::sendBinary(const QByteArray &bytes) { sendFrame(0x2, bytes); }

void WsClient::close() {
    if (m_closed)
        return;
    QByteArray payload(2, 0);
    qToBigEndian<quint16>(1000, payload.data());
    sendFrame(0x8, payload);
    m_closed = true;
    m_socket.disconnectFromServer();
}

void WsClient::onReadyRead() {
    m_buffer += m_socket.readAll();
    if (!m_upgraded && !readHandshake())
        return;
    while (!m_closed && readFrame()) {
    }
}

bool WsClient::readHandshake() {
    const int end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0)
        return false;
    const QByteArray head = m_buffer.left(end);
    m_buffer.remove(0, end + 4);
    // ponytail: trusts the 101 without checking Sec-WebSocket-Accept, since the
    // peer is our own user's Unix socket. Check it once TCP hosts are added.
    if (!head.startsWith("HTTP/1.1 101")) {
        m_closed = true;
        emit closed(1002, QString::fromUtf8(head.left(head.indexOf("\r\n"))));
        m_socket.disconnectFromServer();
        return false;
    }
    m_upgraded = true;
    emit opened();
    return true;
}

bool WsClient::readFrame() {
    if (m_buffer.size() < 2)
        return false;
    const auto *b = reinterpret_cast<const uchar *>(m_buffer.constData());
    const bool fin = b[0] & 0x80;
    const int opcode = b[0] & 0x0f;
    quint64 len = b[1] & 0x7f;
    int header = 2;
    if (len == 126) {
        if (m_buffer.size() < 4)
            return false;
        len = qFromBigEndian<quint16>(b + 2);
        header = 4;
    } else if (len == 127) {
        if (m_buffer.size() < 10)
            return false;
        len = qFromBigEndian<quint64>(b + 2);
        header = 10;
    }
    // Servers never mask their frames, so there is no mask key to skip.
    if (quint64(m_buffer.size()) < header + len)
        return false;
    const QByteArray payload = m_buffer.mid(header, int(len));
    m_buffer.remove(0, header + int(len));

    switch (opcode) {
    case 0x0: // continuation
    case 0x1:
    case 0x2:
        if (opcode != 0x0)
            m_messageOpcode = opcode;
        m_message += payload;
        if (fin) {
            const QByteArray message = std::exchange(m_message, {});
            if (m_messageOpcode == 0x1)
                emit textMessage(message);
            else
                emit binaryMessage(message);
        }
        break;
    case 0x8: {
        const int status = payload.size() >= 2 ? qFromBigEndian<quint16>(payload.constData()) : 1005;
        const QString reason = QString::fromUtf8(payload.mid(2));
        if (!m_closed)
            sendFrame(0x8, payload.left(2));
        m_closed = true;
        m_socket.disconnectFromServer();
        emit closed(status, reason);
        break;
    }
    case 0x9:
        sendFrame(0xA, payload);
        break;
    default: // pong, or a reserved opcode we skip
        break;
    }
    return true;
}

void WsClient::sendFrame(int opcode, const QByteArray &payload) {
    if (m_closed && opcode != 0x8)
        return;
    QByteArray frame;
    frame.append(char(0x80 | opcode));
    const quint64 len = payload.size();
    // Clients must mask every frame they send.
    if (len < 126) {
        frame.append(char(0x80 | len));
    } else if (len < 65536) {
        frame.append(char(0x80 | 126));
        char ext[2];
        qToBigEndian<quint16>(quint16(len), ext);
        frame.append(ext, 2);
    } else {
        frame.append(char(0x80 | 127));
        char ext[8];
        qToBigEndian<quint64>(len, ext);
        frame.append(ext, 8);
    }
    const quint32 maskWord = QRandomGenerator::global()->generate();
    const char *mask = reinterpret_cast<const char *>(&maskWord);
    frame.append(mask, 4);
    QByteArray masked = payload;
    for (qsizetype i = 0; i < masked.size(); ++i)
        masked[i] = char(masked[i] ^ mask[i % 4]);
    frame += masked;
    m_socket.write(frame);
}
