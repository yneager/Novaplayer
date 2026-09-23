#include "windowbridge.h"

WindowBridge::WindowBridge(QObject *parent)
    : QObject(parent)
{
}

void WindowBridge::setWindowState(bool maximized, bool fullscreen, bool mini)
{
    if (maximized == maximized_ && fullscreen == fullscreen_ && mini == mini_) {
        return;
    }
    maximized_ = maximized;
    fullscreen_ = fullscreen;
    mini_ = mini;
    emit stateChanged();
}

QList<QRectF> WindowBridge::toRects(const QVariantList &list)
{
    QList<QRectF> rects;
    rects.reserve(list.size());
    for (const QVariant &entry : list) {
        const QVariantList values = entry.toList();
        if (values.size() != 4) {
            continue;
        }
        const QRectF rect(values[0].toDouble(), values[1].toDouble(),
                          values[2].toDouble(), values[3].toDouble());
        if (rect.isValid()) {
            rects.append(rect);
        }
    }
    return rects;
}

void WindowBridge::setDragRegions(const QVariantList &drag, const QVariantList &holes)
{
    drag_ = toRects(drag);
    holes_ = toRects(holes);
}

bool WindowBridge::isDragPoint(const QPointF &point) const
{
    for (const QRectF &hole : holes_) {
        if (hole.contains(point)) {
            return false;
        }
    }
    for (const QRectF &area : drag_) {
        if (area.contains(point)) {
            return true;
        }
    }
    return false;
}
