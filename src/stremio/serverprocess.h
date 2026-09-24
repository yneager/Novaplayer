#pragma once

// Runs LAMBDA's bundled streaming server (lambda-stream-server.exe, built from
// the open-source stremio-native/stream-server; see tools/stream-server) as a
// child process bound to 127.0.0.1.
//
// The host prints "LAMBDA_STREAM_SERVER_READY http://127.0.0.1:<port>" once
// it listens and exits when its stdin closes, so it never outlives LAMBDA.

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QProcess;
class QTimer;

namespace stremio {

class StreamingServerProcess final : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QString program;     // lambda-stream-server.exe
        QString configDir;   // server settings.json, logs
        QString cacheDir;    // torrent/archive cache
        quint16 port = 0;    // preferred port; 0 = any free port
    };

    explicit StreamingServerProcess(QObject *parent = nullptr);
    ~StreamingServerProcess() override;

    Options options() const { return options_; }
    // Restarts a running server when anything but the port changes.
    void setOptions(const Options &options);

    bool isAvailable() const;   // the program exists
    bool isRunning() const { return !url_.isEmpty(); }
    QString url() const { return url_; }   // "http://127.0.0.1:<port>/" when running
    quint16 port() const { return port_; }
    QString lastError() const { return lastError_; }

    // Starts the server if needed; `done` runs once it is ready or failed.
    void ensureStarted(QObject *context, std::function<void(bool ok, const QString &error)> done);
    // Graceful stop (closes stdin, then kills after a short wait). Blocking.
    void stop();

    // Parses the host's ready line; exposed for tests.
    static quint16 parseReadyLine(const QByteArray &line);

signals:
    void started(const QString &url);
    void stopped();
    void portChanged(quint16 port);

private:
    struct Waiter
    {
        QPointer<QObject> context;
        bool hasContext = false;
        std::function<void(bool, const QString &)> done;
    };

    void launch(quint16 port);
    void finishWaiters(bool ok, const QString &error);
    void handleOutput();
    void handleFinished();

    Options options_;
    QProcess *process_ = nullptr;
    QTimer *readyTimer_ = nullptr;
    QList<Waiter> waiters_;
    QByteArray stdout_;
    QByteArray stderr_;
    QString url_;
    QString lastError_;
    quint16 port_ = 0;
    quint16 launchPort_ = 0;
    bool stopping_ = false;
    bool retriedWithAnyPort_ = false;
    QList<qint64> crashTimes_;
};

} // namespace stremio
