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
        // When set, the route serves this file and honours "Range: bytes=a-b"
        // with 206 Partial Content (unless ignoreRange).
        QByteArray file;
        bool ignoreRange = false;
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
    void clearRequests() { requests_.clear(); methods_.clear(); bodies_.clear(); headers_.clear(); }
    QStringList methods() const { return methods_; }
    QList<QByteArray> bodies() const { return bodies_; }
    // Raw header lines ("Name: value\r") of every request.
    QList<QList<QByteArray>> requestHeaders() const { return headers_; }

private:
    void onReadyRead(QTcpSocket *socket)
    {
        QByteArray &buffer = buffers_[socket];
        buffer += socket->readAll();
        const qsizetype end = buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            return;
        }
        const QByteArray head = buffer.left(end);
        qsizetype contentLength = 0;
        for (const QByteArray &line : head.split('\n')) {
            if (line.toLower().startsWith("content-length:")) {
                contentLength = line.mid(15).trimmed().toLongLong();
            }
        }
        if (buffer.size() < end + 4 + contentLength) {
            return; // wait for the body
        }
        const QByteArray body = buffer.mid(end + 4, contentLength);
        const QByteArray requestLine = buffer.left(buffer.indexOf("\r\n"));
        buffers_.remove(socket);
        const QList<QByteArray> parts = requestLine.split(' ');
        const QByteArray target = parts.size() > 1 ? parts[1] : QByteArray();
        requests_.append(QString::fromUtf8(target));
        methods_.append(QString::fromUtf8(parts.value(0)));
        bodies_.append(body);
        QList<QByteArray> headerLines = head.split('\n');
        headerLines.removeFirst();
        headers_.append(headerLines);

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
        if (!r.file.isEmpty()) {
            r.body = r.file;
            r.status = 200;
            for (const QByteArray &line : headerLines) {
                const QByteArray lower = line.toLower();
                if (!r.ignoreRange && lower.startsWith("range: bytes=")) {
                    const QList<QByteArray> range = line.trimmed().mid(13).split('-');
                    const qint64 from = range.value(0).toLongLong();
                    const qint64 to = qMin<qint64>(range.value(1).toLongLong(), r.file.size() - 1);
                    r.body = r.file.mid(from, to - from + 1);
                    r.status = 206;
                    r.headers.emplaceBack("Content-Range", "bytes " + QByteArray::number(from) + "-"
                                              + QByteArray::number(to) + "/" + QByteArray::number(r.file.size()));
                }
            }
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
    QStringList methods_;
    QList<QByteArray> bodies_;
    QList<QList<QByteArray>> headers_;
};
