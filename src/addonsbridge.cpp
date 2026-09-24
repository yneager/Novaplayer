#include "addonsbridge.h"

#include "addonconfigurewindow.h"
#include "stremiobackend.h"
#include "stremio/addonurl.h"
#include "stremio/transport.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <memory>

using namespace stremio;

namespace {

QJsonObject outcomeJson(const InstallOutcome &outcome)
{
    QJsonObject object{
        {"status", installStatusName(outcome.status)},
        {"message", outcome.message},
        {"transportUrl", outcome.transportUrl},
        {"addonName", outcome.addonName},
        {"imported", outcome.imported},
        {"skipped", outcome.skipped},
        {"failed", outcome.failed},
    };
    if (!outcome.configureUrl.isEmpty()) {
        object.insert("configureUrl", outcome.configureUrl);
    }
    return object;
}

bool isWebUrl(const QString &url)
{
    const QString scheme = QUrl(url).scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

} // namespace

AddonsBridge::AddonsBridge(StremioBackend *backend, QWidget *dialogParent, QObject *parent)
    : QObject(parent)
    , backend_(backend)
    , dialogParent_(dialogParent)
{
    connect(backend_->addons(), &AddonManager::changed, this, &AddonsBridge::emitState);
    connect(backend_->streamingServer(), &StreamingServer::availabilityChanged, this, [this] {
        emit streamingServerChanged(streamingServerStatus());
    });
}

// ---- Addons -------------------------------------------------------------------

QJsonObject AddonsBridge::streamingServerStatus() const
{
    const StreamingServer *server = backend_->streamingServer();
    return {
        {"url", server->url()},
        {"available", server->isAvailable()},
        {"checked", server->lastChecked().isValid()},
    };
}

QJsonObject AddonsBridge::state() const
{
    QJsonArray addons;
    for (const InstalledAddon &addon : backend_->addons()->addons()) {
        QJsonObject item = ContentService::descriptorSummary(addon.descriptor);
        item.insert("enabled", addon.enabled);
        item.insert("lastError", addon.lastError);
        item.insert("updated", addon.updated.toString(Qt::ISODate));
        item.insert("host", displayHost(addon.descriptor.transportUrl));
        addons.append(item);
    }
    return {
        {"addons", addons},
        {"streamingServer", streamingServerStatus()},
        {"defaults", QJsonArray::fromStringList(StremioBackend::defaultAddonUrls())},
    };
}

void AddonsBridge::emitState()
{
    emit addonsChanged(state());
}

void AddonsBridge::install(const QString &input, const QString &requestId)
{
    backend_->addons()->install(input, [this, requestId](const InstallOutcome &outcome) {
        emit installResult(requestId, outcomeJson(outcome));
    });
}

void AddonsBridge::installDefaults(const QString &requestId)
{
    const QStringList urls = StremioBackend::defaultAddonUrls();
    auto remaining = std::make_shared<int>(int(urls.size()));
    auto summary = std::make_shared<InstallOutcome>();
    summary->status = InstallOutcome::Status::CollectionImported;
    for (const QString &url : urls) {
        backend_->addons()->install(url, [this, requestId, remaining, summary](const InstallOutcome &outcome) {
            if (outcome.status == InstallOutcome::Status::Installed || outcome.status == InstallOutcome::Status::Updated) {
                ++summary->imported;
            } else if (outcome.status == InstallOutcome::Status::AlreadyInstalled) {
                ++summary->skipped;
            } else {
                ++summary->failed;
                summary->message = outcome.message;
            }
            if (--(*remaining) == 0) {
                if (summary->imported + summary->skipped == 0) {
                    summary->status = InstallOutcome::Status::Failed;
                } else {
                    summary->message = QStringLiteral("Installed Cinemeta and OpenSubtitles v3.");
                }
                emit installResult(requestId, outcomeJson(*summary));
            }
        });
    }
}

void AddonsBridge::importCollectionText(const QString &text, const QString &requestId)
{
    const QString trimmed = text.trimmed();
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
    QJsonValue json;
    if (error.error == QJsonParseError::NoError) {
        json = document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object());
    } else {
        // A pasted list of manifest URLs, one per line.
        QJsonArray urls;
        for (const QString &line : trimmed.split(QRegularExpression(QStringLiteral("[\\r\\n\\s,]+")), Qt::SkipEmptyParts)) {
            urls.append(line);
        }
        json = urls;
    }
    backend_->addons()->importCollection(json, [this, requestId](const InstallOutcome &outcome) {
        emit installResult(requestId, outcomeJson(outcome));
    });
}

void AddonsBridge::importCollectionFile(const QString &requestId)
{
    const QString path = QFileDialog::getOpenFileName(
        dialogParent_, QStringLiteral("Import addons"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStringLiteral("Addon collections (*.json);;All files (*.*)"));
    if (path.isEmpty()) {
        emit installResult(requestId, QJsonObject{{"status", "cancelled"}});
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit installResult(requestId, QJsonObject{{"status", "failed"}, {"message", QStringLiteral("Could not read the file.")}});
        return;
    }
    importCollectionText(QString::fromUtf8(file.readAll()), requestId);
}

void AddonsBridge::exportCollection()
{
    const QString path = QFileDialog::getSaveFileName(
        dialogParent_, QStringLiteral("Export addons"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/lambda-addons.json"),
        QStringLiteral("Addon collections (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(backend_->addons()->exportCollection()).toJson(QJsonDocument::Indented));
        if (file.commit()) {
            emit notify(QStringLiteral("Exported %1 addon(s).").arg(backend_->addons()->addons().size()), false);
            return;
        }
    }
    emit notify(QStringLiteral("Could not write the export file."), true);
}

bool AddonsBridge::remove(const QString &transportUrl)
{
    return backend_->addons()->remove(transportUrl);
}

bool AddonsBridge::setEnabled(const QString &transportUrl, bool enabled)
{
    return backend_->addons()->setEnabled(transportUrl, enabled);
}

bool AddonsBridge::move(const QString &transportUrl, int toIndex)
{
    return backend_->addons()->move(transportUrl, toIndex);
}

void AddonsBridge::refresh(const QString &transportUrl)
{
    if (transportUrl.isEmpty()) {
        backend_->client()->clearMemoryCache();
        backend_->addons()->refreshAll();
        return;
    }
    backend_->addons()->refresh(transportUrl, [this](bool ok, const QString &error) {
        emit notify(ok ? QStringLiteral("Addon refreshed.") : QStringLiteral("Refresh failed: %1").arg(error), !ok);
    });
}

void AddonsBridge::configure(const QString &transportUrl)
{
    const QString url = isWebUrl(transportUrl) && transportUrl.contains(QStringLiteral("manifest.json"))
        ? configureUrl(transportUrl)
        : transportUrl;
    if (!isWebUrl(url)) {
        return;
    }
    if (!configureWindow_) {
        configureWindow_ = new AddonConfigureWindow(dialogParent_);
        connect(configureWindow_, &AddonConfigureWindow::installRequested, this, [this](const QString &manifestUrl) {
            backend_->addons()->install(manifestUrl, [this](const InstallOutcome &outcome) {
                emit installResult(QStringLiteral("configure"), outcomeJson(outcome));
                const bool ok = outcome.status == InstallOutcome::Status::Installed
                    || outcome.status == InstallOutcome::Status::Updated
                    || outcome.status == InstallOutcome::Status::AlreadyInstalled;
                if (ok && configureWindow_) {
                    configureWindow_->close();
                }
            });
        });
    }
    configureWindow_->openUrl(url);
}

void AddonsBridge::openExternal(const QString &url)
{
    if (isWebUrl(url)) {
        QDesktopServices::openUrl(QUrl(url, QUrl::TolerantMode));
    }
}

void AddonsBridge::setStreamingServerUrl(const QString &url)
{
    backend_->setStreamingServerUrl(url);
    checkStreamingServer();
}

void AddonsBridge::checkStreamingServer()
{
    backend_->streamingServer()->probe(this, [this](bool) {
        emit streamingServerChanged(streamingServerStatus());
    }, true);
}

QJsonArray AddonsBridge::addonCatalogRows() const
{
    QJsonArray rows;
    for (const CatalogRowInfo &row : backend_->content()->addonCatalogs()) {
        rows.append(row.toJson());
    }
    return rows;
}

// ---- Catalogs -----------------------------------------------------------------

QJsonArray AddonsBridge::boardRows() const
{
    QJsonArray rows;
    for (const CatalogRowInfo &row : backend_->content()->board()) {
        rows.append(row.toJson());
    }
    return rows;
}

QJsonArray AddonsBridge::searchRows(const QString &query) const
{
    QJsonArray rows;
    for (const CatalogRowInfo &row : backend_->content()->search(query)) {
        rows.append(row.toJson());
    }
    return rows;
}

void AddonsBridge::track(const QString &token, QNetworkReply *reply)
{
    if (!reply) {
        return;
    }
    QList<QPointer<QNetworkReply>> &list = pending_[token];
    list.removeIf([](const QPointer<QNetworkReply> &item) { return item.isNull(); });
    list.append(reply);
}

void AddonsBridge::loadCatalog(const QString &token, const QJsonObject &request, bool force)
{
    const ResourceRequest resource = ResourceRequest::fromJson(request);
    // Only requests for installed, enabled addons are served.
    if (!backend_->content()->addon(resource.base)) {
        QJsonObject result{{"request", request}, {"status", "error"}, {"error", QStringLiteral("Addon is not installed")}};
        emit catalogResult(token, result);
        return;
    }
    track(token, backend_->client()->fetchResource(resource, this, [this, token](const ResourceResult &result) {
        if (result.error.kind == FetchError::Kind::Cancelled) {
            return;
        }
        QJsonObject json = ContentService::resultJson(result);
        if (const Descriptor *addon = backend_->content()->addon(result.request.base)) {
            json.insert("addon", ContentService::addonJson(*addon));
        }
        emit catalogResult(token, json);
    }, force));
}

QJsonValue AddonsBridge::nextPage(const QJsonObject &request, int lastPageItems) const
{
    const auto next = backend_->content()->nextPage(ResourceRequest::fromJson(request), lastPageItems);
    return next ? QJsonValue(next->toJson()) : QJsonValue(QJsonValue::Null);
}

QJsonObject AddonsBridge::discover(const QJsonValue &selected, const QJsonArray &pageSizes) const
{
    std::optional<ResourceRequest> request;
    if (selected.isObject()) {
        request = ResourceRequest::fromJson(selected.toObject());
    }
    QList<int> sizes;
    for (const QJsonValue &size : pageSizes) {
        sizes.append(size.toInt());
    }
    return backend_->content()->discover(request, sizes);
}

// ---- Details ------------------------------------------------------------------

void AddonsBridge::loadMeta(const QString &token, const QString &type, const QString &id, bool force)
{
    const QList<PlannedRequest> plan = backend_->content()->metaPlan(type, id);
    MetaSession session;
    QJsonArray slotList;
    for (const PlannedRequest &planned : plan) {
        session.requests.append(planned.request);
        session.items.append(std::nullopt);
        QJsonObject slot{{"request", planned.request.toJson()}};
        if (const Descriptor *addon = backend_->content()->addon(planned.request.base)) {
            slot.insert("addon", ContentService::addonJson(*addon));
        }
        slotList.append(slot);
    }
    metaSessions_.insert(token, session);
    emit metaPlan(token, slotList);
    for (int index = 0; index < plan.size(); ++index) {
        track(token, backend_->client()->fetchResource(plan[index].request, this, [this, token, index](const ResourceResult &result) {
            if (result.error.kind == FetchError::Kind::Cancelled) {
                return;
            }
            auto session = metaSessions_.find(token);
            if (session != metaSessions_.end() && index < session->items.size() && result.response
                && result.response->meta) {
                session->items[index] = result.response->meta;
            }
            emit metaResult(token, index, ContentService::resultJson(result));
        }, force));
    }
}

void AddonsBridge::loadStreams(const QString &token, const QString &type, const QString &videoId,
                               const QString &metaToken, bool force)
{
    // meta_details.rs meta_streams_update + serialize_meta_details: streams
    // embedded in the selected video (or a yt_id:CH:VID id) are shown
    // instead of the addon stream responses.
    const auto session = metaSessions_.constFind(metaToken);
    if (session != metaSessions_.cend()) {
        for (int i = 0; i < session->items.size(); ++i) {
            if (!session->items[i]) {
                continue;
            }
            const Video *video = session->items[i]->video(videoId);
            if (!video) {
                break; // only the first ready meta item decides
            }
            QList<Stream> embedded = video->streams;
            if (embedded.isEmpty()) {
                if (auto youtube = Stream::youtubeFromVideoId(video->id)) {
                    embedded.append(*youtube);
                }
            }
            if (embedded.isEmpty()) {
                break;
            }
            const ResourceRequest request{session->requests[i].base,
                                          {QString::fromLatin1(kStreamResource), type, videoId, {}}};
            QJsonObject slot{{"request", request.toJson()}};
            if (const Descriptor *addon = backend_->content()->addon(request.base)) {
                slot.insert("addon", ContentService::addonJson(*addon));
            }
            emit streamsPlan(token, QJsonObject{{"embedded", true}, {"slots", QJsonArray{slot}}});
            QJsonArray streams;
            for (const Stream &stream : embedded) {
                streams.append(stream.toJson());
            }
            emit streamsResult(token, 0, QJsonObject{{"request", request.toJson()}, {"status", "ready"}, {"streams", streams}});
            return;
        }
    }

    const QList<PlannedRequest> plan = backend_->content()->streamPlan(type, videoId);
    QJsonArray slotList;
    for (const PlannedRequest &planned : plan) {
        QJsonObject slot{{"request", planned.request.toJson()}};
        if (const Descriptor *addon = backend_->content()->addon(planned.request.base)) {
            slot.insert("addon", ContentService::addonJson(*addon));
        }
        slotList.append(slot);
    }
    emit streamsPlan(token, QJsonObject{{"embedded", false}, {"slots", slotList}});
    for (int index = 0; index < plan.size(); ++index) {
        track(token, backend_->client()->fetchResource(plan[index].request, this, [this, token, index](const ResourceResult &result) {
            if (result.error.kind != FetchError::Kind::Cancelled) {
                emit streamsResult(token, index, ContentService::resultJson(result));
            }
        }, force));
    }
}

void AddonsBridge::play(const QJsonObject &streamJson, const QJsonObject &context)
{
    QString error;
    const auto stream = Stream::fromJson(streamJson.value(QStringLiteral("raw")), &error);
    if (!stream) {
        emit playStatus(QJsonObject{{"state", "error"}, {"message", QStringLiteral("Invalid stream: %1").arg(error)}});
        return;
    }
    AddonPlayback playback;
    playback.stream = *stream;
    playback.streamTransportUrl = context.value(QStringLiteral("streamTransportUrl")).toString();
    playback.metaTransportUrl = context.value(QStringLiteral("metaTransportUrl")).toString();
    playback.type = context.value(QStringLiteral("type")).toString();
    playback.metaId = context.value(QStringLiteral("metaId")).toString();
    playback.videoId = context.value(QStringLiteral("videoId")).toString();
    playback.title = context.value(QStringLiteral("title")).toString();
    playback.episodeLabel = context.value(QStringLiteral("episodeLabel")).toString();
    playback.addonName = context.value(QStringLiteral("addonName")).toString();
    playback.poster = context.value(QStringLiteral("poster")).toString();
    playback.background = context.value(QStringLiteral("background")).toString();
    if (context.value(QStringLiteral("season")).isDouble() && context.value(QStringLiteral("episode")).isDouble()) {
        playback.seriesInfo = SeriesInfo{context.value(QStringLiteral("season")).toInt(),
                                         context.value(QStringLiteral("episode")).toInt()};
    }

    emit playStatus(QJsonObject{{"state", "resolving"}});
    ResolveContext resolveContext;
    resolveContext.seriesInfo = playback.seriesInfo;
    backend_->resolver()->resolve(*stream, resolveContext, this, [this, playback](const PlaybackSource &source) mutable {
        switch (source.kind) {
        case PlaybackSource::Kind::Play:
            playback.source = source;
            emit playStatus(QJsonObject{{"state", "playing"}});
            emit backend_->playRequested(playback);
            break;
        case PlaybackSource::Kind::OpenExternally:
            openExternal(source.url);
            emit playStatus(QJsonObject{{"state", "external"}, {"message", QStringLiteral("Opened in your browser.")}});
            break;
        case PlaybackSource::Kind::Unsupported:
            emit playStatus(QJsonObject{{"state", "error"}, {"message", source.error}});
            break;
        }
    });
}

void AddonsBridge::cancel(const QString &token)
{
    for (const QPointer<QNetworkReply> &reply : pending_.take(token)) {
        AddonClient::cancel(reply);
    }
    metaSessions_.remove(token);
}
