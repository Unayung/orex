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
#include <QVariantList>

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

public:
    explicit Workspace(Theme *theme, QObject *parent = nullptr);
    ~Workspace() override;

    QVariantList sessions() const { return m_sessions; }
    QString currentSession() const { return m_sessionId; }
    QVariantList windows() const;
    QString activeWindow() const { return m_view.value("active_window_id").toString(); }
    QString status() const { return m_status; }

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

signals:
    void sessionsChanged();
    void currentSessionChanged();
    void viewChanged();
    void statusChanged();

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
};
