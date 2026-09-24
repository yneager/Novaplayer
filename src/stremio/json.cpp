#include "stremio/json.h"

#include <QLocale>
#include <QRegularExpression>
#include <QUrl>

#include <cmath>

namespace stremio::json {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

bool isMissing(const QJsonValue &value)
{
    return value.isUndefined() || value.isNull();
}

} // namespace

QString describeType(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Null: return QStringLiteral("null");
    case QJsonValue::Bool: return QStringLiteral("boolean");
    case QJsonValue::Double: return QStringLiteral("number");
    case QJsonValue::String: return QStringLiteral("string");
    case QJsonValue::Array: return QStringLiteral("array");
    case QJsonValue::Object: return QStringLiteral("object");
    case QJsonValue::Undefined: return QStringLiteral("missing");
    }
    return QStringLiteral("unknown");
}

bool requiredString(const QJsonObject &object, const QString &key, QString &out, QString *error)
{
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        return fail(error, QStringLiteral("\"%1\" must be a string (got %2)").arg(key, describeType(value)));
    }
    out = value.toString();
    return true;
}

bool optionalString(const QJsonObject &object, const QString &key, std::optional<QString> &out, QString *error)
{
    const QJsonValue value = object.value(key);
    if (isMissing(value)) {
        out.reset();
        return true;
    }
    if (!value.isString()) {
        return fail(error, QStringLiteral("\"%1\" must be a string (got %2)").arg(key, describeType(value)));
    }
    out = value.toString();
    return true;
}

bool defaultString(const QJsonObject &object, const QString &key, QString &out, QString *error)
{
    std::optional<QString> value;
    if (!optionalString(object, key, value, error)) {
        return false;
    }
    out = value.value_or(QString());
    return true;
}

QString numberToString(double value)
{
    if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 9.007199254740992e15) {
        return QString::number(qint64(value));
    }
    // Shortest representation that round-trips, like Rust's f64 Display.
    return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

bool optionalNumberAsString(const QJsonObject &object, const QString &key, std::optional<QString> &out, QString *error)
{
    const QJsonValue value = object.value(key);
    if (isMissing(value)) {
        out.reset();
        return true;
    }
    if (value.isString()) {
        out = value.toString();
        return true;
    }
    if (value.isDouble()) {
        out = numberToString(value.toDouble());
        return true;
    }
    return fail(error, QStringLiteral("\"%1\" must be a string or number (got %2)").arg(key, describeType(value)));
}

bool stringList(const QJsonValue &value, QStringList &out, QString *error)
{
    if (!value.isArray()) {
        return fail(error, QStringLiteral("expected an array of strings (got %1)").arg(describeType(value)));
    }
    QStringList result;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &item : array) {
        if (!item.isString()) {
            return fail(error, QStringLiteral("expected an array of strings (found %1)").arg(describeType(item)));
        }
        result.append(item.toString());
    }
    out = result;
    return true;
}

bool optionalStringList(const QJsonObject &object, const QString &key, std::optional<QStringList> &out, QString *error)
{
    const QJsonValue value = object.value(key);
    if (isMissing(value)) {
        out.reset();
        return true;
    }
    QStringList list;
    QString itemError;
    if (!stringList(value, list, &itemError)) {
        return fail(error, QStringLiteral("\"%1\": %2").arg(key, itemError));
    }
    out = list;
    return true;
}

bool defaultStringList(const QJsonObject &object, const QString &key, QStringList &out, QString *error)
{
    std::optional<QStringList> list;
    if (!optionalStringList(object, key, list, error)) {
        return false;
    }
    out = list.value_or(QStringList());
    return true;
}

bool defaultBool(const QJsonObject &object, const QString &key, bool &out, QString *error)
{
    const QJsonValue value = object.value(key);
    if (isMissing(value)) {
        return true;
    }
    if (!value.isBool()) {
        return fail(error, QStringLiteral("\"%1\" must be a boolean (got %2)").arg(key, describeType(value)));
    }
    out = value.toBool();
    return true;
}

bool optionalUnsigned(const QJsonObject &object, const QString &key, qint64 max, std::optional<qint64> &out, QString *error)
{
    const QJsonValue value = object.value(key);
    if (isMissing(value)) {
        out.reset();
        return true;
    }
    const double number = value.toDouble(-1.0);
    if (!value.isDouble() || number < 0 || number != std::floor(number) || number > double(max)) {
        return fail(error, QStringLiteral("\"%1\" must be an integer between 0 and %2").arg(key).arg(max));
    }
    out = qint64(number);
    return true;
}

bool defaultUnsigned(const QJsonObject &object, const QString &key, qint64 max, qint64 &out, QString *error)
{
    std::optional<qint64> value;
    if (!optionalUnsigned(object, key, max, value, error)) {
        return false;
    }
    if (value) {
        out = *value;
    }
    return true;
}

bool isAbsoluteUrl(const QString &value)
{
    if (value.isEmpty()) {
        return false;
    }
    const QUrl url(value, QUrl::TolerantMode);
    if (!url.isValid() || url.scheme().isEmpty() || url.isRelative()) {
        return false;
    }
    // A scheme needs at least a letter and must not be a Windows drive ("C:").
    if (url.scheme().size() < 2) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")
        || scheme == QLatin1String("ftp") || scheme == QLatin1String("ftps")
        || scheme == QLatin1String("ws") || scheme == QLatin1String("wss")) {
        return !url.host().isEmpty();
    }
    return true;
}

bool optionalUrl(const QJsonObject &object, const QString &key, bool strict, QString &out, QString *error)
{
    out.clear();
    const QJsonValue value = object.value(key);
    if (isMissing(value)) {
        return true;
    }
    if (!value.isString()) {
        if (strict) {
            return fail(error, QStringLiteral("\"%1\" must be a URL string (got %2)").arg(key, describeType(value)));
        }
        return true;
    }
    const QString text = value.toString();
    if (text.isEmpty()) {
        return true;
    }
    if (!isAbsoluteUrl(text)) {
        if (strict) {
            return fail(error, QStringLiteral("\"%1\" is not a valid absolute URL").arg(key));
        }
        return true;
    }
    out = text;
    return true;
}

bool isSemver(const QString &value)
{
    static const QRegularExpression re(QStringLiteral(
        "^(0|[1-9]\\d*)\\.(0|[1-9]\\d*)\\.(0|[1-9]\\d*)"
        "(?:-((?:0|[1-9]\\d*|\\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\\.(?:0|[1-9]\\d*|\\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?"
        "(?:\\+([0-9a-zA-Z-]+(?:\\.[0-9a-zA-Z-]+)*))?$"));
    return re.match(value).hasMatch();
}

} // namespace stremio::json
