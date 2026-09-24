#pragma once

// Legacy (v1/v2) addon transport: JSON-RPC requests encoded in the q.json
// query string. Port of stremio-core
// src/addon_transport/http_transport/legacy/{mod,legacy_manifest}.rs.

#include "stremio/common.h"
#include "stremio/manifest.h"
#include "stremio/resources.h"

#include <QJsonValue>
#include <QString>

#include <optional>

namespace stremio::legacy {

QString requestUrl(const QString &transportUrl, const ResourcePath &path, QString *error = nullptr);
QString manifestRequestUrl(const QString &transportUrl);

// The JSON-RPC body sent for a resource request (exposed for tests).
std::optional<QJsonObject> requestBody(const ResourcePath &path, QString *error = nullptr);
QJsonValue queryFromId(const QString &id);

std::optional<Manifest> parseManifestResponse(const QJsonValue &value, QString *error = nullptr);
std::optional<ResourceResponse> parseResourceResponse(const QString &resource, const QJsonValue &value,
                                                      QString *error = nullptr);

} // namespace stremio::legacy
