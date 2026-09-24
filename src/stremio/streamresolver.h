#pragma once

// Turns an addon Stream into something libmpv can play.
//
// Sources: stremio-core types/resource/stream.rs (Stream::convert) and
// stremio-video src/withStreamingServer/{convertStream,createTorrent,
// buildProxyUrl}.js. Streams that need torrent/archive/NZB/FTP/YouTube
// handling go through the Stremio streaming server (the same server Stremio
// desktop and Stremio Service run, http://127.0.0.1:11470 by default),
// exactly as Stremio does; LAMBDA does not bundle a torrent engine. Direct
// URLs go to mpv like stremio-shell-ng's ShellVideo. Request proxyHeaders are
// applied with mpv's http-header-fields (Nuvio iOS MPVPlayerBridge.swift).

#include "stremio/resources.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>

#include <functional>
#include <optional>

class QNetworkAccessManager;

namespace stremio {

// Detects the Stremio streaming server the way stremio-core does
// (models/streaming_server.rs get_settings: GET {url}/settings).
class StreamingServer final : public QObject
{
    Q_OBJECT

public:
    static constexpr auto kDefaultUrl = "http://127.0.0.1:11470/";

    explicit StreamingServer(QNetworkAccessManager *network, QObject *parent = nullptr);

    QString url() const { return url_; }
    void setUrl(const QString &url);
    // Base URL reported by the server (settings.baseUrl), or url().
    QString baseUrl() const { return baseUrl_.isEmpty() ? url_ : baseUrl_; }
    bool isAvailable() const { return available_; }
    QDateTime lastChecked() const { return checked_; }

    // Result is cached for a few seconds unless `force`.
    void probe(QObject *context, std::function<void(bool available)> done, bool force = false);

signals:
    void availabilityChanged(bool available);

private:
    QNetworkAccessManager *network_;
    QString url_;
    QString baseUrl_;
    bool available_ = false;
    QDateTime checked_;
};

struct PlaybackSource
{
    enum class Kind {
        Play,            // hand `url` (+ headers) to mpv
        OpenExternally,  // external/playerFrame/YouTube page: system browser
        Unsupported,
    };
    Kind kind = Kind::Unsupported;
    QString url;
    QList<QPair<QString, QString>> httpHeaders; // mpv http-header-fields
    QString error;
    bool viaStreamingServer = false;
    // Torrent context for the subtitle filename (stremio-video fetchFilename).
    QString infoHash;
    std::optional<int> fileIdx;
};

struct ResolveContext
{
    std::optional<SeriesInfo> seriesInfo; // for guessFileIdx of season packs
};

class StreamResolver final : public QObject
{
    Q_OBJECT

public:
    StreamResolver(QNetworkAccessManager *network, StreamingServer *server, QObject *parent = nullptr);

    void resolve(const Stream &stream, const ResolveContext &context, QObject *receiver,
                 std::function<void(const PlaybackSource &)> done);

    // Whether a stream needs the streaming server to play (for UI badges).
    static bool needsStreamingServer(const Stream &stream);
    // Whether LAMBDA can play the stream inside the player at all.
    static bool playsInPlayer(const Stream &stream);

    // Pieces exposed for tests.
    static QString formUrlEncode(const QString &value);
    static std::optional<QString> magnetInfoHash(const QString &magnet, QStringList *trackers = nullptr);
    static QString archiveCreateUrl(const QString &serverUrl, const Stream &stream, QString *error = nullptr);
    static QString ftpUrl(const QString &serverUrl, const QString &url, QString *error = nullptr);
    static QString torrentUrl(const QString &serverUrl, const QString &infoHash, int fileIdx, const QStringList &sources);
    static QJsonObject createTorrentBody(const QString &infoHash, std::optional<int> fileIdx,
                                         const QStringList &sources, const std::optional<SeriesInfo> &seriesInfo);
    static QList<QPair<QString, QString>> sanitizedRequestHeaders(const QList<QPair<QString, QString>> &headers);
    // "Key: value" entries for mpv's http-header-fields (set as a list node).
    static QStringList mpvHeaderFields(const QList<QPair<QString, QString>> &headers);

private:
    void createTorrent(const QString &infoHash, std::optional<int> fileIdx, const QStringList &sources,
                       const ResolveContext &context, QObject *receiver,
                       std::function<void(const PlaybackSource &)> done);

    QNetworkAccessManager *network_;
    StreamingServer *server_;
};

} // namespace stremio
