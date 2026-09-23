#pragma once

#include <QByteArray>
#include <QMainWindow>
#include <QString>
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
class QSequentialAnimationGroup;
class QSlider;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class QWidget;
class HomePage;
class PlayerChrome;
class InterpolationController;

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
    void interpolationModeChanged(int index);
    void interpolationDeactivated(const QString &reason);
    void updateInterpolationLabels();
    void toggleSettingsPanel();
    void showHome();
    void resumeFromHome();

private:
    static void wakeup(void *ctx);

    void buildUi();
    void initMpv();
    void handleEvent(mpv_event *event);
    void refreshTracks();
    QString mpvStringProperty(const QByteArray &name) const;
    bool mpvInt64Property(const QByteArray &name, qint64 &value) const;
    bool mpvFlagProperty(const QByteArray &name, bool &value) const;

    void layoutOverlayWidgets();
    void raiseOverlayWidgets();
    void setFullscreenChromeVisible(bool visible);
    void showTrackPicker(QComboBox *combo);
    void updatePlaybackUi();
    void updateCenterState();
    void updateChapterInfo();
    void updateQualityBadge();

    void enterFullscreenControlsMode();
    void leaveFullscreenControlsMode();
    void showFullscreenControls();
    void hideFullscreenControls();
    QRect fullscreenControlsShownRect() const;
    QRect fullscreenControlsHiddenRect() const;

    void showPlayer(bool resumePlayback);
    void transitionTo(QWidget *target);
    bool isPlayerVisible() const;

    void setMpvPropertyFlag(const char *name, bool value);
    void setMpvPropertyDouble(const char *name, double value);
    void setMpvPropertyInt64(const char *name, qint64 value);
    void command(const QStringList &args);
    void updateTimeLabel();
    void syncChromeSettings();
    void showInterpolationError(const QString &message);

    static QString formatTime(double seconds);
    static QString formatFps(double fps);

    mpv_handle *mpv_ = nullptr;
    InterpolationController *interpolation_ = nullptr;

    QWidget *appRoot_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    HomePage *homePage_ = nullptr;
    PlayerChrome *playerChrome_ = nullptr;
    QWidget *root_ = nullptr;
    QWidget *transitionOverlay_ = nullptr;
    QSequentialAnimationGroup *pageTransition_ = nullptr;

    QWidget *video_ = nullptr;
    QVBoxLayout *mainLayout_ = nullptr;

    QWidget *topBar_ = nullptr;
    QWidget *sideRail_ = nullptr;
    QWidget *qualityBadge_ = nullptr;
    QWidget *centerState_ = nullptr;
    QWidget *controls_ = nullptr;
    QWidget *settingsPanel_ = nullptr;

    QLabel *mediaEyebrow_ = nullptr;
    QLabel *mediaTitle_ = nullptr;
    QLabel *qualityPrimary_ = nullptr;
    QLabel *qualitySecondary_ = nullptr;
    QLabel *centerKicker_ = nullptr;
    QLabel *centerText_ = nullptr;
    QLabel *chapterIndexLabel_ = nullptr;
    QLabel *chapterTitleLabel_ = nullptr;
    QLabel *timelinePositionLabel_ = nullptr;
    QLabel *timelineDurationLabel_ = nullptr;
    QLabel *timeLabel_ = nullptr;

    QPushButton *homeButton_ = nullptr;
    QPushButton *playButton_ = nullptr;
    QPushButton *railPlayerButton_ = nullptr;
    QPushButton *centerPlayButton_ = nullptr;
    QPushButton *muteButton_ = nullptr;
    QPushButton *fullscreenButton_ = nullptr;
    QPushButton *loadSubtitleButton_ = nullptr;
    QPushButton *settingsButton_ = nullptr;

    QSlider *seek_ = nullptr;
    QSlider *volume_ = nullptr;
    QComboBox *speed_ = nullptr;
    QComboBox *audioTrack_ = nullptr;
    QComboBox *subtitleTrack_ = nullptr;
    QComboBox *interpolationMode_ = nullptr;

    QTimer *fullscreenControlsTimer_ = nullptr;
    QPropertyAnimation *controlsSlide_ = nullptr;

    QString currentPath_;
    int controlsHeight_ = 0;
    double position_ = 0.0;
    double duration_ = 0.0;
    bool paused_ = false;
    bool muted_ = false;
    bool seeking_ = false;
    bool mediaLoaded_ = false;
    bool settingsVisible_ = false;
    bool fullscreenControlsVisible_ = true;
};
