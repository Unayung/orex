#pragma once

// Connects one Rex terminal block to one TerminalItem: streams the block's
// output into the item over a data connection, and types the item's input
// into the block over the control connection.

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>

#include "wsclient.h"

class RexClient;
class TerminalItem;

class BlockStream : public QObject {
    Q_OBJECT

public:
    BlockStream(RexClient *rex, const QString &sessionId, const QString &blockId,
                TerminalItem *terminal, QObject *parent = nullptr);

    // The session must already be attached with session.attach.
    void start();
    void stop();
    // Asks Rex to give the pty these default colors, as set_theme arguments.
    // Rex applies them while this client owns the block.
    void setTheme(const QJsonObject &theme);

    QString blockId() const { return m_blockId; }

    // How this client claims the pty's size: "take" or "request".
    static QString ownershipMode();

signals:
    void childExited(int exitCode);
    void ended(const QString &reason);

private:
    void onBinary(const QByteArray &bytes);
    void handleLive();
    void handleRexMessage(const QByteArray &json);
    void sendInput(const QByteArray &bytes);
    void flushInput();
    void requestSize(int cols, int rows);

    RexClient *m_rex;
    QString m_sessionId;
    QString m_blockId;
    QPointer<TerminalItem> m_terminal;
    WsClient m_data;

    enum class Phase { Handshake, Snapshot, Live } m_phase = Phase::Handshake;
    // ESC _ rex1;<nonce>; which starts every message Rex puts in the stream.
    QByteArray m_marker;
    QByteArray m_buffer;

    QJsonObject m_theme;
    QByteArray m_unsentInput;
    bool m_writing = false;
};
