#pragma once

// Everything orex shows from one Rex server: the list of sessions, the tabs
// of the session on screen, and a terminal for each pane in the active tab.
// Terminals are made here rather than in QML, so a layout change moves and
// resizes them instead of tearing down their streams.

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QRectF>
#include <QVariantList>
#include <functional>

#include "rexclient.h"
#include "theme.h"

class BlockStream;
class QQuickItem;
class TerminalItem;

class Workspace : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QString currentSession READ currentSession NOTIFY currentSessionChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY viewChanged)
    Q_PROPERTY(QString activeWindow READ activeWindow NOTIFY viewChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // Floating layers of the active tab, for QML to draw their frames:
    // layer_id, title, x, y, width, height (pixels of the pane area), z, focused.
    Q_PROPERTY(QVariantList frames READ frames NOTIFY framesChanged)
    // Minimized floating layers: layer_id, title.
    Q_PROPERTY(QVariantList minimized READ minimized NOTIFY framesChanged)
    // Every action orex offers: id, title, shortcut. The palette lists them.
    Q_PROPERTY(QVariantList actions READ actions CONSTANT)

public:
    explicit Workspace(Theme *theme, QObject *parent = nullptr);
    ~Workspace() override;

    QVariantList sessions() const { return m_sessions; }
    QString currentSession() const { return m_sessionId; }
    QVariantList windows() const;
    QString activeWindow() const { return m_view.value("active_window_id").toString(); }
    QString status() const { return m_status; }
    QVariantList frames() const { return m_frames; }
    QVariantList minimized() const { return m_minimized; }
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
    Q_INVOKABLE void raiseLayer(const QString &layerId);
    Q_INVOKABLE void setLayerMinimized(const QString &layerId, bool minimized);

signals:
    void sessionsChanged();
    void currentSessionChanged();
    void viewChanged();
    void statusChanged();
    void framesChanged();

private:
    struct Pane {
        // The pane area owns the item, and QML may delete it first when
        // the window closes, so hold it weakly.
        QPointer<TerminalItem> terminal;
        BlockStream *stream = nullptr;
        bool started = false;
    };

    void onEvent(const QJsonObject &event);
    void refreshSessions();
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
    void closePane(const QString &blockId);
    void closeAllPanes();
    void setStatus(const QString &status);

    void reconnect();

    Theme *m_theme;
    RexClient m_rex;
    QTimer m_reconnect;
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
    QVariantList m_frames, m_minimized;
    // Blocks of the active tab as drawn, with rects in window fractions.
    QList<QPair<QString, QRectF>> m_placed;
};
