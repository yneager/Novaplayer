#include "homepage.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QShowEvent>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <functional>

namespace {

class VuiPage final : public QWebEnginePage
{
public:
    explicit VuiPage(QObject *parent = nullptr)
        : QWebEnginePage(parent)
    {
    }

    std::function<bool(const QUrl &)> navigationHandler;

protected:
    bool acceptNavigationRequest(const QUrl &url,
                                 NavigationType type,
                                 bool isMainFrame) override
    {
        if (type == QWebEnginePage::NavigationTypeLinkClicked
            && navigationHandler && navigationHandler(url)) {
            return false;
        }

        if (isMainFrame && (url.scheme() == "http" || url.scheme() == "https")) {
            return false;
        }

        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }
};

class VuiView final : public QWebEngineView
{
public:
    explicit VuiView(QWidget *parent = nullptr)
        : QWebEngineView(parent)
    {
        setAcceptDrops(true);
    }

    std::function<void(const QString &)> localFileDropped;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasUrls()) {
            const auto urls = event->mimeData()->urls();
            if (!urls.isEmpty() && urls.first().isLocalFile()) {
                event->acceptProposedAction();
                return;
            }
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

} // namespace

HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("homePage");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *view = new VuiView(this);
    view_ = view;
    view_->setContextMenuPolicy(Qt::NoContextMenu);
    view_->setStyleSheet("background:#050609;border:0;");
    view_->settings()->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    view_->settings()->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    view_->settings()->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);

    auto *page = new VuiPage(view_);
    page->navigationHandler = [this](const QUrl &url) {
        if (url.scheme() == "lambda") {
            if (url.host() == "resume") {
                emit resumeRequested();
            } else {
                emit openVideoRequested();
            }
            return true;
        }

        if (url.path().endsWith("/player.html") || url.fileName() == "player.html") {
            emit openVideoRequested();
            return true;
        }

        return false;
    };
    view_->setPage(page);

    view->localFileDropped = [this](const QString &path) {
        emit openPathRequested(path);
    };

    connect(view_, &QWebEngineView::loadFinished, this, [this](bool ok) {
        pageLoaded_ = ok;
        if (ok) {
            updateSessionCard();
        }
    });

    layout->addWidget(view_);
    view_->load(QUrl("qrc:/vui/index.html"));
}

void HomePage::setCurrentMedia(const QString &displayName, bool available)
{
    currentMediaName_ = displayName;
    currentMediaAvailable_ = available;
    updateSessionCard();
}

void HomePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (view_ && view_->page()) {
        view_->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
    }
}

void HomePage::hideEvent(QHideEvent *event)
{
    if (view_ && view_->page()) {
        view_->page()->setLifecycleState(QWebEnginePage::LifecycleState::Frozen);
    }
    QWidget::hideEvent(event);
}

void HomePage::updateSessionCard()
{
    if (!view_ || !pageLoaded_) {
        return;
    }

    QJsonArray jsonName;
    jsonName.append(currentMediaName_);
    const QString encodedName =
        QString::fromUtf8(QJsonDocument(jsonName).toJson(QJsonDocument::Compact));

    const QString script = QString(R"JS(
(() => {
  const card = document.querySelector('.continue-rail .wide-card');
  if (!card) return;
  const badge = card.querySelector('.card-badge');
  const title = card.querySelector('.wide-info strong');
  const meta = card.querySelector('.wide-info div span');
  if (%1) {
    const mediaName = %2[0];
    card.href = 'lambda://resume';
    if (badge) badge.textContent = 'LOCAL';
    if (title) title.textContent = mediaName || 'Current video';
    if (meta) meta.textContent = 'Resume current session';
  } else {
    card.href = './player.html';
    if (badge) badge.textContent = 'S1 · E4';
    if (title) title.textContent = 'Neon Fields';
    if (meta) meta.textContent = '31 min left';
  }
})()
)JS")
        .arg(currentMediaAvailable_ ? "true" : "false", encodedName);

    view_->page()->runJavaScript(script);
}
