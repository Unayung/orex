#pragma once

// One terminal screen, drawn from a libghostty-vt terminal. It knows nothing
// about Rex: something else feeds it bytes and carries its input away.

#include <QByteArray>
#include <QQuickPaintedItem>

typedef struct GhosttyTerminalImpl *GhosttyTerminal;

class TerminalItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int cols READ cols NOTIFY gridSizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY gridSizeChanged)

public:
    explicit TerminalItem(QQuickItem *parent = nullptr);
    ~TerminalItem() override;

    int cols() const;
    int rows() const;

    // Replaces the terminal with one restored from a Ghostty snapshot
    // (the bytes starting with "GHOSTSNP"). Returns false if it can't be read.
    Q_INVOKABLE bool restoreSnapshot(const QByteArray &snapshot);

    // Program output, in VT bytes, applied after the snapshot.
    Q_INVOKABLE void feed(const QByteArray &vt);

    // The size the program's pty really has. The server decides it, so the
    // local terminal follows this rather than the item's geometry.
    Q_INVOKABLE void resizeGrid(int cols, int rows);

    void paint(QPainter *painter) override;

signals:
    // Bytes to send to the program: encoded keys, pastes, mouse reports.
    void input(const QByteArray &bytes);
    // The cols and rows that fit the item at its current size and font.
    // The owner of the pty asks the server for this size.
    void gridSizeChanged(int cols, int rows);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    struct Private;
    Private *d;
};
