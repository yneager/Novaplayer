#pragma once

// Request planning over the enabled addons, and JSON views of the results for
// the Vui pages. The model logic comes from stremio-core:
// - Board / Search: models/catalogs_with_extra.rs
// - Discover: models/catalog_with_filters.rs
// - Details: models/meta_details.rs (+ stremio-core-web serialize_meta_details)
// - Subtitles: models/player.rs subtitles_update
// Each planned request becomes one independent result slot, in addon order
// (models/common/resource_loadable.rs); one failing addon never affects
// another slot.

#include "stremio/addonclient.h"
#include "stremio/addonmanager.h"
#include "stremio/capabilities.h"
#include "stremio/videoparams.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <optional>

namespace stremio {

struct CatalogRowInfo
{
    Descriptor addon;
    ManifestCatalog catalog;
    ResourceRequest request;

    QJsonObject toJson() const;
};

class ContentService final : public QObject
{
    Q_OBJECT

public:
    ContentService(AddonManager *addons, AddonClient *client, QObject *parent = nullptr);

    AddonClient *client() const { return client_; }
    AddonManager *addons() const { return addons_; }

    // CatalogsWithExtra { extra: [] } and { extra: [search=query] }.
    QList<CatalogRowInfo> board() const;
    QList<CatalogRowInfo> search(const QString &query) const;
    // catalogs_with_extra.rs LoadNextPage: only when the catalog declares
    // "skip"; skip = previous skip + items of the last page.
    std::optional<ResourceRequest> nextPage(const ResourceRequest &lastPage, int lastPageItems) const;
    // catalog_with_filters.rs selectable_update, serialised for the UI.
    QJsonObject discover(const std::optional<ResourceRequest> &selected, const QList<int> &loadedPageSizes) const;
    // addon_catalog catalogs of the installed addons (addon discovery).
    QList<CatalogRowInfo> addonCatalogs() const;

    QList<PlannedRequest> metaPlan(const QString &type, const QString &id) const;
    // stream/{meta type}/{video id}
    QList<PlannedRequest> streamPlan(const QString &type, const QString &videoId) const;
    // subtitles/{meta type}/{video id} with videoHash, videoSize, filename
    // added by extend_one in that order (nothing is requested when the
    // params are all unknown, as in stremio-core).
    QList<PlannedRequest> subtitlePlan(const QString &type, const QString &videoId, const VideoParams &params) const;

    const Descriptor *addon(const QString &transportUrl) const;
    QList<Descriptor> activeAddons() const { return addons_->activeDescriptors(); }

    // {status: ready|empty|error, error, fromCache, request, addon, …content}
    static QJsonObject resultJson(const ResourceResult &result);
    static QJsonObject addonJson(const Descriptor &addon);
    static QJsonObject descriptorSummary(const Descriptor &addon);

private:
    AddonManager *addons_;
    AddonClient *client_;
};

} // namespace stremio
