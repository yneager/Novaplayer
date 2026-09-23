#pragma once

#include <QList>
#include <QObject>
#include <QRectF>
#include <QVariantList>

// Exposed to each Vui web page through Qt WebChannel as "windowBridge".
//
// The pages draw LAMBDA Player's custom window controls and report which of
// their areas behave like a title bar ("drag regions") and which interactive
// elements sit inside those areas ("holes"). MainWindow uses these rectangles
// to answer Windows' WM_NCHITTEST, so dragging, Aero Snap, double-click to
// maximize and the system menu all stay native.
class WindowBridge final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool maximized READ isMaximized NOTIFY stateChanged)
    Q_PROPERTY(bool fullscreen READ isFullscreen NOTIFY stateChanged)
    Q_PROPERTY(bool mini READ isMini NOTIFY stateChanged)

public:
    explicit WindowBridge(QObject *parent = nullptr);

    bool isMaximized() const { return maximized_; }
    bool isFullscreen() const { return fullscreen_; }
    bool isMini() const { return mini_; }

    void setWindowState(bool maximized, bool fullscreen, bool mini);

    // Point in the page's CSS pixels (== Qt logical pixels of the window).
    bool isDragPoint(const QPointF &point) const;

public slots:
    void minimize() { emit minimizeRequested(); }
    void toggleMaximize() { emit toggleMaximizeRequested(); }
    void close() { emit closeRequested(); }
    void setDragRegions(const QVariantList &drag, const QVariantList &holes);

signals:
    void stateChanged();
    void minimizeRequested();
    void toggleMaximizeRequested();
    void closeRequested();

private:
    static QList<QRectF> toRects(const QVariantList &list);

    QList<QRectF> drag_;
    QList<QRectF> holes_;
    bool maximized_ = false;
    bool fullscreen_ = false;
    bool mini_ = false;
};
