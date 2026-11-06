#pragma once

// One terminal screen, drawn from a libghostty-vt terminal. It knows nothing
// about Rex: something else feeds it bytes and carries its input away.

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QQuickPaintedItem>

typedef struct GhosttyTerminalImpl *GhosttyTerminal;

class TerminalItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int cols READ cols NOTIFY gridSizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY gridSizeChanged)
    // The background of selected cells. Left unset, selected cells swap
    // their foreground and background instead.
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY selectionColorChanged)
    // In points. Changing it refits the grid, which resizes the pty.
    Q_PROPERTY(qreal fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    // Whether a finished selection also goes to the clipboard; it always goes
    // to the primary selection.
    Q_PROPERTY(bool copyOnSelect MEMBER m_copyOnSelect)

public:
    explicit TerminalItem(QQuickItem *parent = nullptr);
    ~TerminalItem() override;

    int cols() const;
    int rows() const;

    QColor selectionColor() const;
    qreal fontSize() const;
    void setFontSize(qreal points);
    void setSelectionColor(const QColor &color);

    // Replaces the terminal with one restored from a Ghostty snapshot
    // (the bytes starting with "GHOSTSNP"). Returns false if it can't be read.
    Q_INVOKABLE bool restoreSnapshot(const QByteArray &snapshot);

    // Program output, in VT bytes, applied after the snapshot.
    Q_INVOKABLE void feed(const QByteArray &vt);

    // The size the program's pty really has. The server decides it, so the
    // local terminal follows this rather than the item's geometry.
    Q_INVOKABLE void resizeGrid(int cols, int rows);

    // For the pane's context menu.
    Q_INVOKABLE bool hasSelection() const;
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void paste();

    // Rex's "theme" message: default foreground, background and cursor as
    // RRGGBB or #RRGGBB, and "palette" as "index=RRGGBB" changes to the
    // 256-color palette. A color left out is the built-in one, and each
    // call replaces the theme before it. A restored snapshot keeps its own
    // colors until the next call; Rex sends a theme after each snapshot.
    Q_INVOKABLE void setTheme(const QJsonObject &theme);

    void paint(QPainter *painter) override;

signals:
    // A right click, at a point in the item, for the pane's context menu.
    void contextMenuRequested(QPointF position);
    void fontSizeChanged();
    // The pointer came into the pane, for focus that follows the mouse.
    void pointerEntered();
    // Bytes to send to the program: encoded keys, pastes, mouse reports.
    void input(const QByteArray &bytes);
    // The cols and rows that fit the item at its current size and font.
    // The owner of the pty asks the server for this size.
    void gridSizeChanged(int cols, int rows);
    void selectionColorChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverEnterEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    bool m_copyOnSelect = true;
    struct Private;
    Private *d;
};
