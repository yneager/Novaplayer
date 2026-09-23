#include "playerchrome.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QShowEvent>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <functional>

namespace {

class LambdaBridge final : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    void readyRequested();
    void actionRequested(const QString &action);
    void seekRequested(double ratio);
    void volumeRequested(int value);
    void speedRequested(double value);
    void audioTrackRequested(int index);
    void subtitleTrackRequested(int index);
    void interpolationRequested(int index);
    void videoRectRequested(int x, int y, int width, int height, int radius);

public slots:
    void ready() { emit readyRequested(); }
    void action(const QString &value) { emit actionRequested(value); }
    void seek(double ratio) { emit seekRequested(ratio); }
    void volume(int value) { emit volumeRequested(value); }
    void speed(double value) { emit speedRequested(value); }
    void selectAudio(int index) { emit audioTrackRequested(index); }
    void selectSubtitle(int index) { emit subtitleTrackRequested(index); }
    void selectInterpolation(int index) { emit interpolationRequested(index); }
    void reportVideoRect(double x, double y, double width, double height, double radius)
    {
        emit videoRectRequested(qRound(x), qRound(y), qRound(width), qRound(height), qRound(radius));
    }
};

class LambdaWebView final : public QWebEngineView
{
public:
    using QWebEngineView::QWebEngineView;
    std::function<void(const QString &)> localFileDropped;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        const auto urls = event->mimeData()->urls();
        if (!urls.isEmpty() && urls.first().isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
        QWebEngineView::dragEnterEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        const auto urls = event->mimeData()->urls();
        if (!urls.isEmpty() && urls.first().isLocalFile() && localFileDropped) {
            localFileDropped(urls.first().toLocalFile());
            event->acceptProposedAction();
            return;
        }
        QWebEngineView::dropEvent(event);
    }
};

QJsonArray optionArray(const QStringList &labels, const QList<bool> *enabled = nullptr)
{
    QJsonArray result;
    for (int i = 0; i < labels.size(); ++i) {
        QJsonObject item;
        item.insert("label", labels[i]);
        item.insert("enabled", !enabled || i >= enabled->size() || enabled->at(i));
        result.append(item);
    }
    return result;
}

} // namespace

PlayerChrome::PlayerChrome(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("playerChrome");
    setAttribute(Qt::WA_TranslucentBackground);
    setAcceptDrops(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *view = new LambdaWebView(this);
    view_ = view;
    view_->setContextMenuPolicy(Qt::NoContextMenu);
    view_->setAcceptDrops(true);
    view_->setStyleSheet("background:transparent;border:0;");
    view_->setAttribute(Qt::WA_TranslucentBackground);
    view_->page()->setBackgroundColor(Qt::transparent);
    view_->settings()->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    view_->settings()->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);

    auto *bridge = new LambdaBridge(this);
    auto *channel = new QWebChannel(view_->page());
    channel->registerObject("lambdaBridge", bridge);
    view_->page()->setWebChannel(channel);

    connect(bridge, &LambdaBridge::readyRequested, this, [this] {
        ready_ = true;
        pushState();
        pushSettings();
        emit ready();
    });
    connect(bridge, &LambdaBridge::actionRequested, this, [this](const QString &action) {
        if (action == "open") emit openRequested();
        else if (action == "home") emit homeRequested();
        else if (action == "play") emit togglePauseRequested();
        else if (action == "next") emit nextRequested();
        else if (action == "mute") emit muteRequested();
        else if (action == "fullscreen") emit fullscreenRequested();
        else if (action == "activity") emit activityRequested();
        else if (action == "load-subtitle") emit loadSubtitleRequested();
    });
    connect(bridge, &LambdaBridge::seekRequested, this, &PlayerChrome::seekRequested);
    connect(bridge, &LambdaBridge::volumeRequested, this, &PlayerChrome::volumeRequested);
    connect(bridge, &LambdaBridge::speedRequested, this, &PlayerChrome::speedRequested);
    connect(bridge, &LambdaBridge::audioTrackRequested, this, &PlayerChrome::audioTrackRequested);
    connect(bridge, &LambdaBridge::subtitleTrackRequested, this, &PlayerChrome::subtitleTrackRequested);
    connect(bridge, &LambdaBridge::interpolationRequested, this, &PlayerChrome::interpolationRequested);
    connect(bridge, &LambdaBridge::videoRectRequested, this, &PlayerChrome::videoRectChanged);

    view->localFileDropped = [this](const QString &path) {
        emit openPathRequested(path);
    };

    layout->addWidget(view_);
    view_->load(QUrl("qrc:/vui/player.html"));
}

void PlayerChrome::setMediaLoaded(bool loaded) { loaded_ = loaded; pushState(); }
void PlayerChrome::setMediaTitle(const QString &title, const QString &eyebrow) { title_ = title; eyebrow_ = eyebrow; pushState(); }
void PlayerChrome::setPlaybackState(bool paused, bool muted) { paused_ = paused; muted_ = muted; pushState(); }
void PlayerChrome::setTimeline(double position, double duration) { position_ = position; duration_ = duration; pushState(); }
void PlayerChrome::setVolume(int volume) { volume_ = qBound(0, volume, 100); pushState(); }
void PlayerChrome::setSpeed(double speed) { speed_ = speed; pushState(); }
void PlayerChrome::setQuality(const QString &primary, const QString &secondary) { qualityPrimary_ = primary; qualitySecondary_ = secondary; pushState(); }
void PlayerChrome::setChapter(const QString &index, const QString &title) { chapterIndex_ = index; chapterTitle_ = title; pushState(); }
void PlayerChrome::setChromeVisible(bool visible) { chromeVisible_ = visible; pushState(); }

void PlayerChrome::setSettings(const QStringList &audio, int audioIndex,
                               const QStringList &subtitles, int subtitleIndex,
                               const QStringList &interpolation, const QList<bool> &interpolationEnabled,
                               int interpolationIndex)
{
    audio_ = audio;
    subtitles_ = subtitles;
    interpolation_ = interpolation;
    interpolationEnabled_ = interpolationEnabled;
    audioIndex_ = audioIndex;
    subtitleIndex_ = subtitleIndex;
    interpolationIndex_ = interpolationIndex;
    pushSettings();
}

void PlayerChrome::setActive(bool active)
{
    if (view_ && view_->page()) {
        view_->page()->setLifecycleState(active
            ? QWebEnginePage::LifecycleState::Active
            : QWebEnginePage::LifecycleState::Frozen);
    }
}

void PlayerChrome::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setActive(true);
}

void PlayerChrome::hideEvent(QHideEvent *event)
{
    setActive(false);
    QWidget::hideEvent(event);
}

void PlayerChrome::pushState()
{
    if (!ready_ || !view_) return;
    QJsonObject state;
    state.insert("loaded", loaded_);
    state.insert("paused", paused_);
    state.insert("muted", muted_);
    state.insert("chromeVisible", chromeVisible_);
    state.insert("title", title_);
    state.insert("eyebrow", eyebrow_);
    state.insert("position", position_);
    state.insert("duration", duration_);
    state.insert("volume", volume_);
    state.insert("speed", speed_);
    state.insert("qualityPrimary", qualityPrimary_);
    state.insert("qualitySecondary", qualitySecondary_);
    state.insert("chapterIndex", chapterIndex_);
    state.insert("chapterTitle", chapterTitle_);
    const QString json = QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
    view_->page()->runJavaScript(QString("window.lambdaUi&&window.lambdaUi.setState(%1);").arg(json));
}

void PlayerChrome::pushSettings()
{
    if (!ready_ || !view_) return;
    QJsonObject settings;
    settings.insert("audio", optionArray(audio_));
    settings.insert("subtitles", optionArray(subtitles_));
    settings.insert("interpolation", optionArray(interpolation_, &interpolationEnabled_));
    settings.insert("audioIndex", audioIndex_);
    settings.insert("subtitleIndex", subtitleIndex_);
    settings.insert("interpolationIndex", interpolationIndex_);
    const QString json = QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact));
    view_->page()->runJavaScript(QString("window.lambdaUi&&window.lambdaUi.setSettings(%1);").arg(json));
}

#include "playerchrome.moc"
