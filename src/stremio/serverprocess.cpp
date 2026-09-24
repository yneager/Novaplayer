#include "stremio/serverprocess.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace stremio {

namespace {

constexpr int kReadyTimeoutMs = 20000;
constexpr int kStopTimeoutMs = 4000;
constexpr int kMaxCrashRestarts = 3;
constexpr qint64 kCrashWindowMs = 60000;
constexpr qsizetype kMaxStderr = 16 * 1024;

const QByteArray kReadyPrefix = QByteArrayLiteral("LAMBDA_STREAM_SERVER_READY ");
const QByteArray kErrorPrefix = QByteArrayLiteral("LAMBDA_STREAM_SERVER_ERROR ");

} // namespace

StreamingServerProcess::StreamingServerProcess(QObject *parent)
    : QObject(parent)
{
}

StreamingServerProcess::~StreamingServerProcess()
{
    stop();
}

void StreamingServerProcess::setOptions(const Options &options)
{
    const bool restart = process_
        && (options.program != options_.program || options.configDir != options_.configDir
            || options.cacheDir != options_.cacheDir);
    options_ = options;
    if (restart) {
        stop();
    }
}

bool StreamingServerProcess::isAvailable() const
{
    return !options_.program.isEmpty() && QFileInfo(options_.program).isFile();
}

quint16 StreamingServerProcess::parseReadyLine(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (!trimmed.startsWith(kReadyPrefix)) {
        return 0;
    }
    const QByteArray url = trimmed.mid(kReadyPrefix.size()).trimmed();
    const qsizetype colon = url.lastIndexOf(':');
    if (!url.startsWith("http://127.0.0.1:") || colon < 0) {
        return 0;
    }
    bool ok = false;
    const uint port = url.mid(colon + 1).toUInt(&ok);
    return ok && port > 0 && port <= 65535 ? quint16(port) : 0;
}

void StreamingServerProcess::ensureStarted(QObject *context, std::function<void(bool, const QString &)> done)
{
    if (isRunning()) {
        QPointer<QObject> guard(context);
        const bool hasContext = context != nullptr;
        QMetaObject::invokeMethod(this, [guard, hasContext, done] {
            if (!hasContext || guard) done(true, {});
        }, Qt::QueuedConnection);
        return;
    }
    waiters_.append(Waiter{context, context != nullptr, std::move(done)});
    if (process_) {
        return; // already starting
    }
    if (!isAvailable()) {
        lastError_ = tr("The built-in streaming engine is missing (%1).")
                         .arg(QDir::toNativeSeparators(options_.program.isEmpty() ? QStringLiteral("lambda-stream-server.exe")
                                                                                   : options_.program));
        finishWaiters(false, lastError_);
        return;
    }
    retriedWithAnyPort_ = false;
    launch(options_.port);
}

void StreamingServerProcess::launch(quint16 port)
{
    stopping_ = false;
    stdout_.clear();
    stderr_.clear();
    launchPort_ = port;

    QStringList args{QStringLiteral("--port"), QString::number(port)};
    if (!options_.configDir.isEmpty()) {
        QDir().mkpath(options_.configDir);
        args << QStringLiteral("--config-dir") << QDir::toNativeSeparators(options_.configDir);
    }
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (!options_.cacheDir.isEmpty()) {
        QDir().mkpath(options_.cacheDir);
        args << QStringLiteral("--cache-dir") << QDir::toNativeSeparators(options_.cacheDir);
        // The server downloads HTTP archives to the temp folder and keeps
        // them; inside the cache they are removed by "Clear cache".
        const QString temp = QDir(options_.cacheDir).filePath(QStringLiteral("tmp"));
        QDir().mkpath(temp);
        environment.insert(QStringLiteral("TMP"), QDir::toNativeSeparators(temp));
        environment.insert(QStringLiteral("TEMP"), QDir::toNativeSeparators(temp));
    }
    if (qEnvironmentVariableIsSet("LAMBDA_STREAM_SERVER_LOG")) {
        args << QStringLiteral("--log");
    }

    process_ = new QProcess(this);
    process_->setProgram(options_.program);
    process_->setArguments(args);
    process_->setProcessEnvironment(environment);
    process_->setWorkingDirectory(QFileInfo(options_.program).absolutePath());
#ifdef Q_OS_WIN
    process_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(process_, &QProcess::readyReadStandardOutput, this, &StreamingServerProcess::handleOutput);
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        stderr_ += process_->readAllStandardError();
        if (stderr_.size() > kMaxStderr) {
            stderr_ = stderr_.right(kMaxStderr);
        }
    });
    connect(process_, &QProcess::finished, this, &StreamingServerProcess::handleFinished);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            lastError_ = tr("The built-in streaming engine could not start: %1").arg(process_->errorString());
            process_->deleteLater();
            process_ = nullptr;
            readyTimer_->stop();
            finishWaiters(false, lastError_);
        }
    });

    if (!readyTimer_) {
        readyTimer_ = new QTimer(this);
        readyTimer_->setSingleShot(true);
        connect(readyTimer_, &QTimer::timeout, this, [this] {
            if (process_ && !isRunning()) {
                lastError_ = tr("The built-in streaming engine did not start in time.");
                stopping_ = true;
                process_->kill();
            }
        });
    }
    readyTimer_->start(kReadyTimeoutMs);
    process_->start(QIODevice::ReadWrite);
}

void StreamingServerProcess::handleOutput()
{
    stdout_ += process_->readAllStandardOutput();
    qsizetype newline;
    while ((newline = stdout_.indexOf('\n')) >= 0) {
        const QByteArray line = stdout_.left(newline);
        stdout_.remove(0, newline + 1);
        const quint16 port = parseReadyLine(line);
        if (port == 0 || isRunning()) {
            continue;
        }
        readyTimer_->stop();
        port_ = port;
        url_ = QStringLiteral("http://127.0.0.1:%1/").arg(port);
        lastError_.clear();
        if (options_.port != port) {
            options_.port = port;
            emit portChanged(port);
        }
        emit started(url_);
        finishWaiters(true, {});
    }
}

void StreamingServerProcess::handleFinished()
{
    const bool wasRunning = isRunning();
    const bool requested = stopping_;
    QString error;
    const qsizetype at = stderr_.lastIndexOf(kErrorPrefix);
    if (at >= 0) {
        error = QString::fromUtf8(stderr_.mid(at + kErrorPrefix.size())).section(QLatin1Char('\n'), 0, 0).trimmed();
    }
    process_->deleteLater();
    process_ = nullptr;
    readyTimer_->stop();
    url_.clear();

    if (wasRunning) {
        emit stopped();
        if (requested) {
            return;
        }
        // Crashed while serving: restart, but not in a loop.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        crashTimes_.append(now);
        while (!crashTimes_.isEmpty() && now - crashTimes_.first() > kCrashWindowMs) {
            crashTimes_.removeFirst();
        }
        if (crashTimes_.size() <= kMaxCrashRestarts) {
            launch(options_.port);
        } else {
            lastError_ = tr("The built-in streaming engine keeps stopping%1.")
                             .arg(error.isEmpty() ? QString() : QStringLiteral(": ") + error);
        }
        return;
    }

    // Failed before it was ready. A taken port gets one retry on any free port.
    if (!requested && launchPort_ != 0 && !retriedWithAnyPort_ && error.contains(QLatin1String("bind"))) {
        retriedWithAnyPort_ = true;
        launch(0);
        return;
    }
    if (lastError_.isEmpty() || !requested) {
        lastError_ = error.isEmpty() ? tr("The built-in streaming engine stopped unexpectedly.")
                                     : tr("The built-in streaming engine failed: %1").arg(error);
    }
    finishWaiters(false, lastError_);
}

void StreamingServerProcess::finishWaiters(bool ok, const QString &error)
{
    const QList<Waiter> waiters = std::exchange(waiters_, {});
    for (const Waiter &waiter : waiters) {
        if (!waiter.hasContext || waiter.context) {
            waiter.done(ok, error);
        }
    }
}

void StreamingServerProcess::stop()
{
    if (!process_) {
        return;
    }
    stopping_ = true;
    QProcess *process = process_;
    if (process->state() != QProcess::NotRunning) {
        process->closeWriteChannel();
        if (!process->waitForFinished(kStopTimeoutMs)) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
    // handleFinished ran from waitForFinished; if not (never started), clean up.
    if (process_ == process) {
        disconnect(process, nullptr, this, nullptr);
        process->deleteLater();
        process_ = nullptr;
        const bool wasRunning = isRunning();
        url_.clear();
        if (wasRunning) {
            emit stopped();
        }
        finishWaiters(false, tr("The built-in streaming engine was stopped."));
    }
}

} // namespace stremio
