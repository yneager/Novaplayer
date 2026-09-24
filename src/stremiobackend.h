#pragma once

// Owns the Stremio addon client layer for the application: installed addons,
// the network client and caches, the streaming-server connection, stream
// resolution and subtitle context. Shared by the Home page bridge and the
// player (MainWindow).

#include "stremio/addonclient.h"
#include "stremio/addonmanager.h"
#include "stremio/contentservice.h"
#include "stremio/serverprocess.h"
#include "stremio/streamresolver.h"
#include "stremio/videoparams.h"

#include <QObject>
#include <QStringList>

#include <optional>

// Everything the player needs to open an addon stream and to ask subtitle
// addons for it afterwards.
struct AddonPlayback
{
    stremio::Stream stream;
    stremio::PlaybackSource source;
    QString streamTransportUrl; // addon that provided the stream
    QString metaTransportUrl;   // addon that provided the meta
    QString type;               // meta type (stream/subtitles requests use it)
    QString metaId;
    QString videoId;
    QString title;              // meta name
    QString episodeLabel;       // "S1 · E3 — Title" for series
    QString addonName;
    QString poster;
    QString background;
    std::optional<stremio::SeriesInfo> seriesInfo;
};

class StremioBackend final : public QObject
{
    Q_OBJECT

public:
    explicit StremioBackend(QObject *parent = nullptr);

    stremio::AddonClient *client() const { return client_; }
    stremio::AddonManager *addons() const { return addons_; }
    stremio::ContentService *content() const { return content_; }
    stremio::StreamingServer *streamingServer() const { return server_; }
    stremio::StreamingServerProcess *serverProcess() const { return process_; }
    stremio::StreamResolver *resolver() const { return resolver_; }
    stremio::VideoParamsFetcher *videoParams() const { return videoParams_; }

    // Empty = LAMBDA's built-in engine; otherwise an external Stremio server.
    void setStreamingServerUrl(const QString &url);

    // Built-in engine cache: the folder LAMBDA owns inside the chosen
    // location, its size limit, and clearing it.
    QString cacheLocation() const;          // chosen parent folder
    QString cacheDirectory() const;         // <location>/LAMBDA stream cache
    void setCacheLocation(const QString &location); // empty = default
    bool hasCustomCacheLocation() const;
    qint64 cacheSizeLimit() const;          // bytes
    void setCacheSizeLimit(qint64 bytes);
    qint64 cacheUsage() const;              // bytes on disk
    // Stops the engine and deletes the cache folder's contents.
    bool clearCache(QString *error = nullptr);

    // Official Stremio addons offered from the empty state (the same
    // descriptors Stremio installs by default, stremio-official-addons).
    static QStringList defaultAddonUrls();

signals:
    // A stream was resolved and should be played by the existing player.
    void playRequested(const AddonPlayback &playback);

private:
    void applyServerOptions();
    void pushCacheSize();

    QString dataDir_;
    stremio::StreamingServerProcess *process_ = nullptr;
    stremio::AddonClient *client_ = nullptr;
    stremio::AddonManager *addons_ = nullptr;
    stremio::ContentService *content_ = nullptr;
    stremio::StreamingServer *server_ = nullptr;
    stremio::StreamResolver *resolver_ = nullptr;
    stremio::VideoParamsFetcher *videoParams_ = nullptr;
};
