#include "stremio/streamresolver.h"

#include "stremio/lzstring.h"
#include "stremio/transport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace stremio {

namespace {

constexpr int kProbeTimeoutMs = 2500;
constexpr int kCreateTimeoutMs = 30000;

QString withTrailingSlash(QString url)
{
    if (!url.endsWith(QLatin1Char('/'))) {
        url += QLatin1Char('/');
    }
    return url;
}

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

PlaybackSource unsupported(const QString &message)
{
    PlaybackSource source;
    source.kind = PlaybackSource::Kind::Unsupported;
    source.error = message;
    return source;
}

PlaybackSource external(const QString &url)
{
    PlaybackSource source;
    source.kind = PlaybackSource::Kind::OpenExternally;
    source.url = url;
    return source;
}

QString base32ToHex(const QString &value)
{
    static const QString alphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
    QByteArray bytes;
    quint64 buffer = 0;
    int bits = 0;
    for (const QChar ch : value.toUpper()) {
        const qsizetype index = alphabet.indexOf(ch);
        if (index < 0) {
            return {};
        }
        buffer = (buffer << 5) | quint64(index);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            bytes.append(char((buffer >> bits) & 0xFF));
        }
    }
    return bytes.size() == 20 ? QString::fromLatin1(bytes.toHex()) : QString();
}

QString archiveKind(StreamSourceKind kind)
{
    switch (kind) {
    case StreamSourceKind::Rar: return QStringLiteral("rar");
    case StreamSourceKind::Zip: return QStringLiteral("zip");
    case StreamSourceKind::Zip7: return QStringLiteral("7zip");
    case StreamSourceKind::Tgz: return QStringLiteral("tgz");
    case StreamSourceKind::Tar: return QStringLiteral("tar");
    case StreamSourceKind::Nzb: return QStringLiteral("nzb");
    default: return {};
    }
}

bool isFtp(const QString &url)
{
    const QString scheme = QUrl(url).scheme().toLower();
    return scheme == QLatin1String("ftp") || scheme == QLatin1String("ftps");
}

} // namespace

// ---- StreamingServer ---------------------------------------------------------

StreamingServer::StreamingServer(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , network_(network)
    , url_(QString::fromLatin1(kDefaultUrl))
{
}

void StreamingServer::setUrl(const QString &url)
{
    const QString next = withTrailingSlash(url.trimmed().isEmpty() ? QString::fromLatin1(kDefaultUrl) : url.trimmed());
    if (next == url_) {
        return;
    }
    url_ = next;
    baseUrl_.clear();
    checked_ = {};
    if (available_) {
        available_ = false;
        emit availabilityChanged(false);
    }
}

void StreamingServer::probe(QObject *context, std::function<void(bool)> done, bool force)
{
    if (!force && checked_.isValid() && checked_.secsTo(QDateTime::currentDateTimeUtc()) < 10) {
        QPointer<QObject> guard(context);
        const bool available = available_;
        QMetaObject::invokeMethod(this, [guard, done, available] {
            if (guard) done(available);
        }, Qt::QueuedConnection);
        return;
    }
    QNetworkRequest request(QUrl(url_ + QStringLiteral("settings")));
    request.setTransferTimeout(kProbeTimeoutMs);
    QNetworkReply *reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, context ? context : this, [this, reply, done] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject settings = reply->error() == QNetworkReply::NoError
            ? QJsonDocument::fromJson(reply->readAll()).object()
            : QJsonObject();
        const bool available = reply->error() == QNetworkReply::NoError && (status == 200 || status == 201)
            && settings.contains(QStringLiteral("values"));
        const QString baseUrl = settings.value(QStringLiteral("baseUrl")).toString();
        baseUrl_ = available && !baseUrl.isEmpty() ? withTrailingSlash(baseUrl) : QString();
        checked_ = QDateTime::currentDateTimeUtc();
        if (available != available_) {
            available_ = available;
            emit availabilityChanged(available);
        }
        done(available);
    });
}

// ---- StreamResolver ----------------------------------------------------------

StreamResolver::StreamResolver(QNetworkAccessManager *network, StreamingServer *server, QObject *parent)
    : QObject(parent)
    , network_(network)
    , server_(server)
{
}

QString StreamResolver::formUrlEncode(const QString &value)
{
    // application/x-www-form-urlencoded, as URLSearchParams serialises.
    static const char hex[] = "0123456789ABCDEF";
    QString out;
    for (const char ch : value.toUtf8()) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '*' || c == '-' || c == '.' || c == '_') {
            out.append(QLatin1Char(char(c)));
        } else if (c == ' ') {
            out.append(QLatin1Char('+'));
        } else {
            out.append(QLatin1Char('%'));
            out.append(QLatin1Char(hex[c >> 4]));
            out.append(QLatin1Char(hex[c & 0x0F]));
        }
    }
    return out;
}

std::optional<QString> StreamResolver::magnetInfoHash(const QString &magnet, QStringList *trackers)
{
    if (!magnet.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)) {
        return std::nullopt;
    }
    const qsizetype queryStart = magnet.indexOf(QLatin1Char('?'));
    if (queryStart < 0) {
        return std::nullopt;
    }
    const QUrlQuery query(magnet.mid(queryStart + 1));
    std::optional<QString> hash;
    for (const auto &item : query.queryItems(QUrl::FullyDecoded)) {
        if (item.first == QLatin1String("xt") && item.second.startsWith(QLatin1String("urn:btih:"), Qt::CaseInsensitive)) {
            const QString value = item.second.mid(9);
            static const QRegularExpression hexRe(QStringLiteral("^[0-9a-fA-F]{40}$"));
            if (hexRe.match(value).hasMatch()) {
                hash = value.toLower();
            } else if (value.size() == 32) {
                const QString decoded = base32ToHex(value);
                if (!decoded.isEmpty()) {
                    hash = decoded;
                }
            }
        } else if (item.first == QLatin1String("tr") && trackers) {
            trackers->append(item.second);
        }
    }
    return hash;
}

QString StreamResolver::torrentUrl(const QString &serverUrl, const QString &infoHash, int fileIdx, const QStringList &sources)
{
    // createTorrent.js buildTorrent
    QString url = withTrailingSlash(serverUrl) + encodeUriComponent(infoHash) + QLatin1Char('/')
        + encodeUriComponent(QString::number(fileIdx));
    if (!sources.isEmpty()) {
        QStringList pairs;
        for (const QString &source : sources) {
            pairs.append(QStringLiteral("tr=") + formUrlEncode(source));
        }
        url += QLatin1Char('?') + pairs.join(QLatin1Char('&'));
    }
    return url;
}

QJsonObject StreamResolver::createTorrentBody(const QString &infoHash, std::optional<int> fileIdx,
                                              const QStringList &sources, const std::optional<SeriesInfo> &seriesInfo)
{
    QJsonObject body{{"torrent", QJsonObject{{"infoHash", infoHash}}}};
    if (!sources.isEmpty()) {
        QStringList peerSources{QStringLiteral("dht:") + infoHash};
        for (const QString &source : sources) {
            const QString normalized = source.startsWith(QLatin1String("tracker:")) || source.startsWith(QLatin1String("dht:"))
                ? source
                : QStringLiteral("tracker:") + source;
            if (!peerSources.contains(normalized)) {
                peerSources.append(normalized);
            }
        }
        body.insert("peerSearch", QJsonObject{{"sources", QJsonArray::fromStringList(peerSources)}, {"min", 40}, {"max", 200}});
    }
    if (!fileIdx) {
        QJsonObject guess;
        if (seriesInfo) {
            guess.insert("season", seriesInfo->season);
            guess.insert("episode", seriesInfo->episode);
        }
        body.insert("guessFileIdx", guess);
    } else {
        body.insert("guessFileIdx", false);
    }
    return body;
}

QString StreamResolver::ftpUrl(const QString &serverUrl, const QString &url, QString *error)
{
    // Stream::ftp_url_handler: {server}/ftp/{filename}?lz=...
    const QString path = QUrl(url).path();
    const QString filename = path.section(QLatin1Char('/'), -1);
    if (filename.isEmpty()) {
        fail(error, QStringLiteral("Ftp(s) filepath is missing in the url"));
        return {};
    }
    const QString payload = QString::fromUtf8(QJsonDocument(QJsonObject{{"ftpUrl", url}}).toJson(QJsonDocument::Compact));
    return withTrailingSlash(serverUrl) + QStringLiteral("ftp/") + filename
        + QStringLiteral("?lz=") + lzCompressToEncodedUriComponent(payload);
}

QString StreamResolver::archiveCreateUrl(const QString &serverUrl, const Stream &stream, QString *error)
{
    const StreamSource &source = stream.source;
    const QString kind = archiveKind(source.kind);
    if (kind.isEmpty()) {
        fail(error, QStringLiteral("not an archive stream"));
        return {};
    }
    auto proxied = [&](const QString &url, QString *err) -> QString {
        return isFtp(url) ? ftpUrl(serverUrl, url, err) : url;
    };
    QJsonObject payload;
    if (source.kind == StreamSourceKind::Nzb) {
        if (source.servers.isEmpty()) {
            fail(error, QStringLiteral("No nzb server URLs provided"));
            return {};
        }
        QJsonArray servers;
        for (const QString &server : source.servers) {
            servers.append(proxied(server, error));
        }
        payload.insert("servers", servers);
        if (!source.url.isEmpty()) {
            payload.insert("nzbUrl", proxied(source.url, error));
        }
        if (!source.nzbUrls.isEmpty()) {
            QJsonArray urls;
            for (const QString &url : source.nzbUrls) {
                urls.append(proxied(url, error));
            }
            payload.insert("nzbUrls", urls);
        }
    } else {
        if (source.archiveUrls.isEmpty()) {
            fail(error, QStringLiteral("No %1 URLs provided").arg(kind));
            return {};
        }
        QJsonArray urls;
        for (const ArchiveUrl &archive : source.archiveUrls) {
            QJsonArray entry{proxied(archive.url, error)};
            if (archive.bytes) {
                entry.append(double(*archive.bytes));
            }
            urls.append(entry);
        }
        payload.insert("urls", urls);
        if (source.fileIdx) {
            payload.insert("fileIdx", *source.fileIdx);
        }
        if (!source.fileMustInclude.isEmpty()) {
            payload.insert("fileMustInclude", QJsonArray::fromStringList(source.fileMustInclude));
        }
    }
    const QString json = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    return withTrailingSlash(serverUrl) + kind + QStringLiteral("/create?lz=") + lzCompressToEncodedUriComponent(json);
}

QList<QPair<QString, QString>> StreamResolver::sanitizedRequestHeaders(const QList<QPair<QString, QString>> &headers)
{
    QList<QPair<QString, QString>> result;
    for (const auto &header : headers) {
        const QString key = header.first.trimmed();
        const QString value = header.second.trimmed();
        if (key.isEmpty() || value.isEmpty() || key.compare(QLatin1String("Range"), Qt::CaseInsensitive) == 0
            || key.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) {
            continue;
        }
        result.append({key, value});
    }
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QStringList StreamResolver::mpvHeaderFields(const QList<QPair<QString, QString>> &headers)
{
    QStringList fields;
    for (const auto &header : sanitizedRequestHeaders(headers)) {
        fields.append(header.first + QStringLiteral(": ") + header.second);
    }
    return fields;
}

bool StreamResolver::needsStreamingServer(const Stream &stream)
{
    switch (stream.source.kind) {
    case StreamSourceKind::Url:
        return stream.source.isMagnet() || isFtp(stream.source.url);
    case StreamSourceKind::YouTube:
    case StreamSourceKind::Rar:
    case StreamSourceKind::Zip:
    case StreamSourceKind::Zip7:
    case StreamSourceKind::Tgz:
    case StreamSourceKind::Tar:
    case StreamSourceKind::Nzb:
    case StreamSourceKind::Torrent:
        return true;
    case StreamSourceKind::PlayerFrame:
    case StreamSourceKind::External:
        return false;
    }
    return false;
}

bool StreamResolver::playsInPlayer(const Stream &stream)
{
    return stream.source.kind != StreamSourceKind::PlayerFrame && stream.source.kind != StreamSourceKind::External;
}

void StreamResolver::resolve(const Stream &stream, const ResolveContext &context, QObject *receiver,
                             std::function<void(const PlaybackSource &)> done)
{
    const StreamSource &source = stream.source;

    if (source.kind == StreamSourceKind::External) {
        const QString url = !source.externalUrl.isEmpty() ? source.externalUrl : source.androidTvUrl;
        done(url.isEmpty() ? unsupported(QStringLiteral("This stream is only available on other devices."))
                           : external(url));
        return;
    }
    if (source.kind == StreamSourceKind::PlayerFrame) {
        done(external(source.url));
        return;
    }
    if (source.kind == StreamSourceKind::Url && !source.isMagnet() && !isFtp(source.url)) {
        PlaybackSource play;
        play.kind = PlaybackSource::Kind::Play;
        play.url = source.url;
        play.httpHeaders = sanitizedRequestHeaders(stream.behaviorHints.proxyRequestHeaders);
        // Addons that pre-build streaming-server URLs (convertStream.js fallback).
        const QStringList parts = QUrl(source.url).path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        static const QRegularExpression hashRe(QStringLiteral("^[a-fA-F0-9]{40}$"));
        static const QRegularExpression indexRe(QStringLiteral("^-?\\d+$"));
        if (parts.size() == 2 && hashRe.match(parts[0]).hasMatch() && indexRe.match(parts[1]).hasMatch()) {
            play.infoHash = parts[0].toLower();
            play.fileIdx = parts[1].toInt();
        }
        done(play);
        return;
    }

    // Everything else needs the streaming server.
    QPointer<QObject> guard(receiver);
    server_->probe(receiver, [this, stream, context, guard, done](bool available) {
        if (!guard) {
            return;
        }
        const StreamSource &source = stream.source;
        if (!available) {
            if (source.kind == StreamSourceKind::YouTube) {
                // Stream::download_url for YouTube.
                done(external(QStringLiteral("https://youtube.com/watch?v=") + encodeUriComponent(source.ytId)));
                return;
            }
            done(unsupported(QStringLiteral(
                "This stream needs the Stremio streaming server (Stremio Service or Stremio desktop) running at %1.")
                                 .arg(server_->url())));
            return;
        }
        const QString serverUrl = server_->baseUrl();
        PlaybackSource play;
        play.kind = PlaybackSource::Kind::Play;
        play.viaStreamingServer = true;

        switch (source.kind) {
        case StreamSourceKind::Url: {
            if (source.isMagnet()) {
                QStringList trackers;
                const auto hash = magnetInfoHash(source.url, &trackers);
                if (!hash) {
                    done(unsupported(QStringLiteral("Failed to decode magnet url")));
                    return;
                }
                QStringList sources;
                for (const QString &tracker : std::as_const(trackers)) {
                    sources.append(QStringLiteral("tracker:") + tracker);
                }
                createTorrent(*hash, std::nullopt, sources, context, guard, done);
                return;
            }
            QString error;
            play.url = ftpUrl(serverUrl, source.url, &error);
            if (play.url.isEmpty()) {
                done(unsupported(error));
                return;
            }
            break;
        }
        case StreamSourceKind::YouTube:
            play.url = withTrailingSlash(serverUrl) + QStringLiteral("yt/") + encodeUriComponent(source.ytId);
            break;
        case StreamSourceKind::Torrent:
            createTorrent(source.infoHash, source.fileIdx, source.announce, context, guard, done);
            return;
        default: {
            QString error;
            play.url = archiveCreateUrl(serverUrl, stream, &error);
            if (play.url.isEmpty()) {
                done(unsupported(error));
                return;
            }
            break;
        }
        }
        done(play);
    });
}

void StreamResolver::createTorrent(const QString &infoHash, std::optional<int> fileIdx, const QStringList &sources,
                                   const ResolveContext &context, QObject *receiver,
                                   std::function<void(const PlaybackSource &)> done)
{
    const QString serverUrl = server_->baseUrl();
    auto finish = [infoHash, serverUrl, done](int index, const QStringList &urlSources) {
        PlaybackSource play;
        play.kind = PlaybackSource::Kind::Play;
        play.viaStreamingServer = true;
        play.infoHash = infoHash;
        play.fileIdx = index;
        play.url = torrentUrl(serverUrl, infoHash, index, urlSources);
        done(play);
    };

    // createTorrent.js: no trackers and a known file -> no /create call.
    if (sources.isEmpty() && fileIdx) {
        finish(*fileIdx, sources);
        return;
    }

    const QJsonObject body = createTorrentBody(infoHash, fileIdx, sources, context.seriesInfo);
    QNetworkRequest request(QUrl(withTrailingSlash(serverUrl) + encodeUriComponent(infoHash) + QStringLiteral("/create")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kCreateTimeoutMs);
    QNetworkReply *reply = network_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    const bool guessing = !fileIdx.has_value();
    const QStringList peerSources = body.value(QStringLiteral("peerSearch")).toObject()
                                        .value(QStringLiteral("sources")).toVariant().toStringList();
    connect(reply, &QNetworkReply::finished, receiver ? receiver : this, [reply, guessing, fileIdx, peerSources, finish, done] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || (status != 200 && status != 201)) {
            done(unsupported(QStringLiteral("The streaming server could not open the torrent (%1).")
                                 .arg(status ? QString::number(status) : reply->errorString())));
            return;
        }
        const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        int index = fileIdx.value_or(-1);
        if (guessing) {
            const QJsonValue guessed = response.value(QStringLiteral("guessedFileIdx"));
            index = guessed.isDouble() ? guessed.toInt() : -1;
        }
        finish(index, peerSources);
    });
}

} // namespace stremio
