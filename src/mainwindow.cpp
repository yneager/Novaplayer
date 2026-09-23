#include "mainwindow.h"
#include "interpolationcontroller.h"

#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

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
    root_ = new QWidget(this);
    mainLayout_ = new QVBoxLayout(root_);
    mainLayout_->setContentsMargins(0, 0, 0, 0);
    mainLayout_->setSpacing(0);

    video_ = new QWidget(root_);
    video_->setAttribute(Qt::WA_NativeWindow);
    video_->setAttribute(Qt::WA_DontCreateNativeAncestors);
    video_->setStyleSheet("background:black;");
    video_->setMinimumSize(640, 360);
    video_->setMouseTracking(true);
    mainLayout_->addWidget(video_, 1);

    controls_ = new QWidget(root_);
    auto *controlsLayout = new QVBoxLayout(controls_);
    controlsLayout->setContentsMargins(10, 6, 10, 10);
    controlsLayout->setSpacing(6);

    seek_ = new QSlider(Qt::Horizontal, controls_);
    seek_->setRange(0, 1000);
    connect(seek_, &QSlider::sliderPressed, this, [this] { seeking_ = true; });
    connect(seek_, &QSlider::sliderReleased, this, &MainWindow::seekReleased);
    controlsLayout->addWidget(seek_);

    auto *trackRow = new QHBoxLayout;
    trackRow->setContentsMargins(0, 0, 0, 0);

    audioTrack_ = new QComboBox(controls_);
    audioTrack_->setMinimumWidth(190);
    audioTrack_->addItem("No audio tracks");
    audioTrack_->setEnabled(false);

    subtitleTrack_ = new QComboBox(controls_);
    subtitleTrack_->setMinimumWidth(190);
    subtitleTrack_->addItem("Subtitles Off", QVariant::fromValue<qlonglong>(-1));

    loadSubtitleButton_ = new QPushButton("Load Subtitle", controls_);

    // Frame interpolation: Off (default, no RIFE overhead) or RIFE 2x via
    // mpv's vapoursynth filter. See InterpolationController.
    interpolationMode_ = new QComboBox(controls_);
    interpolationMode_->addItem("Off");
    interpolationMode_->addItem(QString::fromUtf8("RIFE 2×"));
    interpolationMode_->setCurrentIndex(0);
    interpolationMode_->setToolTip("Frame interpolation (RIFE v4.6 via VapourSynth, Vulkan GPU)");

    trackRow->addWidget(new QLabel("Audio", controls_));
    trackRow->addWidget(audioTrack_);
    trackRow->addSpacing(8);
    trackRow->addWidget(new QLabel("Subtitles", controls_));
    trackRow->addWidget(subtitleTrack_);
    trackRow->addWidget(loadSubtitleButton_);
    trackRow->addSpacing(8);
    trackRow->addWidget(new QLabel("Frame Interpolation", controls_));
    trackRow->addWidget(interpolationMode_);
    trackRow->addStretch();

    controlsLayout->addLayout(trackRow);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);

    auto *openButton = new QPushButton("Open", controls_);
    playButton_ = new QPushButton("Play", controls_);
    muteButton_ = new QPushButton("Mute", controls_);
    fullscreenButton_ = new QPushButton("Fullscreen", controls_);
    timeLabel_ = new QLabel("00:00 / 00:00", controls_);

    volume_ = new QSlider(Qt::Horizontal, controls_);
    volume_->setRange(0, 100);
    volume_->setValue(80);
    volume_->setMaximumWidth(140);

    speed_ = new QComboBox(controls_);
    speed_->addItems({"0.50x", "0.75x", "1.00x", "1.25x", "1.50x", "2.00x"});
    speed_->setCurrentIndex(2);

    buttonRow->addWidget(openButton);
    buttonRow->addWidget(playButton_);
    buttonRow->addWidget(timeLabel_);
    buttonRow->addStretch();
    buttonRow->addWidget(new QLabel("Speed", controls_));
    buttonRow->addWidget(speed_);
    buttonRow->addWidget(muteButton_);
    buttonRow->addWidget(volume_);
    buttonRow->addWidget(fullscreenButton_);

    controlsLayout->addLayout(buttonRow);
    mainLayout_->addWidget(controls_);

    setCentralWidget(root_);

    connect(openButton, &QPushButton::clicked, this, &MainWindow::openFile);
    connect(loadSubtitleButton_, &QPushButton::clicked, this, &MainWindow::loadSubtitle);
    connect(playButton_, &QPushButton::clicked, this, &MainWindow::togglePause);
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
    controls_->setMouseTracking(true);
    for (QWidget *child : root_->findChildren<QWidget *>()) {
        child->setMouseTracking(true);
    }
}

void MainWindow::initMpv()
{
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

    // Error-level log messages are needed to notice when mpv disables the
    // RIFE VapourSynth filter after a script/runtime failure.
    mpv_request_log_messages(mpv_, "error");

    interpolation_ = new InterpolationController(mpv_, this);
    connect(interpolation_, &InterpolationController::deactivated,
            this, &MainWindow::interpolationDeactivated);

    mpv_set_wakeup_callback(mpv_, &MainWindow::wakeup, this);

    setMpvPropertyDouble("volume", 80);
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
            playButton_->setText(paused_ ? "Play" : "Pause");
        } else if (name == "mute" && property->format == MPV_FORMAT_FLAG) {
            muted_ = *static_cast<int *>(property->data) != 0;
            muteButton_->setText(muted_ ? "Unmute" : "Mute");
        }
    } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
        paused_ = false;
        playButton_->setText("Pause");
        QTimer::singleShot(0, this, &MainWindow::refreshTracks);
    } else if (event->event_id == MPV_EVENT_END_FILE) {
        playButton_->setText("Play");
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

    const auto mode = index == 1 ? InterpolationController::Mode::Rife2x
                                 : InterpolationController::Mode::Off;

    QString error;
    if (!interpolation_->setMode(mode, &error)) {
        const QSignalBlocker blocker(interpolationMode_);
        interpolationMode_->setCurrentIndex(0);
        showInterpolationError(error);
    }
}

void MainWindow::interpolationDeactivated(const QString &reason)
{
    const QSignalBlocker blocker(interpolationMode_);
    interpolationMode_->setCurrentIndex(0);
    showInterpolationError(reason);
}

void MainWindow::showInterpolationError(const QString &message)
{
    // Deferred so the dialog never runs inside mpv event processing.
    QTimer::singleShot(0, this, [this, message] {
        QMessageBox::warning(
            this,
            "Frame interpolation",
            QString::fromUtf8("RIFE 2× could not be enabled. Playback continues without interpolation.\n\n%1")
                .arg(message));
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

    if (event->type() == QEvent::KeyPress && isActiveWindow()) {
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

    if (isFullScreen() && event->type() == QEvent::MouseMove) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && (widget == this || isAncestorOf(widget))) {
            showFullscreenControls();
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

QRect MainWindow::fullscreenControlsShownRect() const
{
    const int height = controlsHeight_ > 0 ? controlsHeight_ : controls_->sizeHint().height();
    return QRect(0, qMax(0, root_->height() - height), root_->width(), height);
}

QRect MainWindow::fullscreenControlsHiddenRect() const
{
    const int height = controlsHeight_ > 0 ? controlsHeight_ : controls_->sizeHint().height();
    return QRect(0, root_->height(), root_->width(), height);
}

void MainWindow::enterFullscreenControlsMode()
{
    controlsHeight_ = qMax(controls_->height(), controls_->sizeHint().height());

    mainLayout_->removeWidget(controls_);
    controls_->setParent(root_);

    controls_->setAttribute(Qt::WA_NativeWindow);
    controls_->winId();

    fullscreenControlsVisible_ = false;
    controls_->setGeometry(fullscreenControlsHiddenRect());
    controls_->hide();
}

void MainWindow::leaveFullscreenControlsMode()
{
    fullscreenControlsTimer_->stop();
    controlsSlide_->stop();
    fullscreenControlsVisible_ = true;
    unsetCursor();

    controls_->hide();
    controls_->setParent(root_);
    mainLayout_->addWidget(controls_);
    controls_->show();
}

void MainWindow::showFullscreenControls()
{
    if (!isFullScreen()) {
        return;
    }

    fullscreenControlsTimer_->start();
    unsetCursor();

    const QRect shown = fullscreenControlsShownRect();

    if (fullscreenControlsVisible_ && controls_->isVisible()) {
        controls_->setGeometry(shown);
        controls_->raise();
        return;
    }

    fullscreenControlsVisible_ = true;
    controlsSlide_->stop();

    QRect start = controls_->isVisible() ? controls_->geometry()
                                         : fullscreenControlsHiddenRect();

    controls_->setGeometry(start);
    controls_->show();
    controls_->raise();

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
    controlsSlide_->stop();

    controlsSlide_->setDuration(450);
    controlsSlide_->setEasingCurve(QEasingCurve::InCubic);
    controlsSlide_->setStartValue(controls_->geometry());
    controlsSlide_->setEndValue(fullscreenControlsHiddenRect());
    controlsSlide_->start();
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
    command({"loadfile", path, "replace"});
    setWindowTitle(QString("NovaPlayer — %1").arg(QFileInfo(path).fileName()));
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
    if (isFullScreen()) {
        leaveFullscreenControlsMode();
        showNormal();
        fullscreenButton_->setText("Fullscreen");
    } else {
        enterFullscreenControlsMode();
        showFullScreen();
        fullscreenButton_->setText("Window");

        QTimer::singleShot(0, this, [this] {
            controls_->setGeometry(fullscreenControlsHiddenRect());
            showFullscreenControls();
        });
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    if (!isFullScreen() || controls_->parentWidget() != root_) {
        return;
    }

    if (controlsSlide_->state() == QAbstractAnimation::Running) {
        controlsSlide_->stop();
    }

    controls_->setGeometry(fullscreenControlsVisible_
                               ? fullscreenControlsShownRect()
                               : fullscreenControlsHiddenRect());

    if (fullscreenControlsVisible_) {
        controls_->show();
        controls_->raise();
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
    timeLabel_->setText(QString("%1 / %2")
                            .arg(formatTime(position_), formatTime(duration_)));
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
