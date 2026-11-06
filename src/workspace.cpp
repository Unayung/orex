#include "workspace.h"

#include <QColor>
#include <QGuiApplication>
#include <QJsonArray>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QRectF>
#include <QUrl>
#include <memory>

#include "blockstream.h"
#include "paneborder.h"
#include "terminalitem.h"

// Space between split panes. Edges against the window or a floating frame
// get none, so the panes still use the whole area.
static constexpr qreal kGap = 6;
// Each pane's border, inside its slot, and the space between the border
// and the text.
static constexpr qreal kBorder = 1;
static constexpr qreal kPadding = 4;
// Room at the top of a pane for the half of the drag handle inside it.
static constexpr qreal kHandleClearance = 9;
// A floating layer's border, and the title bar it's dragged by.
static constexpr qreal kFrame = 4;
static constexpr qreal kTitleBar = 22;

// Snaps a rect's edges to whole pixels. A terminal drawn at a fractional
// position is resampled when it's composited, which blurs every glyph.
static QRectF snapped(const QRectF &r) {
    const qreal left = qRound(r.left()), top = qRound(r.top());
    return QRectF(left, top, qRound(r.right()) - left, qRound(r.bottom()) - top);
}

// `px` minus half a gap on each edge that faces another pane. `local` is the
// same rect as fractions of its container, which tells inner edges apart.
static QRectF withGaps(const QRectF &px, const QRectF &local) {
    const qreal left = local.left() > 0.001 ? kGap / 2 : 0, top = local.top() > 0.001 ? kGap / 2 : 0;
    const qreal right = local.right() < 0.999 ? kGap / 2 : 0, bottom = local.bottom() < 0.999 ? kGap / 2 : 0;
    return px.adjusted(left, top, -right, -bottom);
}

// The blocks of a layout tree with their rects, as fractions of the window.
void collectBlocks(const QJsonObject &node, QList<QPair<QString, QRectF>> &out) {
    // An empty layer's layout is null, for example the tiled layer after its
    // only pane was floated. Without this check the walk never ends.
    if (!node.contains("block_id") && !node.contains("split"))
        return;
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

Workspace::Workspace(Theme *theme, QObject *parent) : QObject(parent), m_theme(theme) {
    m_focusFollowsMouse = QSettings().value("focusFollowsMouse", false).toBool();
    connect(m_theme, &Theme::changed, this, [this] {
        for (Pane &pane : m_panes) {
            if (pane.stream)
                pane.stream->setTheme(m_theme->terminalTheme());
            if (pane.terminal)
                pane.terminal->setSelectionColor(QColor(m_theme->selection()));
        }
    });
}

Workspace::~Workspace() {
    closeAllPanes();
    qDeleteAll(m_hosts);
}

void Workspace::start(const QString &wanted) {
    m_wanted = wanted;
    setStatus(tr("Connecting to Rex…"));
    addHost("local", tr("This machine"), RexClient::defaultServer());
}

Workspace::Host *Workspace::addHost(const QString &key, const QString &label, const QString &endpoint) {
    auto *host = new Host{key, label, endpoint, new RexClient(this), new QTimer(this), {}, tr("Connecting…"), {}};
    m_hosts.append(host);
    // A closed control connection means the server restarted or the machine
    // went away (a Mac asleep). Keep trying, gently for remote machines.
    host->retry->setSingleShot(true);
    host->retry->setInterval(key == "local" ? 2000 : 15000);
    connect(host->retry, &QTimer::timeout, this, [host] { host->rex->open(host->endpoint); });

    connect(host->rex, &RexClient::serverEvent, this, [this, host](const QJsonObject &event) { onEvent(host, event); });
    connect(host->rex, &RexClient::closed, this, [this, host](const QString &reason) {
        host->sessions.clear();
        host->problem = reason;
        rebuildSessions();
        if (host == m_host) {
            // Every stream and attachment died with the connection; come
            // back to the same session once the host is reachable again.
            closeAllPanes();
            if (!m_sessionId.isEmpty())
                m_wanted = m_sessionId;
            m_sessionId.clear();
            m_view = {};
            m_revision = -1;
            emit currentSessionChanged();
            emit viewChanged();
            setStatus(tr("Disconnected from %1 (%2). Reconnecting…").arg(host->label, reason));
        }
        host->retry->start();
    });
    connect(host->rex, &RexClient::ready, this, [this, host] {
        host->problem.clear();
        // Session-wide events keep the session list current; block output
        // comes over data connections instead, so block events aren't needed.
        // "all" brings every session's block events too, such as title
        // changes, which the sidebar shows for each pane.
        host->rex->call("session.attach_all", {{"event_scope", "all"}},
                        [this, host](const QJsonObject &result, const QJsonObject &error) {
            if (!error.isEmpty()) {
                host->problem = error.value("message").toString();
                return rebuildSessions();
            }
            setSessions(host, result.value("sessions").toArray());
            pickSession(host);
        });
        // The local server keeps the list of other hosts, the same list
        // `rex hosts` shows.
        if (host->key == "local")
            refreshHosts();
    });
    rebuildSessions();
    host->rex->open(endpoint);
    return host;
}

void Workspace::refreshHosts() {
    Host *local = m_hosts.value(0);
    if (!local)
        return;
    local->rex->call("hosts.list", {}, [this](const QJsonObject &result, const QJsonObject &error) {
        if (!error.isEmpty())
            return;
        QSet<QString> listed;
        for (const QJsonValue &value : result.value("hosts").toArray()) {
            const QJsonObject h = value.toObject();
            const QString key = h.value("id").toString();
            const QString endpoint = h.value("endpoints").toArray().first().toString();
            listed.insert(key);
            Host *known = nullptr;
            for (Host *host : std::as_const(m_hosts))
                if (host->key == key)
                    known = host;
            if (!known) {
                addHost(key, h.value("label").toString(), endpoint);
            } else if (known->label != h.value("label").toString()) {
                known->label = h.value("label").toString();
                rebuildSessions();
            }
        }
        // Hosts removed with `rex hosts remove` go away here too.
        for (int i = m_hosts.size() - 1; i > 0; --i) {
            Host *host = m_hosts.at(i);
            if (listed.contains(host->key))
                continue;
            if (host == m_host) {
                closeAllPanes();
                m_host = nullptr;
                m_rex = nullptr;
                m_sessionId.clear();
                m_view = {};
                emit currentSessionChanged();
                emit viewChanged();
            }
            m_hosts.removeAt(i);
            host->retry->stop();
            host->rex->deleteLater();
            host->retry->deleteLater();
            delete host;
        }
        rebuildSessions();
    });
}

void Workspace::setSessions(Host *host, const QJsonArray &sessions) {
    host->sessions.clear();
    QSet<QString> ids;
    for (const QJsonValue &s : sessions) {
        host->sessions.append(s.toObject().toVariantMap());
        const QString id = s.toObject().value("session_id").toString();
        ids.insert(id);
        if (!host->views.contains(id))
            fetchSessionView(host, id);
    }
    for (const QString &id : host->views.keys())
        if (!ids.contains(id))
            host->views.remove(id);
    rebuildSessions();
    // A session orex just created, or the one on screen before a reconnect,
    // is selected as soon as it shows up.
    for (const QVariant &s : std::as_const(host->sessions))
        if (!m_wanted.isEmpty() && s.toMap().value("session_id").toString() == m_wanted)
            return selectSession(m_wanted);
}

void Workspace::rebuildSessions() {
    // One flat list for the sidebar, grouped by host_label: each session,
    // then its panes when it has more than one. A host without sessions gets
    // a placeholder row that says why.
    QVariantList list;
    for (const Host *host : std::as_const(m_hosts)) {
        for (const QVariant &s : host->sessions) {
            QVariantMap row = s.toMap();
            const QString sessionId = row.value("session_id").toString();
            row.insert("kind", "session");
            row.insert("host", host->key);
            row.insert("host_label", host->label);
            // The panes, tab by tab, in layout order.
            QVariantList panes;
            const QJsonObject view = host->views.value(sessionId);
            for (const QJsonValue &w : view.value("windows").toArray()) {
                const QJsonObject window = w.toObject();
                for (const QJsonValue &l : window.value("layers").toArray())
                    for (const QJsonValue &b : l.toObject().value("blocks").toArray()) {
                        const QString blockId = b.toObject().value("block_id").toString();
                        QString title = m_titles.value(blockId);
                        if (title.isEmpty())
                            title = b.toObject().value("label").toString();
                        panes.append(QVariantMap{{"kind", "pane"}, {"session_id", sessionId}, {"block_id", blockId},
                                                 {"label", title}, {"status", m_programStatus.value(blockId)},
                                                 {"focused", blockId == window.value("focused_block_id").toString() &&
                                                                 window.value("window_id") == view.value("active_window_id")},
                                                 {"host", host->key}, {"host_label", host->label}});
                    }
            }
            if (panes.size() == 1) {
                // One pane: its title is the session's second line.
                row.insert("subtitle", panes.first().toMap().value("label"));
                row.insert("status", panes.first().toMap().value("status"));
                list.append(row);
            } else {
                list.append(row);
                list.append(panes);
            }
        }
        if (host->sessions.isEmpty()) {
            // The full reason goes in a tooltip; the row stays short.
            const QString label = host->problem.isEmpty() ? tr("No sessions")
                                : host->problem == tr("Connecting…") ? host->problem
                                : tr("Unreachable, retrying…");
            list.append(QVariantMap{{"session_id", ""}, {"kind", "placeholder"}, {"host", host->key},
                                    {"host_label", host->label}, {"label", label}, {"detail", host->problem},
                                    {"placeholder", true}});
        }
    }
    if (list != m_sessions) {
        m_sessions = list;
        emit sessionsChanged();
    }
}

void Workspace::fetchSessionView(Host *host, const QString &sessionId) {
    host->rex->call("session.view", {{"session_id", sessionId}},
                    [this, host, sessionId](const QJsonObject &view, const QJsonObject &error) {
        if (!error.isEmpty())
            return;
        host->views.insert(sessionId, view);
        for (const QJsonValue &w : view.value("windows").toArray())
            for (const QJsonValue &l : w.toObject().value("layers").toArray())
                for (const QJsonValue &b : l.toObject().value("blocks").toArray()) {
                    const QString blockId = b.toObject().value("block_id").toString();
                    if (m_titles.contains(blockId))
                        continue;
                    m_titles.insert(blockId, QString()); // asked; the block events keep both current
                    const QJsonObject target{{"session_id", sessionId}, {"block_id", blockId}, {"args", QJsonObject{}}};
                    host->rex->call("com.superlogical.terminal.title", target,
                                    [this, blockId](const QJsonObject &result, const QJsonObject &) {
                        const QString title = result.value("title").toString();
                        if (!title.isEmpty()) {
                            m_titles.insert(blockId, title);
                            rebuildSessions();
                        }
                    });
                    // A status reported before orex connected comes only from here.
                    host->rex->call("com.superlogical.terminal.program_status", target,
                                    [this, blockId](const QJsonObject &result, const QJsonObject &) {
                        const QJsonArray records = result.value("records").toArray();
                        if (!records.isEmpty()) {
                            m_programStatus.insert(blockId, records.first().toObject().value("state").toString());
                            rebuildSessions();
                        }
                    });
                }
        rebuildSessions();
    });
}

void Workspace::pickSession(Host *host) {
    if (!m_sessionId.isEmpty())
        return;
    // `wanted` is a label or ID suffix from the command line, or the session
    // that was on screen before a reconnect. Without one, start on the first
    // session of this machine.
    for (const QVariant &s : std::as_const(host->sessions)) {
        const QVariantMap map = s.toMap();
        const QString id = map.value("session_id").toString();
        if ((m_wanted.isEmpty() && host->key == "local") || map.value("label").toString() == m_wanted ||
            (!m_wanted.isEmpty() && id.endsWith(m_wanted)))
            return selectSession(id);
    }
    if (host->key == "local") {
        if (host->sessions.isEmpty())
            setStatus(tr("This machine has no Rex sessions. Start one with Ctrl+Shift+N."));
        else if (!m_wanted.isEmpty())
            setStatus(tr("No session here matches “%1” yet.").arg(m_wanted));
    }
}

Workspace::Host *Workspace::hostOf(const QString &sessionId) const {
    for (Host *host : m_hosts)
        for (const QVariant &s : host->sessions)
            if (s.toMap().value("session_id").toString() == sessionId)
                return host;
    return nullptr;
}

void Workspace::setPaneArea(QQuickItem *area) {
    m_area = area;
    // Coming back to orex from the Mac app takes the visible panes back, so
    // they fit this window again.
    if (QQuickWindow *window = area->window())
        connect(window, &QWindow::activeChanged, this, [this, window] {
            if (!window->isActive())
                return;
            for (const Pane &pane : std::as_const(m_panes))
                if (pane.stream && pane.terminal && pane.terminal->isVisible())
                    pane.stream->claim();
        });
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
    Host *host = hostOf(sessionId);
    if (!host || sessionId == m_sessionId)
        return;
    closeAllPanes();
    if (m_rex && !m_sessionId.isEmpty())
        m_rex->call("session.detach", {{"session_id", m_sessionId}});
    m_host = host;
    m_rex = host->rex;
    m_sessionId = sessionId;
    m_wanted.clear();
    m_view = {};
    m_revision = -1;
    emit currentSessionChanged();
    emit viewChanged();
    m_rex->call("session.attach", {{"session_id", sessionId}},
                [this, sessionId](const QJsonObject &view, const QJsonObject &error) {
        if (sessionId != m_sessionId)
            return;
        if (!error.isEmpty())
            return setStatus(error.value("message").toString());
        setStatus(QString());
        applyView(view.contains("view") ? view.value("view").toObject() : view);
        if (!m_pendingFocus.isEmpty())
            m_rex->call("session.focus_block", {{"session_id", sessionId}, {"block_id", std::exchange(m_pendingFocus, {})}});
    });
}

void Workspace::focusPane(const QString &sessionId, const QString &blockId) {
    if (sessionId == m_sessionId) {
        if (m_rex)
            m_rex->call("session.focus_block", {{"session_id", m_sessionId}, {"block_id", blockId}});
        return;
    }
    // Focusing needs the session attached, which selecting it starts.
    m_pendingFocus = blockId;
    selectSession(sessionId);
}

void Workspace::selectWindow(const QString &windowId) {
    if (!m_rex || m_sessionId.isEmpty() || windowId == activeWindow())
        return;
    m_rex->call("session.focus_window", {{"session_id", m_sessionId}, {"window_id", windowId}});
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
    QStringList ids;
    for (const QVariant &s : std::as_const(m_sessions))
        if (s.toMap().value("kind").toString() == "session")
            ids.append(s.toMap().value("session_id").toString());
    if (ids.isEmpty())
        return;
    const int n = ids.size();
    const int i = qMax(0, ids.indexOf(m_sessionId));
    selectSession(ids.at(((i + delta) % n + n) % n));
}

void Workspace::shutdown() { closeAllPanes(); }

void Workspace::onEvent(Host *host, const QJsonObject &event) {
    const QString type = event.value("type").toString();
    const QJsonObject payload = event.value("payload").toObject();
    if (type == "hosts_changed" && host->key == "local") {
        refreshHosts();
    } else if (type == "session_created" || type == "session_moved") {
        refreshSessions(host);
    } else if (type == "session_destroyed") {
        refreshSessions(host);
        if (host == m_host && payload.value("session_id").toString() == m_sessionId) {
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
            refreshSessions(host);
        } else if (innerType == "session_view_changed") {
            const QString sessionId = payload.value("session_id").toString();
            const qint64 revision = inner.value("payload").toObject().value("revision").toInteger();
            // Changes can arrive out of order; only a newer revision means
            // the view we hold is stale.
            if (revision > host->views.value(sessionId).value("revision").toInteger())
                fetchSessionView(host, sessionId);
            if (host == m_host && sessionId == m_sessionId && revision > m_revision)
                fetchView();
        } else if (innerType == "block_event") {
            const QJsonObject block = inner.value("payload").toObject();
            const QString name = block.value("name").toString();
            const QString blockId = block.value("block_id").toString();
            if (name == "title_changed") {
                m_titles.insert(blockId, block.value("payload").toObject().value("title").toString());
                rebuildSessions();
            } else if (name == "program_status_changed") {
                m_programStatus.insert(blockId, block.value("payload").toObject().value("record").toObject().value("state").toString());
                rebuildSessions();
            } else if (name == "program_status_removed") {
                m_programStatus.remove(blockId);
                rebuildSessions();
            }
        }
    }
}

void Workspace::refreshSessions(Host *host) {
    host->rex->call("session.list", {}, [this, host](const QJsonObject &result, const QJsonObject &error) {
        if (error.isEmpty())
            setSessions(host, result.value("sessions").toArray());
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
    m_rex->call("session.view", {{"session_id", sessionId}},
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
    if (view.value("windows").toArray().isEmpty())
        setStatus(tr("This session has no tabs. Press Ctrl+Shift+T for a new one."));
    else if (m_status.startsWith(tr("This session has no tabs")))
        setStatus(QString());
    emit viewChanged();
    layoutPanes();
}

void Workspace::layoutPanes() {
    if (!m_area || m_view.isEmpty())
        return;

    // Every block the session still has, so panes of closed blocks go away
    // while panes in other tabs keep streaming in the background.
    QSet<QString> alive;
    for (const QJsonValue &w : m_view.value("windows").toArray())
        for (const QJsonValue &layer : w.toObject().value("layers").toArray())
            for (const QJsonValue &b : layer.toObject().value("blocks").toArray())
                alive.insert(b.toObject().value("block_id").toString());
    for (const QString &blockId : m_panes.keys())
        if (!alive.contains(blockId))
            closePane(blockId);

    const QJsonObject active = activeWindowObject();
    const QString focusedBlock = active.value("focused_block_id").toString();
    const qreal w = m_area->width(), h = m_area->height();

    // Where each pane goes, in pixels of the pane area, with its stacking
    // order. Layers come back to front, the tiled one first.
    struct Place { QString blockId; QRectF px; qreal z; };
    QList<Place> places;
    QVariantList frames, minimized;
    m_placed.clear();
    m_layerPanes.clear();

    const QString zoomed = active.value("zoomed_block_id").toString();
    if (!zoomed.isEmpty()) {
        places.append({zoomed, QRectF(0, 0, w, h), 0});
        m_placed.append({zoomed, QRectF(0, 0, 1, 1)});
    } else {
        int index = 0;
        for (const QJsonValue &value : active.value("layers").toArray()) {
            const QJsonObject layer = value.toObject();
            QList<QPair<QString, QRectF>> blocks;
            collectBlocks(layer.value("layout").toObject(), blocks);
            if (layer.value("kind").toString() == "tiled") {
                for (const auto &[blockId, r] : std::as_const(blocks)) {
                    places.append({blockId, withGaps(QRectF(r.x() * w, r.y() * h, r.width() * w, r.height() * h), r), 0});
                    m_placed.append({blockId, r});
                }
                continue;
            }
            ++index;
            const QString layerId = layer.value("layer_id").toString();
            const QString title = layer.value("blocks").toArray().first().toObject().value("label").toString();
            if (layer.value("state").toString() == "minimized") {
                minimized.append(QVariantMap{{"layer_id", layerId}, {"title", title}});
                continue;
            }
            // A floating layer gets a title bar to drag it by; its panes fill
            // the rest of its bounds.
            const QJsonArray b = layer.value("bounds").toArray();
            const QRectF bounds(b.at(0).toDouble(), b.at(1).toDouble(), b.at(2).toDouble(), b.at(3).toDouble());
            const QRectF frame = snapped(QRectF(bounds.x() * w, bounds.y() * h, bounds.width() * w, bounds.height() * h));
            const QRectF inner = frame.adjusted(kFrame, kTitleBar, -kFrame, -kFrame);
            bool hasFocus = false;
            DraggedLayer &dragged = m_layerPanes[layerId];
            dragged.frame = frame.topLeft();
            for (const auto &[blockId, r] : std::as_const(blocks)) {
                const QRectF local((r.x() - bounds.x()) / bounds.width(), (r.y() - bounds.y()) / bounds.height(),
                                   r.width() / bounds.width(), r.height() / bounds.height());
                const QRectF px = withGaps(QRectF(inner.x() + local.x() * inner.width(), inner.y() + local.y() * inner.height(),
                                                  local.width() * inner.width(), local.height() * inner.height()), local);
                places.append({blockId, px, index * 10.0 + 1});
                dragged.panes.append({blockId, snapped(px).topLeft()});  // the slot, border included
                m_placed.append({blockId, r});
                hasFocus = hasFocus || blockId == focusedBlock;
            }
            frames.append(QVariantMap{{"layer_id", layerId}, {"title", title}, {"x", frame.x()}, {"y", frame.y()},
                                      {"width", frame.width()}, {"height", frame.height()}, {"z", index * 10.0},
                                      {"focused", hasFocus}});
        }
    }
    if (frames != m_frames || minimized != m_minimized) {
        m_frames = frames;
        m_minimized = minimized;
        emit framesChanged();
    }

    for (Pane &pane : m_panes) {
        if (pane.terminal)
            pane.terminal->setVisible(false);
        if (pane.border)
            pane.border->setVisible(false);
    }

    QVariantList slotList;
    for (const Place &place : std::as_const(places)) {
        const QString blockId = place.blockId;
        Pane &pane = m_panes[blockId];
        if (!pane.stream) {
            pane.terminal = new TerminalItem(m_area);
            pane.border = new PaneBorder(m_area);
            pane.terminal->setSelectionColor(QColor(m_theme->selection()));
            pane.stream = new BlockStream(m_rex, m_sessionId, blockId, pane.terminal, this);
            pane.stream->setTheme(m_theme->terminalTheme());
            connect(pane.stream, &BlockStream::ended, this, [this](const QString &reason) {
                // 4000 is how Rex says the pane closed, which the next view
                // shows by itself; anything else is worth saying.
                // A pane that closes before its stream starts, such as the
                // placeholder of a rebuilt layout, ends with "not found".
                if (!reason.startsWith("4000") && !reason.contains("not found"))
                    flashStatus(tr("A pane's stream ended: %1").arg(reason));
            });
            // Pointing at a pane focuses it when that's turned on, but only
            // while orex is the active window and no mouse button is held,
            // so passing over it on the way elsewhere or mid-drag doesn't.
            connect(pane.terminal, &TerminalItem::pointerEntered, this, [this, terminal = pane.terminal] {
                if (m_focusFollowsMouse && terminal && !terminal->hasActiveFocus() && m_area && m_area->window() &&
                    m_area->window()->isActive() && QGuiApplication::mouseButtons() == Qt::NoButton)
                    terminal->forceActiveFocus(Qt::MouseFocusReason);
            });
            connect(pane.terminal, &QQuickItem::activeFocusChanged, this, [this, blockId](bool focused) {
                if (focused && blockId != this->focusedBlock())
                    m_rex->call("session.focus_block", {{"session_id", m_sessionId}, {"block_id", blockId}});
                // Only a click or key in orex counts. Focus also moves when a
                // layout change from another client arrives, and claiming then
                // would pull the pane away from the Mac app you're using.
                if (focused && m_panes.value(blockId).stream && m_area && m_area->window() &&
                    m_area->window()->isActive())
                    m_panes.value(blockId).stream->claim();
            });
        }
        if (!pane.terminal)
            continue;
        const qreal inset = kBorder + kPadding;
        const QRectF slot = snapped(place.px);
        // The drag handle sits on the top edge, so the text starts below it.
        const qreal top = kBorder + kHandleClearance;
        slotList.append(QVariantMap{{"block_id", blockId}, {"x", slot.x()}, {"y", slot.y()}, {"width", slot.width()},
                                 {"height", slot.height()}, {"z", place.z}, {"floating", place.z > 0}});
        const QRectF inner = slot.adjusted(inset, top, -inset, -inset);
        pane.terminal->setPosition(inner.topLeft());
        pane.terminal->setSize(inner.size());
        pane.terminal->setZ(place.z);
        pane.terminal->setVisible(true);
        if (pane.border) {
            pane.border->setPosition(slot.topLeft());
            pane.border->setSize(slot.size());
            pane.border->setZ(place.z + 0.5);
            // The focused pane stands out once there's more than one to tell apart.
            pane.border->setColor(blockId == focusedBlock && places.size() > 1
                                      ? QColor(m_theme->accent())
                                      : QColor(m_theme->selection()).lighter(160));
            pane.border->setVisible(true);
        }
        // Start after the first size is set, so the pty is claimed at the
        // size this pane really has.
        if (!pane.started) {
            pane.started = true;
            pane.stream->start();
        }
    }

    if (slotList != m_paneSlots) {
        m_paneSlots = slotList;
        emit paneSlotsChanged();
    }

    if (m_panes.contains(focusedBlock) && m_panes[focusedBlock].terminal && m_panes[focusedBlock].terminal->isVisible())
        m_panes[focusedBlock].terminal->forceActiveFocus();
}

QJsonObject Workspace::activeWindowObject() const {
    for (const QJsonValue &w : m_view.value("windows").toArray())
        if (w.toObject().value("window_id").toString() == activeWindow())
            return w.toObject();
    return {};
}

QString Workspace::focusedBlock() const {
    return activeWindowObject().value("focused_block_id").toString();
}

QJsonObject Workspace::layerOf(const QString &blockId) const {
    for (const QJsonValue &layer : activeWindowObject().value("layers").toArray())
        for (const QJsonValue &b : layer.toObject().value("blocks").toArray())
            if (b.toObject().value("block_id").toString() == blockId)
                return layer.toObject();
    return {};
}

void Workspace::closePane(const QString &blockId) {
    const Pane pane = m_panes.take(blockId);
    if (pane.stream) {
        pane.stream->stop();
        pane.stream->deleteLater();
    }
    if (pane.terminal)
        pane.terminal->deleteLater();
    if (pane.border)
        pane.border->deleteLater();
}

void Workspace::closeAllPanes() {
    for (const QString &blockId : m_panes.keys())
        closePane(blockId);
}

void Workspace::setFocusFollowsMouse(bool on) {
    if (on == m_focusFollowsMouse)
        return;
    m_focusFollowsMouse = on;
    QSettings().setValue("focusFollowsMouse", on);
    emit focusFollowsMouseChanged();
}

void Workspace::flashStatus(const QString &status) {
    setStatus(status);
    QTimer::singleShot(4000, this, [this, status] {
        if (m_status == status)
            setStatus(QString());
    });
}

void Workspace::setStatus(const QString &status) {
    if (status == m_status)
        return;
    m_status = status;
    emit statusChanged();
}

QVariantList Workspace::actions() const {
    // Shortcuts follow Ghostty's Linux defaults where Ghostty has the same
    // action, and Rex.app's (with Cmd as Ctrl+Shift) where it doesn't.
    // Actions marked "qml" need a dialog, so Main.qml runs them.
    struct A { const char *id, *title, *shortcut; };
    static const A list[] = {
        {"palette", QT_TR_NOOP("Command Palette…"), "Ctrl+Shift+P"},
        {"session.switch", QT_TR_NOOP("Change Session…"), "Ctrl+Shift+K"},
        {"session.new", QT_TR_NOOP("New Session"), "Ctrl+Shift+N"},
        {"session.next", QT_TR_NOOP("Next Session"), "Ctrl+Shift+Down"},
        {"session.previous", QT_TR_NOOP("Previous Session"), "Ctrl+Shift+Up"},
        {"session.rename", QT_TR_NOOP("Rename Session…"), ""},
        {"session.close", QT_TR_NOOP("Close Session"), ""},
        {"tab.new", QT_TR_NOOP("New Tab"), "Ctrl+Shift+T"},
        {"tab.next", QT_TR_NOOP("Next Tab"), "Ctrl+Tab"},
        {"tab.previous", QT_TR_NOOP("Previous Tab"), "Ctrl+Shift+Tab"},
        {"tab.rename", QT_TR_NOOP("Rename Tab…"), ""},
        {"tab.close", QT_TR_NOOP("Close Tab"), ""},
        {"pane.split.right", QT_TR_NOOP("Split Pane Right"), "Ctrl+Shift+O"},
        {"pane.split.down", QT_TR_NOOP("Split Pane Down"), "Ctrl+Shift+E"},
        {"pane.split.left", QT_TR_NOOP("Split Pane Left"), ""},
        {"pane.split.up", QT_TR_NOOP("Split Pane Up"), ""},
        {"pane.close", QT_TR_NOOP("Close Pane"), "Ctrl+Shift+W"},
        {"pane.close_others", QT_TR_NOOP("Close Other Panes"), ""},
        {"pane.zoom", QT_TR_NOOP("Zoom Pane"), "Ctrl+Shift+Return"},
        {"pane.focus.left", QT_TR_NOOP("Focus Pane Left"), "Ctrl+Alt+Left"},
        {"pane.focus.right", QT_TR_NOOP("Focus Pane Right"), "Ctrl+Alt+Right"},
        {"pane.focus.up", QT_TR_NOOP("Focus Pane Up"), "Ctrl+Alt+Up"},
        {"pane.focus.down", QT_TR_NOOP("Focus Pane Down"), "Ctrl+Alt+Down"},
        {"pane.focus_next", QT_TR_NOOP("Focus Next Pane"), ""},
        {"pane.focus_previous", QT_TR_NOOP("Focus Previous Pane"), ""},
        {"pane.move.left", QT_TR_NOOP("Move Pane Left"), "Ctrl+Alt+Shift+Left"},
        {"pane.move.right", QT_TR_NOOP("Move Pane Right"), "Ctrl+Alt+Shift+Right"},
        {"pane.move.up", QT_TR_NOOP("Move Pane Up"), "Ctrl+Alt+Shift+Up"},
        {"pane.move.down", QT_TR_NOOP("Move Pane Down"), "Ctrl+Alt+Shift+Down"},
        {"pane.float", QT_TR_NOOP("Float Pane"), "Ctrl+Alt+Return"},
        {"pane.new_floating", QT_TR_NOOP("New Floating Pane"), ""},
        {"pane.minimize_floating", QT_TR_NOOP("Minimize Floating Window"), "Ctrl+Alt+M"},
        {"pane.minimize_all_floating", QT_TR_NOOP("Minimize or Restore Floating Windows"), "Ctrl+Alt+Shift+Return"},
        {"sidebar.toggle", QT_TR_NOOP("Show or Hide Sessions"), "Ctrl+Shift+B"},
        {"focus.follows_mouse", QT_TR_NOOP("Focus Follows Mouse: Turn On or Off"), ""},
        {"help", QT_TR_NOOP("Keyboard Shortcuts"), "Ctrl+Shift+/"},
    };
    QVariantList out;
    for (const A &a : list)
        out.append(QVariantMap{{"id", a.id}, {"title", tr(a.title)}, {"shortcut", a.shortcut}});
    return out;
}

void Workspace::withNewShell(std::function<void(const QJsonObject &)> then) {
    auto shellIn = [](const QString &cwd) {
        QJsonObject block{{"flavor", "com.superlogical.terminal.shell"}};
        if (!cwd.isEmpty())
            block.insert("options", QJsonObject{{"cwd", cwd}});
        return QJsonObject{{"block", block}};
    };
    const QString session = m_sessionId, block = focusedBlock();
    if (session.isEmpty() || block.isEmpty())
        return then(shellIn({}));
    const QJsonObject target{{"session_id", session}, {"block_id", block}, {"args", QJsonObject{}}};
    // The shell's own report (OSC 7) is best, but only shells set up for it
    // send one, so fall back to the directory of the program in front.
    m_rex->call("com.superlogical.terminal.pwd", target, [this, target, then, shellIn](const QJsonObject &result, const QJsonObject &) {
        QString pwd = result.value("pwd").toString();
        if (pwd.startsWith("file://"))
            pwd = QUrl(pwd).toLocalFile();
        if (!pwd.isEmpty())
            return then(shellIn(pwd));
        m_rex->call("com.superlogical.terminal.process", target, [then, shellIn](const QJsonObject &process, const QJsonObject &) {
            QString cwd = process.value("foreground").toObject().value("cwd").toString();
            if (cwd.isEmpty())
                cwd = process.value("child").toObject().value("cwd").toString();
            then(shellIn(cwd));
        });
    });
}

QString Workspace::neighbor(const QString &blockId, const QString &direction) const {
    QRectF from;
    for (const auto &[id, r] : m_placed)
        if (id == blockId)
            from = r;
    if (from.isNull())
        return {};
    // The pane that shares the edge on that side and overlaps it the most.
    constexpr qreal eps = 0.01;
    QString best;
    qreal bestOverlap = 0;
    for (const auto &[id, r] : m_placed) {
        if (id == blockId)
            continue;
        qreal overlap = 0;
        if (direction == "left" && qAbs(r.right() - from.left()) < eps)
            overlap = qMin(r.bottom(), from.bottom()) - qMax(r.top(), from.top());
        else if (direction == "right" && qAbs(r.left() - from.right()) < eps)
            overlap = qMin(r.bottom(), from.bottom()) - qMax(r.top(), from.top());
        else if (direction == "up" && qAbs(r.bottom() - from.top()) < eps)
            overlap = qMin(r.right(), from.right()) - qMax(r.left(), from.left());
        else if (direction == "down" && qAbs(r.top() - from.bottom()) < eps)
            overlap = qMin(r.right(), from.right()) - qMax(r.left(), from.left());
        if (overlap > bestOverlap) {
            bestOverlap = overlap;
            best = id;
        }
    }
    return best;
}

void Workspace::perform(const QString &id, const QVariant &arg) {
    // A setting of orex itself, so it works with no session at all.
    if (id == "focus.follows_mouse") {
        setFocusFollowsMouse(!m_focusFollowsMouse);
        flashStatus(m_focusFollowsMouse ? tr("Focus follows the mouse: on") : tr("Focus follows the mouse: off"));
        return;
    }
    // New sessions go to the host on screen, or this machine.
    if (id == "session.new" && !m_rex && !m_hosts.isEmpty())
        m_rex = m_hosts.first()->rex;
    if (!m_rex || (m_sessionId.isEmpty() && id != "session.new"))
        return;
    const QString session = m_sessionId;
    const QString block = focusedBlock();
    const QJsonObject window = activeWindowObject();
    auto call = [this](const QString &method, QJsonObject payload) {
        m_rex->call(method, payload, [this, method](const QJsonObject &, const QJsonObject &error) {
            // A target that's already gone, such as a tab closed twice in a
            // row, ended up the way the action wanted.
            // A key pressed twice before the first change reached orex, such
            // as floating a pane that is already floating, isn't worth a word.
            const QString message = error.value("message").toString();
            if (!error.isEmpty() && error.value("code").toString() != "not_found" &&
                !message.contains("already in") && !message.contains("no windows") &&
                !message.contains("no longer attached"))
                flashStatus(tr("%1 failed: %2").arg(method, error.value("message").toString()));
        });
    };
    // Pane and tab actions need a tab; a session can be left with none.
    const bool noTabs = m_view.value("windows").toArray().isEmpty() || activeWindow().isEmpty();
    if ((id.startsWith("pane.") || (id.startsWith("tab.") && id != "tab.new")) && noTabs) {
        flashStatus(tr("This session has no tabs. Press Ctrl+Shift+T for a new one."));
        return;
    }
    if (id.startsWith("pane.") && id != "pane.new_floating" && block.isEmpty()) {
        flashStatus(tr("This tab has no focused pane."));
        return;
    }

    if (id == "session.new") {
        const QJsonObject shell{{"block", QJsonObject{{"flavor", "com.superlogical.terminal.shell"}}}};
        const QJsonObject firstWindow{{"layout", shell}};
        m_rex->call("session.create", {{"initial_windows", QJsonArray{firstWindow}}},
                   [this](const QJsonObject &result, const QJsonObject &error) {
            // The new session reaches the list through session_created a
            // moment later; select it once the list has it.
            if (error.isEmpty())
                m_wanted = result.value("session_id").toString();
        });
    } else if (id == "session.next" || id == "session.previous") {
        stepSession(id == "session.next" ? 1 : -1);
    } else if (id == "session.rename") {
        call("session.set_label", {{"session_id", session}, {"label", arg.toString()}});
    } else if (id == "session.close") {
        call("session.destroy", {{"session_id", session}});
    } else if (id == "tab.new") {
        withNewShell([=](const QJsonObject &layout) {
            call("session.new_window", {{"session_id", session}, {"layout", layout}, {"focus", true}});
        });
    } else if (id == "tab.next" || id == "tab.previous") {
        stepWindow(id == "tab.next" ? 1 : -1);
    } else if (id == "tab.goto") {
        const QVariantList list = windows();
        const int index = arg.toInt() - 1;
        if (index >= 0 && index < list.size())
            selectWindow(list.at(index).toMap().value("window_id").toString());
    } else if (id == "tab.rename") {
        call("session.set_window_label", {{"session_id", session}, {"window_id", activeWindow()}, {"label", arg.toString()}});
    } else if (id == "tab.close") {
        call("session.close_window", {{"session_id", session}, {"window_id", activeWindow()}});
    } else if (id.startsWith("pane.split.")) {
        const QString side = id.section('.', 2);
        withNewShell([=](const QJsonObject &layout) {
            call("session.new_split", {{"session_id", session}, {"anchor_id", block},
                                       {"direction", side == "left" || side == "right" ? "horizontal" : "vertical"},
                                       {"side", side == "left" || side == "up" ? "before" : "after"},
                                       {"layout", layout}, {"focus", true}});
        });
    } else if (id == "pane.close") {
        call("block.close", {{"session_id", session}, {"block_id", block}});
    } else if (id == "pane.close_others") {
        for (const auto &[other, r] : std::as_const(m_placed))
            if (other != block)
                call("block.close", {{"session_id", session}, {"block_id", other}});
    } else if (id == "pane.zoom") {
        const bool zoomed = !window.value("zoomed_block_id").toString().isEmpty();
        call("session.zoom_block", {{"session_id", session}, {"block_id", block}, {"zoom", !zoomed}});
    } else if (id.startsWith("pane.focus.")) {
        call("session.focus_direction", {{"session_id", session}, {"direction", id.section('.', 2)}});
    } else if (id == "pane.focus_next") {
        call("session.focus_next_pane", {{"session_id", session}, {"wrap", true}});
    } else if (id == "pane.focus_previous") {
        call("session.focus_previous_pane", {{"session_id", session}, {"wrap", true}});
    } else if (id.startsWith("pane.move.")) {
        const QString other = neighbor(block, id.section('.', 2));
        if (!other.isEmpty())
            call("session.swap_blocks", {{"session_id", session}, {"block_id", block}, {"other_block_id", other}});
    } else if (id == "pane.float") {
        const QJsonObject layer = layerOf(block);
        QString tiledAnchor;
        int nonEmptyLayers = 0;
        for (const QJsonValue &l : window.value("layers").toArray()) {
            const QJsonArray blocks = l.toObject().value("blocks").toArray();
            if (!blocks.isEmpty())
                ++nonEmptyLayers;
            if (l.toObject().value("kind").toString() == "tiled" && !blocks.isEmpty())
                tiledAnchor = blocks.first().toObject().value("block_id").toString();
        }
        const int layerSize = layer.value("blocks").toArray().size();
        auto floatIt = [=] {
            call("session.float_block", {{"session_id", session}, {"block_id", block},
                                         {"bounds", QJsonArray{0.15, 0.12, 0.7, 0.7}}});
        };
        if (layer.value("kind").toString() == "tiled") {
            // Rex docks a pane only beside another pane in the layout, so the
            // layout's last pane must stay: floating it would leave nothing
            // for any floating pane to dock back beside.
            if (layerSize <= 1) {
                flashStatus(tr("This is the last pane in the layout, so it stays docked. "
                               "Split it first, or use a new floating pane."));
                return;
            }
            floatIt();
        } else if (!tiledAnchor.isEmpty()) {
            // Docking puts the pane back beside the first pane of the layout.
            call("session.move_block", {{"session_id", session}, {"block_id", block}, {"anchor_block_id", tiledAnchor},
                                        {"direction", "horizontal"}, {"side", "after"}});
        } else if (nonEmptyLayers == 1) {
            // The layout is empty and every pane sits in this one floating
            // layer, which Rex can neither dock into nor float out of. Rebuild
            // it as a real layout first; then float the pane if it had company.
            rebuildAsLayout(window, layer, layerSize > 1 ? std::function<void()>(floatIt) : std::function<void()>());
        } else {
            flashStatus(tr("Rex can't dock a pane into an empty layout while other floating windows are open."));
        }
    } else if (id == "pane.new_floating") {
        withNewShell([=](const QJsonObject &layout) {
            call("session.new_layer", {{"session_id", session}, {"bounds", QJsonArray{0.2, 0.15, 0.6, 0.6}},
                                       {"layout", layout}, {"focus", true}});
        });
    } else if (id == "pane.minimize_floating") {
        const QJsonObject layer = layerOf(block);
        if (layer.value("kind").toString() != "tiled" && !layer.isEmpty())
            setLayerMinimized(layer.value("layer_id").toString(), true);
    } else if (id == "pane.minimize_all_floating") {
        // Minimize them all, or bring them all back when none is showing.
        const bool anyShowing = !m_frames.isEmpty();
        for (const QJsonValue &l : window.value("layers").toArray())
            if (l.toObject().value("kind").toString() != "tiled")
                setLayerMinimized(l.toObject().value("layer_id").toString(), anyShowing);
    }
}

void Workspace::rebuildAsLayout(const QJsonObject &window, const QJsonObject &layer, std::function<void()> then) {
    // Rex docks a pane only beside another pane, so a tab whose layout is
    // empty can't take one back. Build the same panes in a new tab instead:
    // start it with a placeholder shell, move each pane in beside the one
    // before it (side by side or stacked, as they were), close the
    // placeholder, and let the emptied old tab close by itself. The new tab
    // then takes the old one's name and place.
    QList<QPair<QString, QRectF>> blocks;
    collectBlocks(layer.value("layout").toObject(), blocks);
    if (blocks.isEmpty())
        return;
    const QString session = m_sessionId;
    const QString oldWindow = window.value("window_id").toString();
    const QString label = window.value("label").toString();
    QString nextWindow;
    const QJsonArray windows = m_view.value("windows").toArray();
    for (int i = 0; i + 1 < windows.size(); ++i)
        if (windows.at(i).toObject().value("window_id").toString() == oldWindow)
            nextWindow = windows.at(i + 1).toObject().value("window_id").toString();

    auto fail = [this](const QJsonObject &error) {
        flashStatus(tr("Couldn't rebuild the layout: %1").arg(error.value("message").toString()));
    };
    const QJsonObject shell{{"block", QJsonObject{{"flavor", "com.superlogical.terminal.shell"}}}};
    m_rex->call("session.new_window", {{"session_id", session}, {"layout", shell}, {"focus", true}},
                [=, this](const QJsonObject &created, const QJsonObject &error) {
        if (!error.isEmpty())
            return fail(error);
        const QString newWindow = created.value("window_id").toString();
        const QString placeholder = created.value("block_ids").toArray().first().toString();
        auto step = std::make_shared<std::function<void(int)>>();
        *step = [=, this](int i) {
            if (i == blocks.size()) {
                m_rex->call("block.close", {{"session_id", session}, {"block_id", placeholder}});
                m_rex->call("session.set_window_label", {{"session_id", session}, {"window_id", newWindow}, {"label", label}});
                QJsonObject move{{"session_id", session}, {"window_id", newWindow}};
                if (!nextWindow.isEmpty())
                    move.insert("before_window_id", nextWindow);
                m_rex->call("session.move_window", move, [then](const QJsonObject &, const QJsonObject &) {
                    if (then)
                        then();
                });
                return;
            }
            const QRectF r = blocks.at(i).second;
            const QRectF before = i > 0 ? blocks.at(i - 1).second : QRectF();
            const bool sideBySide = i == 0 || r.left() >= before.right() - 0.001;
            m_rex->call("session.move_block", {{"session_id", session}, {"block_id", blocks.at(i).first},
                                               {"anchor_block_id", i == 0 ? placeholder : blocks.at(i - 1).first},
                                               {"direction", sideBySide ? "horizontal" : "vertical"}, {"side", "after"}},
                        [=](const QJsonObject &, const QJsonObject &error) {
                if (!error.isEmpty())
                    return fail(error);
                (*step)(i + 1);
            });
        };
        (*step)(0);
    });
}

void Workspace::moveLayer(const QString &layerId, qreal x, qreal y) {
    if (!m_area || m_area->width() <= 0 || m_area->height() <= 0)
        return;
    for (const QJsonValue &l : activeWindowObject().value("layers").toArray()) {
        const QJsonObject layer = l.toObject();
        if (layer.value("layer_id").toString() != layerId)
            continue;
        const QJsonArray b = layer.value("bounds").toArray();
        const qreal bw = b.at(2).toDouble(), bh = b.at(3).toDouble();
        // The server refuses bounds that leave the window.
        const qreal fx = qBound(0.0, x / m_area->width(), 1.0 - bw);
        const qreal fy = qBound(0.0, y / m_area->height(), 1.0 - bh);
        m_rex->call("session.resize_layer", {{"session_id", m_sessionId}, {"layer_id", layerId},
                                            {"bounds", QJsonArray{fx, fy, bw, bh}}});
    }
}

void Workspace::dragLayer(const QString &layerId, qreal x, qreal y) {
    const auto it = m_layerPanes.constFind(layerId);
    if (it == m_layerPanes.constEnd())
        return;
    // Whole-pixel steps keep the text sharp while it moves.
    const QPointF offset(qRound(x - it->frame.x()), qRound(y - it->frame.y()));
    for (const auto &[blockId, origin] : it->panes) {
        const Pane pane = m_panes.value(blockId);
        if (pane.terminal)
            pane.terminal->setPosition(origin + offset + QPointF(kBorder + kPadding, kBorder + kHandleClearance));
        if (pane.border)
            pane.border->setPosition(origin + offset);
    }
    // The drag handles follow too.
    QVariantList slotList = m_paneSlots;
    for (QVariant &v : slotList) {
        QVariantMap slot = v.toMap();
        for (const auto &[blockId, origin] : it->panes)
            if (slot.value("block_id").toString() == blockId) {
                slot.insert("x", origin.x() + offset.x());
                slot.insert("y", origin.y() + offset.y());
            }
        v = slot;
    }
    m_paneSlots = slotList;
    emit paneSlotsChanged();
}

void Workspace::dropPane(const QString &blockId, const QString &targetId, const QString &zone) {
    if (!m_rex || blockId.isEmpty() || targetId.isEmpty() || blockId == targetId)
        return;
    auto report = [this](const QJsonObject &, const QJsonObject &error) {
        if (!error.isEmpty())
            flashStatus(tr("Couldn't move the pane: %1").arg(error.value("message").toString()));
    };
    if (zone == "center") {
        m_rex->call("session.swap_blocks", {{"session_id", m_sessionId}, {"block_id", blockId}, {"other_block_id", targetId}},
                    report);
        return;
    }
    // The same rule as floating: moving the layout's last pane into a
    // floating window would leave nothing to dock back beside.
    const QJsonObject from = layerOf(blockId), to = layerOf(targetId);
    if (from.value("kind").toString() == "tiled" && to.value("kind").toString() != "tiled" &&
        from.value("blocks").toArray().size() <= 1) {
        flashStatus(tr("This is the last pane in the layout, so it stays docked."));
        return;
    }
    const bool sideways = zone == "left" || zone == "right";
    m_rex->call("session.move_block", {{"session_id", m_sessionId}, {"block_id", blockId}, {"anchor_block_id", targetId},
                                       {"direction", sideways ? "horizontal" : "vertical"},
                                       {"side", zone == "left" || zone == "up" ? "before" : "after"}},
                report);
}

void Workspace::raiseLayer(const QString &layerId) {
    m_rex->call("session.raise_layer", {{"session_id", m_sessionId}, {"layer_id", layerId}});
}

void Workspace::setLayerMinimized(const QString &layerId, bool minimized) {
    m_rex->call("session.set_layer_state", {{"session_id", m_sessionId}, {"layer_id", layerId},
                                           {"state", minimized ? "minimized" : "normal"}, {"focus", !minimized}});
}
