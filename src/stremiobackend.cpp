#include "stremiobackend.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {

QSettings appSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "LAMBDA", "LAMBDA Player");
}

} // namespace

StremioBackend::StremioBackend(QObject *parent)
    : QObject(parent)
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDir);

    client_ = new stremio::AddonClient(this);
    client_->setDiskCacheDirectory(QDir(dataDir).filePath(QStringLiteral("addon-cache")));

    addons_ = new stremio::AddonManager(client_, QDir(dataDir).filePath(QStringLiteral("addons.json")), this);
    addons_->load();

    content_ = new stremio::ContentService(addons_, client_, this);

    server_ = new stremio::StreamingServer(client_->networkAccessManager(), this);
    server_->setUrl(appSettings().value(QStringLiteral("stremio/streamingServerUrl"),
                                        QString::fromLatin1(stremio::StreamingServer::kDefaultUrl)).toString());

    resolver_ = new stremio::StreamResolver(client_->networkAccessManager(), server_, this);
    videoParams_ = new stremio::VideoParamsFetcher(client_->networkAccessManager(), server_, this);

    // Keep installed manifests current (new catalogs, changed configuration),
    // without blocking startup. A failed refresh keeps the stored manifest.
    QTimer::singleShot(4000, addons_, &stremio::AddonManager::refreshAll);
}

void StremioBackend::setStreamingServerUrl(const QString &url)
{
    server_->setUrl(url);
    appSettings().setValue(QStringLiteral("stremio/streamingServerUrl"), server_->url());
}

QStringList StremioBackend::defaultAddonUrls()
{
    // Stremio/stremio-official-addons index.json: Cinemeta (catalogs and
    // metadata for IMDb ids) and OpenSubtitles v3 (subtitles).
    return {
        QStringLiteral("https://v3-cinemeta.strem.io/manifest.json"),
        QStringLiteral("https://opensubtitles-v3.strem.io/manifest.json"),
    };
}
