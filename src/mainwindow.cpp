#include "mainwindow.h"

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
#include <QMimeData>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QTimer>
#include <QUrl>
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
    connect(playButton_, &QPushButton::clicked, this, &MainWindow::togglePause);
    connect(muteButton_, &QPushButton::clicked, this, &MainWindow::toggleMute);
    connect(fullscreenButton_, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);
    connect(volume_, &QSlider::valueChanged, this, &MainWindow::volumeChanged);
    connect(speed_, &QComboBox::currentIndexChanged, this, &MainWindow::speedChanged);

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

    if (mpv_initialize(mpv_) < 0) {
        throw std::runtime_error("Could not initialize libmpv.");
    }

    mpv_observe_property(mpv_, 1, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 2, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 3, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 4, "mute", MPV_FORMAT_FLAG);
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
    } else if (event->event_id == MPV_EVENT_END_FILE) {
        playButton_->setText("Play");
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
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

    // Make the control panel its own native child window so it can reliably
    // stay above libmpv's native video HWND on Windows.
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
    command({"seek", QString::number(target, 'f', 3), "absolute", "exact"});
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
