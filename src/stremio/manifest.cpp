#include "stremio/manifest.h"

#include "stremio/json.h"

#include <QJsonArray>
#include <QSet>

namespace stremio {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

// ExtraProp (manifest.rs) inside the full `extra` form.
bool parseExtraProp(const QJsonValue &value, ExtraProp &prop, QString *error)
{
    if (!value.isObject()) {
        return fail(error, QStringLiteral("extra entry must be an object"));
    }
    const QJsonObject object = value.toObject();
    if (!json::requiredString(object, QStringLiteral("name"), prop.name, error)
        || !json::defaultBool(object, QStringLiteral("isRequired"), prop.isRequired, error)
        || !json::defaultStringList(object, QStringLiteral("options"), prop.options, error)) {
        return false;
    }
    qint64 limit = 1;
    if (!json::defaultUnsigned(object, QStringLiteral("optionsLimit"), std::numeric_limits<int>::max(), limit, error)) {
        return false;
    }
    prop.optionsLimit = int(limit);
    // ExtraPropValid: a declared "skip" always becomes SKIP_EXTRA_PROP.
    if (prop.name == QLatin1String(kSkipExtra)) {
        prop = ExtraProp::named(QString::fromLatin1(kSkipExtra));
    }
    return true;
}

// ManifestExtra: untagged Full { extra } | Short { extraRequired, extraSupported }.
bool parseCatalogExtra(const QJsonObject &object, ManifestCatalog &catalog, QString *error)
{
    if (object.contains(QStringLiteral("extra"))) {
        const QJsonValue extraValue = object.value(QStringLiteral("extra"));
        if (extraValue.isArray()) {
            QList<ExtraProp> props;
            QSet<QString> seen;
            bool ok = true;
            for (const QJsonValue &item : extraValue.toArray()) {
                ExtraProp prop;
                if (!parseExtraProp(item, prop, nullptr)) {
                    ok = false;
                    break;
                }
                if (!seen.contains(prop.name)) { // UniqueVec: first one wins
                    seen.insert(prop.name);
                    props.append(prop);
                }
            }
            if (ok) {
                catalog.extra = props;
                catalog.shortExtraForm = false;
                return true;
            }
        }
        // Not a valid Full form: serde's untagged enum falls through to Short.
    }

    QStringList required;
    QStringList supported;
    if (!json::defaultStringList(object, QStringLiteral("extraRequired"), required, error)
        || !json::defaultStringList(object, QStringLiteral("extraSupported"), supported, error)) {
        return false;
    }
    required.removeDuplicates();
    supported.removeDuplicates();
    catalog.extra.clear();
    for (const QString &name : std::as_const(supported)) {
        ExtraProp prop;
        prop.name = name;
        prop.isRequired = required.contains(name);
        catalog.extra.append(prop);
    }
    catalog.shortExtraForm = true;
    return true;
}

bool parseCatalog(const QJsonValue &value, ManifestCatalog &catalog, QString *error)
{
    if (!value.isObject()) {
        return fail(error, QStringLiteral("catalog must be an object"));
    }
    const QJsonObject object = value.toObject();
    return json::requiredString(object, QStringLiteral("id"), catalog.id, error)
        && json::requiredString(object, QStringLiteral("type"), catalog.type, error)
        && json::optionalString(object, QStringLiteral("name"), catalog.name, error)
        && parseCatalogExtra(object, catalog, error);
}

bool parseCatalogList(const QJsonObject &object, const QString &key, QList<ManifestCatalog> &out, QString *error)
{
    out.clear();
    const QJsonValue value = object.value(key);
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isArray()) {
        return fail(error, QStringLiteral("\"%1\" must be an array").arg(key));
    }
    QSet<QString> seen;
    const QJsonArray array = value.toArray();
    for (qsizetype i = 0; i < array.size(); ++i) {
        ManifestCatalog catalog;
        QString catalogError;
        if (!parseCatalog(array[i], catalog, &catalogError)) {
            return fail(error, QStringLiteral("%1[%2]: %3").arg(key).arg(i).arg(catalogError));
        }
        // UniqueVec keyed by (id, type): the first declaration wins.
        const QString uniqueKey = catalog.id + QLatin1Char('\n') + catalog.type;
        if (!seen.contains(uniqueKey)) {
            seen.insert(uniqueKey);
            out.append(catalog);
        }
    }
    return true;
}

bool parseResource(const QJsonValue &value, ManifestResource &resource, QString *error)
{
    if (value.isString()) {
        resource.name = value.toString();
        resource.full = false;
        return true;
    }
    if (!value.isObject()) {
        return fail(error, QStringLiteral("resource must be a string or an object"));
    }
    const QJsonObject object = value.toObject();
    resource.full = true;
    return json::requiredString(object, QStringLiteral("name"), resource.name, error)
        && json::optionalStringList(object, QStringLiteral("types"), resource.types, error)
        && json::optionalStringList(object, QStringLiteral("idPrefixes"), resource.idPrefixes, error);
}

bool parseBehaviorHints(const QJsonObject &object, ManifestBehaviorHints &hints, QString *error)
{
    const QJsonValue value = object.value(QStringLiteral("behaviorHints"));
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isObject()) {
        return fail(error, QStringLiteral("\"behaviorHints\" must be an object"));
    }
    const QJsonObject h = value.toObject();
    return json::defaultBool(h, QStringLiteral("adult"), hints.adult, error)
        && json::defaultBool(h, QStringLiteral("p2p"), hints.p2p, error)
        && json::defaultBool(h, QStringLiteral("configurable"), hints.configurable, error)
        && json::defaultBool(h, QStringLiteral("configurationRequired"), hints.configurationRequired, error)
        && json::defaultBool(h, QStringLiteral("epgProvider"), hints.epgProvider, error);
}

bool idSupported(const std::optional<QStringList> &prefixes, const QString &id)
{
    if (!prefixes || prefixes->isEmpty()) {
        return true;
    }
    for (const QString &prefix : *prefixes) {
        if (id.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

} // namespace

const ExtraProp *ManifestCatalog::extraProp(const QString &name) const
{
    for (const ExtraProp &prop : extra) {
        if (prop.name == name) {
            return &prop;
        }
    }
    return nullptr;
}

bool ManifestCatalog::isExtraSupported(const Extra &values) const
{
    for (const ExtraValue &value : values) {
        if (!hasExtra(value.name)) {
            return false;
        }
    }
    for (const ExtraProp &prop : extra) {
        if (!prop.isRequired) {
            continue;
        }
        bool present = false;
        for (const ExtraValue &value : values) {
            present = present || value.name == prop.name;
        }
        if (!present) {
            return false;
        }
    }
    return true;
}

bool ManifestCatalog::areExtraNamesSupported(const QStringList &names) const
{
    for (const QString &name : names) {
        if (!hasExtra(name)) {
            return false;
        }
    }
    for (const ExtraProp &prop : extra) {
        if (prop.isRequired && !names.contains(prop.name)) {
            return false;
        }
    }
    return true;
}

std::optional<Extra> ManifestCatalog::defaultRequiredExtra() const
{
    Extra result;
    for (const ExtraProp &prop : extra) {
        if (!prop.isRequired) {
            continue;
        }
        if (prop.options.isEmpty()) {
            return std::nullopt;
        }
        result.append({prop.name, prop.options.first()});
    }
    return result;
}

QJsonObject ManifestCatalog::toJson() const
{
    QJsonArray extraArray;
    for (const ExtraProp &prop : extra) {
        extraArray.append(QJsonObject{
            {"name", prop.name},
            {"isRequired", prop.isRequired},
            {"options", QJsonArray::fromStringList(prop.options)},
            {"optionsLimit", prop.optionsLimit},
        });
    }
    QJsonObject object{{"id", id}, {"type", type}, {"extra", extraArray}};
    if (name) {
        object.insert("name", *name);
    }
    return object;
}

std::optional<Manifest> Manifest::fromJson(const QJsonObject &object, QString *error, bool preview)
{
    Manifest manifest;
    manifest.raw = object;

    if (!json::requiredString(object, QStringLiteral("id"), manifest.id, error)
        || !json::requiredString(object, QStringLiteral("name"), manifest.name, error)) {
        return std::nullopt;
    }
    const QJsonValue version = object.value(QStringLiteral("version"));
    if (!version.isString() || !json::isSemver(version.toString())) {
        fail(error, QStringLiteral("\"version\" must be a semantic version such as 1.0.0"));
        return std::nullopt;
    }
    manifest.version = version.toString();

    QString typesError;
    if (!json::stringList(object.value(QStringLiteral("types")), manifest.types, &typesError)) {
        fail(error, QStringLiteral("\"types\": %1").arg(typesError));
        return std::nullopt;
    }

    if (!json::optionalString(object, QStringLiteral("description"), manifest.description, error)
        || !json::optionalString(object, QStringLiteral("contactEmail"), manifest.contactEmail, error)
        || !json::optionalUrl(object, QStringLiteral("logo"), false, manifest.logo, error)
        || !json::optionalUrl(object, QStringLiteral("background"), false, manifest.background, error)
        || !parseBehaviorHints(object, manifest.behaviorHints, error)) {
        return std::nullopt;
    }

    if (preview) {
        return manifest;
    }

    const QJsonValue resources = object.value(QStringLiteral("resources"));
    if (!resources.isArray()) {
        fail(error, QStringLiteral("\"resources\" must be an array"));
        return std::nullopt;
    }
    for (const QJsonValue &item : resources.toArray()) {
        ManifestResource resource;
        QString resourceError;
        if (!parseResource(item, resource, &resourceError)) {
            fail(error, QStringLiteral("\"resources\": %1").arg(resourceError));
            return std::nullopt;
        }
        manifest.resources.append(resource);
    }

    if (!json::optionalStringList(object, QStringLiteral("idPrefixes"), manifest.idPrefixes, error)
        || !parseCatalogList(object, QStringLiteral("catalogs"), manifest.catalogs, error)
        || !parseCatalogList(object, QStringLiteral("addonCatalogs"), manifest.addonCatalogs, error)) {
        return std::nullopt;
    }
    return manifest;
}

bool Manifest::isResourceSupported(const ResourcePath &path) const
{
    if (path.resource == QLatin1String(kCatalogResource)
        || path.resource == QLatin1String(kAddonCatalogResource)) {
        const QList<ManifestCatalog> &list =
            path.resource == QLatin1String(kCatalogResource) ? catalogs : addonCatalogs;
        for (const ManifestCatalog &catalog : list) {
            if (catalog.type == path.type && catalog.id == path.id && catalog.isExtraSupported(path.extra)) {
                return true;
            }
        }
        return false;
    }

    const ManifestResource *found = resource(path.resource);
    if (!found) {
        return false;
    }
    const std::optional<QStringList> resourceTypes = found->full ? found->types : std::optional<QStringList>(types);
    const std::optional<QStringList> &prefixes = found->full ? found->idPrefixes : idPrefixes;
    const bool typeSupported = resourceTypes && resourceTypes->contains(path.type);
    return typeSupported && idSupported(prefixes, path.id);
}

const ManifestResource *Manifest::resource(const QString &name) const
{
    // Like Iterator::find: the first resource with that name decides.
    for (const ManifestResource &item : resources) {
        if (item.name == name) {
            return &item;
        }
    }
    return nullptr;
}

bool Manifest::hasResource(const QString &name) const
{
    if (name == QLatin1String(kCatalogResource)) {
        return !catalogs.isEmpty();
    }
    if (name == QLatin1String(kAddonCatalogResource)) {
        return !addonCatalogs.isEmpty();
    }
    return resource(name) != nullptr;
}

QJsonObject Descriptor::toJson() const
{
    return {
        {"manifest", manifest.raw},
        {"transportUrl", transportUrl},
        {"flags", QJsonObject{{"official", flags.official}, {"protected", flags.protectedAddon}}},
    };
}

std::optional<Descriptor> Descriptor::fromJson(const QJsonObject &object, QString *error, bool preview)
{
    Descriptor descriptor;
    if (!json::requiredString(object, QStringLiteral("transportUrl"), descriptor.transportUrl, error)) {
        return std::nullopt;
    }
    if (!json::isAbsoluteUrl(descriptor.transportUrl)) {
        fail(error, QStringLiteral("\"transportUrl\" is not a valid URL"));
        return std::nullopt;
    }
    const QJsonValue manifestValue = object.value(QStringLiteral("manifest"));
    if (!manifestValue.isObject()) {
        fail(error, QStringLiteral("\"manifest\" must be an object"));
        return std::nullopt;
    }
    QString manifestError;
    auto manifest = Manifest::fromJson(manifestValue.toObject(), &manifestError, preview);
    if (!manifest) {
        fail(error, QStringLiteral("manifest: %1").arg(manifestError));
        return std::nullopt;
    }
    descriptor.manifest = *manifest;
    const QJsonObject flags = object.value(QStringLiteral("flags")).toObject();
    descriptor.flags.official = flags.value(QStringLiteral("official")).toBool();
    descriptor.flags.protectedAddon = flags.value(QStringLiteral("protected")).toBool();
    return descriptor;
}

} // namespace stremio
