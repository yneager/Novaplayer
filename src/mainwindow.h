#pragma once

#include <QByteArray>
#include <QMainWindow>
#include <QStringList>
#include <mpv/client.h>

class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QLabel;
class QKeyEvent;
class QPropertyAnimation;
class QPushButton;
class QResizeEvent;
class QSlider;
class QTimer;
class QVBoxLayout;
class QWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openPath(const QString &path);

signals:
    void mpvWakeup();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void openFile();
    void loadSubtitle();
    void togglePause();
    void toggleMute();
    void toggleFullscreen();
    void processMpvEvents();
    void seekReleased();
    void volumeChanged(int value);
    void speedChanged(int index);
    void audioTrackChanged(int index);
    void subtitleTrackChanged(int index);

private:
    static void wakeup(void *ctx);

    void buildUi();
    void initMpv();
    void handleEvent(mpv_event *event);
    void refreshTracks();
    QString mpvStringProperty(const QByteArray &name) const;
    bool mpvInt64Property(const QByteArray &name, qint64 &value) const;
    bool mpvFlagProperty(const QByteArray &name, bool &value) const;
    void enterFullscreenControlsMode();
    void leaveFullscreenControlsMode();
    void showFullscreenControls();
    void hideFullscreenControls();
    QRect fullscreenControlsShownRect() const;
    QRect fullscreenControlsHiddenRect() const;
    void setMpvPropertyFlag(const char *name, bool value);
    void setMpvPropertyDouble(const char *name, double value);
    void setMpvPropertyInt64(const char *name, qint64 value);
    void command(const QStringList &args);
    void updateTimeLabel();
    static QString formatTime(double seconds);

    mpv_handle *mpv_ = nullptr;
    QWidget *root_ = nullptr;
    QWidget *video_ = nullptr;
    QWidget *controls_ = nullptr;
    QVBoxLayout *mainLayout_ = nullptr;
    QPushButton *playButton_ = nullptr;
    QPushButton *muteButton_ = nullptr;
    QPushButton *fullscreenButton_ = nullptr;
    QPushButton *loadSubtitleButton_ = nullptr;
    QSlider *seek_ = nullptr;
    QSlider *volume_ = nullptr;
    QLabel *timeLabel_ = nullptr;
    QComboBox *speed_ = nullptr;
    QComboBox *audioTrack_ = nullptr;
    QComboBox *subtitleTrack_ = nullptr;
    QTimer *fullscreenControlsTimer_ = nullptr;
    QPropertyAnimation *controlsSlide_ = nullptr;
    int controlsHeight_ = 0;
    double position_ = 0.0;
    double duration_ = 0.0;
    bool paused_ = false;
    bool muted_ = false;
    bool seeking_ = false;
    bool fullscreenControlsVisible_ = true;
};
