#pragma once

// Turns an addon Stream into something libmpv can play.
//
// Sources: stremio-core types/resource/stream.rs (Stream::convert) and
// stremio-video src/withStreamingServer/{convertStream,createTorrent,
// buildProxyUrl}.js. Streams that need torrent/archive/NZB/FTP handling go
// through a Stremio-compatible streaming server, exactly as Stremio does. By
// default that is LAMBDA's bundled engine (StreamingServerProcess: the
// open-source stremio-native/stream-server on 127.0.0.1); an external server
// such as Stremio Service can be configured instead. YouTube opens in the
// browser (Stream::download_url). Direct URLs go to mpv like
// stremio-shell-ng's ShellVideo. Request proxyHeaders are applied with mpv's
// http-header-fields (Nuvio iOS MPVPlayerBridge.swift).

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

class StreamingServerProcess;

// The streaming server used for torrents/archives: the bundled engine
// (default) or an external Stremio server. Detected the way stremio-core does
// (models/streaming_server.rs get_settings: GET {url}/settings).
class StreamingServer final : public QObject
{
    Q_OBJECT

public:
    // Stremio desktop / Stremio Service address, suggested for external use.
    static constexpr auto kDefaultUrl = "http://127.0.0.1:11470/";

    explicit StreamingServer(QNetworkAccessManager *network, QObject *parent = nullptr);

    // Bundled engine; used whenever no external URL is set.
    void setProcess(StreamingServerProcess *process);
    StreamingServerProcess *process() const { return process_; }
    bool usesBuiltIn() const { return externalUrl_.isEmpty() && process_; }
    QString externalUrl() const { return externalUrl_; }
    // Empty = built-in engine.
    void setExternalUrl(const QString &url);

    QString url() const { return url_; }
    // Base URL reported by the server (settings.baseUrl), or url().
    QString baseUrl() const { return baseUrl_.isEmpty() ? url_ : baseUrl_; }
    bool isAvailable() const { return available_; }
    QDateTime lastChecked() const { return checked_; }

    // Result is cached for a few seconds unless `force`.
    void probe(QObject *context, std::function<void(bool available)> done, bool force = false);
    // Makes the server usable: starts the built-in engine when needed, then
    // probes it. `error` explains a failure.
    void ensure(QObject *context, std::function<void(bool available, const QString &error)> done);

signals:
    void availabilityChanged(bool available);

private:
    void setAvailable(bool available);

    QNetworkAccessManager *network_;
    StreamingServerProcess *process_ = nullptr;
    QString externalUrl_;
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
    // Torrent context for subtitles (stremio-video fetchFilename/videoParams).
    QString infoHash;
    std::optional<int> fileIdx;
    QString filename;
    std::optional<qint64> fileSize;
};

struct ResolveContext
{
    std::optional<SeriesInfo> seriesInfo; // for guessFileIdx of season packs
    // Progress messages for the UI ("Starting the streaming engine…").
    std::function<void(const QString &)> progress;
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

    // YouTube video id of a youtube.com / youtu.be URL, if it is one.
    static QString youTubeId(const QString &url);
    // YouTube watch page (opened in the browser).
    static QString youTubeWatchUrl(const QString &videoId);
    // Whether a direct URL clearly names a media file or playlist, so no
    // content check is needed before handing it to mpv.
    static bool looksLikeMedia(const QString &url);

    // Pieces exposed for tests.
    static QString formUrlEncode(const QString &value);
    static std::optional<QString> magnetInfoHash(const QString &magnet, QStringList *trackers = nullptr);
    static QString archiveCreateUrl(const QString &serverUrl, const Stream &stream, QString *error = nullptr);
    static QString ftpUrl(const QString &serverUrl, const QString &url, QString *error = nullptr);
    static QString torrentUrl(const QString &serverUrl, const QString &infoHash, int fileIdx, const QStringList &sources);
    static QJsonObject createTorrentBody(const QString &infoHash, std::optional<int> fileIdx,
                                         const QStringList &sources, const std::optional<SeriesInfo> &seriesInfo);
    // Validates a /create answer and picks the file; empty error = success.
    static QString torrentCreateResult(const QJsonObject &response, std::optional<int> fileIdx,
                                       int *index, QString *filename, qint64 *size);
    static QList<QPair<QString, QString>> sanitizedRequestHeaders(const QList<QPair<QString, QString>> &headers);
    // "Key: value" entries for mpv's http-header-fields (set as a list node).
    static QStringList mpvHeaderFields(const QList<QPair<QString, QString>> &headers);

private:
    void resolveDirect(const Stream &stream, QObject *receiver, std::function<void(const PlaybackSource &)> done);
    void createTorrent(const QString &infoHash, std::optional<int> fileIdx, const QStringList &sources,
                       const ResolveContext &context, QObject *receiver,
                       std::function<void(const PlaybackSource &)> done);

    QNetworkAccessManager *network_;
    StreamingServer *server_;
};

} // namespace stremio
