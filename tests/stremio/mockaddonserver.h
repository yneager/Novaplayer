#pragma once

// A deterministic local addon server for tests: answers GET requests from a
// route table (status, headers, body, optional delay) and records the raw
// request targets exactly as they arrived on the wire.

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

class MockAddonServer : public QObject
{
public:
    struct Route
    {
        int status = 200;
        QByteArray body;
        QList<QPair<QByteArray, QByteArray>> headers;
        int delayMs = 0;
        bool neverAnswer = false;
    };

    explicit MockAddonServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        QObject::connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = server_.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        server_.listen(QHostAddress::LocalHost, 0);
    }

    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort()); }

    void route(const QByteArray &target, const QByteArray &body, int status = 200)
    {
        Route r;
        r.status = status;
        r.body = body;
        r.headers.emplaceBack("Content-Type", "application/json; charset=utf-8");
        routes_.insert(target, r);
    }
    void route(const QByteArray &target, const Route &r) { routes_.insert(target, r); }

    QStringList requests() const { return requests_; }
    int count(const QByteArray &target) const { return requests_.count(QString::fromUtf8(target)); }
    void clearRequests() { requests_.clear(); }

private:
    void onReadyRead(QTcpSocket *socket)
    {
        QByteArray &buffer = buffers_[socket];
        buffer += socket->readAll();
        const qsizetype end = buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            return;
        }
        const QByteArray requestLine = buffer.left(buffer.indexOf("\r\n"));
        buffers_.remove(socket);
        const QList<QByteArray> parts = requestLine.split(' ');
        const QByteArray target = parts.size() > 1 ? parts[1] : QByteArray();
        requests_.append(QString::fromUtf8(target));

        Route r;
        if (routes_.contains(target)) {
            r = routes_.value(target);
        } else {
            r.status = 404;
            r.body = "{\"err\":\"not found\"}";
        }
        if (r.neverAnswer) {
            return;
        }
        QPointer<QTcpSocket> guard(socket);
        auto respond = [guard, r] {
            if (!guard) {
                return;
            }
            QByteArray response = "HTTP/1.1 " + QByteArray::number(r.status) + " X\r\n";
            for (const auto &header : r.headers) {
                response += header.first + ": " + header.second + "\r\n";
            }
            response += "Content-Length: " + QByteArray::number(r.body.size()) + "\r\nConnection: close\r\n\r\n" + r.body;
            guard->write(response);
            guard->disconnectFromHost();
        };
        if (r.delayMs > 0) {
            QTimer::singleShot(r.delayMs, this, respond);
        } else {
            respond();
        }
    }

    QTcpServer server_;
    QHash<QByteArray, Route> routes_;
    QHash<QTcpSocket *, QByteArray> buffers_;
    QStringList requests_;
};
