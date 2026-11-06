#include "blockstream.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include "rexclient.h"
#include "terminalitem.h"

BlockStream::BlockStream(RexClient *rex, const QString &sessionId, const QString &blockId,
                         TerminalItem *terminal, QObject *parent)
    : QObject(parent), m_rex(rex), m_sessionId(sessionId), m_blockId(blockId), m_terminal(terminal) {
    connect(&m_data, &WsClient::binaryMessage, this, &BlockStream::onBinary);
    connect(&m_data, &WsClient::closed, this, [this](int status, const QString &reason) {
        emit ended(QStringLiteral("%1 %2").arg(status).arg(reason));
    });
    connect(terminal, &TerminalItem::input, this, &BlockStream::sendInput);
    connect(terminal, &TerminalItem::gridSizeChanged, this, &BlockStream::requestSize);
    m_resizeTimer.setSingleShot(true);
    m_resizeTimer.setInterval(120);
    connect(&m_resizeTimer, &QTimer::timeout, this, &BlockStream::sendSize);
}

QString BlockStream::ownershipMode() {
    // The machine you sit at decides the size: orex takes the pty when it
    // attaches, and stop() gives it back so the Mac app can size it again.
    // "request" would instead leave a block with the Mac app while it's there.
    return QStringLiteral("take");
}

void BlockStream::start() {
    const QJsonObject args{{"session_id", m_sessionId},
                           {"block_id", m_blockId},
                           {"data_ref_id", "terminal"},
                           {"subprotocol", "rex.data.terminal.v1"}};
    m_rex->call("data.connect", args, [this](const QJsonObject &payload, const QJsonObject &error) {
        if (!error.isEmpty()) {
            emit ended(error.value("message").toString());
            return;
        }
        const QByteArray ticket = QByteArray::fromBase64(
            payload.value("ticket").toString().toLatin1(),
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        connect(&m_data, &WsClient::opened, this, [this, ticket] {
            m_data.sendBinary(QByteArray(1, '\x01') + ticket);
        });
        m_rex->openData(m_data, "/api/data", "rex.data.terminal.v1");
    });

    claim();
}

void BlockStream::claim() {
    m_rex->call("session.own_block",
                {{"session_id", m_sessionId},
                 {"block_ids", QJsonArray{m_blockId}},
                 {"mode", ownershipMode()}},
                [this](const QJsonObject &, const QJsonObject &) {
                    // Size and colors apply once we own the block, so send
                    // ours again: another client may have changed both.
                    if (m_terminal && m_terminal->cols() > 0)
                        requestSize(m_terminal->cols(), m_terminal->rows());
                    if (!m_theme.isEmpty())
                        setTheme(m_theme);
                });
}

void BlockStream::setTheme(const QJsonObject &theme) {
    m_theme = theme;
    if (theme.isEmpty())
        return;
    m_rex->call("com.superlogical.terminal.set_theme",
                {{"session_id", m_sessionId}, {"block_id", m_blockId}, {"args", theme}});
}

void BlockStream::stop() {
    // Giving the block back lets the Mac app size it again.
    m_rex->call("session.disown_block", {{"session_id", m_sessionId}, {"block_ids", QJsonArray{m_blockId}}});
    m_data.close();
}

void BlockStream::onBinary(const QByteArray &bytes) {
    if (m_phase == Phase::Handshake) {
        if (bytes.size() == 32 && uchar(bytes[0]) == 0x81) {
            const quint64 nonce = qFromLittleEndian<quint64>(bytes.constData() + 24);
            m_marker = "\x1b_rex1;" + QByteArray::number(nonce) + ";";
            m_phase = Phase::Snapshot;
        } else {
            emit ended(QStringLiteral("data handshake rejected, reason 0x%1")
                           .arg(bytes.size() > 1 ? uchar(bytes[1]) : 0, 2, 16, QChar('0')));
        }
        return;
    }

    m_buffer += bytes;
    if (m_phase == Phase::Snapshot) {
        // Rex sends a resize message right after the snapshot, so the first
        // marker is where the snapshot ends and live output begins.
        const qsizetype end = m_buffer.indexOf(m_marker);
        if (end < 0)
            return;
        if (m_terminal && !m_terminal->restoreSnapshot(m_buffer.left(end)))
            emit ended(QStringLiteral("could not read the terminal snapshot"));
        m_buffer.remove(0, end);
        m_phase = Phase::Live;
    }
    handleLive();
}

void BlockStream::handleLive() {
    for (;;) {
        const qsizetype start = m_buffer.indexOf(m_marker);
        if (start < 0) {
            // Hold back a tail that could be the start of a marker split
            // across two messages, and feed the rest.
            qsizetype keep = qMin<qsizetype>(m_marker.size() - 1, m_buffer.size());
            while (keep > 0 && !m_buffer.endsWith(m_marker.left(keep)))
                --keep;
            if (m_terminal && m_buffer.size() > keep)
                m_terminal->feed(m_buffer.left(m_buffer.size() - keep));
            m_buffer.remove(0, m_buffer.size() - keep);
            return;
        }
        if (start > 0 && m_terminal)
            m_terminal->feed(m_buffer.left(start));
        const qsizetype end = m_buffer.indexOf("\x1b\\", start + m_marker.size());
        if (end < 0) {
            m_buffer.remove(0, start);
            return; // the rest of the message hasn't arrived yet
        }
        handleRexMessage(m_buffer.mid(start + m_marker.size(), end - start - m_marker.size()));
        m_buffer.remove(0, end + 2);
    }
}

void BlockStream::handleRexMessage(const QByteArray &json) {
    const QJsonObject message = QJsonDocument::fromJson(json).object();
    const QString type = message.value("type").toString();
    if (type == "resize") {
        if (m_terminal)
            m_terminal->resizeGrid(message.value("cols").toInt(), message.value("rows").toInt());
    } else if (type == "theme") {
        // Rex echoes the pty's colors, whoever set them, so the pane always
        // draws what programs in it see when they ask.
        if (m_terminal)
            m_terminal->setTheme(message);
    } else if (type == "child_exited") {
        emit childExited(message.value("exit_code").toInt());
    }
}

void BlockStream::sendInput(const QByteArray &bytes) {
    m_unsentInput += bytes;
    flushInput();
}

void BlockStream::flushInput() {
    // Block methods run concurrently on the server, so two writes in flight
    // could arrive out of order. Send one at a time and batch what piles up.
    if (m_writing || m_unsentInput.isEmpty())
        return;
    m_writing = true;
    const QString data = QString::fromUtf8(std::exchange(m_unsentInput, {}));
    m_rex->call("com.superlogical.terminal.write",
                {{"session_id", m_sessionId}, {"block_id", m_blockId}, {"args", QJsonObject{{"data", data}}}},
                [this](const QJsonObject &, const QJsonObject &) {
                    m_writing = false;
                    flushInput();
                });
}

void BlockStream::requestSize(int cols, int rows) {
    if (cols <= 0 || rows <= 0)
        return;
    // A drag of the window or the sidebar edge changes the size many times a
    // second; each resize makes the program redraw, so send only where the
    // size settles.
    m_pendingSize = QSize(cols, rows);
    m_resizeTimer.start();
}

void BlockStream::sendSize() {
    if (m_pendingSize.isEmpty())
        return;
    m_rex->call("com.superlogical.terminal.resize",
                {{"session_id", m_sessionId},
                 {"block_id", m_blockId},
                 {"args", QJsonObject{{"columns", m_pendingSize.width()}, {"rows", m_pendingSize.height()}}}});
}
