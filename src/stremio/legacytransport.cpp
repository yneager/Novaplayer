#include "stremio/legacytransport.h"

#include "stremio/json.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace stremio::legacy {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

QJsonObject jsonRpc(const QString &method, const QJsonValue &params)
{
    return {
        {"params", QJsonArray{QJsonValue::Null, params}},
        {"method", method},
        {"id", 1},
        {"jsonrpc", QStringLiteral("2.0")},
    };
}

QString encodeBody(const QJsonObject &body)
{
    // QJsonDocument writes object keys sorted, like serde_json's default map;
    // standard (not URL-safe) base64 reproduces the legacy client's encoding.
    return QString::fromLatin1(QJsonDocument(body).toJson(QJsonDocument::Compact).toBase64());
}

QString trimmedUrl(const QString &transportUrl)
{
    return transportUrl.trimmed();
}

// JsonRPCResp<T>: { result: T } | { error: { message, code } }
std::optional<QJsonValue> rpcResult(const QJsonValue &value, QString *error)
{
    const QJsonObject object = value.toObject();
    if (object.contains(QStringLiteral("result"))) {
        return object.value(QStringLiteral("result"));
    }
    const QJsonValue rpcError = object.value(QStringLiteral("error"));
    if (rpcError.isObject() && rpcError.toObject().value(QStringLiteral("message")).isString()) {
        fail(error, QStringLiteral("rpc error %1: %2")
                        .arg(qint64(rpcError.toObject().value(QStringLiteral("code")).toDouble()))
                        .arg(rpcError.toObject().value(QStringLiteral("message")).toString()));
        return std::nullopt;
    }
    fail(error, QStringLiteral("legacy transport: unexpected response"));
    return std::nullopt;
}

} // namespace

QJsonValue queryFromId(const QString &id)
{
    const QStringList parts = id.split(QLatin1Char(':'));
    if (id.startsWith(QLatin1String("tt"))) {
        if (parts.size() == 3) {
            bool seasonOk = false;
            bool episodeOk = false;
            const int season = parts[1].toUShort(&seasonOk);
            const int episode = parts[2].toUShort(&episodeOk);
            return QJsonObject{
                {"imdb_id", parts[0]},
                {"season", seasonOk ? season : 1},
                {"episode", episodeOk ? episode : 1},
            };
        }
        return QJsonObject{{"imdb_id", parts[0]}};
    }
    if (id.startsWith(QLatin1String("UC"))) {
        if (parts.size() == 2) {
            return QJsonObject{{"yt_id", parts[0]}, {"video_id", parts[1]}};
        }
        return QJsonObject{{"yt_id", parts[0]}};
    }
    if (parts.size() == 3) {
        return QJsonObject{{parts[0], parts[1]}, {"video_id", parts[2]}};
    }
    if (parts.size() == 2) {
        return QJsonObject{{parts[0], parts[1]}};
    }
    return QJsonValue::Null;
}

std::optional<QJsonObject> requestBody(const ResourcePath &path, QString *error)
{
    if (path.resource == QLatin1String(kCatalogResource)) {
        QJsonObject query{{"type", path.type}};
        if (const auto genre = firstExtraValue(path.extra, QString::fromLatin1(kGenreExtra))) {
            query.insert("genre", *genre);
        }
        // Follows the stremboard convention, as stremio-core does.
        QJsonValue sort = QJsonValue::Null;
        if (path.id != QLatin1String("top")) {
            sort = QJsonObject{{path.id, -1}, {"popularity", -1}};
        }
        uint skip = 0;
        if (const auto value = firstExtraValue(path.extra, QString::fromLatin1(kSkipExtra))) {
            bool ok = false;
            skip = value->toUInt(&ok);
            if (!ok) {
                skip = 0;
            }
        }
        return jsonRpc(QStringLiteral("meta.find"),
                       QJsonObject{{"query", query}, {"limit", 100}, {"sort", sort}, {"skip", double(skip)}});
    }
    if (path.resource == QLatin1String(kMetaResource)) {
        return jsonRpc(QStringLiteral("meta.get"), QJsonObject{{"query", queryFromId(path.id)}});
    }
    if (path.resource == QLatin1String(kStreamResource)) {
        const QJsonValue query = queryFromId(path.id);
        if (!query.isObject()) {
            fail(error, QStringLiteral("legacy: stream request without a valid id"));
            return std::nullopt;
        }
        QJsonObject object = query.toObject();
        object.insert("type", path.type);
        return jsonRpc(QStringLiteral("stream.find"), QJsonObject{{"query", object}});
    }
    if (path.resource == QLatin1String(kSubtitlesResource)) {
        QString itemHash = path.id;
        itemHash.replace(QLatin1Char(':'), QLatin1Char(' '));
        QJsonObject query{{"itemHash", itemHash}};
        if (const auto hash = firstExtraValue(path.extra, QString::fromLatin1(kVideoHashExtra))) {
            query.insert("videoHash", *hash);
        }
        if (const auto size = firstExtraValue(path.extra, QString::fromLatin1(kVideoSizeExtra))) {
            bool ok = false;
            const qulonglong value = size->toULongLong(&ok);
            if (ok) {
                query.insert("videoSize", double(value));
            }
        }
        if (const auto filename = firstExtraValue(path.extra, QString::fromLatin1(kVideoFilenameExtra))) {
            query.insert("filename", *filename);
        }
        return jsonRpc(QStringLiteral("subtitles.find"), QJsonObject{{"query", query}});
    }
    fail(error, QStringLiteral("legacy transport: unsupported request"));
    return std::nullopt;
}

QString requestUrl(const QString &transportUrl, const ResourcePath &path, QString *error)
{
    const auto body = requestBody(path, error);
    if (!body) {
        return {};
    }
    return trimmedUrl(transportUrl) + QStringLiteral("/q.json?b=") + encodeBody(*body);
}

QString manifestRequestUrl(const QString &transportUrl)
{
    // base64 of {"params":[],"method":"meta","id":1,"jsonrpc":"2.0"}
    return trimmedUrl(transportUrl)
        + QStringLiteral("/q.json?b=eyJwYXJhbXMiOltdLCJtZXRob2QiOiJtZXRhIiwiaWQiOjEsImpzb25ycGMiOiIyLjAifQ==");
}

std::optional<Manifest> parseManifestResponse(const QJsonValue &value, QString *error)
{
    const auto result = rpcResult(value, error);
    if (!result) {
        return std::nullopt;
    }
    const QJsonObject legacy = result->toObject().value(QStringLiteral("manifest")).toObject();
    QString id;
    QString name;
    QStringList methods;
    QStringList types;
    if (!json::requiredString(legacy, QStringLiteral("id"), id, error)
        || !json::requiredString(legacy, QStringLiteral("name"), name, error)
        || !json::stringList(legacy.value(QStringLiteral("methods")), methods, error)
        || !json::stringList(legacy.value(QStringLiteral("types")), types, error)) {
        return std::nullopt;
    }
    const QString version = legacy.value(QStringLiteral("version")).toString();
    if (!json::isSemver(version)) {
        fail(error, QStringLiteral("\"version\" must be a semantic version"));
        return std::nullopt;
    }

    // Build the equivalent modern manifest (legacy_manifest.rs From impl).
    QJsonObject modern{
        {"id", id},
        {"name", name},
        {"version", version},
        {"types", QJsonArray::fromStringList(types)},
    };
    for (const char *key : {"description", "logo", "background", "contactEmail"}) {
        const QJsonValue item = legacy.value(QLatin1String(key));
        if (item.isString()) {
            modern.insert(QLatin1String(key), item);
        }
    }

    QJsonArray catalogs;
    if (methods.contains(QStringLiteral("meta.find"))) {
        const QJsonValue sorts = legacy.value(QStringLiteral("sorts"));
        if (sorts.isArray()) {
            for (const QJsonValue &sortValue : sorts.toArray()) {
                const QJsonObject sort = sortValue.toObject();
                QStringList sortTypes = types;
                if (sort.value(QStringLiteral("types")).isArray()) {
                    json::stringList(sort.value(QStringLiteral("types")), sortTypes, nullptr);
                }
                for (const QString &type : std::as_const(sortTypes)) {
                    QJsonObject catalog{{"type", type}, {"id", sort.value(QStringLiteral("prop")).toString()}};
                    if (sort.value(QStringLiteral("name")).isString()) {
                        catalog.insert("name", sort.value(QStringLiteral("name")));
                    }
                    catalogs.append(catalog);
                }
            }
        } else {
            for (const QString &type : std::as_const(types)) {
                catalogs.append(QJsonObject{{"type", type}, {"id", "top"}});
            }
        }
    }
    modern.insert("catalogs", catalogs);

    const QJsonValue idProperty = legacy.value(QStringLiteral("idProperty"));
    if (idProperty.isString() || idProperty.isArray()) {
        QStringList properties;
        if (idProperty.isString()) {
            properties.append(idProperty.toString());
        } else {
            json::stringList(idProperty, properties, nullptr);
        }
        QJsonArray prefixes;
        for (const QString &property : std::as_const(properties)) {
            if (property == QLatin1String("imdb_id")) prefixes.append("tt");
            else if (property == QLatin1String("yt_id")) prefixes.append("UC");
            else prefixes.append(property + QLatin1Char(':'));
        }
        modern.insert("idPrefixes", prefixes);
    }

    QJsonArray resources;
    if (methods.contains(QStringLiteral("meta.get"))) resources.append("meta");
    if (methods.contains(QStringLiteral("stream.find"))) resources.append("stream");
    if (methods.contains(QStringLiteral("subtitles.find"))) resources.append("subtitles");
    modern.insert("resources", resources);

    return Manifest::fromJson(modern, error);
}

std::optional<ResourceResponse> parseResourceResponse(const QString &resource, const QJsonValue &value, QString *error)
{
    const auto result = rpcResult(value, error);
    if (!result) {
        return std::nullopt;
    }
    ResourceResponse response;
    // Legacy results are plain vectors: one invalid item fails the request.
    if (resource == QLatin1String(kCatalogResource)) {
        response.kind = ResourceResponse::Kind::Metas;
        for (const QJsonValue &item : result->toArray()) {
            auto meta = MetaItemPreview::fromJson(item, error);
            if (!meta) return std::nullopt;
            response.metas.append(*meta);
        }
        return response;
    }
    if (resource == QLatin1String(kMetaResource)) {
        response.kind = ResourceResponse::Kind::Meta;
        response.meta = MetaItem::fromJson(*result, error);
        if (!response.meta) return std::nullopt;
        return response;
    }
    if (resource == QLatin1String(kStreamResource)) {
        response.kind = ResourceResponse::Kind::Streams;
        for (const QJsonValue &item : result->toArray()) {
            auto stream = Stream::fromJson(item, error);
            if (!stream) return std::nullopt;
            response.streams.append(*stream);
        }
        return response;
    }
    if (resource == QLatin1String(kSubtitlesResource)) {
        response.kind = ResourceResponse::Kind::Subtitles;
        for (const QJsonValue &item : result->toObject().value(QStringLiteral("all")).toArray()) {
            auto subtitle = Subtitles::fromJson(item, error);
            if (!subtitle) return std::nullopt;
            response.subtitles.append(*subtitle);
        }
        return response;
    }
    fail(error, QStringLiteral("legacy transport: unsupported resource"));
    return std::nullopt;
}

} // namespace stremio::legacy
