#include "stremio/streamresolver.h"

#include "stremio/lzstring.h"
#include "stremio/serverprocess.h"
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

#include <memory>

namespace stremio {

namespace {

constexpr int kProbeTimeoutMs = 2500;
// The server waits up to 30 s for torrent metadata before answering /create.
constexpr int kCreateTimeoutMs = 45000;
constexpr int kContentCheckTimeoutMs = 8000;

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
{
}

void StreamingServer::setProcess(StreamingServerProcess *process)
{
    process_ = process;
    connect(process, &StreamingServerProcess::started, this, [this](const QString &url) {
        if (usesBuiltIn()) {
            url_ = url;
            baseUrl_.clear();
            checked_ = {};
        }
    });
    connect(process, &StreamingServerProcess::stopped, this, [this] {
        if (usesBuiltIn()) {
            url_.clear();
            baseUrl_.clear();
            checked_ = {};
            setAvailable(false);
        }
    });
    if (externalUrl_.isEmpty()) {
        url_ = process->url();
    }
}

void StreamingServer::setExternalUrl(const QString &url)
{
    const QString next = url.trimmed().isEmpty() ? QString() : withTrailingSlash(url.trimmed());
    if (next == externalUrl_) {
        return;
    }
    externalUrl_ = next;
    url_ = !next.isEmpty() ? next : (process_ ? process_->url() : QString());
    baseUrl_.clear();
    checked_ = {};
    setAvailable(false);
}

void StreamingServer::setAvailable(bool available)
{
    if (available != available_) {
        available_ = available;
        emit availabilityChanged(available);
    }
}

void StreamingServer::probe(QObject *context, std::function<void(bool)> done, bool force)
{
    if (url_.isEmpty() || (!force && checked_.isValid() && checked_.secsTo(QDateTime::currentDateTimeUtc()) < 10)) {
        QPointer<QObject> guard(context);
        const bool hasContext = context != nullptr;
        const bool available = !url_.isEmpty() && available_;
        QMetaObject::invokeMethod(this, [guard, hasContext, done, available] {
            if (!hasContext || guard) done(available);
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
        setAvailable(available);
        done(available);
    });
}

void StreamingServer::ensure(QObject *context, std::function<void(bool, const QString &)> done)
{
    if (usesBuiltIn()) {
        QPointer<QObject> guard(context);
        const bool hasContext = context != nullptr;
        process_->ensureStarted(context, [this, guard, hasContext, done](bool ok, const QString &error) {
            if (hasContext && !guard) {
                return;
            }
            if (!ok) {
                setAvailable(false);
                done(false, error);
                return;
            }
            url_ = process_->url();
            probe(guard, [done](bool available) {
                done(available, available ? QString() : tr("The built-in streaming engine is not responding."));
            }, !available_);
        });
        return;
    }
    if (url_.isEmpty()) {
        QMetaObject::invokeMethod(this, [done] {
            done(false, tr("No streaming server is configured."));
        }, Qt::QueuedConnection);
        return;
    }
    probe(context, [this, done](bool available) {
        done(available, available ? QString()
                                  : tr("The streaming server at %1 is not reachable. Start it, or switch back to "
                                       "LAMBDA's built-in engine in Add-ons → Streaming.").arg(url_));
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

QString StreamResolver::youTubeId(const QString &url)
{
    const QUrl parsed(url);
    const QString scheme = parsed.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        return {};
    }
    QString host = parsed.host().toLower();
    if (host.startsWith(QLatin1String("www."))) {
        host = host.mid(4);
    } else if (host.startsWith(QLatin1String("m."))) {
        host = host.mid(2);
    }
    static const QRegularExpression idRe(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    const QStringList parts = parsed.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString id;
    if (host == QLatin1String("youtu.be")) {
        id = parts.value(0);
    } else if (host == QLatin1String("youtube.com") || host == QLatin1String("music.youtube.com")
               || host == QLatin1String("youtube-nocookie.com")) {
        if (parts.value(0) == QLatin1String("watch")) {
            id = QUrlQuery(parsed).queryItemValue(QStringLiteral("v"));
        } else if (parts.size() >= 2 && (parts[0] == QLatin1String("embed") || parts[0] == QLatin1String("shorts")
                                         || parts[0] == QLatin1String("live") || parts[0] == QLatin1String("v"))) {
            id = parts[1];
        }
    }
    return idRe.match(id).hasMatch() ? id : QString();
}

QString StreamResolver::youTubeWatchUrl(const QString &videoId)
{
    // Stream::download_url for YouTube.
    return QStringLiteral("https://www.youtube.com/watch?v=") + videoId;
}

bool StreamResolver::looksLikeMedia(const QString &url)
{
    static const QRegularExpression mediaRe(QStringLiteral(
        "\\.(mkv|mp4|m4v|mov|avi|wmv|webm|ts|m2ts|mts|mpg|mpeg|flv|ogv|3gp|m3u8|mpd|mp3|m4a|flac|ogg|opus|wav|aac)$"),
        QRegularExpression::CaseInsensitiveOption);
    return mediaRe.match(QUrl(url).path()).hasMatch();
}

bool StreamResolver::needsStreamingServer(const Stream &stream)
{
    switch (stream.source.kind) {
    case StreamSourceKind::Url:
        return stream.source.isMagnet() || isFtp(stream.source.url);
    case StreamSourceKind::Rar:
    case StreamSourceKind::Zip:
    case StreamSourceKind::Zip7:
    case StreamSourceKind::Tgz:
    case StreamSourceKind::Tar:
    case StreamSourceKind::Nzb:
    case StreamSourceKind::Torrent:
        return true;
    case StreamSourceKind::YouTube:     // opened in the browser
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
        done(url.isEmpty() ? unsupported(tr("This stream is only available on other devices."))
                           : external(url));
        return;
    }
    if (source.kind == StreamSourceKind::PlayerFrame) {
        done(external(source.url));
        return;
    }
    if (source.kind == StreamSourceKind::YouTube) {
        done(external(youTubeWatchUrl(source.ytId)));
        return;
    }
    if (source.kind == StreamSourceKind::Url && !source.isMagnet() && !isFtp(source.url)) {
        resolveDirect(stream, receiver, done);
        return;
    }

    // Torrents, magnets, archives, NZB and FTP need the streaming server.
    QPointer<QObject> guard(receiver);
    if (context.progress) {
        context.progress(server_->usesBuiltIn() && !server_->process()->isRunning()
                             ? tr("Starting the streaming engine…")
                             : tr("Connecting to the streaming engine…"));
    }
    server_->ensure(receiver, [this, stream, context, guard, done](bool available, const QString &error) {
        if (!guard) {
            return;
        }
        if (!available) {
            done(unsupported(error));
            return;
        }
        const StreamSource &source = stream.source;
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
                    done(unsupported(tr("This magnet link has no valid info hash.")));
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

void StreamResolver::resolveDirect(const Stream &stream, QObject *receiver, std::function<void(const PlaybackSource &)> done)
{
    const QString url = stream.source.url;
    PlaybackSource play;
    play.kind = PlaybackSource::Kind::Play;

    const QString ytId = youTubeId(url);
    if (!ytId.isEmpty()) {
        done(external(youTubeWatchUrl(ytId)));
        return;
    }

    play.url = url;
    play.httpHeaders = sanitizedRequestHeaders(stream.behaviorHints.proxyRequestHeaders);
    play.filename = stream.behaviorHints.filename.value_or(QString());
    // Addons that pre-build streaming-server URLs (convertStream.js fallback).
    const QStringList parts = QUrl(url).path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    static const QRegularExpression hashRe(QStringLiteral("^[a-fA-F0-9]{40}$"));
    static const QRegularExpression indexRe(QStringLiteral("^-?\\d+$"));
    if (parts.size() == 2 && hashRe.match(parts[0]).hasMatch() && indexRe.match(parts[1]).hasMatch()) {
        play.infoHash = parts[0].toLower();
        play.fileIdx = parts[1].toInt();
    }
    const QString scheme = QUrl(url).scheme().toLower();
    if (looksLikeMedia(url) || !play.infoHash.isEmpty()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))) {
        done(play);
        return;
    }

    // Unknown link: look at the first byte's headers so a web page is opened
    // in the browser instead of being handed to mpv.
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(kContentCheckTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("Range", "bytes=0-0");
    for (const auto &header : std::as_const(play.httpHeaders)) {
        request.setRawHeader(header.first.toUtf8(), header.second.toUtf8());
    }
    QNetworkReply *reply = network_->get(request);
    auto decided = std::make_shared<bool>(false);
    auto decide = [reply, decided, play, url, done]() {
        if (*decided) {
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 && status < 400) {
            return; // redirect being followed
        }
        *decided = true;
        const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
        reply->abort();
        if (type.startsWith(QLatin1String("text/html")) || type.startsWith(QLatin1String("application/xhtml"))) {
            PlaybackSource page = external(url);
            page.error = tr("This link is a web page, not a video.");
            done(page);
        } else if (status == 401 || status == 403 || status == 404 || status == 410) {
            done(unsupported(tr("The stream link is not available (HTTP %1).").arg(status)));
        } else {
            done(play); // media, or undecidable: mpv reports its own errors
        }
    };
    connect(reply, &QNetworkReply::metaDataChanged, receiver ? receiver : this, decide);
    connect(reply, &QNetworkReply::finished, receiver ? receiver : this, decide);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

QString StreamResolver::torrentCreateResult(const QJsonObject &response, std::optional<int> fileIdx,
                                            int *index, QString *filename, qint64 *size)
{
    const QJsonValue error = response.value(QStringLiteral("error"));
    if (error.isString() && !error.toString().isEmpty()) {
        return tr("The streaming engine could not open the torrent: %1").arg(error.toString());
    }
    const QJsonArray files = response.value(QStringLiteral("files")).toArray();
    if (files.isEmpty()) {
        return tr("Could not get the torrent's metadata: no peers are sharing it right now. Try another source.");
    }
    int chosen = fileIdx.value_or(-1);
    if (!fileIdx) {
        const QJsonValue guessed = response.value(QStringLiteral("guessedFileIdx"));
        chosen = guessed.isDouble() ? guessed.toInt() : -1;
        if (chosen < 0) {
            return tr("No video file was found in this torrent.");
        }
    }
    if (chosen < 0 || chosen >= files.size()) {
        return tr("The file this stream points to (#%1) is not in the torrent.").arg(chosen);
    }
    const QJsonObject file = files.at(chosen).toObject();
    if (index) *index = chosen;
    if (filename) *filename = file.value(QStringLiteral("name")).toString().section(QLatin1Char('/'), -1);
    if (size) *size = qint64(file.value(QStringLiteral("length")).toDouble());
    return {};
}

void StreamResolver::createTorrent(const QString &infoHash, std::optional<int> fileIdx, const QStringList &sources,
                                   const ResolveContext &context, QObject *receiver,
                                   std::function<void(const PlaybackSource &)> done)
{
    const QString serverUrl = server_->baseUrl();
    if (context.progress) {
        context.progress(tr("Fetching torrent metadata…"));
    }

    // createTorrent.js skips /create when the file is known and there are no
    // trackers. LAMBDA always creates and asks the server to resolve the file
    // list, so a dead torrent or a wrong fileIdx is reported here instead of
    // as an mpv timeout (see docs/stremio/COMPATIBILITY.md C-019).
    QJsonObject body = createTorrentBody(infoHash, fileIdx, sources, context.seriesInfo);
    if (fileIdx) {
        body.insert("guessFileIdx", QJsonObject());
    }
    QNetworkRequest request(QUrl(withTrailingSlash(serverUrl) + encodeUriComponent(infoHash) + QStringLiteral("/create")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kCreateTimeoutMs);
    QNetworkReply *reply = network_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    const QStringList peerSources = body.value(QStringLiteral("peerSearch")).toObject()
                                        .value(QStringLiteral("sources")).toVariant().toStringList();
    QObject *owner = receiver ? receiver : this;
    connect(reply, &QNetworkReply::finished, owner, [this, owner, reply, infoHash, serverUrl, fileIdx, peerSources, done] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::OperationCanceledError || reply->error() == QNetworkReply::TimeoutError) {
            done(unsupported(tr("Could not get the torrent's metadata in time: no peers answered. Try another source.")));
            return;
        }
        if (reply->error() != QNetworkReply::NoError || (status != 200 && status != 201)) {
            done(unsupported(tr("The streaming engine could not open the torrent (%1).")
                                 .arg(status ? QString::number(status) : reply->errorString())));
            return;
        }
        auto finish = [infoHash, serverUrl, fileIdx, peerSources, done](const QJsonObject &response) {
            int index = -1;
            QString filename;
            qint64 size = 0;
            const QString error = torrentCreateResult(response, fileIdx, &index, &filename, &size);
            if (!error.isEmpty()) {
                done(unsupported(error));
                return;
            }
            PlaybackSource play;
            play.kind = PlaybackSource::Kind::Play;
            play.viaStreamingServer = true;
            play.infoHash = infoHash;
            play.fileIdx = index;
            play.filename = filename;
            if (size > 0) {
                play.fileSize = size;
            }
            play.url = torrentUrl(serverUrl, infoHash, index, peerSources);
            done(play);
        };
        const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        if (!response.value(QStringLiteral("files")).toArray().isEmpty() || response.value(QStringLiteral("error")).isString()) {
            finish(response);
            return;
        }
        // stream-server resolves the file list (for guessedFileIdx) after it
        // took the statistics it answers with, so a fresh torrent comes back
        // without files; the engine's stats have them by now.
        QNetworkRequest statsRequest(QUrl(withTrailingSlash(serverUrl) + encodeUriComponent(infoHash) + QStringLiteral("/stats.json")));
        statsRequest.setTransferTimeout(kProbeTimeoutMs * 4);
        QNetworkReply *statsReply = network_->get(statsRequest);
        connect(statsReply, &QNetworkReply::finished, statsReply, &QObject::deleteLater);
        connect(statsReply, &QNetworkReply::finished, owner, [statsReply, response, finish] {
            QJsonObject merged = response;
            if (statsReply->error() == QNetworkReply::NoError) {
                const QJsonObject stats = QJsonDocument::fromJson(statsReply->readAll()).object();
                merged.insert(QStringLiteral("files"), stats.value(QStringLiteral("files")));
            }
            finish(merged);
        });
    });
}

} // namespace stremio
