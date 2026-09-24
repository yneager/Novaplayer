#include "stremio/common.h"

#include "stremio/transport.h"

namespace stremio {

Extra removeAll(const Extra &extra, const ExtraProp &prop)
{
    Extra result;
    for (const ExtraValue &value : extra) {
        if (value.name != prop.name) {
            result.append(value);
        }
    }
    return result;
}

Extra extendOne(const Extra &extra, const ExtraProp &prop, const std::optional<QString> &value)
{
    Extra same;
    Extra other;
    for (const ExtraValue &item : extra) {
        (item.name == prop.name ? same : other).append(item);
    }

    Extra next;
    if (value && prop.optionsLimit == 1) {
        next = {ExtraValue{prop.name, *value}};
    } else if (value && prop.optionsLimit > 1) {
        bool present = false;
        for (const ExtraValue &item : same) {
            present = present || item.value == *value;
        }
        if (present) {
            for (const ExtraValue &item : same) {
                if (item.value != *value) {
                    next.append(item);
                }
            }
        } else {
            next.append(ExtraValue{prop.name, *value});
            for (const ExtraValue &item : same) {
                if (next.size() >= prop.optionsLimit) {
                    break;
                }
                next.append(item);
            }
        }
    } else if (!value && !prop.isRequired) {
        // removed
    } else if (prop.optionsLimit == 0) {
        // removed
    } else {
        next = same;
    }
    return next + other;
}

std::optional<QString> firstExtraValue(const Extra &extra, const QString &name)
{
    for (const ExtraValue &value : extra) {
        if (value.name == name) {
            return value.value;
        }
    }
    return std::nullopt;
}

QJsonArray extraToJson(const Extra &extra)
{
    QJsonArray array;
    for (const ExtraValue &value : extra) {
        array.append(QJsonArray{value.name, value.value});
    }
    return array;
}

Extra extraFromJson(const QJsonValue &value)
{
    Extra extra;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &item : array) {
        if (item.isArray()) {
            const QJsonArray pair = item.toArray();
            if (pair.size() == 2 && pair[0].isString() && pair[1].isString()) {
                extra.append({pair[0].toString(), pair[1].toString()});
            }
        } else if (item.isObject()) {
            const QJsonObject object = item.toObject();
            if (object.value("name").isString() && object.value("value").isString()) {
                extra.append({object.value("name").toString(), object.value("value").toString()});
            }
        }
    }
    return extra;
}

QJsonObject ResourcePath::toJson() const
{
    return {
        {"resource", resource},
        {"type", type},
        {"id", id},
        {"extra", extraToJson(extra)},
    };
}

ResourcePath ResourcePath::fromJson(const QJsonObject &object)
{
    ResourcePath path;
    path.resource = object.value("resource").toString();
    path.type = object.value("type").toString();
    path.id = object.value("id").toString();
    path.extra = extraFromJson(object.value("extra"));
    return path;
}

QJsonObject ResourceRequest::toJson() const
{
    return {{"base", base}, {"path", path.toJson()}};
}

ResourceRequest ResourceRequest::fromJson(const QJsonObject &object)
{
    return {object.value("base").toString(), ResourcePath::fromJson(object.value("path").toObject())};
}

QString ResourceRequest::key() const
{
    return base + QLatin1Char('\n') + resourcePathString(path);
}

} // namespace stremio
