// AddonClient against a local mock addon: what goes over the wire, status and
// JSON handling, timeouts, caching and redirects.

#include "mockaddonserver.h"
#include "stremio/addonclient.h"

#include <QtTest>

using namespace stremio;

class AddonClientTest : public QObject
{
    Q_OBJECT

private slots:
    void configuredPathAndQueryOnTheWire();
    void onlyStatus200And201Succeed();
    void invalidJsonAndHtml();
    void timeoutIsPerRequest();
    void memoryCacheAndForce();
    void redirectsAreFollowed();
    void legacyTransportRequest();
    void manifestFetch();
    void cancel();
};

namespace {

const QByteArray kMetas = R"({"metas":[{"id":"tt1","type":"movie","name":"One"}]})";

ResourceResult fetch(AddonClient &client, const ResourceRequest &request, bool force = false)
{
    std::optional<ResourceResult> out;
    client.fetchResource(request, &client, [&out](const ResourceResult &r) { out = r; }, force);
    QTest::qWaitFor([&out] { return out.has_value(); }, 20000);
    return out.value_or(ResourceResult{});
}

} // namespace

void AddonClientTest::configuredPathAndQueryOnTheWire()
{
    MockAddonServer server;
    const QByteArray config = "/%7B%22key%22%3A%22a%2Fb%22%7D/providers=yts,eztv%7Crarbg&lang=%F0%9F%94%B4";
    server.route(config + "/catalog/movie/top/search=Harry%20Potter&skip=100.json?token=s3cr%2Bt", kMetas);
    AddonClient client;
    const ResourceRequest request{server.base() + config + "/manifest.json?token=s3cr%2Bt",
                                  {"catalog", "movie", "top", {{"search", "Harry Potter"}, {"skip", "100"}}}};
    const ResourceResult result = fetch(client, request);
    QVERIFY2(!result.error.isError(), qPrintable(result.error.toString() + " " + server.requests().join(' ')));
    QCOMPARE(result.response->metas.size(), 1);
    QCOMPARE(server.requests(), QStringList{QString::fromUtf8(config + "/catalog/movie/top/search=Harry%20Potter&skip=100.json?token=s3cr%2Bt")});
}

void AddonClientTest::onlyStatus200And201Succeed()
{
    MockAddonServer server;
    server.route("/a/catalog/movie/top.json", kMetas, 201);
    server.route("/b/catalog/movie/top.json", kMetas, 204);
    server.route("/c/catalog/movie/top.json", "{\"err\":1}", 500);
    AddonClient client;
    QVERIFY(!fetch(client, {server.base() + "/a/manifest.json", {"catalog", "movie", "top", {}}}).error.isError());
    const ResourceResult noContent = fetch(client, {server.base() + "/b/manifest.json", {"catalog", "movie", "top", {}}});
    QCOMPARE(noContent.error.kind, FetchError::Kind::HttpStatus);
    QCOMPARE(noContent.error.httpStatus, 204);
    const ResourceResult serverError = fetch(client, {server.base() + "/c/manifest.json", {"catalog", "movie", "top", {}}});
    QCOMPARE(serverError.error.httpStatus, 500);
    const ResourceResult notFound = fetch(client, {server.base() + "/d/manifest.json", {"catalog", "movie", "top", {}}});
    QCOMPARE(notFound.error.httpStatus, 404);
}

void AddonClientTest::invalidJsonAndHtml()
{
    MockAddonServer server;
    server.route("/html/meta/movie/tt1.json", "<html><body>Cloudflare</body></html>");
    server.route("/bad/meta/movie/tt1.json", "{\"meta\": ");
    server.route("/wrong/meta/movie/tt1.json", "{\"streams\": [], \"meta\": {}}");
    AddonClient client;
    const ResourceResult html = fetch(client, {server.base() + "/html/manifest.json", {"meta", "movie", "tt1", {}}});
    QCOMPARE(html.error.kind, FetchError::Kind::InvalidJson);
    QVERIFY(html.error.message.contains("web page"));
    QCOMPARE(fetch(client, {server.base() + "/bad/manifest.json", {"meta", "movie", "tt1", {}}}).error.kind,
             FetchError::Kind::InvalidJson);
    QCOMPARE(fetch(client, {server.base() + "/wrong/manifest.json", {"meta", "movie", "tt1", {}}}).error.kind,
             FetchError::Kind::UnexpectedResponse);
}

void AddonClientTest::timeoutIsPerRequest()
{
    MockAddonServer server;
    MockAddonServer::Route hang;
    hang.neverAnswer = true;
    server.route("/slow/stream/movie/tt1.json", hang);
    server.route("/fast/stream/movie/tt1.json", R"({"streams":[{"url":"https://x/y.mp4"}]})");
    AddonClient client;
    client.setTimeout(700);

    std::optional<ResourceResult> slow;
    std::optional<ResourceResult> fast;
    QElapsedTimer timer;
    timer.start();
    client.fetchResource({server.base() + "/slow/manifest.json", {"stream", "movie", "tt1", {}}}, &client,
                         [&slow](const ResourceResult &r) { slow = r; });
    client.fetchResource({server.base() + "/fast/manifest.json", {"stream", "movie", "tt1", {}}}, &client,
                         [&fast](const ResourceResult &r) { fast = r; });
    QTRY_VERIFY_WITH_TIMEOUT(fast.has_value(), 5000);
    QVERIFY(!slow.has_value()); // the slow addon does not hold back the fast one
    QCOMPARE(fast->response->streams.size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(slow.has_value(), 5000);
    QCOMPARE(slow->error.kind, FetchError::Kind::Timeout);
    QVERIFY(timer.elapsed() < 5000);
}

void AddonClientTest::memoryCacheAndForce()
{
    MockAddonServer server;
    server.route("/c/catalog/movie/top.json", kMetas);
    server.route("/e/catalog/movie/top.json", "{}", 500);
    AddonClient client;
    const ResourceRequest request{server.base() + "/c/manifest.json", {"catalog", "movie", "top", {}}};
    QVERIFY(!fetch(client, request).fromCache);
    QVERIFY(fetch(client, request).fromCache);
    QCOMPARE(server.count("/c/catalog/movie/top.json"), 1);
    QVERIFY(!fetch(client, request, true).fromCache);
    QCOMPARE(server.count("/c/catalog/movie/top.json"), 2);

    // Failures are never cached.
    const ResourceRequest failing{server.base() + "/e/manifest.json", {"catalog", "movie", "top", {}}};
    fetch(client, failing);
    fetch(client, failing);
    QCOMPARE(server.count("/e/catalog/movie/top.json"), 2);

    client.invalidateAddon(request.base);
    QVERIFY(!fetch(client, request).fromCache);
}

void AddonClientTest::redirectsAreFollowed()
{
    MockAddonServer server;
    MockAddonServer::Route redirect;
    redirect.status = 302;
    redirect.headers.emplaceBack("Location", "/new/manifest.json");
    server.route("/old/manifest.json", redirect);
    server.route("/new/manifest.json", R"({"id":"a","version":"1.0.0","name":"A","types":["movie"],"resources":["stream"]})");
    AddonClient client;
    std::optional<ManifestResult> result;
    client.fetchManifest(server.base() + "/old/manifest.json", &client, [&result](const ManifestResult &r) { result = r; });
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 5000);
    QVERIFY2(result->manifest, qPrintable(result->error.toString()));
    QCOMPARE(result->transportUrl, server.base() + "/old/manifest.json");
}

void AddonClientTest::legacyTransportRequest()
{
    MockAddonServer server;
    const QByteArray target = "/stremio/v1/q.json?b=eyJpZCI6MSwianNvbnJwYyI6IjIuMCIsIm1ldGhvZCI6InN0cmVhbS5maW5kIiwicGFyYW1zIjpbbnVsbCx7InF1ZXJ5Ijp7ImVwaXNvZGUiOjEsImltZGJfaWQiOiJ0dDAzODY2NzYiLCJzZWFzb24iOjUsInR5cGUiOiJzZXJpZXMifX1dfQ==";
    server.route(target, R"({"result":[{"infoHash":"24c8802e2624e17d46cd555f364debd949c2c392","fileIdx":0}]})");
    AddonClient client;
    const ResourceResult result = fetch(client, {server.base() + "/stremio/v1", {"stream", "series", "tt0386676:5:1", {}}});
    QVERIFY2(!result.error.isError(), qPrintable(result.error.toString() + server.requests().join(' ')));
    QCOMPARE(result.response->streams.size(), 1);
    QCOMPARE(result.response->streams[0].source.kind, StreamSourceKind::Torrent);
}

void AddonClientTest::manifestFetch()
{
    MockAddonServer server;
    server.route("/bad/manifest.json", R"({"id":"a","name":"no version","types":[],"resources":[]})");
    AddonClient client;
    std::optional<ManifestResult> result;
    client.fetchManifest(server.base() + "/bad/manifest.json", &client, [&result](const ManifestResult &r) { result = r; });
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 5000);
    QVERIFY(!result->manifest);
    QCOMPARE(result->error.kind, FetchError::Kind::UnexpectedResponse);
}

void AddonClientTest::cancel()
{
    MockAddonServer server;
    MockAddonServer::Route hang;
    hang.neverAnswer = true;
    server.route("/h/meta/movie/tt1.json", hang);
    AddonClient client;
    std::optional<ResourceResult> result;
    QNetworkReply *reply = client.fetchResource({server.base() + "/h/manifest.json", {"meta", "movie", "tt1", {}}}, &client,
                                                [&result](const ResourceResult &r) { result = r; });
    QTest::qWait(100);
    AddonClient::cancel(reply);
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 5000);
    QCOMPARE(result->error.kind, FetchError::Kind::Cancelled);

    // Destroying the context drops the callback.
    bool called = false;
    auto *context = new QObject;
    client.fetchResource({server.base() + "/h/manifest.json", {"meta", "movie", "tt1", {}}}, context,
                         [&called](const ResourceResult &) { called = true; });
    delete context;
    QTest::qWait(300);
    QVERIFY(!called);
}

QTEST_GUILESS_MAIN(AddonClientTest)
#include "tst_addonclient.moc"
