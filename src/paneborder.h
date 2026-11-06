#pragma once

// The 1-pixel frame around one pane. It is a sibling of the pane's terminal
// rather than part of it, so the terminal's grid and mouse mapping stay
// unaware of it.

#include <QPainter>
#include <QQuickPaintedItem>

class PaneBorder : public QQuickPaintedItem {
    Q_OBJECT

public:
    explicit PaneBorder(QQuickItem *parent = nullptr) : QQuickPaintedItem(parent) {}

    void setColor(const QColor &color) {
        if (color == m_color)
            return;
        m_color = color;
        update();
    }

    void paint(QPainter *painter) override {
        painter->setPen(QPen(m_color, 1));
        painter->drawRect(QRectF(0.5, 0.5, width() - 1, height() - 1));
    }

private:
    QColor m_color;
};
