#pragma once

// Everything orex shows from the Rex servers it knows: this machine's and
// the hosts in `rex hosts`, each with its sessions, plus the tabs of the
// session on screen and a terminal for each pane in its active tab.
// Terminals are made here rather than in QML, so a layout change moves and
// resizes them instead of tearing down their streams.

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QPointF>
#include <QRectF>
#include <QVariantList>
#include <functional>

#include "rexclient.h"
#include "theme.h"

class BlockStream;
class QQuickItem;
class TerminalItem;
class PaneBorder;

class Workspace : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QString currentSession READ currentSession NOTIFY currentSessionChanged)
    // The key of the host the current session runs on; "local" is this machine.
    Q_PROPERTY(QString currentHost READ currentHost NOTIFY currentSessionChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY viewChanged)
    Q_PROPERTY(QString activeWindow READ activeWindow NOTIFY viewChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // Floating layers of the active tab, for QML to draw their frames:
    // layer_id, title, x, y, width, height (pixels of the pane area), z, focused.
    Q_PROPERTY(QVariantList frames READ frames NOTIFY framesChanged)
    // Minimized floating layers: layer_id, title.
    Q_PROPERTY(QVariantList minimized READ minimized NOTIFY framesChanged)
    // Every visible pane's slot, for drag handles and drop targets:
    // block_id, x, y, width, height (pixels of the pane area), z, floating.
    Q_PROPERTY(QVariantList paneSlots READ paneSlots NOTIFY paneSlotsChanged)
    // Every action orex offers: id, title, shortcut. The palette lists them.
    Q_PROPERTY(QVariantList actions READ actions CONSTANT)

public:
    explicit Workspace(Theme *theme, QObject *parent = nullptr);
    ~Workspace() override;

    QVariantList sessions() const { return m_sessions; }
    QString currentSession() const { return m_sessionId; }
    QString currentHost() const { return m_host ? m_host->key : QString(); }
    QVariantList windows() const;
    QString activeWindow() const { return m_view.value("active_window_id").toString(); }
    QString status() const { return m_status; }
    QVariantList frames() const { return m_frames; }
    QVariantList minimized() const { return m_minimized; }
    QVariantList paneSlots() const { return m_paneSlots; }
    QVariantList actions() const;

    // Connects, then shows the session whose label or ID suffix is
    // `wanted`, or the first session.
    void start(const QString &wanted);
    // The QML item the panes are laid out in.
    Q_INVOKABLE void setPaneArea(QQuickItem *area);

    Q_INVOKABLE void selectSession(const QString &sessionId);
    Q_INVOKABLE void selectWindow(const QString &windowId);
    Q_INVOKABLE void stepWindow(int delta);
    Q_INVOKABLE void stepSession(int delta);
    // Gives every pane back to Rex, so other clients size them again.
    Q_INVOKABLE void shutdown();

    // Runs one of `actions` by id. `arg` is the tab number for tab.goto and
    // the new name for the rename actions.
    Q_INVOKABLE void perform(const QString &id, const QVariant &arg = {});
    // Moves a floating layer so its top-left corner is at x, y (pixels of the
    // pane area), keeping its size.
    Q_INVOKABLE void moveLayer(const QString &layerId, qreal x, qreal y);
    // Moves a floating layer's panes along with its frame while it's being
    // dragged, before the server hears about it. x, y as for moveLayer.
    Q_INVOKABLE void dragLayer(const QString &layerId, qreal x, qreal y);
    Q_INVOKABLE void raiseLayer(const QString &layerId);
    // Drops pane `blockId` on `targetId`: "left", "right", "up" or "down"
    // splits the target and puts the pane on that side; "center" swaps them.
    Q_INVOKABLE void dropPane(const QString &blockId, const QString &targetId, const QString &zone);
    Q_INVOKABLE void setLayerMinimized(const QString &layerId, bool minimized);

signals:
    void sessionsChanged();
    void currentSessionChanged();
    void viewChanged();
    void statusChanged();
    void framesChanged();
    void paneSlotsChanged();

private:
    struct Host {
        QString key;      // "local", or the host's ID in the host list
        QString label;
        QString endpoint;
        RexClient *rex;
        QTimer *retry;
        QVariantList sessions;
        QString problem;  // why it has no sessions to show, or empty
    };

    struct Pane {
        // The pane area owns the item, and QML may delete it first when
        // the window closes, so hold it weakly.
        QPointer<TerminalItem> terminal;
        QPointer<PaneBorder> border;
        BlockStream *stream = nullptr;
        bool started = false;
    };

    Host *addHost(const QString &key, const QString &label, const QString &endpoint);
    void refreshHosts();
    void setSessions(Host *host, const QJsonArray &sessions);
    void rebuildSessions();
    void pickSession(Host *host);
    Host *hostOf(const QString &sessionId) const;
    void onEvent(Host *host, const QJsonObject &event);
    void refreshSessions(Host *host);
    void fetchView();
    void applyView(const QJsonObject &view);
    void layoutPanes();
    QJsonObject activeWindowObject() const;
    QString focusedBlock() const;
    QJsonObject layerOf(const QString &blockId) const;
    // The pane next to `blockId` in a direction, from the last layout.
    QString neighbor(const QString &blockId, const QString &direction) const;
    // Calls `then` with a new shell block's layout node, started in the
    // focused pane's directory, the way a new tab or split opens in Rex.app.
    void withNewShell(std::function<void(const QJsonObject &layout)> then);
    // Moves a floating layer's panes into a new tab's layout, in place of
    // `window`, then calls `then`. For tabs whose layout is empty.
    void rebuildAsLayout(const QJsonObject &window, const QJsonObject &layer, std::function<void()> then);
    void closePane(const QString &blockId);
    void closeAllPanes();
    void setStatus(const QString &status);
    // Shows a status for a few seconds, for errors of single actions.
    void flashStatus(const QString &status);

    Theme *m_theme;
    QList<Host *> m_hosts;    // this machine first
    Host *m_host = nullptr;   // the host of the session on screen
    RexClient *m_rex = nullptr; // m_host's control connection
    QString m_wanted;
    QVariantList m_sessions;
    QString m_sessionId;
    QJsonObject m_view;
    qint64 m_revision = -1;
    bool m_fetching = false;
    bool m_refetch = false;
    QPointer<QQuickItem> m_area;
    QHash<QString, Pane> m_panes;
    QString m_status;
    QVariantList m_frames, m_minimized, m_paneSlots;
    // Each floating layer's frame position and its panes' positions as last
    // laid out, so a drag can move them by the same offset.
    struct DraggedLayer { QPointF frame; QList<QPair<QString, QPointF>> panes; };
    QHash<QString, DraggedLayer> m_layerPanes;
    // Blocks of the active tab as drawn, with rects in window fractions.
    QList<QPair<QString, QRectF>> m_placed;
};
