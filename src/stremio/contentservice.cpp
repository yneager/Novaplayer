#include "stremio/contentservice.h"

#include "stremio/addonurl.h"

#include <algorithm>

namespace stremio {

namespace {

QJsonValue optionalString(const std::optional<QString> &value)
{
    return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

QJsonValue optionalList(const std::optional<QStringList> &value)
{
    return value ? QJsonValue(QJsonArray::fromStringList(*value)) : QJsonValue(QJsonValue::Null);
}

} // namespace

QJsonObject CatalogRowInfo::toJson() const
{
    QJsonObject object{
        {"key", request.key()},
        {"request", request.toJson()},
        {"addon", ContentService::addonJson(addon)},
        {"catalog", catalog.toJson()},
        {"name", catalog.displayName()},
        {"type", catalog.type},
        {"supportsSkip", catalog.hasExtra(QString::fromLatin1(kSkipExtra))},
    };
    return object;
}

ContentService::ContentService(AddonManager *addons, AddonClient *client, QObject *parent)
    : QObject(parent)
    , addons_(addons)
    , client_(client)
{
}

const Descriptor *ContentService::addon(const QString &transportUrl) const
{
    const InstalledAddon *installed = addons_->find(transportUrl);
    return installed ? &installed->descriptor : nullptr;
}

QList<CatalogRowInfo> ContentService::board() const
{
    const QList<Descriptor> addons = activeAddons();
    QList<CatalogRowInfo> rows;
    for (const PlannedRequest &planned : planAllCatalogs(addons, {})) {
        const Descriptor &addon = addons[planned.addonIndex];
        for (const ManifestCatalog &catalog : addon.manifest.catalogs) {
            if (catalog.id == planned.request.path.id && catalog.type == planned.request.path.type) {
                rows.append({addon, catalog, planned.request});
                break;
            }
        }
    }
    return rows;
}

QList<CatalogRowInfo> ContentService::search(const QString &query) const
{
    const QList<Descriptor> addons = activeAddons();
    QList<CatalogRowInfo> rows;
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        return rows;
    }
    for (const PlannedRequest &planned : planAllCatalogs(addons, {{QString::fromLatin1(kSearchExtra), trimmed}})) {
        const Descriptor &addon = addons[planned.addonIndex];
        for (const ManifestCatalog &catalog : addon.manifest.catalogs) {
            if (catalog.id == planned.request.path.id && catalog.type == planned.request.path.type) {
                rows.append({addon, catalog, planned.request});
                break;
            }
        }
    }
    return rows;
}

std::optional<ResourceRequest> ContentService::nextPage(const ResourceRequest &lastPage, int lastPageItems) const
{
    if (lastPageItems <= 0) {
        return std::nullopt;
    }
    const Descriptor *descriptor = addon(lastPage.base);
    if (!descriptor) {
        return std::nullopt;
    }
    const bool declaresSkip = std::any_of(descriptor->manifest.catalogs.cbegin(), descriptor->manifest.catalogs.cend(),
                                          [&](const ManifestCatalog &catalog) {
                                              return catalog.id == lastPage.path.id && catalog.type == lastPage.path.type
                                                  && catalog.hasExtra(QString::fromLatin1(kSkipExtra));
                                          });
    if (!declaresSkip) {
        return std::nullopt;
    }
    bool ok = false;
    int skip = firstExtraValue(lastPage.path.extra, QString::fromLatin1(kSkipExtra)).value_or(QString()).toInt(&ok);
    if (!ok) {
        skip = 0;
    }
    ResourceRequest next = lastPage;
    next.path.extra = extendOne(lastPage.path.extra, ExtraProp::named(QString::fromLatin1(kSkipExtra)),
                                QString::number(skip + lastPageItems));
    return next;
}

QJsonObject ContentService::discover(const std::optional<ResourceRequest> &selectedIn, const QList<int> &loadedPageSizes) const
{
    const QList<Descriptor> addons = activeAddons();
    std::optional<ResourceRequest> selected = selectedIn;
    if (selected) {
        // selected_update strips "skip" from the selection.
        selected->path.extra = removeAll(selected->path.extra, ExtraProp::named(QString::fromLatin1(kSkipExtra)));
    } else {
        selected = defaultDiscoverRequest(addons);
    }
    const DiscoverSelectable selectable = discoverSelectable(addons, selected, loadedPageSizes);

    QJsonArray catalogs;
    for (const SelectableCatalog &catalog : selectable.catalogs) {
        const Descriptor *owner = nullptr;
        for (const Descriptor &candidate : addons) {
            if (candidate.transportUrl == catalog.request.base) {
                owner = &candidate;
                break;
            }
        }
        catalogs.append(QJsonObject{
            {"name", catalog.name},
            {"addonName", owner ? owner->manifest.name : QString()},
            {"request", catalog.request.toJson()},
            {"selected", selected && selected->base == catalog.request.base && selected->path.id == catalog.request.path.id
                             && selected->path.resource == catalog.request.path.resource},
        });
    }
    // SelectableType.request: the first selectable catalog of each type
    // (catalog_with_filters.rs unique_by type keeps the first request).
    QJsonArray types;
    const QList<SelectableCatalog> all = selectableCatalogs(addons);
    for (const QString &type : selectable.types) {
        for (const SelectableCatalog &catalog : all) {
            if (catalog.request.path.type == type) {
                types.append(QJsonObject{
                    {"type", type},
                    {"request", catalog.request.toJson()},
                    {"selected", selected && selected->path.type == type},
                });
                break;
            }
        }
    }
    QJsonArray extra;
    for (const SelectableExtra &item : selectable.extra) {
        QJsonArray options;
        for (const SelectableExtraOption &option : item.options) {
            options.append(QJsonObject{
                {"value", option.value ? QJsonValue(*option.value) : QJsonValue(QJsonValue::Null)},
                {"selected", option.selected},
                {"request", option.request.toJson()},
            });
        }
        extra.append(QJsonObject{{"name", item.name}, {"isRequired", item.isRequired}, {"options", options}});
    }
    return {
        {"types", types},
        {"selected", selected ? QJsonValue(selected->toJson()) : QJsonValue(QJsonValue::Null)},
        {"catalogs", catalogs},
        {"extra", extra},
        {"nextPage", selectable.nextPage ? QJsonValue(selectable.nextPage->toJson()) : QJsonValue(QJsonValue::Null)},
    };
}

QList<CatalogRowInfo> ContentService::addonCatalogs() const
{
    QList<CatalogRowInfo> rows;
    for (const Descriptor &addon : activeAddons()) {
        for (const ManifestCatalog &catalog : addon.manifest.addonCatalogs) {
            const std::optional<Extra> extra = catalog.defaultRequiredExtra();
            if (!extra) {
                continue;
            }
            rows.append({addon, catalog,
                         {addon.transportUrl, {QString::fromLatin1(kAddonCatalogResource), catalog.type, catalog.id, *extra}}});
        }
    }
    return rows;
}

QList<PlannedRequest> ContentService::metaPlan(const QString &type, const QString &id) const
{
    return planAllOfResource(activeAddons(), {QString::fromLatin1(kMetaResource), type, id, {}});
}

QList<PlannedRequest> ContentService::streamPlan(const QString &type, const QString &videoId) const
{
    return planAllOfResource(activeAddons(), {QString::fromLatin1(kStreamResource), type, videoId, {}});
}

QList<PlannedRequest> ContentService::subtitlePlan(const QString &type, const QString &videoId,
                                                   const VideoParams &params) const
{
    if (params.isEmpty()) {
        return {};
    }
    Extra extra;
    extra = extendOne(extra, ExtraProp::named(QString::fromLatin1(kVideoHashExtra)), params.hash);
    extra = extendOne(extra, ExtraProp::named(QString::fromLatin1(kVideoSizeExtra)),
                      params.size ? std::optional<QString>(QString::number(*params.size)) : std::nullopt);
    extra = extendOne(extra, ExtraProp::named(QString::fromLatin1(kVideoFilenameExtra)), params.filename);
    return planAllOfResource(activeAddons(), {QString::fromLatin1(kSubtitlesResource), type, videoId, extra});
}

QJsonObject ContentService::addonJson(const Descriptor &addon)
{
    QJsonObject object{
        {"transportUrl", addon.transportUrl},
        {"id", addon.manifest.id},
        {"name", addon.manifest.name},
        {"version", addon.manifest.version},
    };
    if (!addon.manifest.logo.isEmpty()) {
        object.insert("logo", addon.manifest.logo);
    }
    return object;
}

QJsonObject ContentService::descriptorSummary(const Descriptor &addon)
{
    const Manifest &manifest = addon.manifest;
    QJsonArray resources;
    for (const ManifestResource &resource : manifest.resources) {
        resources.append(QJsonObject{
            {"name", resource.name},
            {"types", resource.full ? optionalList(resource.types) : QJsonValue(QJsonArray::fromStringList(manifest.types))},
            {"idPrefixes", resource.full ? optionalList(resource.idPrefixes) : optionalList(manifest.idPrefixes)},
        });
    }
    QJsonArray catalogs;
    for (const ManifestCatalog &catalog : manifest.catalogs) {
        catalogs.append(catalog.toJson());
    }
    QJsonArray addonCatalogs;
    for (const ManifestCatalog &catalog : manifest.addonCatalogs) {
        addonCatalogs.append(catalog.toJson());
    }
    QJsonObject object = addonJson(addon);
    object.insert("description", optionalString(manifest.description));
    object.insert("background", manifest.background);
    object.insert("types", QJsonArray::fromStringList(manifest.types));
    object.insert("idPrefixes", optionalList(manifest.idPrefixes));
    object.insert("resources", resources);
    object.insert("catalogs", catalogs);
    object.insert("addonCatalogs", addonCatalogs);
    object.insert("behaviorHints", QJsonObject{
        {"adult", manifest.behaviorHints.adult},
        {"p2p", manifest.behaviorHints.p2p},
        {"configurable", manifest.behaviorHints.configurable},
        {"configurationRequired", manifest.behaviorHints.configurationRequired},
    });
    object.insert("official", addon.flags.official);
    if (manifest.behaviorHints.configurable || manifest.behaviorHints.configurationRequired) {
        object.insert("configureUrl", configureUrl(addon.transportUrl));
    }
    return object;
}

QJsonObject ContentService::resultJson(const ResourceResult &result)
{
    QJsonObject object{{"request", result.request.toJson()}, {"fromCache", result.fromCache}};
    if (result.error.isError()) {
        object.insert("status", QStringLiteral("error"));
        object.insert("error", result.error.toString());
        return object;
    }
    const ResourceResponse &response = *result.response;
    const bool empty = response.kind != ResourceResponse::Kind::Meta && response.itemCount() == 0;
    object.insert("status", empty ? QStringLiteral("empty") : QStringLiteral("ready"));
    if (response.skippedItems > 0) {
        object.insert("skippedItems", response.skippedItems);
    }
    switch (response.kind) {
    case ResourceResponse::Kind::Metas: {
        QJsonArray metas;
        for (const MetaItemPreview &meta : response.metas) metas.append(meta.toJson());
        object.insert("metas", metas);
        break;
    }
    case ResourceResponse::Kind::MetasDetailed: {
        QJsonArray metas;
        for (const MetaItem &meta : response.metasDetailed) metas.append(meta.toJson());
        object.insert("metas", metas);
        break;
    }
    case ResourceResponse::Kind::Meta:
        object.insert("meta", response.meta->toJson());
        break;
    case ResourceResponse::Kind::Streams: {
        QJsonArray streams;
        for (const Stream &stream : response.streams) streams.append(stream.toJson());
        object.insert("streams", streams);
        break;
    }
    case ResourceResponse::Kind::Subtitles: {
        QJsonArray subtitles;
        for (const Subtitles &subtitle : response.subtitles) subtitles.append(subtitle.toJson());
        object.insert("subtitles", subtitles);
        break;
    }
    case ResourceResponse::Kind::Addons: {
        QJsonArray addons;
        for (const Descriptor &descriptor : response.addons) addons.append(descriptorSummary(descriptor));
        object.insert("addons", addons);
        break;
    }
    }
    return object;
}

} // namespace stremio
