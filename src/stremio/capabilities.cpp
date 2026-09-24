#include "stremio/capabilities.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <limits>

namespace stremio {

QList<PlannedRequest> planAllCatalogs(const QList<Descriptor> &addons, const Extra &extra,
                                      const std::optional<QString> &type)
{
    QList<PlannedRequest> planned;
    for (int i = 0; i < addons.size(); ++i) {
        const Descriptor &addon = addons[i];
        for (const ManifestCatalog &catalog : addon.manifest.catalogs) {
            if (!catalog.isExtraSupported(extra)) {
                continue;
            }
            if (type && catalog.type != *type) {
                continue;
            }
            planned.append({i, {addon.transportUrl,
                                {QString::fromLatin1(kCatalogResource), catalog.type, catalog.id, extra}}});
        }
    }
    return planned;
}

QList<PlannedRequest> planAllOfResource(const QList<Descriptor> &addons, const ResourcePath &path)
{
    QList<PlannedRequest> planned;
    for (int i = 0; i < addons.size(); ++i) {
        if (addons[i].manifest.isResourceSupported(path)) {
            planned.append({i, {addons[i].transportUrl, path}});
        }
    }
    return planned;
}

namespace {

std::optional<int> typePriority(const QString &type)
{
    static const QHash<QString, int> priorities{
        {QStringLiteral("all"), 5},
        {QStringLiteral("movie"), 4},
        {QStringLiteral("series"), 3},
        {QStringLiteral("channel"), 2},
        {QStringLiteral("tv"), 1},
        {QStringLiteral("other"), std::numeric_limits<int>::min()},
    };
    const auto it = priorities.constFind(type);
    if (it == priorities.cend()) {
        return std::nullopt;
    }
    return *it;
}

// compare_with_priorities(a, b): negative = Less, positive = Greater.
int corePriorityCompare(const QString &a, const QString &b)
{
    const std::optional<int> pa = typePriority(a);
    const std::optional<int> pb = typePriority(b);
    if (pa && pb) {
        return *pa < *pb ? -1 : (*pa > *pb ? 1 : 0);
    }
    if (pa) {
        return *pa == std::numeric_limits<int>::min() ? -1 : 1;
    }
    if (pb) {
        return *pb == std::numeric_limits<int>::min() ? 1 : -1;
    }
    return b.compare(a); // b.cmp(a)
}

} // namespace

int compareTypes(const QString &a, const QString &b)
{
    // Stremio sorts ascending with compare_with_priorities and then reverses.
    return -corePriorityCompare(a, b);
}

QList<SelectableCatalog> selectableCatalogs(const QList<Descriptor> &addons, const QString &resource)
{
    QList<SelectableCatalog> result;
    const bool isCatalog = resource == QLatin1String(kCatalogResource);
    for (const Descriptor &addon : addons) {
        const QList<ManifestCatalog> &catalogs = isCatalog ? addon.manifest.catalogs : addon.manifest.addonCatalogs;
        for (const ManifestCatalog &catalog : catalogs) {
            const std::optional<Extra> extra = catalog.defaultRequiredExtra();
            if (!extra) {
                continue;
            }
            SelectableCatalog selectable;
            selectable.name = catalog.displayName();
            selectable.isEpgGuide = isCatalog && addon.manifest.behaviorHints.epgProvider && catalog.isEpgGuide();
            selectable.request = {addon.transportUrl, {resource, catalog.type, catalog.id, *extra}};
            result.append(selectable);
        }
    }
    return result;
}

std::optional<ResourceRequest> defaultDiscoverRequest(const QList<Descriptor> &addons)
{
    const DiscoverSelectable selectable = discoverSelectable(addons, std::nullopt, {});
    if (selectable.types.isEmpty()) {
        return std::nullopt;
    }
    for (const SelectableCatalog &catalog : selectableCatalogs(addons)) {
        if (catalog.request.path.type == selectable.types.first()) {
            return catalog.request;
        }
    }
    return std::nullopt;
}

DiscoverSelectable discoverSelectable(const QList<Descriptor> &addons,
                                      const std::optional<ResourceRequest> &selected,
                                      const QList<int> &loadedPageSizes)
{
    DiscoverSelectable result;
    const QList<SelectableCatalog> all = selectableCatalogs(addons);

    QSet<QString> seenTypes;
    for (const SelectableCatalog &catalog : all) {
        if (!seenTypes.contains(catalog.request.path.type)) {
            seenTypes.insert(catalog.request.path.type);
            result.types.append(catalog.request.path.type);
        }
    }
    std::stable_sort(result.types.begin(), result.types.end(), [](const QString &a, const QString &b) {
        return compareTypes(a, b) < 0;
    });

    for (const SelectableCatalog &catalog : all) {
        if (!selected || catalog.request.path.type == selected->path.type) {
            result.catalogs.append(catalog);
        }
    }

    if (!selected || selected->path.resource != QLatin1String(kCatalogResource)) {
        return result;
    }
    const Descriptor *addon = nullptr;
    for (const Descriptor &candidate : addons) {
        if (candidate.transportUrl == selected->base) {
            addon = &candidate;
            break;
        }
    }
    if (!addon) {
        return result;
    }
    const ManifestCatalog *catalog = nullptr;
    for (const ManifestCatalog &candidate : addon->manifest.catalogs) {
        if (candidate.id == selected->path.id && candidate.type == selected->path.type) {
            catalog = &candidate;
            break;
        }
    }
    if (!catalog) {
        return result;
    }

    auto requestWith = [&](const Extra &extra) {
        return ResourceRequest{selected->base, {selected->path.resource, catalog->type, catalog->id, extra}};
    };

    for (const ExtraProp &prop : catalog->extra) {
        if (prop.name == QLatin1String(kSkipExtra) || prop.options.isEmpty()) {
            continue;
        }
        SelectableExtra selectableExtra;
        selectableExtra.name = prop.name;
        selectableExtra.isRequired = prop.isRequired;
        if (!prop.isRequired) {
            SelectableExtraOption none;
            none.selected = !firstExtraValue(selected->path.extra, prop.name).has_value();
            none.request = requestWith(extendOne(selected->path.extra, prop, std::nullopt));
            selectableExtra.options.append(none);
        }
        for (const QString &option : prop.options) {
            SelectableExtraOption item;
            item.value = option;
            for (const ExtraValue &value : selected->path.extra) {
                item.selected = item.selected || (value.name == prop.name && value.value == option);
            }
            item.request = requestWith(extendOne(selected->path.extra, prop, option));
            selectableExtra.options.append(item);
        }
        result.extra.append(selectableExtra);
    }

    if (catalog->hasExtra(QString::fromLatin1(kSkipExtra)) && !loadedPageSizes.isEmpty()) {
        int skip = 0;
        bool allReady = true;
        for (const int size : loadedPageSizes) {
            if (size <= 0) {
                allReady = false;
                break;
            }
            skip += size;
        }
        if (allReady) {
            result.nextPage = requestWith(extendOne(selected->path.extra, ExtraProp::named(QString::fromLatin1(kSkipExtra)),
                                                    QString::number(skip)));
        }
    }
    return result;
}

} // namespace stremio
