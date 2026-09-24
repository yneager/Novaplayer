#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

inline QJsonObject jsonObject(const char *text)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(text), &error);
    Q_ASSERT_X(error.error == QJsonParseError::NoError, "jsonObject", qPrintable(error.errorString()));
    return document.object();
}

inline QJsonValue jsonValue(const char *text)
{
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray("[") + text + "]");
    return document.array().first();
}
