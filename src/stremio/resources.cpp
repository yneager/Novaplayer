#include "stremio/resources.h"

#include "stremio/json.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <limits>

namespace stremio {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

bool isMissing(const QJsonValue &value)
{
    return value.isUndefined() || value.isNull();
}

// Option<Url> field: missing/null -> empty; invalid -> error.
bool optionalStrictUrl(const QJsonObject &object, const QString &key, QString &out, QString *error)
{
    return json::optionalUrl(object, key, true, out, error);
}

bool requiredUrl(const QJsonObject &object, const QString &key, QString &out, QString *error)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || !json::isAbsoluteUrl(value.toString())) {
        return fail(error, QStringLiteral("\"%1\" must be an absolute URL").arg(key));
    }
    out = value.toString();
    return true;
}

bool urlList(const QJsonValue &value, QStringList &out, QString *error)
{
    out.clear();
    if (isMissing(value)) {
        return true;
    }
    if (!value.isArray()) {
        return fail(error, QStringLiteral("expected an array of URLs"));
    }
    for (const QJsonValue &item : value.toArray()) {
        if (!item.isString() || !json::isAbsoluteUrl(item.toString())) {
            return fail(error, QStringLiteral("expected an array of URLs"));
        }
        out.append(item.toString());
    }
    return true;
}

bool archiveUrls(const QJsonValue &value, QList<ArchiveUrl> &out)
{
    if (!value.isArray()) {
        return false;
    }
    out.clear();
    for (const QJsonValue &item : value.toArray()) {
        // ArchiveUrlShort: [url] or [url, bytes]
        if (!item.isArray()) {
            return false;
        }
        const QJsonArray pair = item.toArray();
        if (pair.isEmpty() || pair.size() > 2 || !pair[0].isString() || !json::isAbsoluteUrl(pair[0].toString())) {
            return false;
        }
        ArchiveUrl archive;
        archive.url = pair[0].toString();
        if (pair.size() == 2 && !pair[1].isNull()) {
            const double bytes = pair[1].toDouble(-1);
            if (!pair[1].isDouble() || bytes < 0 || bytes != std::floor(bytes)) {
                return false;
            }
            archive.bytes = qint64(bytes);
        }
        out.append(archive);
    }
    return true;
}

bool fileIdxAndIncludes(const QJsonObject &object, StreamSource &source)
{
    std::optional<qint64> fileIdx;
    if (!json::optionalUnsigned(object, QStringLiteral("fileIdx"), std::numeric_limits<quint16>::max(), fileIdx, nullptr)) {
        return false;
    }
    source.fileIdx = fileIdx ? std::optional<int>(int(*fileIdx)) : std::nullopt;
    return json::defaultStringList(object, QStringLiteral("fileMustInclude"), source.fileMustInclude, nullptr);
}

bool isInfoHash(const QString &value)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{40}$"));
    return re.match(value).hasMatch();
}

// Tries the StreamSource variants in the order serde's untagged enum does.
bool parseStreamSource(const QJsonObject &object, StreamSource &source)
{
    // Url { url }
    {
        const QJsonValue url = object.value(QStringLiteral("url"));
        if (url.isString() && json::isAbsoluteUrl(url.toString())) {
            source = {};
            source.kind = StreamSourceKind::Url;
            source.url = url.toString();
            return true;
        }
    }
    // YouTube { ytId }
    if (object.value(QStringLiteral("ytId")).isString()) {
        source = {};
        source.kind = StreamSourceKind::YouTube;
        source.ytId = object.value(QStringLiteral("ytId")).toString();
        return true;
    }
    // Archives
    struct ArchiveKey
    {
        const char *key;
        StreamSourceKind kind;
    };
    static const ArchiveKey archives[] = {
        {"rarUrls", StreamSourceKind::Rar},
        {"zipUrls", StreamSourceKind::Zip},
        {"7zipUrls", StreamSourceKind::Zip7},
        {"tgzUrls", StreamSourceKind::Tgz},
        {"tarUrls", StreamSourceKind::Tar},
    };
    for (const ArchiveKey &archive : archives) {
        StreamSource candidate;
        candidate.kind = archive.kind;
        if (archiveUrls(object.value(QLatin1String(archive.key)), candidate.archiveUrls)
            && fileIdxAndIncludes(object, candidate)) {
            source = candidate;
            return true;
        }
    }
    // Nzb { nzbUrl?, nzbUrls, servers }
    if (object.contains(QStringLiteral("servers"))) {
        StreamSource candidate;
        candidate.kind = StreamSourceKind::Nzb;
        const QJsonValue servers = object.value(QStringLiteral("servers"));
        if (servers.isArray() && urlList(servers, candidate.servers, nullptr)
            && optionalStrictUrl(object, QStringLiteral("nzbUrl"), candidate.url, nullptr)
            && urlList(object.value(QStringLiteral("nzbUrls")), candidate.nzbUrls, nullptr)) {
            source = candidate;
            return true;
        }
    }
    // Torrent { infoHash, fileIdx?, announce|sources, fileMustInclude }
    {
        const QJsonValue infoHash = object.value(QStringLiteral("infoHash"));
        if (infoHash.isString() && isInfoHash(infoHash.toString())) {
            StreamSource candidate;
            candidate.kind = StreamSourceKind::Torrent;
            candidate.infoHash = infoHash.toString().toLower();
            const QString announceKey = object.contains(QStringLiteral("announce")) ? QStringLiteral("announce")
                                                                                     : QStringLiteral("sources");
            if (fileIdxAndIncludes(object, candidate)
                && json::defaultStringList(object, announceKey, candidate.announce, nullptr)) {
                source = candidate;
                return true;
            }
        }
    }
    // PlayerFrame { playerFrameUrl }
    {
        const QJsonValue url = object.value(QStringLiteral("playerFrameUrl"));
        if (url.isString() && json::isAbsoluteUrl(url.toString())) {
            source = {};
            source.kind = StreamSourceKind::PlayerFrame;
            source.url = url.toString();
            return true;
        }
    }
    // External { externalUrl?, androidTvUrl?, tizenUrl?, webosUrl? } (at least one)
    {
        StreamSource candidate;
        candidate.kind = StreamSourceKind::External;
        std::optional<QString> tizen;
        std::optional<QString> webos;
        if (optionalStrictUrl(object, QStringLiteral("externalUrl"), candidate.externalUrl, nullptr)
            && optionalStrictUrl(object, QStringLiteral("androidTvUrl"), candidate.androidTvUrl, nullptr)
            && json::optionalString(object, QStringLiteral("tizenUrl"), tizen, nullptr)
            && json::optionalString(object, QStringLiteral("webosUrl"), webos, nullptr)) {
            candidate.tizenUrl = tizen.value_or(QString());
            candidate.webosUrl = webos.value_or(QString());
            if (!candidate.externalUrl.isEmpty() || !candidate.androidTvUrl.isEmpty() || tizen || webos) {
                source = candidate;
                return true;
            }
        }
    }
    return false;
}

bool parseHeaderMap(const QJsonValue &value, QList<QPair<QString, QString>> &out)
{
    out.clear();
    if (isMissing(value)) {
        return true;
    }
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!it.value().isString()) {
            return false;
        }
        out.append({it.key(), it.value().toString()});
    }
    return true;
}

bool parseStreamBehaviorHints(const QJsonValue &value, StreamBehaviorHints &hints, QString *error)
{
    if (isMissing(value)) {
        return true;
    }
    if (!value.isObject()) {
        return fail(error, QStringLiteral("\"behaviorHints\" must be an object"));
    }
    const QJsonObject object = value.toObject();
    if (!json::defaultBool(object, QStringLiteral("notWebReady"), hints.notWebReady, error)
        || !json::optionalString(object, QStringLiteral("bingeGroup"), hints.bingeGroup, error)
        || !json::optionalStringList(object, QStringLiteral("countryWhitelist"), hints.countryWhitelist, error)
        || !json::optionalString(object, QStringLiteral("filename"), hints.filename, error)
        || !json::optionalString(object, QStringLiteral("videoHash"), hints.videoHash, error)) {
        return false;
    }
    std::optional<qint64> size;
    if (!json::optionalUnsigned(object, QStringLiteral("videoSize"), std::numeric_limits<qint64>::max(), size, error)) {
        return false;
    }
    hints.videoSize = size;
    const QJsonValue proxy = object.value(QStringLiteral("proxyHeaders"));
    if (!isMissing(proxy)) {
        if (!proxy.isObject()) {
            return fail(error, QStringLiteral("\"proxyHeaders\" must be an object"));
        }
        const QJsonObject headers = proxy.toObject();
        if (!parseHeaderMap(headers.value(QStringLiteral("request")), hints.proxyRequestHeaders)
            || !parseHeaderMap(headers.value(QStringLiteral("response")), hints.proxyResponseHeaders)) {
            return fail(error, QStringLiteral("\"proxyHeaders\" values must be strings"));
        }
        hints.hasProxyHeaders = true;
    }
    return true;
}

// released: RFC 3339 string or a millisecond timestamp (PickFirst).
bool parseReleased(const QJsonObject &object, const QString &key, bool allowTimestamp,
                   std::optional<QDateTime> &out, QString *error)
{
    const QJsonValue value = object.value(key);
    out.reset();
    if (isMissing(value)) {
        return true;
    }
    if (value.isString()) {
        const QDateTime date = parseRfc3339(value.toString());
        if (date.isValid()) {
            out = date;
            return true;
        }
    } else if (allowTimestamp && value.isDouble()) {
        out = QDateTime::fromMSecsSinceEpoch(qint64(value.toDouble()), QTimeZone::UTC);
        return true;
    }
    return fail(error, QStringLiteral("\"%1\" is not a valid date").arg(key));
}

bool parseStreamList(const QJsonValue &value, QList<Stream> &out, bool oneOrMany, QString *error)
{
    out.clear();
    if (isMissing(value)) {
        return true;
    }
    if (oneOrMany && value.isObject()) {
        auto stream = Stream::fromJson(value, error);
        if (!stream) {
            return false;
        }
        out.append(*stream);
        return true;
    }
    if (!value.isArray()) {
        return fail(error, QStringLiteral("expected an array of streams"));
    }
    for (const QJsonValue &item : value.toArray()) {
        auto stream = Stream::fromJson(item, error);
        if (!stream) {
            return false;
        }
        out.append(*stream);
    }
    return true;
}

bool parseLinks(const QJsonValue &value, QList<Link> &out, QString *error)
{
    out.clear();
    if (!value.isArray()) {
        return fail(error, QStringLiteral("\"links\" must be an array"));
    }
    for (const QJsonValue &item : value.toArray()) {
        const QJsonObject object = item.toObject();
        Link link;
        if (!item.isObject() || !json::requiredString(object, QStringLiteral("name"), link.name, error)
            || !json::requiredString(object, QStringLiteral("category"), link.category, error)
            || !requiredUrl(object, QStringLiteral("url"), link.url, error)) {
            return fail(error, QStringLiteral("invalid link"));
        }
        out.append(link);
    }
    return true;
}

QJsonArray streamsToJson(const QList<Stream> &streams)
{
    QJsonArray array;
    for (const Stream &stream : streams) {
        array.append(stream.toJson());
    }
    return array;
}

QString dateToJson(const std::optional<QDateTime> &date)
{
    return date ? date->toUTC().toString(Qt::ISODateWithMs) : QString();
}

} // namespace

QDateTime parseRfc3339(const QString &text)
{
    // chrono's DateTime<Utc> accepts RFC 3339 date-times only (a time and an
    // offset are required).
    static const QRegularExpression re(QStringLiteral(
        "^\\d{4}-\\d{2}-\\d{2}[Tt ]\\d{2}:\\d{2}:\\d{2}(\\.\\d+)?([Zz]|[+-]\\d{2}:\\d{2})$"));
    if (!re.match(text).hasMatch()) {
        return {};
    }
    QString normalized = text;
    normalized.replace(10, 1, QLatin1Char('T'));
    QDateTime date = QDateTime::fromString(normalized, Qt::ISODateWithMs);
    if (!date.isValid()) {
        date = QDateTime::fromString(normalized, Qt::ISODate);
    }
    return date.isValid() ? date.toUTC() : QDateTime();
}

std::optional<Subtitles> Subtitles::fromJson(const QJsonValue &value, QString *error)
{
    if (!value.isObject()) {
        fail(error, QStringLiteral("subtitle must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    Subtitles subtitles;
    if (!json::requiredString(object, QStringLiteral("lang"), subtitles.lang, error)
        || !requiredUrl(object, QStringLiteral("url"), subtitles.url, error)
        || !json::optionalString(object, QStringLiteral("label"), subtitles.label, error)) {
        return std::nullopt;
    }
    // stremio-core requires "id", but the SDK's own protocol example omits it;
    // like Nuvio we fall back to the URL (COMPATIBILITY.md C-011).
    std::optional<QString> id;
    json::optionalString(object, QStringLiteral("id"), id, nullptr);
    if (!id && object.value(QStringLiteral("id")).isDouble()) {
        id = json::numberToString(object.value(QStringLiteral("id")).toDouble());
    }
    subtitles.id = id.value_or(subtitles.url);
    for (const QJsonValue &font : object.value(QStringLiteral("fonts")).toArray()) {
        if (font.isString() && json::isAbsoluteUrl(font.toString())) {
            subtitles.fonts.append(font.toString());
        }
    }
    return subtitles;
}

QJsonObject Subtitles::toJson() const
{
    QJsonObject object{{"id", id}, {"lang", lang}, {"url", url}};
    if (label) {
        object.insert("label", *label);
    }
    if (!fonts.isEmpty()) {
        object.insert("fonts", QJsonArray::fromStringList(fonts));
    }
    return object;
}

bool StreamSource::isMagnet() const
{
    return kind == StreamSourceKind::Url && url.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive);
}

QString StreamSource::kindName() const
{
    switch (kind) {
    case StreamSourceKind::Url: return isMagnet() ? QStringLiteral("magnet") : QStringLiteral("url");
    case StreamSourceKind::YouTube: return QStringLiteral("youtube");
    case StreamSourceKind::Rar: return QStringLiteral("rar");
    case StreamSourceKind::Zip: return QStringLiteral("zip");
    case StreamSourceKind::Zip7: return QStringLiteral("7zip");
    case StreamSourceKind::Tgz: return QStringLiteral("tgz");
    case StreamSourceKind::Tar: return QStringLiteral("tar");
    case StreamSourceKind::Nzb: return QStringLiteral("nzb");
    case StreamSourceKind::Torrent: return QStringLiteral("torrent");
    case StreamSourceKind::PlayerFrame: return QStringLiteral("playerFrame");
    case StreamSourceKind::External: return QStringLiteral("external");
    }
    return {};
}

std::optional<Stream> Stream::fromJson(const QJsonValue &value, QString *error)
{
    if (!value.isObject()) {
        fail(error, QStringLiteral("stream must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    Stream stream;
    stream.raw = object;
    if (!parseStreamSource(object, stream.source)) {
        fail(error, QStringLiteral("stream has no valid source (url, ytId, infoHash, archive, nzb, playerFrameUrl or externalUrl)"));
        return std::nullopt;
    }
    std::optional<QString> description;
    if (!json::optionalString(object, QStringLiteral("name"), stream.name, error)
        || !json::optionalString(object, QStringLiteral("description"), description, error)
        || !json::optionalString(object, QStringLiteral("thumbnail"), stream.thumbnail, error)) {
        return std::nullopt;
    }
    if (!description) {
        // "title" is the deprecated alias of "description".
        if (!json::optionalString(object, QStringLiteral("title"), description, error)) {
            return std::nullopt;
        }
    }
    stream.description = description;
    const QJsonValue subtitles = object.value(QStringLiteral("subtitles"));
    if (subtitles.isArray()) {
        for (const QJsonValue &item : subtitles.toArray()) {
            if (auto subtitle = Subtitles::fromJson(item)) { // VecSkipError
                stream.subtitles.append(*subtitle);
            }
        }
    }
    if (!parseStreamBehaviorHints(object.value(QStringLiteral("behaviorHints")), stream.behaviorHints, error)) {
        return std::nullopt;
    }
    return stream;
}

std::optional<Stream> Stream::youtubeFromVideoId(const QString &videoId)
{
    if (!videoId.startsWith(QLatin1String("yt_id:"))) {
        return std::nullopt;
    }
    const QStringList parts = videoId.split(QLatin1Char(':'));
    if (parts.size() < 3) {
        return std::nullopt;
    }
    Stream stream;
    stream.source.kind = StreamSourceKind::YouTube;
    stream.source.ytId = parts[2];
    stream.raw = QJsonObject{{"ytId", parts[2]}};
    return stream;
}

QJsonObject Stream::toJson() const
{
    QJsonObject object;
    object.insert("kind", source.kindName());
    if (name) object.insert("name", *name);
    if (description) object.insert("description", *description);
    if (thumbnail) object.insert("thumbnail", *thumbnail);
    if (!source.url.isEmpty()) object.insert("url", source.url);
    if (!source.infoHash.isEmpty()) object.insert("infoHash", source.infoHash);
    if (source.fileIdx) object.insert("fileIdx", *source.fileIdx);
    if (!source.ytId.isEmpty()) object.insert("ytId", source.ytId);
    if (!source.externalUrl.isEmpty()) object.insert("externalUrl", source.externalUrl);
    QJsonObject hints;
    if (behaviorHints.notWebReady) hints.insert("notWebReady", true);
    if (behaviorHints.bingeGroup) hints.insert("bingeGroup", *behaviorHints.bingeGroup);
    if (behaviorHints.filename) hints.insert("filename", *behaviorHints.filename);
    if (behaviorHints.videoHash) hints.insert("videoHash", *behaviorHints.videoHash);
    if (behaviorHints.videoSize) hints.insert("videoSize", double(*behaviorHints.videoSize));
    if (behaviorHints.hasProxyHeaders) hints.insert("proxyHeaders", true);
    if (behaviorHints.countryWhitelist) hints.insert("countryWhitelist", QJsonArray::fromStringList(*behaviorHints.countryWhitelist));
    object.insert("behaviorHints", hints);
    if (!subtitles.isEmpty()) {
        QJsonArray list;
        for (const Subtitles &subtitle : subtitles) {
            list.append(subtitle.toJson());
        }
        object.insert("subtitles", list);
    }
    object.insert("raw", raw);
    return object;
}

std::optional<MetaItemPreview> MetaItemPreview::fromJson(const QJsonValue &value, QString *error)
{
    if (!value.isObject()) {
        fail(error, QStringLiteral("meta must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    MetaItemPreview meta;
    if (!json::requiredString(object, QStringLiteral("id"), meta.id, error)
        || !json::requiredString(object, QStringLiteral("type"), meta.type, error)
        || !json::defaultString(object, QStringLiteral("name"), meta.name, error)
        || !json::optionalUrl(object, QStringLiteral("poster"), true, meta.poster, error)
        || !json::optionalUrl(object, QStringLiteral("background"), true, meta.background, error)
        || !json::optionalUrl(object, QStringLiteral("logo"), true, meta.logo, error)
        || !json::optionalString(object, QStringLiteral("description"), meta.description, error)
        || !json::optionalNumberAsString(object, QStringLiteral("releaseInfo"), meta.releaseInfo, error)
        || !json::optionalNumberAsString(object, QStringLiteral("runtime"), meta.runtime, error)
        || !parseReleased(object, QStringLiteral("released"), true, meta.released, error)) {
        return std::nullopt;
    }

    const QJsonValue shape = object.value(QStringLiteral("posterShape"));
    if (shape.isString()) {
        const QString text = shape.toString();
        meta.posterShape = (text == QLatin1String("square") || text == QLatin1String("landscape")) ? text : QStringLiteral("poster");
    } else if (!isMissing(shape)) {
        fail(error, QStringLiteral("\"posterShape\" must be a string"));
        return std::nullopt;
    }

    std::optional<QString> imdbRating;
    QStringList legacyGenres;
    if (!json::optionalNumberAsString(object, QStringLiteral("imdbRating"), imdbRating, error)
        || !json::defaultStringList(object, QStringLiteral("genres"), legacyGenres, error)) {
        return std::nullopt;
    }

    const QJsonValue links = object.value(QStringLiteral("links"));
    if (!isMissing(links)) {
        if (!parseLinks(links, meta.links, error)) {
            return std::nullopt;
        }
        for (const Link &link : std::as_const(meta.links)) {
            if (link.category == QLatin1String("Genres")) {
                meta.genres.append(link.name);
            } else if (link.category == QLatin1String("imdb") && !meta.imdbRating) {
                meta.imdbRating = link.name;
            }
        }
    } else {
        // MetaItemPreviewLegacy -> links from imdbRating and genres.
        meta.imdbRating = imdbRating;
        meta.genres = legacyGenres;
        if (imdbRating) {
            meta.links.append({*imdbRating, QStringLiteral("imdb"),
                               QStringLiteral("https://imdb.com/title/") + meta.id});
        }
        for (const QString &genre : std::as_const(legacyGenres)) {
            meta.links.append({genre, QStringLiteral("Genres"), QString()});
        }
    }

    const QJsonValue trailerStreams = object.value(QStringLiteral("trailerStreams"));
    if (!isMissing(trailerStreams)) {
        if (!parseStreamList(trailerStreams, meta.trailerStreams, false, error)) {
            return std::nullopt;
        }
    } else {
        const QJsonValue trailers = object.value(QStringLiteral("trailers"));
        if (!isMissing(trailers)) {
            if (!trailers.isArray()) {
                fail(error, QStringLiteral("\"trailers\" must be an array"));
                return std::nullopt;
            }
            for (const QJsonValue &item : trailers.toArray()) {
                const QJsonObject trailer = item.toObject();
                if (trailer.value(QStringLiteral("source")).isString() && trailer.value(QStringLiteral("type")).isString()) {
                    if (trailer.value(QStringLiteral("type")).toString() == QLatin1String("Trailer")) {
                        Stream stream;
                        stream.source.kind = StreamSourceKind::YouTube;
                        stream.source.ytId = trailer.value(QStringLiteral("source")).toString();
                        stream.raw = QJsonObject{{"ytId", stream.source.ytId}};
                        meta.trailerStreams.append(stream);
                    }
                    continue;
                }
                auto stream = Stream::fromJson(item);
                if (!stream || stream->source.kind != StreamSourceKind::YouTube) {
                    fail(error, QStringLiteral("unsupported trailer"));
                    return std::nullopt;
                }
                meta.trailerStreams.append(*stream);
            }
        }
    }

    const QJsonValue hints = object.value(QStringLiteral("behaviorHints"));
    if (!isMissing(hints)) {
        if (!hints.isObject()) {
            fail(error, QStringLiteral("\"behaviorHints\" must be an object"));
            return std::nullopt;
        }
        const QJsonObject h = hints.toObject();
        if (!json::defaultBool(h, QStringLiteral("isLive"), meta.behaviorHints.isLive, error)
            || !json::optionalString(h, QStringLiteral("defaultVideoId"), meta.behaviorHints.defaultVideoId, error)
            || !json::optionalString(h, QStringLiteral("featuredVideoId"), meta.behaviorHints.featuredVideoId, error)
            || !json::defaultBool(h, QStringLiteral("hasScheduledVideos"), meta.behaviorHints.hasScheduledVideos, error)) {
            return std::nullopt;
        }
    }
    return meta;
}

QJsonObject MetaItemPreview::toJson() const
{
    QJsonObject object{
        {"id", id},
        {"type", type},
        {"name", name},
        {"posterShape", posterShape},
        {"genres", QJsonArray::fromStringList(genres)},
        {"isLive", isLive()},
    };
    if (!poster.isEmpty()) object.insert("poster", poster);
    if (!background.isEmpty()) object.insert("background", background);
    if (!logo.isEmpty()) object.insert("logo", logo);
    if (description) object.insert("description", *description);
    if (releaseInfo) object.insert("releaseInfo", *releaseInfo);
    if (runtime) object.insert("runtime", *runtime);
    if (released) object.insert("released", dateToJson(released));
    if (imdbRating) object.insert("imdbRating", *imdbRating);
    if (behaviorHints.defaultVideoId) object.insert("defaultVideoId", *behaviorHints.defaultVideoId);
    if (!trailerStreams.isEmpty()) object.insert("trailerStreams", streamsToJson(trailerStreams));
    QJsonArray linkArray;
    for (const Link &link : links) {
        linkArray.append(QJsonObject{{"name", link.name}, {"category", link.category}, {"url", link.url}});
    }
    object.insert("links", linkArray);
    return object;
}

std::optional<Video> Video::fromJson(const QJsonValue &value, QString *error)
{
    if (!value.isObject()) {
        fail(error, QStringLiteral("video must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    Video video;
    if (!json::requiredString(object, QStringLiteral("id"), video.id, error)) {
        return std::nullopt;
    }
    const QString titleKey = object.contains(QStringLiteral("title")) ? QStringLiteral("title") : QStringLiteral("name");
    if (!json::defaultString(object, titleKey, video.title, error)
        || !parseReleased(object, QStringLiteral("released"), false, video.released, error)
        || !json::optionalString(object, QStringLiteral("overview"), video.overview, error)
        || !json::optionalString(object, QStringLiteral("thumbnail"), video.thumbnail, error)) {
        return std::nullopt;
    }
    const QString streamsKey = object.contains(QStringLiteral("streams")) ? QStringLiteral("streams") : QStringLiteral("stream");
    if (!parseStreamList(object.value(streamsKey), video.streams, true, error)
        || !parseStreamList(object.value(QStringLiteral("trailerStreams")), video.trailerStreams, false, error)) {
        return std::nullopt;
    }
    // Flattened Option<SeriesInfo>: present only when both numbers are valid.
    std::optional<qint64> season;
    std::optional<qint64> episode;
    if (json::optionalUnsigned(object, QStringLiteral("season"), std::numeric_limits<quint32>::max(), season, nullptr)
        && json::optionalUnsigned(object, QStringLiteral("episode"), std::numeric_limits<quint32>::max(), episode, nullptr)
        && season && episode) {
        video.seriesInfo = SeriesInfo{int(*season), int(*episode)};
    }
    std::optional<QDateTime> start;
    std::optional<QDateTime> end;
    if (parseReleased(object, QStringLiteral("startTime"), false, start, nullptr)
        && parseReleased(object, QStringLiteral("endTime"), false, end, nullptr) && start && end) {
        video.startTime = start;
        video.endTime = end;
    }
    return video;
}

QJsonObject Video::toJson() const
{
    QJsonObject object{{"id", id}, {"title", title}};
    if (released) object.insert("released", dateToJson(released));
    if (overview) object.insert("overview", *overview);
    if (thumbnail) object.insert("thumbnail", *thumbnail);
    if (seriesInfo) {
        object.insert("season", seriesInfo->season);
        object.insert("episode", seriesInfo->episode);
    }
    if (startTime) object.insert("startTime", dateToJson(startTime));
    if (endTime) object.insert("endTime", dateToJson(endTime));
    if (!streams.isEmpty()) object.insert("streams", streamsToJson(streams));
    return object;
}

void normalizeVideos(QList<Video> &videos)
{
    // UniqueVec by id (first kept).
    QSet<QString> seen;
    QList<Video> unique;
    for (const Video &video : std::as_const(videos)) {
        if (!seen.contains(video.id)) {
            seen.insert(video.id);
            unique.append(video);
        }
    }
    // SortedVec with VideoSortedVecAdapter.
    bool isSeries = false;
    for (const Video &video : std::as_const(unique)) {
        isSeries = isSeries || video.seriesInfo.has_value();
    }
    std::stable_sort(unique.begin(), unique.end(), [isSeries](const Video &a, const Video &b) {
        if (a.seriesInfo && b.seriesInfo) {
            const quint64 aSeason = a.seriesInfo->season == 0 ? std::numeric_limits<quint64>::max() : quint64(a.seriesInfo->season);
            const quint64 bSeason = b.seriesInfo->season == 0 ? std::numeric_limits<quint64>::max() : quint64(b.seriesInfo->season);
            if (aSeason != bSeason) {
                return aSeason < bSeason;
            }
            return a.seriesInfo->episode < b.seriesInfo->episode;
        }
        if (a.seriesInfo) {
            return true;
        }
        if (b.seriesInfo) {
            return false;
        }
        if (a.released && b.released) {
            return isSeries ? *a.released < *b.released : *b.released < *a.released;
        }
        if (a.released) {
            return true;
        }
        return false;
    });
    videos = unique;
}

std::optional<MetaItem> MetaItem::fromJson(const QJsonValue &value, QString *error)
{
    auto preview = MetaItemPreview::fromJson(value, error);
    if (!preview) {
        return std::nullopt;
    }
    MetaItem meta;
    meta.preview = *preview;
    const QJsonValue videos = value.toObject().value(QStringLiteral("videos"));
    if (!isMissing(videos)) {
        if (!videos.isArray()) {
            fail(error, QStringLiteral("\"videos\" must be an array"));
            return std::nullopt;
        }
        for (const QJsonValue &item : videos.toArray()) {
            QString videoError;
            auto video = Video::fromJson(item, &videoError);
            if (!video) {
                fail(error, QStringLiteral("video: %1").arg(videoError));
                return std::nullopt;
            }
            meta.videos.append(*video);
        }
        normalizeVideos(meta.videos);
    }
    return meta;
}

QJsonObject MetaItem::toJson() const
{
    QJsonObject object = preview.toJson();
    QJsonArray list;
    for (const Video &video : videos) {
        list.append(video.toJson());
    }
    object.insert("videos", list);
    return object;
}

std::optional<QString> MetaItem::guessStreamVideoId() const
{
    if (preview.behaviorHints.defaultVideoId) {
        return preview.behaviorHints.defaultVideoId;
    }
    if (videos.isEmpty() || preview.isLive()) {
        return preview.id;
    }
    return std::nullopt;
}

const Video *MetaItem::video(const QString &videoId) const
{
    for (const Video &video : videos) {
        if (video.id == videoId) {
            return &video;
        }
    }
    return nullptr;
}

std::optional<ResourceResponse> ResourceResponse::fromJson(const QJsonValue &value, QString *error)
{
    static const char *const keys[] = {"metas", "metasDetailed", "meta", "streams", "subtitles", "addons"};
    if (!value.isObject()) {
        fail(error, QStringLiteral("Cannot deserialize as ResourceResponse, expected an Object response"));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    int present = 0;
    QString key;
    for (const char *candidate : keys) {
        if (object.contains(QLatin1String(candidate))) {
            ++present;
            key = QLatin1String(candidate);
        }
    }
    if (present == 0) {
        fail(error, QStringLiteral("the response didn't contain any of the required keys: metas, metasDetailed, meta, streams, subtitles, addons"));
        return std::nullopt;
    }
    if (present > 1) {
        fail(error, QStringLiteral("the response contained more than 1 of the unique keys: metas, metasDetailed, meta, streams, subtitles, addons"));
        return std::nullopt;
    }

    ResourceResponse response;
    json::optionalUnsigned(object, QStringLiteral("cacheMaxAge"), std::numeric_limits<qint64>::max(), response.cacheMaxAge, nullptr);
    json::optionalUnsigned(object, QStringLiteral("staleRevalidate"), std::numeric_limits<qint64>::max(), response.staleRevalidate, nullptr);
    json::optionalUnsigned(object, QStringLiteral("staleError"), std::numeric_limits<qint64>::max(), response.staleError, nullptr);

    const QJsonValue content = object.value(key);
    if (key == QLatin1String("meta")) {
        response.kind = Kind::Meta;
        QString metaError;
        auto meta = MetaItem::fromJson(content, &metaError);
        if (!meta) {
            fail(error, QStringLiteral("meta: %1").arg(metaError));
            return std::nullopt;
        }
        response.meta = meta;
        return response;
    }

    // Vector responses: null -> []; a non-array is an error; invalid items
    // are skipped one by one (VecSkipError).
    if (!content.isNull() && !content.isArray()) {
        fail(error, QStringLiteral("invalid type: expected a sequence for \"%1\"").arg(key));
        return std::nullopt;
    }
    const QJsonArray array = content.toArray();
    if (key == QLatin1String("metas")) {
        response.kind = Kind::Metas;
        for (const QJsonValue &item : array) {
            if (auto meta = MetaItemPreview::fromJson(item)) response.metas.append(*meta);
            else ++response.skippedItems;
        }
    } else if (key == QLatin1String("metasDetailed")) {
        response.kind = Kind::MetasDetailed;
        for (const QJsonValue &item : array) {
            if (auto meta = MetaItem::fromJson(item)) response.metasDetailed.append(*meta);
            else ++response.skippedItems;
        }
    } else if (key == QLatin1String("streams")) {
        response.kind = Kind::Streams;
        for (const QJsonValue &item : array) {
            if (auto stream = Stream::fromJson(item)) response.streams.append(*stream);
            else ++response.skippedItems;
        }
    } else if (key == QLatin1String("subtitles")) {
        response.kind = Kind::Subtitles;
        for (const QJsonValue &item : array) {
            if (auto subtitle = Subtitles::fromJson(item)) response.subtitles.append(*subtitle);
            else ++response.skippedItems;
        }
    } else {
        response.kind = Kind::Addons;
        for (const QJsonValue &item : array) {
            if (auto descriptor = Descriptor::fromJson(item.toObject())) response.addons.append(*descriptor);
            else ++response.skippedItems;
        }
    }
    return response;
}

qsizetype ResourceResponse::itemCount() const
{
    switch (kind) {
    case Kind::Metas: return metas.size();
    case Kind::MetasDetailed: return metasDetailed.size();
    case Kind::Meta: return meta ? 1 : 0;
    case Kind::Streams: return streams.size();
    case Kind::Subtitles: return subtitles.size();
    case Kind::Addons: return addons.size();
    }
    return 0;
}

} // namespace stremio
