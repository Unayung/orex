#include "wsclient.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QtEndian>

// How long a TCP or TLS connection may take to complete the WebSocket
// upgrade. An offline Tailscale peer drops packets instead of refusing them,
// so without this a connection would wait for the kernel's much longer
// timeout. The local socket has none, as before: it answers or fails at once.
static constexpr int kConnectTimeoutMs = 5000;

WsClient::WsClient(QObject *parent) : QObject(parent) {
    m_connectTimer.setSingleShot(true);
    m_connectTimer.setInterval(kConnectTimeoutMs);
    connect(&m_connectTimer, &QTimer::timeout, this, [this] {
        fail(1006, QStringLiteral("%1: no answer within %2 seconds").arg(m_peer).arg(kConnectTimeoutMs / 1000));
    });

    connect(&m_local, &QLocalSocket::connected, this, [this] { m_local.write(m_request); });
    connect(&m_local, &QLocalSocket::readyRead, this, &WsClient::onReadyRead);
    connect(&m_local, &QLocalSocket::disconnected, this, [this] { fail(1006, QStringLiteral("connection lost")); });
    connect(&m_local, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) { fail(1006, m_local.errorString()); });

    // For TLS the request waits for the handshake, which follows connected.
    connect(&m_net, &QSslSocket::connected, this, [this] {
        if (m_net.peerVerifyName().isEmpty())
            m_net.write(m_request);
    });
    connect(&m_net, &QSslSocket::encrypted, this, [this] { m_net.write(m_request); });
    connect(&m_net, &QSslSocket::readyRead, this, &WsClient::onReadyRead);
    connect(&m_net, &QSslSocket::disconnected, this, [this] { fail(1006, QStringLiteral("connection lost")); });
    // Certificate errors also end up here, as SslHandshakeFailedError, since
    // nothing calls ignoreSslErrors(): Tailscale's certificates are real ones.
    connect(&m_net, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        fail(1006, QStringLiteral("%1: %2").arg(m_peer, m_net.errorString()));
    });
}

WsClient::~WsClient() {
    // The sockets are destroyed after the rest of this object, and a
    // connected one reports disconnected() as it goes. Without this, that
    // would run fail() on a half-destroyed object and emit closed() to
    // owners that are being destroyed too.
    m_local.disconnect(this);
    m_net.disconnect(this);
}

void WsClient::open(const QString &address, const QString &path, const QString &subprotocol) {
    QString error;
    const QList<Endpoint> endpoints = parseEndpoint(address, &error);
    if (endpoints.isEmpty()) {
        // Report it the way a failed connection is reported, after open()
        // returns, so callers handle both in one place.
        m_closed = false;
        QMetaObject::invokeMethod(this, [this, error] { fail(1006, error); }, Qt::QueuedConnection);
        return;
    }
    open(endpoints.first(), path, subprotocol);
}

void WsClient::open(const Endpoint &endpoint, const QString &path, const QString &subprotocol) {
    // The same object is reused to reconnect, so start from a clean slate,
    // without reporting the end of a connection that is being replaced.
    m_closed = true;
    m_connectTimer.stop();
    m_local.abort();
    m_net.abort();
    m_buffer.clear();
    m_message.clear();
    m_upgraded = false;
    m_closed = false;
    m_peer = endpoint.toString();

    QByteArray key(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(key.data()), 4);
    const QByteArray key64 = key.toBase64();
    m_expectedAccept = QCryptographicHash::hash(key64 + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                                QCryptographicHash::Sha1)
                           .toBase64();
    // The login server picks the account from a second subprotocol, the way
    // the rex CLI asks for one; the server answers with the first.
    QByteArray protocols = subprotocol.toUtf8();
    if (!endpoint.account.isEmpty())
        protocols += ", rex.as." + endpoint.account.toUtf8();
    m_request = "GET " + path.toUtf8() + " HTTP/1.1\r\n"
                "Host: " + endpoint.hostHeader() + "\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "Sec-WebSocket-Key: " + key64 + "\r\n"
                "Sec-WebSocket-Protocol: " + protocols + "\r\n\r\n";

    switch (endpoint.kind) {
    case Endpoint::Unix:
        m_io = &m_local;
        m_local.connectToServer(endpoint.socketPath);
        return;
    case Endpoint::Tcp:
        m_io = &m_net;
        // An empty verify name tells the connected handler this is plain TCP.
        m_net.setPeerVerifyName(QString());
        m_net.connectToHost(endpoint.host, endpoint.port);
        break;
    case Endpoint::Tls:
        m_io = &m_net;
        m_net.setPeerVerifyName(endpoint.host);
        m_net.connectToHostEncrypted(endpoint.host, endpoint.port);
        break;
    }
    m_connectTimer.start();
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
    m_connectTimer.stop();
    disconnectSocket();
}

void WsClient::disconnectSocket() {
    if (m_io == &m_local)
        m_local.disconnectFromServer();
    else
        m_net.disconnectFromHost();
}

void WsClient::fail(int status, const QString &reason) {
    if (m_closed)
        return;
    m_closed = true;
    m_connectTimer.stop();
    // A timed-out or refused upgrade leaves the socket open, and nothing
    // else will close it.
    if (!m_upgraded)
        m_io->close();
    emit closed(status, reason);
}

void WsClient::onReadyRead() {
    m_buffer += m_io->readAll();
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
    const QList<QByteArray> lines = head.split('\n');
    const QByteArray status = lines.first().trimmed();
    if (!status.startsWith("HTTP/1.1 101")) {
        fail(1002, QStringLiteral("%1: answered %2").arg(m_peer, QString::fromUtf8(status)));
        return false;
    }
    // The accept value proves the answer came from a WebSocket server that
    // read this request, not from some other service on the same port.
    QByteArray accept;
    for (const QByteArray &line : lines) {
        const int colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == "sec-websocket-accept")
            accept = line.mid(colon + 1).trimmed();
    }
    if (accept != m_expectedAccept) {
        fail(1002, QStringLiteral("%1: the WebSocket upgrade has a wrong Sec-WebSocket-Accept").arg(m_peer));
        return false;
    }
    m_upgraded = true;
    m_connectTimer.stop();
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
        disconnectSocket();
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
    m_io->write(frame);
}
