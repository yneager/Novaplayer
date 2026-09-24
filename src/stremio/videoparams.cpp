#include "stremio/videoparams.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>
#include <QtEndian>

#include <memory>

namespace stremio {

namespace {

constexpr qint64 kChunk = 65536;
constexpr int kTimeoutMs = 15000;

QNetworkRequest jsonRequest(const QString &url)
{
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(kTimeoutMs);
    return request;
}

QString serverRoot(const QString &serverUrl)
{
    // url.resolve(streamingServerURL, '/…') resolves against the origin.
    const QUrl url(serverUrl);
    return url.scheme() + QStringLiteral("://") + url.authority();
}

} // namespace

QString openSubtitlesHash(qint64 fileSize, const QByteArray &head, const QByteArray &tail)
{
    quint64 hash = quint64(fileSize);
    auto add = [&hash](const QByteArray &chunk) {
        const qsizetype words = chunk.size() / 8;
        for (qsizetype i = 0; i < words; ++i) {
            hash += qFromLittleEndian<quint64>(chunk.constData() + i * 8);
        }
    };
    add(head.left(kChunk));
    add(tail.right(kChunk));
    return QStringLiteral("%1").arg(hash, 16, 16, QLatin1Char('0'));
}

QString filenameFromUrl(const QString &url)
{
    // decodeURIComponent(mediaURL.split('/').pop())
    const QString last = url.section(QLatin1Char('/'), -1);
    return QUrl::fromPercentEncoding(last.toUtf8());
}

VideoParamsFetcher::VideoParamsFetcher(QNetworkAccessManager *network, StreamingServer *server, QObject *parent)
    : QObject(parent)
    , network_(network)
    , server_(server)
{
}

void VideoParamsFetcher::fetch(const Stream &stream, const PlaybackSource &source, QObject *receiver,
                               std::function<void(const VideoParams &)> done)
{
    struct State
    {
        VideoParams params;
        int pending = 2;
    };
    auto state = std::make_shared<State>();
    auto finishOne = [state, done] {
        if (--state->pending == 0) {
            done(state->params);
        }
    };

    const StreamBehaviorHints &hints = stream.behaviorHints;
    const QString mediaUrl = source.url;

    // Hash and size.
    if (hints.videoHash && hints.videoSize) {
        state->params.hash = hints.videoHash;
        state->params.size = hints.videoSize;
        finishOne();
    } else {
        const std::optional<qint64> knownSize = hints.videoSize ? hints.videoSize : source.fileSize;
        auto combine = [state, hints, knownSize, finishOne](std::optional<QString> hash, std::optional<qint64> size) {
            state->params.hash = hints.videoHash ? hints.videoHash : hash;
            state->params.size = knownSize ? knownSize : size;
            finishOne();
        };
        QPointer<QObject> guard(receiver);
        server_->probe(receiver, [this, guard, mediaUrl, source, combine](bool available) {
            if (!guard) {
                return;
            }
            // Streams served by the streaming server are hashed by it
            // (/opensubHash, as Stremio does); other links locally, with
            // their own request headers, which a server could not send.
            if (available && source.viaStreamingServer) {
                fetchHashFromServer(mediaUrl, guard, combine);
            } else if (mediaUrl.startsWith(QLatin1String("http"), Qt::CaseInsensitive)) {
                computeHash(mediaUrl, source.httpHeaders, guard, combine);
            } else {
                combine(std::nullopt, std::nullopt);
            }
        });
    }

    // Filename.
    if (hints.filename) {
        state->params.filename = hints.filename;
        finishOne();
    } else if (!source.filename.isEmpty()) {
        state->params.filename = source.filename; // torrent file from /create
        finishOne();
    } else if (!source.infoHash.isEmpty()) {
        fetchTorrentFilename(source, receiver, [state, finishOne, mediaUrl](std::optional<QString> name) {
            state->params.filename = name;
            finishOne();
        });
    } else {
        const QString name = filenameFromUrl(mediaUrl);
        if (!name.isEmpty()) {
            state->params.filename = name;
        }
        finishOne();
    }
}

void VideoParamsFetcher::fetchHashFromServer(const QString &mediaUrl, QObject *receiver,
                                             std::function<void(std::optional<QString>, std::optional<qint64>)> done)
{
    const QString url = serverRoot(server_->baseUrl()) + QStringLiteral("/opensubHash?videoUrl=")
        + StreamResolver::formUrlEncode(mediaUrl);
    QNetworkReply *reply = network_->get(jsonRequest(url));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, receiver, [reply, done] {
        if (reply->error() != QNetworkReply::NoError) {
            done(std::nullopt, std::nullopt);
            return;
        }
        const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject result = response.value(QStringLiteral("result")).toObject();
        // {"error": null, "result": {...}} on success (stream-server subtitles.rs).
        const QJsonValue error = response.value(QStringLiteral("error"));
        if (!error.isUndefined() && !error.isNull()) {
            done(std::nullopt, std::nullopt);
            return;
        }
        std::optional<QString> hash;
        std::optional<qint64> size;
        if (result.value(QStringLiteral("hash")).isString()) {
            hash = result.value(QStringLiteral("hash")).toString();
        }
        if (result.value(QStringLiteral("size")).isDouble()) {
            size = qint64(result.value(QStringLiteral("size")).toDouble());
        }
        done(hash, size);
    });
}

void VideoParamsFetcher::computeHash(const QString &mediaUrl, const QList<QPair<QString, QString>> &headers,
                                     QObject *receiver,
                                     std::function<void(std::optional<QString>, std::optional<qint64>)> done)
{
    auto makeRequest = [mediaUrl, headers](qint64 from, qint64 to) {
        QNetworkRequest request{QUrl(mediaUrl, QUrl::TolerantMode)};
        request.setTransferTimeout(kTimeoutMs);
        for (const auto &header : headers) {
            request.setRawHeader(header.first.toUtf8(), header.second.toUtf8());
        }
        request.setRawHeader("Range", QStringLiteral("bytes=%1-%2").arg(from).arg(to).toLatin1());
        return request;
    };
    // Only ranged answers are read: a server that ignores Range would make us
    // download the whole video.
    auto abortUnlessPartial = [](QNetworkReply *reply) {
        connect(reply, &QNetworkReply::metaDataChanged, reply, [reply] {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            // Redirects (CDNs, debrid links) are followed; only a final
            // non-ranged answer is aborted.
            if (status != 206 && (status < 300 || status >= 400)) {
                reply->abort();
            }
        });
    };

    QNetworkReply *head = network_->get(makeRequest(0, kChunk - 1));
    abortUnlessPartial(head);
    connect(head, &QNetworkReply::finished, head, &QObject::deleteLater);
    connect(head, &QNetworkReply::finished, receiver, [this, head, receiver, makeRequest, abortUnlessPartial, done] {
        if (head->error() != QNetworkReply::NoError) {
            done(std::nullopt, std::nullopt);
            return;
        }
        const QByteArray headData = head->readAll();
        static const QRegularExpression totalRe(QStringLiteral("/(\\d+)\\s*$"));
        const QRegularExpressionMatch total = totalRe.match(QString::fromLatin1(head->rawHeader("Content-Range")));
        if (!total.hasMatch()) {
            done(std::nullopt, std::nullopt);
            return;
        }
        const qint64 size = total.captured(1).toLongLong();
        if (size <= kChunk) {
            done(openSubtitlesHash(size, headData, headData), size);
            return;
        }
        QNetworkReply *tail = network_->get(makeRequest(size - kChunk, size - 1));
        abortUnlessPartial(tail);
        connect(tail, &QNetworkReply::finished, tail, &QObject::deleteLater);
        connect(tail, &QNetworkReply::finished, receiver, [tail, headData, size, done] {
            const QByteArray tailData = tail->error() == QNetworkReply::NoError ? tail->readAll() : QByteArray();
            if (tailData.size() < kChunk || headData.size() < kChunk) {
                done(std::nullopt, size);
                return;
            }
            done(openSubtitlesHash(size, headData, tailData), size);
        });
    });
}

void VideoParamsFetcher::fetchTorrentFilename(const PlaybackSource &source, QObject *receiver,
                                              std::function<void(std::optional<QString>)> done)
{
    const QString root = serverRoot(server_->baseUrl());
    const bool specific = source.fileIdx && *source.fileIdx != -1;
    const QString url = specific
        ? root + QLatin1Char('/') + source.infoHash + QLatin1Char('/') + QString::number(*source.fileIdx) + QStringLiteral("/stats.json")
        : root + QLatin1Char('/') + source.infoHash + QStringLiteral("/stats.json");
    QNetworkReply *reply = network_->get(jsonRequest(url));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, receiver, [reply, specific, done] {
        if (reply->error() != QNetworkReply::NoError) {
            done(std::nullopt);
            return;
        }
        const QJsonObject stats = QJsonDocument::fromJson(reply->readAll()).object();
        if (specific) {
            const QJsonValue name = stats.value(QStringLiteral("streamName"));
            done(name.isString() ? std::optional<QString>(name.toString()) : std::nullopt);
            return;
        }
        const QJsonArray files = stats.value(QStringLiteral("files")).toArray();
        const QJsonValue guessed = stats.value(QStringLiteral("guessedFileIdx"));
        if (guessed.isDouble()) {
            const QJsonObject file = files.at(guessed.toInt()).toObject();
            if (file.value(QStringLiteral("name")).isString()) {
                done(file.value(QStringLiteral("name")).toString());
                return;
            }
        }
        // Largest video file (mirrors the server's GuessFileIdx for movies).
        static const QRegularExpression videoExt(QStringLiteral("\\.(mp4|mkv|avi|mov|wmv|flv|webm|m4v|mpg|mpeg|ts|m2ts)$"),
                                                 QRegularExpression::CaseInsensitiveOption);
        QJsonObject best;
        bool anyVideo = false;
        for (const QJsonValue &value : files) {
            const QJsonObject file = value.toObject();
            if (!file.value(QStringLiteral("name")).isString()) continue;
            const bool isVideo = videoExt.match(file.value(QStringLiteral("name")).toString()).hasMatch();
            if (isVideo && !anyVideo) {
                best = {};
                anyVideo = true;
            }
            if (anyVideo && !isVideo) continue;
            if (best.isEmpty() || file.value(QStringLiteral("length")).toDouble() > best.value(QStringLiteral("length")).toDouble()) {
                best = file;
            }
        }
        done(best.isEmpty() ? std::nullopt : std::optional<QString>(best.value(QStringLiteral("name")).toString()));
    });
}

} // namespace stremio
