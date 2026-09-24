#include "addonconfigurewindow.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <functional>

namespace {

class ConfigurePage final : public QWebEnginePage
{
public:
    ConfigurePage(QWebEngineProfile *profile, QObject *parent)
        : QWebEnginePage(profile, parent)
    {
    }

    std::function<void(const QUrl &)> onInstallLink;

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override
    {
        Q_UNUSED(type);
        Q_UNUSED(isMainFrame);
        if (AddonConfigureWindow::isInstallLink(url)) {
            if (onInstallLink) {
                onInstallLink(url);
            }
            return false;
        }
        const QString scheme = url.scheme().toLower();
        // Only web pages are shown; other schemes go to the system.
        if (scheme != QLatin1String("http") && scheme != QLatin1String("https")
            && scheme != QLatin1String("about") && scheme != QLatin1String("data") && scheme != QLatin1String("blob")) {
            QDesktopServices::openUrl(url);
            return false;
        }
        return true;
    }

    QWebEnginePage *createWindow(WebWindowType type) override
    {
        Q_UNUSED(type);
        // target=_blank links (often the Install button) stay in this window.
        return this;
    }
};

} // namespace

bool AddonConfigureWindow::isInstallLink(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("stremio")) {
        return true;
    }
    return (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
        && url.path().endsWith(QLatin1String("/manifest.json"));
}

AddonConfigureWindow::AddonConfigureWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(QStringLiteral("Configure addon — LAMBDA Player"));
    resize(980, 760);
    setStyleSheet(QStringLiteral(
        "AddonConfigureWindow{background:#0b0e14;}"
        "QLabel{color:rgba(235,239,248,.7);font-size:9pt;}"
        "QLineEdit{background:#11151d;color:#f5f7fb;border:1px solid rgba(255,255,255,.12);border-radius:8px;padding:6px 9px;}"
        "QPushButton{background:rgba(255,255,255,.06);color:#f5f7fb;border:1px solid rgba(255,255,255,.12);border-radius:8px;padding:6px 12px;}"
        "QPushButton:hover{background:rgba(255,255,255,.1);}"
        "QPushButton#primary{background:#79eddd;color:#07100f;border-color:#b6fff6;}"));

    profile_ = new QWebEngineProfile(this); // off the record, isolated from LAMBDA's pages
    view_ = new QWebEngineView(this);
    auto *page = new ConfigurePage(profile_, view_);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    page->onInstallLink = [this](const QUrl &url) {
        QString manifest = url.toString(QUrl::FullyEncoded);
        if (url.scheme().compare(QLatin1String("stremio"), Qt::CaseInsensitive) == 0) {
            manifest = QStringLiteral("https://") + manifest.mid(qsizetype(qstrlen("stremio://")));
        }
        emit installRequested(manifest);
        hint_->setText(QStringLiteral("Installing the configured addon…"));
    };
    view_->setPage(page);

    address_ = new QLineEdit(this);
    address_->setReadOnly(true);
    auto *openInBrowser = new QPushButton(QStringLiteral("Open in browser"), this);
    auto *paste = new QLineEdit(this);
    paste->setPlaceholderText(QStringLiteral("…or paste the configured manifest URL here"));
    auto *install = new QPushButton(QStringLiteral("Install URL"), this);
    install->setObjectName(QStringLiteral("primary"));
    hint_ = new QLabel(QStringLiteral("Configure the addon, then press its Install button. LAMBDA installs the configured URL."), this);
    hint_->setWordWrap(true);

    auto *top = new QHBoxLayout;
    top->addWidget(address_, 1);
    top->addWidget(openInBrowser);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(paste, 1);
    bottom->addWidget(install);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    layout->addLayout(top);
    layout->addWidget(view_, 1);
    layout->addWidget(hint_);
    layout->addLayout(bottom);

    connect(view_, &QWebEngineView::urlChanged, this, [this](const QUrl &url) {
        address_->setText(url.toString());
    });
    connect(openInBrowser, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(view_->url());
    });
    connect(install, &QPushButton::clicked, this, [this, paste] {
        const QString text = paste->text().trimmed();
        if (!text.isEmpty()) {
            emit installRequested(text);
            hint_->setText(QStringLiteral("Installing the configured addon…"));
            paste->clear();
        }
    });
}

void AddonConfigureWindow::openUrl(const QString &url)
{
    hint_->setText(QStringLiteral("Configure the addon, then press its Install button. LAMBDA installs the configured URL."));
    view_->setUrl(QUrl(url, QUrl::TolerantMode));
    show();
    raise();
    activateWindow();
}
