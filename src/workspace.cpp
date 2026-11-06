#include "workspace.h"

#include <QJsonArray>
#include <QQuickItem>
#include <QRectF>

#include "blockstream.h"
#include "terminalitem.h"

// Space between panes, where the area's background shows as a divider.
static constexpr qreal kGap = 2;

// The blocks of a layout tree with their rects, as fractions of the window.
static void collectBlocks(const QJsonObject &node, QList<QPair<QString, QRectF>> &out) {
    if (node.contains("block_id")) {
        const QJsonArray r = node.value("rect").toArray();
        out.append({node.value("block_id").toString(),
                    QRectF(r.at(0).toDouble(), r.at(1).toDouble(), r.at(2).toDouble(), r.at(3).toDouble())});
        return;
    }
    const QJsonObject split = node.value("split").toObject();
    collectBlocks(split.value("before").toObject(), out);
    collectBlocks(split.value("after").toObject(), out);
}

Workspace::Workspace(QObject *parent) : QObject(parent) {
    connect(&m_rex, &RexClient::serverEvent, this, &Workspace::onEvent);
    connect(&m_rex, &RexClient::closed, this, [this](const QString &reason) {
        setStatus(tr("Disconnected from Rex: %1").arg(reason));
    });
    connect(&m_rex, &RexClient::ready, this, [this] {
        // Session-wide events keep the session list current; block output
        // comes over data connections instead, so block events aren't needed.
        m_rex.call("session.attach_all", {{"event_scope", "session"}},
                   [this](const QJsonObject &result, const QJsonObject &error) {
            if (!error.isEmpty())
                return setStatus(error.value("message").toString());
            QVariantList sessions;
            for (const QJsonValue &s : result.value("sessions").toArray())
                sessions.append(s.toObject().toVariantMap());
            m_sessions = sessions;
            emit sessionsChanged();
            QString pick;
            for (const QVariant &s : std::as_const(m_sessions)) {
                const QVariantMap map = s.toMap();
                const QString id = map.value("session_id").toString();
                if (m_wanted.isEmpty() || map.value("label").toString() == m_wanted || id.endsWith(m_wanted)) {
                    pick = id;
                    break;
                }
            }
            if (!pick.isEmpty())
                selectSession(pick);
            else
                setStatus(m_sessions.isEmpty() ? tr("The Rex server has no sessions. Start one with “rex new”.")
                                               : tr("No Rex session matches “%1”.").arg(m_wanted));
        });
    });
}

Workspace::~Workspace() { closeAllPanes(); }

void Workspace::start(const QString &wanted) {
    m_wanted = wanted;
    setStatus(tr("Connecting to Rex…"));
    m_rex.open(RexClient::defaultSocketPath());
}

void Workspace::setPaneArea(QQuickItem *area) {
    m_area = area;
    connect(area, &QQuickItem::widthChanged, this, &Workspace::layoutPanes);
    connect(area, &QQuickItem::heightChanged, this, &Workspace::layoutPanes);
    layoutPanes();
}

QVariantList Workspace::windows() const {
    QVariantList list;
    for (const QJsonValue &w : m_view.value("windows").toArray()) {
        const QJsonObject window = w.toObject();
        list.append(QVariantMap{{"window_id", window.value("window_id").toString()},
                                {"label", window.value("label").toString()}});
    }
    return list;
}

void Workspace::selectSession(const QString &sessionId) {
    if (sessionId == m_sessionId)
        return;
    closeAllPanes();
    if (!m_sessionId.isEmpty())
        m_rex.call("session.detach", {{"session_id", m_sessionId}});
    m_sessionId = sessionId;
    m_view = {};
    m_revision = -1;
    emit currentSessionChanged();
    emit viewChanged();
    m_rex.call("session.attach", {{"session_id", sessionId}},
               [this, sessionId](const QJsonObject &view, const QJsonObject &error) {
        if (sessionId != m_sessionId)
            return;
        if (!error.isEmpty())
            return setStatus(error.value("message").toString());
        setStatus(QString());
        applyView(view.contains("view") ? view.value("view").toObject() : view);
    });
}

void Workspace::selectWindow(const QString &windowId) {
    if (m_sessionId.isEmpty() || windowId == activeWindow())
        return;
    m_rex.call("session.focus_window", {{"session_id", m_sessionId}, {"window_id", windowId}});
}

void Workspace::stepWindow(int delta) {
    const QVariantList list = windows();
    if (list.isEmpty())
        return;
    int i = 0;
    while (i < list.size() && list.at(i).toMap().value("window_id").toString() != activeWindow())
        ++i;
    selectWindow(list.at(((i + delta) % list.size() + list.size()) % list.size()).toMap().value("window_id").toString());
}

void Workspace::stepSession(int delta) {
    if (m_sessions.isEmpty())
        return;
    int i = 0;
    while (i < m_sessions.size() && m_sessions.at(i).toMap().value("session_id").toString() != m_sessionId)
        ++i;
    const int n = m_sessions.size();
    selectSession(m_sessions.at(((i + delta) % n + n) % n).toMap().value("session_id").toString());
}

void Workspace::shutdown() { closeAllPanes(); }

void Workspace::onEvent(const QJsonObject &event) {
    const QString type = event.value("type").toString();
    const QJsonObject payload = event.value("payload").toObject();
    if (type == "session_created" || type == "session_moved") {
        refreshSessions();
    } else if (type == "session_destroyed") {
        refreshSessions();
        if (payload.value("session_id").toString() == m_sessionId) {
            closeAllPanes();
            m_sessionId.clear();
            m_view = {};
            emit currentSessionChanged();
            emit viewChanged();
            setStatus(tr("The session was closed."));
        }
    } else if (type == "session_event") {
        const QJsonObject inner = payload.value("event").toObject();
        const QString innerType = inner.value("type").toString();
        if (innerType == "session_label_changed") {
            refreshSessions();
        } else if (innerType == "session_view_changed" && payload.value("session_id").toString() == m_sessionId) {
            // Changes can arrive out of order; only a newer revision means
            // the view we hold is stale.
            if (inner.value("payload").toObject().value("revision").toInteger() > m_revision)
                fetchView();
        }
    }
}

void Workspace::refreshSessions() {
    m_rex.call("session.list", {}, [this](const QJsonObject &result, const QJsonObject &error) {
        if (!error.isEmpty())
            return;
        QVariantList sessions;
        for (const QJsonValue &s : result.value("sessions").toArray())
            sessions.append(s.toObject().toVariantMap());
        m_sessions = sessions;
        emit sessionsChanged();
    });
}

void Workspace::fetchView() {
    // One request at a time; a burst of changes needs only one more fetch.
    if (m_fetching) {
        m_refetch = true;
        return;
    }
    m_fetching = true;
    const QString sessionId = m_sessionId;
    m_rex.call("session.view", {{"session_id", sessionId}},
               [this, sessionId](const QJsonObject &view, const QJsonObject &error) {
        m_fetching = false;
        if (sessionId == m_sessionId && error.isEmpty())
            applyView(view);
        if (std::exchange(m_refetch, false))
            fetchView();
    });
}

void Workspace::applyView(const QJsonObject &view) {
    const qint64 revision = view.value("revision").toInteger();
    if (revision <= m_revision)
        return;
    m_revision = revision;
    m_view = view;
    emit viewChanged();
    layoutPanes();
}

void Workspace::layoutPanes() {
    if (!m_area || m_view.isEmpty())
        return;

    // Every block the session still has, so panes of closed blocks go away
    // while panes in other tabs keep streaming in the background.
    QSet<QString> alive;
    QJsonObject active;
    for (const QJsonValue &w : m_view.value("windows").toArray()) {
        const QJsonObject window = w.toObject();
        for (const QJsonValue &layer : window.value("layers").toArray())
            for (const QJsonValue &b : layer.toObject().value("blocks").toArray())
                alive.insert(b.toObject().value("block_id").toString());
        if (window.value("window_id").toString() == activeWindow())
            active = window;
    }
    for (const QString &blockId : m_panes.keys())
        if (!alive.contains(blockId))
            closePane(blockId);

    // ponytail: only the tiled layout is drawn. Floating layers wait until
    // someone uses them from the Mac app.
    QList<QPair<QString, QRectF>> placed;
    const QString zoomed = active.value("zoomed_block_id").toString();
    if (!zoomed.isEmpty()) {
        placed.append({zoomed, QRectF(0, 0, 1, 1)});
    } else {
        for (const QJsonValue &layer : active.value("layers").toArray())
            if (layer.toObject().value("kind").toString() == "tiled")
                collectBlocks(layer.toObject().value("layout").toObject(), placed);
    }

    for (Pane &pane : m_panes)
        if (pane.terminal)
            pane.terminal->setVisible(false);

    const qreal w = m_area->width(), h = m_area->height();
    for (const auto &[blockId, r] : std::as_const(placed)) {
        Pane &pane = m_panes[blockId];
        if (!pane.stream) {
            pane.terminal = new TerminalItem(m_area);
            pane.stream = new BlockStream(&m_rex, m_sessionId, blockId, pane.terminal, this);
            connect(pane.stream, &BlockStream::ended, this, [this](const QString &reason) {
                setStatus(tr("A pane's stream ended: %1").arg(reason));
            });
            connect(pane.terminal, &QQuickItem::activeFocusChanged, this, [this, blockId](bool focused) {
                const QString focusedInView = [&] {
                    for (const QJsonValue &win : m_view.value("windows").toArray())
                        if (win.toObject().value("window_id").toString() == activeWindow())
                            return win.toObject().value("focused_block_id").toString();
                    return QString();
                }();
                if (focused && blockId != focusedInView)
                    m_rex.call("session.focus_block", {{"session_id", m_sessionId}, {"block_id", blockId}});
            });
        }
        if (!pane.terminal)
            continue;
        // Edges inside the window get half a gap on each side; window edges get none.
        const qreal left = r.left() > 0 ? kGap / 2 : 0, top = r.top() > 0 ? kGap / 2 : 0;
        const qreal right = r.right() < 0.999 ? kGap / 2 : 0, bottom = r.bottom() < 0.999 ? kGap / 2 : 0;
        pane.terminal->setPosition(QPointF(r.x() * w + left, r.y() * h + top));
        pane.terminal->setSize(QSizeF(r.width() * w - left - right, r.height() * h - top - bottom));
        pane.terminal->setVisible(true);
        // Start after the first size is set, so the pty is claimed at the
        // size this pane really has.
        if (!pane.started) {
            pane.started = true;
            pane.stream->start();
        }
    }

    const QString focusedBlock = active.value("focused_block_id").toString();
    if (m_panes.contains(focusedBlock) && m_panes[focusedBlock].terminal && m_panes[focusedBlock].terminal->isVisible())
        m_panes[focusedBlock].terminal->forceActiveFocus();
}

void Workspace::closePane(const QString &blockId) {
    const Pane pane = m_panes.take(blockId);
    if (pane.stream) {
        pane.stream->stop();
        pane.stream->deleteLater();
    }
    if (pane.terminal)
        pane.terminal->deleteLater();
}

void Workspace::closeAllPanes() {
    for (const QString &blockId : m_panes.keys())
        closePane(blockId);
}

void Workspace::setStatus(const QString &status) {
    if (status == m_status)
        return;
    m_status = status;
    emit statusChanged();
}
