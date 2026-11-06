#pragma once

// The frame around one pane. It is a sibling of the pane's terminal rather
// than part of it, so the terminal's grid and mouse mapping stay unaware of
// it. It also carries the pane's program status: a wider, colored line, and
// a slow pulse while the program works.

#include <QPainter>
#include <QPropertyAnimation>
#include <QQuickPaintedItem>

class PaneBorder : public QQuickPaintedItem {
    Q_OBJECT

public:
    explicit PaneBorder(QQuickItem *parent = nullptr) : QQuickPaintedItem(parent), m_pulse(this, "opacity") {
        m_pulse.setDuration(1400);
        m_pulse.setKeyValueAt(0, 1.0);
        m_pulse.setKeyValueAt(0.5, 0.3);
        m_pulse.setKeyValueAt(1, 1.0);
        m_pulse.setEasingCurve(QEasingCurve::InOutSine);
        m_pulse.setLoopCount(-1);
    }

    void setStyle(const QColor &color, qreal width, bool pulse) {
        if (color != m_color || width != m_width) {
            m_color = color;
            m_width = width;
            update();
        }
        if (pulse && m_pulse.state() != QAbstractAnimation::Running) {
            m_pulse.start();
        } else if (!pulse && m_pulse.state() == QAbstractAnimation::Running) {
            m_pulse.stop();
            setOpacity(1);
        }
    }

    void paint(QPainter *painter) override {
        painter->setPen(QPen(m_color, m_width));
        const qreal half = m_width / 2;
        painter->drawRect(QRectF(half, half, width() - m_width, height() - m_width));
    }

private:
    QColor m_color;
    qreal m_width = 1;
    QPropertyAnimation m_pulse;
};
