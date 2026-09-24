#pragma once

// Decides which addons receive which requests.
// Port of stremio-core src/types/addon/request.rs (AggrRequest::plan),
// src/models/catalog_with_filters.rs (selectable_update) and
// src/models/common/compare_with_priorities.rs.

#include "stremio/common.h"
#include "stremio/manifest.h"

#include <QList>

#include <optional>

namespace stremio {

struct PlannedRequest
{
    int addonIndex = -1; // index into the addon list passed to the planner
    ResourceRequest request;
};

// AggrRequest::AllCatalogs { extra, type }: every catalog of every addon (in
// addon order, then catalog order) whose extras accept `extra`.
QList<PlannedRequest> planAllCatalogs(const QList<Descriptor> &addons, const Extra &extra,
                                      const std::optional<QString> &type = std::nullopt);

// AggrRequest::AllOfResource(path): the same path for every addon that
// supports it according to Manifest::isResourceSupported.
QList<PlannedRequest> planAllOfResource(const QList<Descriptor> &addons, const ResourcePath &path);

// constants.rs TYPE_PRIORITIES + compare_with_priorities, used to order types.
// Returns <0 when a should come before b (higher priority first).
int compareTypes(const QString &a, const QString &b);

// catalog_with_filters.rs: the pieces of the Discover selection.
struct SelectableCatalog
{
    QString name;
    bool isEpgGuide = false;
    ResourceRequest request; // with default_required_extra
};

struct SelectableExtraOption
{
    std::optional<QString> value; // nullopt = "none" option of a non-required extra
    bool selected = false;
    ResourceRequest request;
};

struct SelectableExtra
{
    QString name;
    bool isRequired = false;
    QList<SelectableExtraOption> options;
};

struct DiscoverSelectable
{
    QStringList types;                     // ordered by TYPE_PRIORITIES
    QList<SelectableCatalog> catalogs;     // filtered to the selected type
    QList<SelectableExtra> extra;          // of the selected catalog
    std::optional<ResourceRequest> nextPage;
};

// All catalogs that Discover can show (catalog resource), in addon order.
QList<SelectableCatalog> selectableCatalogs(const QList<Descriptor> &addons, const QString &resource = QString::fromLatin1(kCatalogResource));

// Builds the selectable lists for `selected` (the first type's first catalog
// when nullopt). `loadedPageSizes` are the item counts of the pages already
// loaded for the selected request, used for the next-page skip; pass an
// empty list when nothing is loaded yet.
DiscoverSelectable discoverSelectable(const QList<Descriptor> &addons,
                                      const std::optional<ResourceRequest> &selected,
                                      const QList<int> &loadedPageSizes);

// The request Discover opens by default (first type, first catalog).
std::optional<ResourceRequest> defaultDiscoverRequest(const QList<Descriptor> &addons);

} // namespace stremio
