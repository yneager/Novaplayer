#include "mainwindow.h"
#include "homepage.h"
#include "interpolationcontroller.h"

#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSequentialAnimationGroup>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <stdexcept>
#include <vector>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("NovaPlayer");
    setAcceptDrops(true);

    buildUi();
    initMpv();

    connect(this, &MainWindow::mpvWakeup,
            this, &MainWindow::processMpvEvents,
            Qt::QueuedConnection);

    qApp->installEventFilter(this);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);

    if (mpv_) {
        mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
        mpv_terminate_destroy(mpv_);
    }
}

void MainWindow::buildUi()
{
    setMinimumSize(960, 620);

    appRoot_ = new QWidget(this);
    appRoot_->setObjectName("appRoot");
    auto *appLayout = new QVBoxLayout(appRoot_);
    appLayout->setContentsMargins(0, 0, 0, 0);
    appLayout->setSpacing(0);

    stack_ = new QStackedWidget(appRoot_);
    stack_->setObjectName("applicationStack");
    appLayout->addWidget(stack_);

    homePage_ = new HomePage(stack_);
    root_ = new QWidget(stack_);
    root_->setObjectName("playerPage");
    stack_->addWidget(homePage_);
    stack_->addWidget(root_);
    stack_->setCurrentWidget(homePage_);

    connect(homePage_, &HomePage::openVideoRequested, this, &MainWindow::openFile);
    connect(homePage_, &HomePage::resumeRequested, this, &MainWindow::resumeFromHome);

    mainLayout_ = new QVBoxLayout(root_);
    mainLayout_->setContentsMargins(0, 0, 0, 0);
    mainLayout_->setSpacing(0);

    video_ = new QWidget(root_);
    video_->setObjectName("videoSurface");
    video_->setAttribute(Qt::WA_NativeWindow);
    video_->setAttribute(Qt::WA_DontCreateNativeAncestors);
    video_->setMinimumSize(640, 360);
    video_->setMouseTracking(true);
    mainLayout_->addWidget(video_, 1);

    // Top glass bar: Vui's brand/title region plus two global action buttons.
    // The concept's share slot is mapped to NovaPlayer's existing Open action
    // rather than inventing a new sharing service.
    topBar_ = new QWidget(root_);
    topBar_->setObjectName("topBar");
    auto *topLayout = new QHBoxLayout(topBar_);
    topLayout->setContentsMargins(14, 10, 12, 10);
    topLayout->setSpacing(12);

    auto *brand = new QLabel("◉  NOVA", topBar_);
    brand->setObjectName("brandMark");
    brand->setMinimumWidth(92);
    topLayout->addWidget(brand);

    auto *titleStack = new QWidget(topBar_);
    auto *titleLayout = new QVBoxLayout(titleStack);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(1);

    mediaEyebrow_ = new QLabel("READY", titleStack);
    mediaEyebrow_->setObjectName("eyebrow");
    mediaTitle_ = new QLabel("Open or drop a video", titleStack);
    mediaTitle_->setObjectName("mediaTitle");
    mediaTitle_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleLayout->addWidget(mediaEyebrow_);
    titleLayout->addWidget(mediaTitle_);
    topLayout->addWidget(titleStack, 1);

    auto makeButton = [](QWidget *parent, const QString &text, const QString &role,
                         const QString &tooltip) {
        auto *button = new QPushButton(text, parent);
        button->setProperty("uiRole", role);
        button->setToolTip(tooltip);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::StrongFocus);
        return button;
    };

    homeButton_ = makeButton(topBar_, "⌂", "icon", "Back to Vui home");
    auto *topOpenButton = makeButton(topBar_, "↥", "icon", "Open video");
    auto *moreButton = makeButton(topBar_, "•••", "icon", "More playback options");
    topLayout->addWidget(homeButton_);
    topLayout->addWidget(topOpenButton);
    topLayout->addWidget(moreButton);

    // Four-button Vui quick-action rail. Every button is retained and mapped
    // onto an existing NovaPlayer action.
    sideRail_ = new QWidget(root_);
    sideRail_->setObjectName("sideRail");
    auto *railLayout = new QVBoxLayout(sideRail_);
    railLayout->setContentsMargins(8, 8, 8, 8);
    railLayout->setSpacing(7);

    railPlayerButton_ = makeButton(sideRail_, "▶", "rail", "Play / pause");
    railPlayerButton_->setProperty("active", true);
    auto *railAudioButton = makeButton(sideRail_, "♪", "rail", "Audio tracks");
    auto *railCaptionsButton = makeButton(sideRail_, "CC", "rail", "Subtitle tracks");
    auto *railCinemaButton = makeButton(sideRail_, "▣", "rail", "Cinema / fullscreen");

    railLayout->addWidget(railPlayerButton_);
    railLayout->addWidget(railAudioButton);
    railLayout->addWidget(railCaptionsButton);
    railLayout->addWidget(railCinemaButton);

    // Center play state from the Vui concept. It appears before a file is
    // loaded and while playback is paused.
    centerState_ = new QWidget(root_);
    centerState_->setAttribute(Qt::WA_TranslucentBackground);
    auto *centerLayout = new QVBoxLayout(centerState_);
    centerLayout->setContentsMargins(8, 8, 8, 8);
    centerLayout->setAlignment(Qt::AlignCenter);
    centerLayout->setSpacing(8);

    centerPlayButton_ = makeButton(centerState_, "▶", "playCore", "Play / pause");
    centerKicker_ = new QLabel("READY", centerState_);
    centerKicker_->setObjectName("centerKicker");
    centerKicker_->setAlignment(Qt::AlignCenter);
    centerText_ = new QLabel("Open or drop a video", centerState_);
    centerText_->setObjectName("mutedLabel");
    centerText_->setAlignment(Qt::AlignCenter);

    centerLayout->addWidget(centerPlayButton_, 0, Qt::AlignHCenter);
    centerLayout->addWidget(centerKicker_);
    centerLayout->addWidget(centerText_);

    // Truthful quality/status badge: resolution + current interpolation path.
    // We intentionally do not claim HDR unless we actually add verified HDR
    // metadata detection later.
    qualityBadge_ = new QWidget(root_);
    qualityBadge_->setObjectName("qualityBadge");
    auto *qualityLayout = new QHBoxLayout(qualityBadge_);
    qualityLayout->setContentsMargins(10, 6, 10, 6);
    qualityLayout->setSpacing(7);
    auto *statusDot = new QLabel("●", qualityBadge_);
    statusDot->setObjectName("statusDot");
    qualityPrimary_ = new QLabel("VIDEO", qualityBadge_);
    qualityPrimary_->setObjectName("qualityPrimary");
    auto *divider = new QLabel("│", qualityBadge_);
    divider->setObjectName("mutedLabel");
    qualitySecondary_ = new QLabel("ORIGINAL", qualityBadge_);
    qualitySecondary_->setObjectName("qualitySecondary");
    qualityLayout->addWidget(statusDot);
    qualityLayout->addWidget(qualityPrimary_);
    qualityLayout->addWidget(divider);
    qualityLayout->addWidget(qualitySecondary_);

    // Bottom Vui control deck.
    controls_ = new QWidget(root_);
    controls_->setObjectName("controlDeck");
    auto *controlsLayout = new QVBoxLayout(controls_);
    controlsLayout->setContentsMargins(16, 12, 16, 12);
    controlsLayout->setSpacing(8);

    auto *timelineLabels = new QHBoxLayout;
    timelineLabels->setContentsMargins(1, 0, 1, 0);
    timelinePositionLabel_ = new QLabel("00:00", controls_);
    timelinePositionLabel_->setObjectName("timelineLabel");
    timelineDurationLabel_ = new QLabel("00:00", controls_);
    timelineDurationLabel_->setObjectName("timelineLabel");
    timelineLabels->addWidget(timelinePositionLabel_);
    timelineLabels->addStretch();
    timelineLabels->addWidget(timelineDurationLabel_);
    controlsLayout->addLayout(timelineLabels);

    seek_ = new QSlider(Qt::Horizontal, controls_);
    seek_->setObjectName("timelineSlider");
    seek_->setRange(0, 1000);
    seek_->setCursor(Qt::PointingHandCursor);
    connect(seek_, &QSlider::sliderPressed, this, [this] { seeking_ = true; });
    connect(seek_, &QSlider::sliderReleased, this, &MainWindow::seekReleased);
    controlsLayout->addWidget(seek_);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(7);

    playButton_ = makeButton(controls_, "▶", "strong", "Play / pause");
    auto *nextButton = makeButton(controls_, "⏭", "icon", "Next playlist item");
    muteButton_ = makeButton(controls_, "VOL", "text", "Mute / unmute");

    volume_ = new QSlider(Qt::Horizontal, controls_);
    volume_->setObjectName("volumeSlider");
    volume_->setRange(0, 100);
    volume_->setValue(80);
    volume_->setFixedWidth(82);

    timeLabel_ = new QLabel("00:00  /  00:00", controls_);
    timeLabel_->setObjectName("timecode");

    buttonRow->addWidget(playButton_);
    buttonRow->addWidget(nextButton);
    buttonRow->addWidget(muteButton_);
    buttonRow->addWidget(volume_);
    buttonRow->addWidget(timeLabel_);
    buttonRow->addStretch(1);

    auto *chapterPill = new QWidget(controls_);
    chapterPill->setObjectName("chapterPill");
    auto *chapterLayout = new QHBoxLayout(chapterPill);
    chapterLayout->setContentsMargins(8, 5, 9, 5);
    chapterLayout->setSpacing(8);

    chapterIndexLabel_ = new QLabel("--", chapterPill);
    chapterIndexLabel_->setObjectName("chapterIndex");

    auto *chapterCopy = new QWidget(chapterPill);
    auto *chapterCopyLayout = new QVBoxLayout(chapterCopy);
    chapterCopyLayout->setContentsMargins(0, 0, 0, 0);
    chapterCopyLayout->setSpacing(0);
    auto *chapterKicker = new QLabel("CHAPTER", chapterCopy);
    chapterKicker->setObjectName("chapterKicker");
    chapterTitleLabel_ = new QLabel("No chapters", chapterCopy);
    chapterTitleLabel_->setObjectName("chapterTitle");
    chapterCopyLayout->addWidget(chapterKicker);
    chapterCopyLayout->addWidget(chapterTitleLabel_);

    auto *wave = new QLabel("▂▅▇▃▆", chapterPill);
    wave->setObjectName("wave");

    chapterLayout->addWidget(chapterIndexLabel_);
    chapterLayout->addWidget(chapterCopy);
    chapterLayout->addWidget(wave);
    buttonRow->addWidget(chapterPill);
    buttonRow->addStretch(1);

    speed_ = new QComboBox(controls_);
    speed_->setFixedWidth(72);
    speed_->addItems({"0.50×", "0.75×", "1.00×", "1.25×", "1.50×", "2.00×"});
    speed_->setCurrentIndex(2);
    speed_->setToolTip("Playback speed");

    auto *captionsButton = makeButton(controls_, "CC", "icon", "Subtitle tracks");
    settingsButton_ = makeButton(controls_, "⚙", "icon", "Settings");
    auto *pipButton = makeButton(controls_, "▣", "icon", "Picture in picture");
    pipButton->setEnabled(false);
    pipButton->setToolTip("Picture in picture is not implemented in v0.2.1");
    fullscreenButton_ = makeButton(controls_, "⛶", "strong", "Fullscreen");

    buttonRow->addWidget(speed_);
    buttonRow->addWidget(captionsButton);
    buttonRow->addWidget(settingsButton_);
    buttonRow->addWidget(pipButton);
    buttonRow->addWidget(fullscreenButton_);
    controlsLayout->addLayout(buttonRow);

    // NovaPlayer-specific controls live in a compact expandable settings row
    // instead of being lost when adopting Vui's cleaner deck.
    settingsPanel_ = new QWidget(controls_);
    settingsPanel_->setObjectName("settingsPanel");
    auto *settingsLayout = new QHBoxLayout(settingsPanel_);
    settingsLayout->setContentsMargins(9, 7, 9, 7);
    settingsLayout->setSpacing(7);

    auto *openButton = makeButton(settingsPanel_, "Open File", "text", "Open video");
    audioTrack_ = new QComboBox(settingsPanel_);
    audioTrack_->setMinimumWidth(170);
    audioTrack_->addItem("No audio tracks");
    audioTrack_->setEnabled(false);

    subtitleTrack_ = new QComboBox(settingsPanel_);
    subtitleTrack_->setMinimumWidth(170);
    subtitleTrack_->addItem("Subtitles Off", QVariant::fromValue<qlonglong>(-1));

    loadSubtitleButton_ = makeButton(settingsPanel_, "Load Subtitle", "text", "Load external subtitle");

    interpolationMode_ = new QComboBox(settingsPanel_);
    interpolationMode_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    interpolationMode_->addItem("Original");
    interpolationMode_->addItem("Double frame rate (RIFE)");
    interpolationMode_->addItem("60 fps (RIFE)");
    interpolationMode_->setCurrentIndex(0);
    interpolationMode_->setToolTip("Frame interpolation (RIFE v4.6 via VapourSynth, Vulkan GPU)");

    auto *audioLabel = new QLabel("AUDIO", settingsPanel_);
    audioLabel->setObjectName("mutedLabel");
    auto *subLabel = new QLabel("SUBTITLES", settingsPanel_);
    subLabel->setObjectName("mutedLabel");
    auto *rifeLabel = new QLabel("INTERPOLATION", settingsPanel_);
    rifeLabel->setObjectName("mutedLabel");

    settingsLayout->addWidget(openButton);
    settingsLayout->addSpacing(4);
    settingsLayout->addWidget(audioLabel);
    settingsLayout->addWidget(audioTrack_);
    settingsLayout->addWidget(subLabel);
    settingsLayout->addWidget(subtitleTrack_);
    settingsLayout->addWidget(loadSubtitleButton_);
    settingsLayout->addWidget(rifeLabel);
    settingsLayout->addWidget(interpolationMode_);
    settingsLayout->addStretch();

    settingsPanel_->setVisible(false);
    controlsLayout->addWidget(settingsPanel_);

    setCentralWidget(appRoot_);

    transitionOverlay_ = new QWidget(appRoot_);
    transitionOverlay_->setObjectName("transitionCurtain");
    transitionOverlay_->setAttribute(Qt::WA_NativeWindow);
    transitionOverlay_->winId();
    transitionOverlay_->hide();

    connect(homeButton_, &QPushButton::clicked, this, &MainWindow::showHome);
    connect(topOpenButton, &QPushButton::clicked, this, &MainWindow::openFile);
    connect(moreButton, &QPushButton::clicked, this, &MainWindow::toggleSettingsPanel);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openFile);
    connect(loadSubtitleButton_, &QPushButton::clicked, this, &MainWindow::loadSubtitle);

    connect(playButton_, &QPushButton::clicked, this, &MainWindow::togglePause);
    connect(railPlayerButton_, &QPushButton::clicked, this, &MainWindow::togglePause);
    connect(centerPlayButton_, &QPushButton::clicked, this, &MainWindow::togglePause);
    connect(nextButton, &QPushButton::clicked, this, [this] {
        command({"playlist-next", "weak"});
    });

    connect(railAudioButton, &QPushButton::clicked, this, [this] {
        showTrackPicker(audioTrack_);
    });
    connect(railCaptionsButton, &QPushButton::clicked, this, [this] {
        showTrackPicker(subtitleTrack_);
    });
    connect(captionsButton, &QPushButton::clicked, this, [this] {
        showTrackPicker(subtitleTrack_);
    });
    connect(railCinemaButton, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);

    connect(settingsButton_, &QPushButton::clicked, this, &MainWindow::toggleSettingsPanel);
    connect(muteButton_, &QPushButton::clicked, this, &MainWindow::toggleMute);
    connect(fullscreenButton_, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);
    connect(volume_, &QSlider::valueChanged, this, &MainWindow::volumeChanged);
    connect(speed_, &QComboBox::currentIndexChanged, this, &MainWindow::speedChanged);
    connect(audioTrack_, &QComboBox::currentIndexChanged, this, &MainWindow::audioTrackChanged);
    connect(subtitleTrack_, &QComboBox::currentIndexChanged, this, &MainWindow::subtitleTrackChanged);
    connect(interpolationMode_, &QComboBox::currentIndexChanged, this, &MainWindow::interpolationModeChanged);

    fullscreenControlsTimer_ = new QTimer(this);
    fullscreenControlsTimer_->setSingleShot(true);
    fullscreenControlsTimer_->setInterval(2200);
    connect(fullscreenControlsTimer_, &QTimer::timeout,
            this, &MainWindow::hideFullscreenControls);

    controlsSlide_ = new QPropertyAnimation(controls_, "geometry", this);
    controlsSlide_->setDuration(500);
    controlsSlide_->setEasingCurve(QEasingCurve::OutCubic);
    connect(controlsSlide_, &QPropertyAnimation::finished, this, [this] {
        if (isFullScreen() && !fullscreenControlsVisible_) {
            controls_->hide();
            setCursor(Qt::BlankCursor);
        }
    });

    root_->setMouseTracking(true);
    for (QWidget *widget : root_->findChildren<QWidget *>()) {
        widget->setMouseTracking(true);
    }

    updatePlaybackUi();
    updateCenterState();
    updateQualityBadge();
    layoutOverlayWidgets();
}

void MainWindow::initMpv()
{
    // Before mpv_create(): mpv snapshots the environment on first use.
    InterpolationController::configureProcessEnvironment();

    mpv_ = mpv_create();
    if (!mpv_) {
        throw std::runtime_error("Could not create libmpv context.");
    }

    int64_t wid = static_cast<int64_t>(video_->winId());
    mpv_set_option(mpv_, "wid", MPV_FORMAT_INT64, &wid);
    mpv_set_option_string(mpv_, "hwdec", "auto-safe");
    mpv_set_option_string(mpv_, "keep-open", "yes");
    mpv_set_option_string(mpv_, "osc", "no");
    mpv_set_option_string(mpv_, "input-default-bindings", "no");
    mpv_set_option_string(mpv_, "sub-visibility", "yes");
    mpv_set_option_string(mpv_, "embeddedfonts", "yes");
    mpv_set_option_string(mpv_, "demuxer-mkv-subtitle-preroll", "yes");

    if (mpv_initialize(mpv_) < 0) {
        throw std::runtime_error("Could not initialize libmpv.");
    }

    mpv_observe_property(mpv_, 1, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 2, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 3, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 4, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 5, "chapter", MPV_FORMAT_INT64);

    // Error-level log messages are needed to notice when mpv disables the
    // RIFE VapourSynth filter after a script/runtime failure.
    mpv_request_log_messages(mpv_, "error");

    interpolation_ = new InterpolationController(mpv_, this);
    connect(interpolation_, &InterpolationController::deactivated,
            this, &MainWindow::interpolationDeactivated);

    mpv_set_wakeup_callback(mpv_, &MainWindow::wakeup, this);

    // libmpv owns a native video child window. Make the Vui overlay surfaces
    // native siblings too so they reliably stay above the video HWND on Windows.
    for (QWidget *overlay : {topBar_, sideRail_, qualityBadge_, centerState_, controls_}) {
        overlay->setAttribute(Qt::WA_NativeWindow);
        overlay->winId();
    }

    setMpvPropertyDouble("volume", 80);
    layoutOverlayWidgets();
    raiseOverlayWidgets();
}

void MainWindow::wakeup(void *ctx)
{
    emit static_cast<MainWindow *>(ctx)->mpvWakeup();
}

void MainWindow::processMpvEvents()
{
    if (!mpv_) {
        return;
    }

    for (;;) {
        auto *event = mpv_wait_event(mpv_, 0);
        if (!event || event->event_id == MPV_EVENT_NONE) {
            break;
        }
        handleEvent(event);
    }
}

void MainWindow::handleEvent(mpv_event *event)
{
    if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
        auto *property = static_cast<mpv_event_property *>(event->data);
        if (!property || !property->data) {
            return;
        }

        const QString name = QString::fromUtf8(property->name);

        if (name == "time-pos" && property->format == MPV_FORMAT_DOUBLE) {
            position_ = *static_cast<double *>(property->data);
            if (!seeking_ && duration_ > 0) {
                seek_->setValue(int(position_ / duration_ * 1000));
            }
            updateTimeLabel();
        } else if (name == "duration" && property->format == MPV_FORMAT_DOUBLE) {
            duration_ = *static_cast<double *>(property->data);
            updateTimeLabel();
        } else if (name == "pause" && property->format == MPV_FORMAT_FLAG) {
            paused_ = *static_cast<int *>(property->data) != 0;
            updatePlaybackUi();
            updateCenterState();
        } else if (name == "mute" && property->format == MPV_FORMAT_FLAG) {
            muted_ = *static_cast<int *>(property->data) != 0;
            updatePlaybackUi();
        } else if (name == "chapter" && property->format == MPV_FORMAT_INT64) {
            updateChapterInfo();
        }
    } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
        mediaLoaded_ = true;
        paused_ = false;
        if (homePage_ && !currentPath_.isEmpty()) {
            homePage_->setCurrentMedia(QFileInfo(currentPath_).fileName(), true);
        }
        mediaEyebrow_->setText("NOW PLAYING");
        updatePlaybackUi();
        updateCenterState();
        QTimer::singleShot(0, this, &MainWindow::refreshTracks);
        QTimer::singleShot(0, this, &MainWindow::updateInterpolationLabels);
        QTimer::singleShot(0, this, &MainWindow::updateChapterInfo);
        QTimer::singleShot(0, this, &MainWindow::updateQualityBadge);
    } else if (event->event_id == MPV_EVENT_END_FILE) {
        paused_ = true;
        updatePlaybackUi();
        updateCenterState();
    } else if (event->event_id == MPV_EVENT_LOG_MESSAGE) {
        auto *message = static_cast<mpv_event_log_message *>(event->data);
        if (message && interpolation_) {
            interpolation_->handleLogMessage(QString::fromUtf8(message->prefix),
                                             QString::fromUtf8(message->level),
                                             QString::fromUtf8(message->text));
        }
    }
}

void MainWindow::interpolationModeChanged(int index)
{
    if (!interpolation_ || index < 0) {
        return;
    }

    auto mode = InterpolationController::Mode::Off;
    if (index == 1) {
        mode = InterpolationController::Mode::RifeDouble;
    } else if (index == 2) {
        mode = InterpolationController::Mode::Rife60;
    }

    QString error;
    if (!interpolation_->setMode(mode, &error)) {
        const QSignalBlocker blocker(interpolationMode_);
        interpolationMode_->setCurrentIndex(0);
        updateQualityBadge();
        showInterpolationError(error);
        return;
    }

    updateQualityBadge();
}

QString MainWindow::formatFps(double fps)
{
    if (std::abs(fps - std::round(fps)) < 0.005) {
        return QString::number(qRound(fps));
    }
    QString text = QString::number(fps, 'f', 3);
    while (text.endsWith('0')) {
        text.chop(1);
    }
    return text;
}

void MainWindow::updateInterpolationLabels()
{
    if (!mpv_ || !interpolationMode_) {
        return;
    }

    double containerFps = 0.0;
    mpv_get_property(mpv_, "container-fps", MPV_FORMAT_DOUBLE, &containerFps);
    const double fps = InterpolationController::normalizedFps(containerFps);

    if (fps > 0.0) {
        interpolationMode_->setItemText(0, QString("Original (%1 fps)").arg(formatFps(fps)));
        interpolationMode_->setItemText(1, QString("%1 fps (RIFE)").arg(formatFps(fps * 2.0)));
    } else {
        interpolationMode_->setItemText(0, "Original");
        interpolationMode_->setItemText(1, "Double frame rate (RIFE)");
    }

    // 60 fps mode only makes sense below 60 fps.
    const bool can60 = InterpolationController::supports60(fps);
    if (auto *model = qobject_cast<QStandardItemModel *>(interpolationMode_->model())) {
        if (QStandardItem *item = model->item(2)) {
            item->setEnabled(can60);
            item->setToolTip(can60 ? QString()
                                   : QString("Only available for videos below 60 fps"));
        }
    }

    if (!can60 && interpolationMode_->currentIndex() == 2) {
        interpolationMode_->setCurrentIndex(0); // turns RIFE off via interpolationModeChanged
        showInterpolationError(fps > 0.0
                                   ? QString("This video is already %1 fps, so 60 fps mode was turned off.")
                                         .arg(formatFps(fps))
                                   : QString("This video does not report a frame rate, so 60 fps mode was turned off."));
    }
}

void MainWindow::interpolationDeactivated(const QString &reason)
{
    const QSignalBlocker blocker(interpolationMode_);
    interpolationMode_->setCurrentIndex(0);
    updateQualityBadge();
    showInterpolationError(reason);
}

void MainWindow::showInterpolationError(const QString &message)
{
    // Deferred so the dialog never runs inside mpv event processing.
    QTimer::singleShot(0, this, [this, message] {
        QMessageBox::warning(
            this,
            "Frame interpolation",
            QString("Frame interpolation is off. Playback continues normally.\n\n%1").arg(message));
    });
}

QString MainWindow::mpvStringProperty(const QByteArray &name) const
{
    if (!mpv_) {
        return {};
    }

    char *value = mpv_get_property_string(mpv_, name.constData());
    if (!value) {
        return {};
    }

    const QString result = QString::fromUtf8(value);
    mpv_free(value);
    return result;
}

bool MainWindow::mpvInt64Property(const QByteArray &name, qint64 &value) const
{
    if (!mpv_) {
        return false;
    }

    int64_t raw = 0;
    if (mpv_get_property(mpv_, name.constData(), MPV_FORMAT_INT64, &raw) < 0) {
        return false;
    }

    value = static_cast<qint64>(raw);
    return true;
}

bool MainWindow::mpvFlagProperty(const QByteArray &name, bool &value) const
{
    if (!mpv_) {
        return false;
    }

    int raw = 0;
    if (mpv_get_property(mpv_, name.constData(), MPV_FORMAT_FLAG, &raw) < 0) {
        return false;
    }

    value = raw != 0;
    return true;
}

void MainWindow::refreshTracks()
{
    if (!mpv_) {
        return;
    }

    const QSignalBlocker audioBlocker(audioTrack_);
    const QSignalBlocker subtitleBlocker(subtitleTrack_);

    audioTrack_->clear();
    subtitleTrack_->clear();
    subtitleTrack_->addItem("Off", QVariant::fromValue<qlonglong>(-1));

    qint64 trackCount = 0;
    mpvInt64Property("track-list/count", trackCount);

    int selectedAudioIndex = -1;
    int selectedSubtitleIndex = 0;
    int audioNumber = 0;
    int subtitleNumber = 0;

    for (qint64 i = 0; i < trackCount; ++i) {
        const QByteArray prefix = "track-list/" + QByteArray::number(i) + "/";
        const QString type = mpvStringProperty(prefix + "type");

        if (type != "audio" && type != "sub") {
            continue;
        }

        qint64 id = -1;
        if (!mpvInt64Property(prefix + "id", id)) {
            continue;
        }

        const QString title = mpvStringProperty(prefix + "title").trimmed();
        const QString language = mpvStringProperty(prefix + "lang").trimmed();
        bool selected = false;
        mpvFlagProperty(prefix + "selected", selected);

        QStringList details;
        if (!language.isEmpty()) {
            details << language;
        }
        if (!title.isEmpty() && title.compare(language, Qt::CaseInsensitive) != 0) {
            details << title;
        }

        if (type == "audio") {
            ++audioNumber;
            QString label = QString("Audio %1").arg(audioNumber);
            if (!details.isEmpty()) {
                label += QString(" — %1").arg(details.join(" · "));
            }

            audioTrack_->addItem(label, QVariant::fromValue<qlonglong>(id));
            if (selected) {
                selectedAudioIndex = audioTrack_->count() - 1;
            }
        } else {
            ++subtitleNumber;
            QString label = QString("Subtitle %1").arg(subtitleNumber);
            if (!details.isEmpty()) {
                label += QString(" — %1").arg(details.join(" · "));
            }

            subtitleTrack_->addItem(label, QVariant::fromValue<qlonglong>(id));
            if (selected) {
                selectedSubtitleIndex = subtitleTrack_->count() - 1;
            }
        }
    }

    if (audioTrack_->count() == 0) {
        audioTrack_->addItem("No audio tracks");
        audioTrack_->setEnabled(false);
    } else {
        audioTrack_->setEnabled(true);
        audioTrack_->setCurrentIndex(selectedAudioIndex >= 0 ? selectedAudioIndex : 0);
    }

    subtitleTrack_->setCurrentIndex(selectedSubtitleIndex);
}

void MainWindow::audioTrackChanged(int index)
{
    if (index < 0 || !audioTrack_->isEnabled()) {
        return;
    }

    bool ok = false;
    const qlonglong id = audioTrack_->itemData(index).toLongLong(&ok);
    if (ok) {
        setMpvPropertyInt64("aid", id);
    }
}

void MainWindow::subtitleTrackChanged(int index)
{
    if (index < 0) {
        return;
    }

    bool ok = false;
    const qlonglong id = subtitleTrack_->itemData(index).toLongLong(&ok);
    if (!ok) {
        return;
    }

    if (id < 0) {
        mpv_set_property_string(mpv_, "sid", "no");
    } else {
        setMpvPropertyFlag("sub-visibility", true);
        setMpvPropertyInt64("sid", id);
    }

    QTimer::singleShot(0, this, &MainWindow::refreshTracks);
}

void MainWindow::loadSubtitle()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Load subtitle",
        {},
        "Subtitle files (*.srt *.ass *.ssa *.vtt *.sub *.idx *.sup);;All files (*.*)");

    if (path.isEmpty()) {
        return;
    }

    setMpvPropertyFlag("sub-visibility", true);
    command({"sub-add", path, "select"});

    QTimer::singleShot(250, this, &MainWindow::refreshTracks);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == seek_ && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && seek_->width() > 0) {
            const int value = QStyle::sliderValueFromPosition(
                seek_->minimum(),
                seek_->maximum(),
                qRound(mouseEvent->position().x()),
                seek_->width());

            seek_->setValue(value);
            seeking_ = true;
            seekReleased();
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress && isActiveWindow() && isPlayerVisible()) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        switch (keyEvent->key()) {
        case Qt::Key_Space:
            togglePause();
            return true;
        case Qt::Key_F:
            toggleFullscreen();
            return true;
        case Qt::Key_Escape:
            if (isFullScreen()) {
                toggleFullscreen();
                return true;
            }
            break;
        case Qt::Key_M:
            toggleMute();
            return true;
        case Qt::Key_Right:
            command({"seek", "5", "relative"});
            return true;
        case Qt::Key_Left:
            command({"seek", "-5", "relative"});
            return true;
        default:
            break;
        }
    }

    if (isPlayerVisible() && isFullScreen() && event->type() == QEvent::MouseMove) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && (widget == this || isAncestorOf(widget))) {
            showFullscreenControls();
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::toggleSettingsPanel()
{
    settingsVisible_ = !settingsVisible_;
    settingsPanel_->setVisible(settingsVisible_);
    settingsButton_->setProperty("active", settingsVisible_);
    settingsButton_->style()->unpolish(settingsButton_);
    settingsButton_->style()->polish(settingsButton_);

    controlsHeight_ = controls_->sizeHint().height();

    if (isFullScreen()) {
        showFullscreenControls();
    }
    layoutOverlayWidgets();
    raiseOverlayWidgets();
}

void MainWindow::showTrackPicker(QComboBox *combo)
{
    if (!combo) {
        return;
    }

    if (!settingsVisible_) {
        settingsVisible_ = true;
        settingsPanel_->setVisible(true);
        settingsButton_->setProperty("active", true);
        settingsButton_->style()->unpolish(settingsButton_);
        settingsButton_->style()->polish(settingsButton_);
        controlsHeight_ = controls_->sizeHint().height();
        layoutOverlayWidgets();
    }

    if (isFullScreen()) {
        showFullscreenControls();
    }

    combo->setFocus(Qt::MouseFocusReason);
    QTimer::singleShot(0, combo, &QComboBox::showPopup);
}

void MainWindow::updatePlaybackUi()
{
    const QString playGlyph = paused_ ? QStringLiteral("▶") : QStringLiteral("Ⅱ");
    playButton_->setText(playGlyph);
    railPlayerButton_->setText(playGlyph);
    centerPlayButton_->setText("▶");

    muteButton_->setText(muted_ ? "MUTE" : "VOL");
    muteButton_->setProperty("active", muted_);
    muteButton_->style()->unpolish(muteButton_);
    muteButton_->style()->polish(muteButton_);
}

void MainWindow::updateCenterState()
{
    if (!centerState_) {
        return;
    }

    if (!mediaLoaded_) {
        centerKicker_->setText("READY");
        centerText_->setText("Open or drop a video");
        centerState_->show();
    } else if (paused_) {
        centerKicker_->setText("PAUSED");
        centerText_->setText("Press play to continue");
        centerState_->show();
    } else {
        centerState_->hide();
    }

    if (isFullScreen() && !fullscreenControlsVisible_) {
        centerState_->hide();
    }

    layoutOverlayWidgets();
    raiseOverlayWidgets();
}

void MainWindow::updateChapterInfo()
{
    qint64 chapter = -1;
    qint64 chapterCount = 0;

    mpvInt64Property("chapter", chapter);
    mpvInt64Property("chapter-list/count", chapterCount);

    if (chapter < 0 || chapterCount <= 0) {
        chapterIndexLabel_->setText("--");
        chapterTitleLabel_->setText("No chapters");
        return;
    }

    chapterIndexLabel_->setText(QString("%1").arg(chapter + 1, 2, 10, QChar('0')));
    QString title = mpvStringProperty("chapter-list/" + QByteArray::number(chapter) + "/title").trimmed();
    if (title.isEmpty()) {
        title = QString("Chapter %1").arg(chapter + 1);
    }
    chapterTitleLabel_->setText(title);
}

void MainWindow::updateQualityBadge()
{
    if (!qualityPrimary_ || !qualitySecondary_) {
        return;
    }

    qint64 height = 0;
    QString quality = "VIDEO";

    if (mpvInt64Property("video-params/h", height) && height > 0) {
        if (height >= 2160) {
            quality = "4K";
        } else if (height >= 1440) {
            quality = "1440P";
        } else if (height >= 1080) {
            quality = "1080P";
        } else if (height >= 720) {
            quality = "720P";
        } else {
            quality = QString("%1P").arg(height);
        }
    }

    qualityPrimary_->setText(quality);

    const int mode = interpolationMode_ ? interpolationMode_->currentIndex() : 0;
    if (mode == 1) {
        qualitySecondary_->setText("RIFE DOUBLE");
    } else if (mode == 2) {
        qualitySecondary_->setText("RIFE 60");
    } else {
        qualitySecondary_->setText("ORIGINAL");
    }
}

void MainWindow::layoutOverlayWidgets()
{
    if (!root_ || !controls_) {
        return;
    }

    const int w = root_->width();
    const int h = root_->height();
    if (w <= 0 || h <= 0) {
        return;
    }

    const int margin = qBound(14, w / 45, 28);
    const int topHeight = 64;
    topBar_->setGeometry(margin, margin, qMax(320, w - margin * 2), topHeight);

    const int railWidth = 56;
    const int railHeight = sideRail_->sizeHint().height();
    const int railY = qMax(margin + topHeight + 14, (h - railHeight) / 2);
    sideRail_->setGeometry(margin, railY, railWidth, railHeight);

    qualityBadge_->adjustSize();
    const QSize badgeSize = qualityBadge_->sizeHint();
    qualityBadge_->setGeometry(
        qMax(margin, w - margin - badgeSize.width()),
        qMax(margin + topHeight + 14, (h - badgeSize.height()) / 2),
        badgeSize.width(),
        badgeSize.height());

    centerState_->adjustSize();
    const QSize centerSize = centerState_->sizeHint();
    centerState_->setGeometry(
        qMax(margin, (w - centerSize.width()) / 2),
        qMax(margin + topHeight, (h - centerSize.height()) / 2 - 12),
        centerSize.width(),
        centerSize.height());

    controlsHeight_ = qMax(controls_->sizeHint().height(), 118);

    if (isFullScreen()) {
        if (controlsSlide_->state() != QAbstractAnimation::Running) {
            controls_->setGeometry(fullscreenControlsVisible_
                                       ? fullscreenControlsShownRect()
                                       : fullscreenControlsHiddenRect());
        }
    } else {
        controls_->setGeometry(fullscreenControlsShownRect());
        setFullscreenChromeVisible(true);
        unsetCursor();
    }

    raiseOverlayWidgets();
}

void MainWindow::raiseOverlayWidgets()
{
    if (!root_) {
        return;
    }

    for (QWidget *overlay : {topBar_, sideRail_, qualityBadge_, centerState_, controls_}) {
        if (overlay && overlay->isVisible()) {
            overlay->raise();
        }
    }
}

void MainWindow::setFullscreenChromeVisible(bool visible)
{
    if (!isFullScreen()) {
        visible = true;
    }

    topBar_->setVisible(visible);
    sideRail_->setVisible(visible);
    qualityBadge_->setVisible(visible);

    // Do not call updateCenterState() here. layoutOverlayWidgets() calls this
    // helper, while updateCenterState() calls layoutOverlayWidgets(); calling
    // back into updateCenterState() would recurse until stack overflow during
    // application startup.
    if (!visible) {
        centerState_->hide();
    } else if (!mediaLoaded_ || paused_) {
        centerState_->show();
    } else {
        centerState_->hide();
    }
}

QRect MainWindow::fullscreenControlsShownRect() const
{
    const int margin = qBound(14, root_->width() / 45, 28);
    const int height = controlsHeight_ > 0 ? controlsHeight_ : controls_->sizeHint().height();
    return QRect(margin,
                 qMax(margin, root_->height() - margin - height),
                 qMax(320, root_->width() - margin * 2),
                 height);
}

QRect MainWindow::fullscreenControlsHiddenRect() const
{
    const QRect shown = fullscreenControlsShownRect();
    return QRect(shown.x(), root_->height() + 4, shown.width(), shown.height());
}

void MainWindow::enterFullscreenControlsMode()
{
    controlsHeight_ = qMax(controls_->sizeHint().height(), 118);
    fullscreenControlsVisible_ = false;
    controlsSlide_->stop();

    controls_->setGeometry(fullscreenControlsHiddenRect());
    controls_->hide();
    setFullscreenChromeVisible(false);
}

void MainWindow::leaveFullscreenControlsMode()
{
    fullscreenControlsTimer_->stop();
    controlsSlide_->stop();
    fullscreenControlsVisible_ = true;
    unsetCursor();

    controls_->show();
    topBar_->show();
    sideRail_->show();
    qualityBadge_->show();
    updateCenterState();
    layoutOverlayWidgets();
}

void MainWindow::showFullscreenControls()
{
    if (!isFullScreen()) {
        return;
    }

    fullscreenControlsTimer_->start();
    unsetCursor();
    setFullscreenChromeVisible(true);

    controlsHeight_ = qMax(controls_->sizeHint().height(), 118);
    const QRect shown = fullscreenControlsShownRect();

    if (fullscreenControlsVisible_ && controls_->isVisible()) {
        if (controlsSlide_->state() != QAbstractAnimation::Running) {
            controls_->setGeometry(shown);
        }
        raiseOverlayWidgets();
        return;
    }

    fullscreenControlsVisible_ = true;
    controlsSlide_->stop();

    const QRect start = controls_->isVisible() ? controls_->geometry()
                                               : fullscreenControlsHiddenRect();

    controls_->setGeometry(start);
    controls_->show();
    raiseOverlayWidgets();

    controlsSlide_->setDuration(500);
    controlsSlide_->setEasingCurve(QEasingCurve::OutCubic);
    controlsSlide_->setStartValue(start);
    controlsSlide_->setEndValue(shown);
    controlsSlide_->start();
}

void MainWindow::hideFullscreenControls()
{
    if (!isFullScreen() || !controls_->isVisible()) {
        return;
    }

    fullscreenControlsVisible_ = false;
    setFullscreenChromeVisible(false);
    controlsSlide_->stop();

    controlsSlide_->setDuration(450);
    controlsSlide_->setEasingCurve(QEasingCurve::InCubic);
    controlsSlide_->setStartValue(controls_->geometry());
    controlsSlide_->setEndValue(fullscreenControlsHiddenRect());
    controlsSlide_->start();
}

void MainWindow::showHome()
{
    if (!homePage_) {
        return;
    }

    if (isFullScreen()) {
        leaveFullscreenControlsMode();
        showNormal();
    }

    if (mediaLoaded_) {
        setMpvPropertyFlag("pause", true);
    }

    if (!currentPath_.isEmpty()) {
        homePage_->setCurrentMedia(QFileInfo(currentPath_).fileName(), mediaLoaded_);
    }

    transitionTo(homePage_);
    setWindowTitle("NovaPlayer");
}

void MainWindow::resumeFromHome()
{
    if (!mediaLoaded_ || currentPath_.isEmpty()) {
        openFile();
        return;
    }

    showPlayer(true);
}

void MainWindow::showPlayer(bool resumePlayback)
{
    transitionTo(root_);

    QTimer::singleShot(180, this, [this] {
        layoutOverlayWidgets();
        raiseOverlayWidgets();
    });

    if (resumePlayback && mediaLoaded_) {
        setMpvPropertyFlag("pause", false);
    }
}

void MainWindow::transitionTo(QWidget *target)
{
    if (!stack_ || !target || stack_->currentWidget() == target) {
        if (target == root_) {
            layoutOverlayWidgets();
            raiseOverlayWidgets();
        }
        return;
    }

    if (pageTransition_) {
        pageTransition_->stop();
        pageTransition_->deleteLater();
        pageTransition_ = nullptr;
    }

    transitionOverlay_->setGeometry(appRoot_->rect());
    transitionOverlay_->raise();
    transitionOverlay_->show();

    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(transitionOverlay_->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(transitionOverlay_);
        transitionOverlay_->setGraphicsEffect(effect);
    }
    effect->setOpacity(0.0);

    auto *group = new QSequentialAnimationGroup(this);
    auto *cover = new QPropertyAnimation(effect, "opacity", group);
    cover->setDuration(150);
    cover->setStartValue(0.0);
    cover->setEndValue(1.0);
    cover->setEasingCurve(QEasingCurve::InOutCubic);

    auto *reveal = new QPropertyAnimation(effect, "opacity", group);
    reveal->setDuration(220);
    reveal->setStartValue(1.0);
    reveal->setEndValue(0.0);
    reveal->setEasingCurve(QEasingCurve::OutCubic);

    connect(cover, &QPropertyAnimation::finished, this, [this, target] {
        stack_->setCurrentWidget(target);
        if (target == root_) {
            layoutOverlayWidgets();
            raiseOverlayWidgets();
        }
        transitionOverlay_->raise();
    });

    connect(group, &QSequentialAnimationGroup::finished, this, [this, group] {
        transitionOverlay_->hide();
        transitionOverlay_->setGraphicsEffect(nullptr);
        group->deleteLater();
        pageTransition_ = nullptr;
    });

    pageTransition_ = group;
    group->start();
}

bool MainWindow::isPlayerVisible() const
{
    return stack_ && stack_->currentWidget() == root_;
}

void MainWindow::command(const QStringList &args)
{
    if (!mpv_) {
        return;
    }

    std::vector<QByteArray>utf8;
    std::vector<const char *>values;

    for (const auto &arg : args) {
        utf8.push_back(arg.toUtf8());
    }
    for (auto &arg : utf8) {
        values.push_back(arg.constData());
    }
    values.push_back(nullptr);

    mpv_command_async(mpv_, 0, values.data());
}

void MainWindow::openFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open video",
        {},
        "Video files (*.mkv *.mp4 *.avi *.mov *.webm *.m4v *.ts *.mts *.wmv);;All files (*.*)");

    if (!path.isEmpty()) {
        openPath(path);
    }
}

void MainWindow::openPath(const QString &path)
{
    currentPath_ = path;
    mediaLoaded_ = false;

    const QString fileName = QFileInfo(path).fileName();
    if (homePage_) {
        homePage_->setCurrentMedia(fileName, true);
    }
    showPlayer(false);

    mediaEyebrow_->setText("LOADING");
    mediaTitle_->setText(fileName);
    centerKicker_->setText("LOADING");
    centerText_->setText(fileName);
    centerState_->show();

    command({"loadfile", path, "replace"});
    setWindowTitle(QString("NovaPlayer — %1").arg(fileName));
    raiseOverlayWidgets();
}

void MainWindow::togglePause()
{
    setMpvPropertyFlag("pause", !paused_);
}

void MainWindow::toggleMute()
{
    setMpvPropertyFlag("mute", !muted_);
}

void MainWindow::toggleFullscreen()
{
    if (!isPlayerVisible()) {
        return;
    }

    if (isFullScreen()) {
        leaveFullscreenControlsMode();
        showNormal();
        fullscreenButton_->setToolTip("Fullscreen");
        QTimer::singleShot(0, this, &MainWindow::layoutOverlayWidgets);
    } else {
        enterFullscreenControlsMode();
        showFullScreen();
        fullscreenButton_->setToolTip("Exit fullscreen");

        QTimer::singleShot(0, this, [this] {
            layoutOverlayWidgets();
            controls_->setGeometry(fullscreenControlsHiddenRect());
            showFullscreenControls();
        });
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    if (transitionOverlay_) {
        transitionOverlay_->setGeometry(appRoot_->rect());
        if (transitionOverlay_->isVisible()) {
            transitionOverlay_->raise();
        }
    }

    if (isPlayerVisible() && isFullScreen()
        && controlsSlide_->state() == QAbstractAnimation::Running) {
        controlsSlide_->stop();
    }

    if (root_) {
        layoutOverlayWidgets();
    }
}

void MainWindow::seekReleased()
{
    seeking_ = false;
    if (duration_ <= 0) {
        return;
    }

    const double target = duration_ * double(seek_->value()) / 1000.0;
    command({"seek", QString::number(target, 'f', 3), "absolute+exact"});
}

void MainWindow::volumeChanged(int value)
{
    setMpvPropertyDouble("volume", value);
}

void MainWindow::speedChanged(int index)
{
    static double speeds[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
    if (index >= 0 && index < 6) {
        setMpvPropertyDouble("speed", speeds[index]);
    }
}

void MainWindow::setMpvPropertyFlag(const char *name, bool value)
{
    if (!mpv_) {
        return;
    }

    int flag = value;
    mpv_set_property_async(mpv_, 0, name, MPV_FORMAT_FLAG, &flag);
}

void MainWindow::setMpvPropertyDouble(const char *name, double value)
{
    if (mpv_) {
        mpv_set_property_async(mpv_, 0, name, MPV_FORMAT_DOUBLE, &value);
    }
}

void MainWindow::setMpvPropertyInt64(const char *name, qint64 value)
{
    if (!mpv_) {
        return;
    }

    int64_t raw = static_cast<int64_t>(value);
    mpv_set_property_async(mpv_, 0, name, MPV_FORMAT_INT64, &raw);
}

void MainWindow::updateTimeLabel()
{
    const QString position = formatTime(position_);
    const QString duration = formatTime(duration_);

    timeLabel_->setText(QString("%1  /  %2").arg(position, duration));
    timelinePositionLabel_->setText(position);
    timelineDurationLabel_->setText(duration);
}

QString MainWindow::formatTime(double seconds)
{
    if (seconds < 0) {
        seconds = 0;
    }

    const int total = int(seconds);
    const int hours = total / 3600;
    const int minutes = total % 3600 / 60;
    const int secs = total % 60;

    if (hours) {
        return QString("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    }

    return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile()) {
        openPath(urls.first().toLocalFile());
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        togglePause();
        break;
    case Qt::Key_F:
        toggleFullscreen();
        break;
    case Qt::Key_Escape:
        if (isFullScreen()) {
            toggleFullscreen();
        } else {
            QMainWindow::keyPressEvent(event);
        }
        break;
    case Qt::Key_M:
        toggleMute();
        break;
    case Qt::Key_Right:
        command({"seek", "5", "relative"});
        break;
    case Qt::Key_Left:
        command({"seek", "-5", "relative"});
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}
