#pragma once

// Normalisation of addon URLs typed or pasted by the user.
// stremio-core AddonDetails maps stremio:// to https://; Debrify
// (lib/utils/stremio_url.dart) and Nuvio (AddonRepository.kt
// normalizeManifestUrl) additionally default the scheme to https and append a
// missing /manifest.json. URLs Stremio already accepts are left unchanged, and
// the configured path/query is never decoded or re-encoded.

#include <QString>

namespace stremio {

struct InstallUrl
{
    QString url;          // normalised URL (scheme fixed, fragment removed)
    QString manifestUrl;  // url with /manifest.json appended when missing
    bool isLegacy = false;
    bool endsWithManifest = false;
    bool looksLikeJsonFile = false; // path ends with ".json" but not manifest.json
};

// Returns false (and sets *error) for input that is not an http(s) URL.
bool normalizeInstallUrl(const QString &input, InstallUrl &out, QString *error = nullptr);

// "…/configure" page of a configurable addon (stremio-web AddonDetailsModal:
// transportUrl.replace('manifest.json', 'configure')).
QString configureUrl(const QString &transportUrl);

// Host of a URL for display ("torrentio.strem.fun").
QString displayHost(const QString &url);

} // namespace stremio
