#pragma once

// URL construction for addon requests.
// Port of stremio-core src/addon_transport/http_transport/http_transport.rs,
// src/types/query_params_encode.rs and constants.rs URI_COMPONENT_ENCODE_SET.

#include "stremio/common.h"

#include <QString>

namespace stremio {

enum class TransportKind {
    Http,        // .../manifest.json
    Legacy,      // .../stremio/v1
    Unsupported,
};

// encodeURIComponent: keeps A-Z a-z 0-9 - _ . ! ~ * ' ( ) and percent-encodes
// every other UTF-8 byte (upper-case hex, as percent_encoding does).
QString encodeUriComponent(const QString &value);

// k=v&k=v with both sides encoded by encodeUriComponent.
QString queryParamsEncode(const Extra &extra);

// "/{resource}/{type}/{id}.json" or "/{resource}/{type}/{id}/{extra}.json".
QString resourcePathString(const ResourcePath &path);

// The transport is chosen from the URL path (query and fragment ignored).
TransportKind transportKind(const QString &transportUrl);

// Full request URL for a resource. The "/manifest.json" suffix of the
// transport URL's path is replaced by the resource path; the query string
// (configured-addon tokens) is kept byte-for-byte after it. Legacy addons use
// the JSON-RPC q.json endpoint. Returns an empty string and sets *error when
// the transport URL cannot carry resource requests.
QString resourceUrl(const QString &transportUrl, const ResourcePath &path, QString *error = nullptr);

// URL to fetch the addon manifest from.
QString manifestUrl(const QString &transportUrl);

// Split helpers used by the URL functions above (exposed for tests).
struct SplitUrl
{
    QString beforeQuery; // scheme://authority/path
    QString suffix;      // "?query#fragment" (may be empty)
};
SplitUrl splitUrl(const QString &url);

} // namespace stremio
