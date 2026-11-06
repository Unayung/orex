#pragma once

// Follows the Omarchy theme live: the accent and window colors for orex's
// own chrome, and the terminal palette that orex gives the panes it owns.

#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QString selection READ selection NOTIFY changed)
    // A highlight that text in the foreground color reads on: the theme's
    // selection when it contrasts with the foreground, else a blend of
    // background and foreground. Some themes pick a light selection meant
    // for dark text, which light foreground text can't sit on.
    Q_PROPERTY(QString surface READ surface NOTIFY changed)
    // The palette's red and green, for status marks that must not look like
    // the accent.
    Q_PROPERTY(QString red READ red NOTIFY changed)
    Q_PROPERTY(QString green READ green NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return color("accent", "#FFD60A"); }
    QString accentForeground() const;
    QString background() const { return color("background", "#101010"); }
    QString foreground() const { return color("foreground", "#e0e0e0"); }
    QString selection() const { return color("selection", "#3a3a3a"); }
    QString surface() const;
    QString red() const { return color("color1", "#e06c75"); }
    QString green() const { return color("color2", "#7fbf7f"); }
    int radius() const { return m_radius; }

    // Arguments for com.superlogical.terminal.set_theme, or empty when
    // there's no Omarchy theme, so Rex keeps its built-in colors.
    QJsonObject terminalTheme() const;

    // The `key = "#rrggbb"` lines of a colors.toml.
    static QHash<QString, QString> readColors(const QString &path);

signals:
    void changed();

private:
    QString color(const QString &key, const QString &fallback) const { return m_colors.value(key, fallback); }
    void reload();
    void refreshRounding();

    QString m_directory;
    QHash<QString, QString> m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
