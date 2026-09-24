#pragma once

// The per-file data subtitle addons are queried with (videoHash, videoSize,
// filename). Port of stremio-video src/withStreamingServer/fetchVideoParams.js
// with stremio-core models/player.rs VideoParams.
//
// Stremio asks its streaming server for the OpenSubtitles hash
// (/opensubHash?videoUrl=). When that server is not running LAMBDA computes
// the same hash itself from two HTTP range requests (first and last 64 KiB,
// the OpenSubtitles "HashSourceCodes" algorithm referenced by the SDK's
// defineSubtitlesHandler docs), so subtitle addons still get an exact match
// (COMPATIBILITY.md C-013).

#include "stremio/resources.h"
#include "stremio/streamresolver.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QNetworkAccessManager;

namespace stremio {

struct VideoParams
{
    std::optional<QString> hash;
    std::optional<qint64> size;
    std::optional<QString> filename;

    bool isEmpty() const { return !hash && !size && !filename; }
};

// OpenSubtitles hash: size + sum of the little-endian 64-bit words of the
// first and last 64 KiB, as 16 lower-case hex digits.
QString openSubtitlesHash(qint64 fileSize, const QByteArray &head, const QByteArray &tail);

// filename fallback of fetchVideoParams.js: decodeURIComponent of the last
// path segment of the media URL.
QString filenameFromUrl(const QString &url);

class VideoParamsFetcher final : public QObject
{
    Q_OBJECT

public:
    VideoParamsFetcher(QNetworkAccessManager *network, StreamingServer *server, QObject *parent = nullptr);

    void fetch(const Stream &stream, const PlaybackSource &source, QObject *receiver,
               std::function<void(const VideoParams &)> done);

private:
    void fetchHashFromServer(const QString &mediaUrl, QObject *receiver,
                             std::function<void(std::optional<QString>, std::optional<qint64>)> done);
    void computeHash(const QString &mediaUrl, const QList<QPair<QString, QString>> &headers, QObject *receiver,
                     std::function<void(std::optional<QString>, std::optional<qint64>)> done);
    void fetchTorrentFilename(const PlaybackSource &source, QObject *receiver,
                              std::function<void(std::optional<QString>)> done);

    QNetworkAccessManager *network_;
    StreamingServer *server_;
};

} // namespace stremio
