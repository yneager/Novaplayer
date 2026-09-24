#pragma once

// Small typed readers for JSON received from addons. They reproduce the serde
// rules stremio-core applies to the same fields: a required field that is
// missing or has the wrong type is an error; a defaulted field that is missing
// uses its default. Where serde would reject an explicit `null` for a field
// that has a default, these readers use the default instead (documented in
// docs/stremio/COMPATIBILITY.md, C-012).

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace stremio::json {

bool requiredString(const QJsonObject &object, const QString &key, QString &out, QString *error);
// Missing or null -> nullopt; a non-string value is an error.
bool optionalString(const QJsonObject &object, const QString &key, std::optional<QString> &out, QString *error);
// Missing or null -> default; non-string is an error.
bool defaultString(const QJsonObject &object, const QString &key, QString &out, QString *error);
// Numbers are converted to their shortest text form (NumberAsString).
bool optionalNumberAsString(const QJsonObject &object, const QString &key, std::optional<QString> &out, QString *error);
bool stringList(const QJsonValue &value, QStringList &out, QString *error);
// Missing or null -> nullopt (Option<Vec<String>>).
bool optionalStringList(const QJsonObject &object, const QString &key, std::optional<QStringList> &out, QString *error);
// Missing or null -> empty (DefaultOnNull<Vec<String>>).
bool defaultStringList(const QJsonObject &object, const QString &key, QStringList &out, QString *error);
bool defaultBool(const QJsonObject &object, const QString &key, bool &out, QString *error);
// Non-negative integer within [0, max]; missing or null -> default.
bool defaultUnsigned(const QJsonObject &object, const QString &key, qint64 max, qint64 &out, QString *error);
// Missing or null -> nullopt; otherwise an integer within [0, max].
bool optionalUnsigned(const QJsonObject &object, const QString &key, qint64 max, std::optional<qint64> &out, QString *error);

// An absolute URL (what url::Url accepts): a scheme and, for http(s)/ftp, a host.
bool isAbsoluteUrl(const QString &value);
// Optional URL field. `strict` mirrors DefaultOnNull<NoneAsEmptyString<Url>>
// (an invalid URL is an error); non-strict mirrors DefaultOnError (-> none).
bool optionalUrl(const QJsonObject &object, const QString &key, bool strict, QString &out, QString *error);

// Number to string as Rust's f64::to_string does for the values addons send.
QString numberToString(double value);

// semver 2.0.0 version as accepted by the semver crate.
bool isSemver(const QString &value);

QString describeType(const QJsonValue &value);

} // namespace stremio::json
