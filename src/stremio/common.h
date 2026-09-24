#pragma once

// Core request types of the Stremio addon protocol.
// Port of stremio-core src/types/addon/request.rs (development @ 88be65b).

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace stremio {

inline constexpr auto kCatalogResource = "catalog";
inline constexpr auto kMetaResource = "meta";
inline constexpr auto kStreamResource = "stream";
inline constexpr auto kSubtitlesResource = "subtitles";
inline constexpr auto kAddonCatalogResource = "addon_catalog";
inline constexpr auto kManifestPath = "/manifest.json";
inline constexpr auto kLegacyPath = "/stremio/v1";

inline constexpr auto kSearchExtra = "search";
inline constexpr auto kSkipExtra = "skip";
inline constexpr auto kGenreExtra = "genre";
inline constexpr auto kVideoHashExtra = "videoHash";
inline constexpr auto kVideoSizeExtra = "videoSize";
inline constexpr auto kVideoFilenameExtra = "filename";

// Stremio's standard catalog page size (constants.rs CATALOG_PAGE_SIZE).
inline constexpr int kCatalogPageSize = 100;

// manifest.rs ExtraProp. optionsLimit defaults to 1.
struct ExtraProp
{
    QString name;
    bool isRequired = false;
    QStringList options;
    int optionsLimit = 1;

    static ExtraProp named(const QString &name, int optionsLimit = 1)
    {
        ExtraProp prop;
        prop.name = name;
        prop.optionsLimit = optionsLimit;
        return prop;
    }
};

// request.rs ExtraValue, serialised as a [name, value] pair.
struct ExtraValue
{
    QString name;
    QString value;

    bool operator==(const ExtraValue &other) const = default;
};

using Extra = QList<ExtraValue>;

// request.rs ExtraExt::remove_all
Extra removeAll(const Extra &extra, const ExtraProp &prop);
// request.rs ExtraExt::extend_one (including its ordering: the changed
// property's values come first, the other properties keep their order).
Extra extendOne(const Extra &extra, const ExtraProp &prop, const std::optional<QString> &value);
std::optional<QString> firstExtraValue(const Extra &extra, const QString &name);

QJsonArray extraToJson(const Extra &extra);
Extra extraFromJson(const QJsonValue &value);

// request.rs ResourcePath: /{resource}/{type}/{id}[/{extra}].json
struct ResourcePath
{
    QString resource;
    QString type;
    QString id;
    Extra extra;

    bool operator==(const ResourcePath &other) const = default;
    bool eqNoExtra(const ResourcePath &other) const
    {
        return resource == other.resource && type == other.type && id == other.id;
    }

    QJsonObject toJson() const;
    static ResourcePath fromJson(const QJsonObject &object);
};

// request.rs ResourceRequest: an addon transport URL plus a path.
struct ResourceRequest
{
    QString base;
    ResourcePath path;

    bool operator==(const ResourceRequest &other) const = default;

    QJsonObject toJson() const;
    static ResourceRequest fromJson(const QJsonObject &object);
    // Stable key for caches and result slots.
    QString key() const;
};

} // namespace stremio
