#pragma once

// Addon responses: meta previews, meta items, videos, streams, subtitles.
// Port of stremio-core src/types/addon/response.rs and
// src/types/resource/{meta_item,stream,subtitles}.rs.

#include "stremio/common.h"
#include "stremio/manifest.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <optional>

namespace stremio {

struct Subtitles
{
    QString id;
    QString lang;
    QString url;
    std::optional<QString> label;
    QStringList fonts;

    static std::optional<Subtitles> fromJson(const QJsonValue &value, QString *error = nullptr);
    QJsonObject toJson() const;
};

enum class StreamSourceKind {
    Url,
    YouTube,
    Rar,
    Zip,
    Zip7,
    Tgz,
    Tar,
    Nzb,
    Torrent,
    PlayerFrame,
    External,
};

struct ArchiveUrl
{
    QString url;
    std::optional<qint64> bytes;
};

// stream.rs StreamSource (untagged; the first matching form wins).
struct StreamSource
{
    StreamSourceKind kind = StreamSourceKind::Url;
    QString url;               // Url; Nzb: nzbUrl; PlayerFrame: playerFrameUrl
    QString ytId;              // YouTube
    QList<ArchiveUrl> archiveUrls; // Rar/Zip/7zip/Tgz/Tar
    std::optional<int> fileIdx;    // archives, torrent
    QStringList fileMustInclude;   // archives, torrent
    QStringList nzbUrls;
    QStringList servers;           // Nzb
    QString infoHash;              // Torrent, lower-case hex
    QStringList announce;          // Torrent ("announce", alias "sources")
    QString externalUrl;
    QString androidTvUrl;
    QString tizenUrl;
    QString webosUrl;

    bool isMagnet() const;
    QString kindName() const;
};

struct StreamBehaviorHints
{
    bool notWebReady = false;
    std::optional<QString> bingeGroup;
    std::optional<QStringList> countryWhitelist;
    QList<QPair<QString, QString>> proxyRequestHeaders;
    QList<QPair<QString, QString>> proxyResponseHeaders;
    bool hasProxyHeaders = false;
    std::optional<QString> filename;
    std::optional<QString> videoHash;
    std::optional<qint64> videoSize;
};

struct Stream
{
    StreamSource source;
    std::optional<QString> name;
    std::optional<QString> description; // "description" or its alias "title"
    std::optional<QString> thumbnail;
    QList<Subtitles> subtitles;
    StreamBehaviorHints behaviorHints;
    QJsonObject raw;

    static std::optional<Stream> fromJson(const QJsonValue &value, QString *error = nullptr);
    // stream.rs Stream::youtube: "yt_id:CHANNEL:VIDEO" ids play the YouTube video.
    static std::optional<Stream> youtubeFromVideoId(const QString &videoId);
    // Summary for the UI (normalised fields plus the raw stream).
    QJsonObject toJson() const;
};

struct Link
{
    QString name;
    QString category;
    QString url;
};

struct MetaItemBehaviorHints
{
    bool isLive = false;
    std::optional<QString> defaultVideoId;
    std::optional<QString> featuredVideoId;
    bool hasScheduledVideos = false;
};

// meta_item.rs MetaItemPreview (built from MetaItemPreviewLegacy).
struct MetaItemPreview
{
    QString id;
    QString type;
    QString name;
    QString poster;
    QString background;
    QString logo;
    std::optional<QString> description;
    std::optional<QString> releaseInfo;
    std::optional<QString> runtime;
    std::optional<QDateTime> released;
    QString posterShape = QStringLiteral("poster"); // square | landscape | poster
    QList<Link> links;
    QStringList genres;                  // "Genres" links (or legacy genres)
    std::optional<QString> imdbRating;   // "imdb" link name (or legacy imdbRating)
    QList<Stream> trailerStreams;
    MetaItemBehaviorHints behaviorHints;

    static std::optional<MetaItemPreview> fromJson(const QJsonValue &value, QString *error = nullptr);
    QJsonObject toJson() const;
    bool isLive() const { return behaviorHints.isLive || type == QLatin1String("tv"); }
};

struct SeriesInfo
{
    int season = 0;
    int episode = 0;
};

struct Video
{
    QString id;
    QString title; // "title" or its alias "name"
    std::optional<QDateTime> released;
    std::optional<QString> overview;
    std::optional<QString> thumbnail;
    QList<Stream> streams; // "streams" or alias "stream", one or many
    std::optional<SeriesInfo> seriesInfo;
    QList<Stream> trailerStreams;
    // EPG programme fields (startTime/endTime) are kept for display only.
    std::optional<QDateTime> startTime;
    std::optional<QDateTime> endTime;

    static std::optional<Video> fromJson(const QJsonValue &value, QString *error = nullptr);
    QJsonObject toJson() const;
};

struct MetaItem
{
    MetaItemPreview preview;
    QList<Video> videos; // unique by id, sorted like stremio-core

    static std::optional<MetaItem> fromJson(const QJsonValue &value, QString *error = nullptr);
    QJsonObject toJson() const;

    // meta_details.rs selected_guess_stream_update: the video id to request
    // streams for without asking the user, if any.
    std::optional<QString> guessStreamVideoId() const;
    const Video *video(const QString &videoId) const;
};

// response.rs ResourceResponse: exactly one of the keys below.
struct ResourceResponse
{
    enum class Kind { Metas, MetasDetailed, Meta, Streams, Subtitles, Addons };
    Kind kind = Kind::Metas;
    QList<MetaItemPreview> metas;
    QList<MetaItem> metasDetailed;
    std::optional<MetaItem> meta;
    QList<Stream> streams;
    QList<Subtitles> subtitles;
    QList<Descriptor> addons; // addon_catalog responses (manifest previews)
    std::optional<qint64> cacheMaxAge;
    std::optional<qint64> staleRevalidate;
    std::optional<qint64> staleError;
    int skippedItems = 0; // invalid array items dropped (VecSkipError)

    static std::optional<ResourceResponse> fromJson(const QJsonValue &value, QString *error = nullptr);
    // Number of items in the vector content (for EmptyContent / paging).
    qsizetype itemCount() const;
};

// Sorting and de-duplication applied to MetaItem.videos (meta_item.rs).
void normalizeVideos(QList<Video> &videos);

QDateTime parseRfc3339(const QString &text);

} // namespace stremio
