#include "stremio/transport.h"

#include "stremio/legacytransport.h"

namespace stremio {

namespace {

bool isUriComponentSafe(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
        || c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*'
        || c == '\'' || c == '(' || c == ')';
}

} // namespace

QString encodeUriComponent(const QString &value)
{
    static const char hex[] = "0123456789ABCDEF";
    const QByteArray utf8 = value.toUtf8();
    QString out;
    out.reserve(utf8.size() * 3);
    for (const char ch : utf8) {
        const auto c = static_cast<unsigned char>(ch);
        if (isUriComponentSafe(c)) {
            out.append(QLatin1Char(char(c)));
        } else {
            out.append(QLatin1Char('%'));
            out.append(QLatin1Char(hex[c >> 4]));
            out.append(QLatin1Char(hex[c & 0x0F]));
        }
    }
    return out;
}

QString queryParamsEncode(const Extra &extra)
{
    QStringList pairs;
    pairs.reserve(extra.size());
    for (const ExtraValue &value : extra) {
        pairs.append(encodeUriComponent(value.name) + QLatin1Char('=') + encodeUriComponent(value.value));
    }
    return pairs.join(QLatin1Char('&'));
}

QString resourcePathString(const ResourcePath &path)
{
    QString result = QLatin1Char('/') + encodeUriComponent(path.resource)
        + QLatin1Char('/') + encodeUriComponent(path.type)
        + QLatin1Char('/') + encodeUriComponent(path.id);
    if (!path.extra.isEmpty()) {
        result += QLatin1Char('/') + queryParamsEncode(path.extra);
    }
    return result + QStringLiteral(".json");
}

SplitUrl splitUrl(const QString &url)
{
    // The authority can't contain '?' or '#', so the first one ends the path.
    const qsizetype schemeEnd = url.indexOf(QStringLiteral("://"));
    const qsizetype searchFrom = schemeEnd < 0 ? 0 : schemeEnd + 3;
    qsizetype cut = -1;
    for (qsizetype i = searchFrom; i < url.size(); ++i) {
        if (url[i] == QLatin1Char('?') || url[i] == QLatin1Char('#')) {
            cut = i;
            break;
        }
    }
    if (cut < 0) {
        return {url, {}};
    }
    return {url.left(cut), url.mid(cut)};
}

TransportKind transportKind(const QString &transportUrl)
{
    const SplitUrl split = splitUrl(transportUrl.trimmed());
    const QString lower = split.beforeQuery.toLower();
    if (!lower.startsWith(QStringLiteral("http://")) && !lower.startsWith(QStringLiteral("https://"))) {
        return TransportKind::Unsupported;
    }
    if (split.beforeQuery.endsWith(QLatin1String(kLegacyPath))) {
        return TransportKind::Legacy;
    }
    if (split.beforeQuery.endsWith(QLatin1String(kManifestPath))) {
        return TransportKind::Http;
    }
    return TransportKind::Unsupported;
}

QString resourceUrl(const QString &transportUrl, const ResourcePath &path, QString *error)
{
    switch (transportKind(transportUrl)) {
    case TransportKind::Legacy:
        return legacy::requestUrl(transportUrl, path, error);
    case TransportKind::Http: {
        const SplitUrl split = splitUrl(transportUrl.trimmed());
        const QString root = split.beforeQuery.left(split.beforeQuery.size() - qsizetype(qstrlen(kManifestPath)));
        return root + resourcePathString(path) + split.suffix;
    }
    case TransportKind::Unsupported:
        break;
    }
    if (error) {
        *error = QStringLiteral("addon http transport url must end with %1").arg(QLatin1String(kManifestPath));
    }
    return {};
}

QString manifestUrl(const QString &transportUrl)
{
    if (transportKind(transportUrl) == TransportKind::Legacy) {
        return legacy::manifestRequestUrl(transportUrl);
    }
    return transportUrl.trimmed();
}

} // namespace stremio
