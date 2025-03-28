#pragma once

#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>

class ImageLabel : public QLabel {
    Q_OBJECT

public:
    explicit ImageLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setMouseTracking(true);
    }

    void setScrollArea(QScrollArea *scroll) {
        scrollArea = scroll;
    }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            dragging = true;
            lastPos = event->pos();
            setCursor(Qt::ClosedHandCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (dragging && scrollArea) {
            QPoint delta = event->pos() - lastPos;
            lastPos = event->pos();

            scrollArea->horizontalScrollBar()->setValue(
                scrollArea->horizontalScrollBar()->value() - delta.x());
            scrollArea->verticalScrollBar()->setValue(
                scrollArea->verticalScrollBar()->value() - delta.y());
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            dragging = false;
            setCursor(Qt::ArrowCursor);
        }
    }

private:
    bool dragging = false;
    QPoint lastPos;
    QScrollArea *scrollArea = nullptr;
};
