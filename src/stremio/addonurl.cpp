#include "stremio/addonurl.h"

#include "stremio/common.h"
#include "stremio/transport.h"

#include <QRegularExpression>
#include <QUrl>

namespace stremio {

bool normalizeInstallUrl(const QString &input, InstallUrl &out, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    QString url = input.trimmed();
    if (url.isEmpty()) {
        return fail(QStringLiteral("Enter an addon URL."));
    }

    static const QRegularExpression schemeRe(QStringLiteral("^([a-zA-Z][a-zA-Z0-9+.-]*)://"));
    const QRegularExpressionMatch scheme = schemeRe.match(url);
    if (scheme.hasMatch()) {
        const QString name = scheme.captured(1).toLower();
        const QString rest = url.mid(scheme.capturedLength());
        if (name == QLatin1String("stremio")) {
            url = QStringLiteral("https://") + rest;
        } else if (name == QLatin1String("http") || name == QLatin1String("https")) {
            // Lower-case the scheme only; everything else stays byte-for-byte.
            url = name + QStringLiteral("://") + rest;
        } else {
            return fail(QStringLiteral("Addon URLs must start with https://, http:// or stremio://."));
        }
    } else {
        url = QStringLiteral("https://") + url;
    }

    // Drop the fragment: it is never sent to the addon.
    const qsizetype hash = url.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        url.truncate(hash);
    }

    const QUrl parsed(url, QUrl::TolerantMode);
    const QString host = parsed.host();
    const bool plausibleHost = host.contains(QLatin1Char('.')) || host.contains(QLatin1Char(':'))
        || host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0;
    if (!parsed.isValid() || host.isEmpty() || !plausibleHost) {
        return fail(QStringLiteral("That doesn't look like an addon URL."));
    }

    SplitUrl split = splitUrl(url);
    // Trailing slashes of the path ("…/manifest.json/", "…/config/", "host/").
    const qsizetype authorityStart = split.beforeQuery.indexOf(QStringLiteral("://")) + 3;
    while (split.beforeQuery.size() > authorityStart && split.beforeQuery.endsWith(QLatin1Char('/'))) {
        split.beforeQuery.chop(1);
    }
    out = {};
    out.url = split.beforeQuery + split.suffix;
    out.isLegacy = split.beforeQuery.endsWith(QLatin1String(kLegacyPath));
    out.endsWithManifest = split.beforeQuery.endsWith(QLatin1String(kManifestPath));
    const QString lastSegment = split.beforeQuery.section(QLatin1Char('/'), -1);
    out.looksLikeJsonFile = !out.endsWithManifest && lastSegment.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)
        && split.beforeQuery.count(QLatin1Char('/')) > 2;
    if (out.endsWithManifest || out.isLegacy) {
        out.manifestUrl = out.url;
    } else {
        out.manifestUrl = split.beforeQuery + QLatin1String(kManifestPath) + split.suffix;
    }
    return true;
}

QString configureUrl(const QString &transportUrl)
{
    // JavaScript String.replace with a string pattern replaces the first match.
    QString url = transportUrl;
    const qsizetype index = url.indexOf(QStringLiteral("manifest.json"));
    if (index >= 0) {
        url.replace(index, qsizetype(qstrlen("manifest.json")), QStringLiteral("configure"));
    }
    return url;
}

QString displayHost(const QString &url)
{
    return QUrl(url, QUrl::TolerantMode).host();
}

} // namespace stremio
