#pragma once

// Addon manifest and descriptor.
// Port of stremio-core src/types/addon/manifest.rs and descriptor.rs.

#include "stremio/common.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace stremio {

// ManifestResource: either the short form ("stream") or the full form
// ({ name, types?, idPrefixes? }). For the full form a missing `types` means
// "no types" and a missing `idPrefixes` means "every id" (see
// Manifest::isResourceSupported).
struct ManifestResource
{
    QString name;
    bool full = false;
    std::optional<QStringList> types;
    std::optional<QStringList> idPrefixes;
};

struct ManifestCatalog
{
    QString id;
    QString type;
    std::optional<QString> name;
    QList<ExtraProp> extra;
    // true when declared with the legacy extraRequired/extraSupported keys.
    bool shortExtraForm = false;

    const ExtraProp *extraProp(const QString &name) const;
    bool hasExtra(const QString &name) const { return extraProp(name) != nullptr; }
    // manifest.rs ManifestCatalog::is_extra_supported
    bool isExtraSupported(const Extra &extra) const;
    bool areExtraNamesSupported(const QStringList &names) const;
    // manifest.rs default_required_extra: first option of every required prop,
    // or nullopt when a required prop has no options.
    std::optional<Extra> defaultRequiredExtra() const;
    bool isEpgGuide() const { return hasExtra(QStringLiteral("date")); }
    QString displayName() const { return name.value_or(id); }

    QJsonObject toJson() const;
};

struct ManifestBehaviorHints
{
    bool adult = false;
    bool p2p = false;
    bool configurable = false;
    bool configurationRequired = false;
    bool epgProvider = false;
};

struct Manifest
{
    QString id;
    QString version;
    QString name;
    std::optional<QString> contactEmail;
    std::optional<QString> description;
    QString logo;       // empty when missing or not a valid absolute URL
    QString background; // same
    QStringList types;
    QList<ManifestResource> resources;
    std::optional<QStringList> idPrefixes;
    QList<ManifestCatalog> catalogs;
    QList<ManifestCatalog> addonCatalogs;
    ManifestBehaviorHints behaviorHints;
    // The manifest exactly as the addon served it (persisted and re-parsed).
    QJsonObject raw;

    // Parses a manifest. With `preview` set, only the ManifestPreview fields
    // are required (used for addon_catalog responses).
    static std::optional<Manifest> fromJson(const QJsonObject &object, QString *error = nullptr, bool preview = false);

    // manifest.rs Manifest::is_resource_supported
    bool isResourceSupported(const ResourcePath &path) const;

    bool hasResource(const QString &name) const;
    const ManifestResource *resource(const QString &name) const;
};

struct DescriptorFlags
{
    bool official = false;
    bool protectedAddon = false;
};

// descriptor.rs Descriptor: { manifest, transportUrl, flags }
struct Descriptor
{
    Manifest manifest;
    QString transportUrl;
    DescriptorFlags flags;

    QJsonObject toJson() const;
    static std::optional<Descriptor> fromJson(const QJsonObject &object, QString *error = nullptr, bool preview = false);
};

} // namespace stremio
