#pragma once

// Owns the Stremio addon client layer for the application: installed addons,
// the network client and caches, the streaming-server connection, stream
// resolution and subtitle context. Shared by the Home page bridge and the
// player (MainWindow).

#include "stremio/addonclient.h"
#include "stremio/addonmanager.h"
#include "stremio/contentservice.h"
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
    stremio::StreamResolver *resolver() const { return resolver_; }
    stremio::VideoParamsFetcher *videoParams() const { return videoParams_; }

    void setStreamingServerUrl(const QString &url);

    // Official Stremio addons offered from the empty state (the same
    // descriptors Stremio installs by default, stremio-official-addons).
    static QStringList defaultAddonUrls();

signals:
    // A stream was resolved and should be played by the existing player.
    void playRequested(const AddonPlayback &playback);

private:
    stremio::AddonClient *client_ = nullptr;
    stremio::AddonManager *addons_ = nullptr;
    stremio::ContentService *content_ = nullptr;
    stremio::StreamingServer *server_ = nullptr;
    stremio::StreamResolver *resolver_ = nullptr;
    stremio::VideoParamsFetcher *videoParams_ = nullptr;
};
