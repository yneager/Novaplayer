// URL construction. Expected strings are taken from stremio-core unit tests
// (src/unit_tests/**, legacy/mod.rs tests) and Debrify test/stremio_url_test.dart.

#include "stremio/legacytransport.h"
#include "stremio/transport.h"

#include <QtTest>

using namespace stremio;

class TransportTest : public QObject
{
    Q_OBJECT

private slots:
    void encodeUriComponentMatchesJavaScript();
    void resourcePaths();
    void extrasJoinedWithAmpersand();
    void queryTokenPreserved();
    void configuredPathNotReencoded();
    void transportKinds();
    void legacyCatalog();
    void legacyStreamImdb();
    void legacySubtitles();
    void legacyQueryFromId();
    void legacyManifestUrl();
    void extendOneOrdering();
};

void TransportTest::encodeUriComponentMatchesJavaScript()
{
    QCOMPARE(encodeUriComponent("tt1:1:7"), QString("tt1%3A1%3A7"));
    QCOMPARE(encodeUriComponent("Harry Potter"), QString("Harry%20Potter"));
    QCOMPARE(encodeUriComponent("a-_.!~*'()b"), QString("a-_.!~*'()b"));
    QCOMPARE(encodeUriComponent("rock&roll/=?#"), QString("rock%26roll%2F%3D%3F%23"));
    QCOMPARE(encodeUriComponent(QString::fromUtf8("é🎬")), QString("%C3%A9%F0%9F%8E%AC"));
}

void TransportTest::resourcePaths()
{
    const QString base = "https://v3-cinemeta.strem.io/manifest.json";
    QCOMPARE(resourceUrl(base, {"catalog", "movie", "top", {}}),
             QString("https://v3-cinemeta.strem.io/catalog/movie/top.json"));
    QCOMPARE(resourceUrl(base, {"catalog", "movie", "top", {{"search", "Harry Potter"}}}),
             QString("https://v3-cinemeta.strem.io/catalog/movie/top/search=Harry%20Potter.json"));
    QCOMPARE(resourceUrl("https://addon_1.com/manifest.json", {"stream", "series", "tt1:1:7", {}}),
             QString("https://addon_1.com/stream/series/tt1%3A1%3A7.json"));
    QCOMPARE(resourceUrl("https://addon/manifest.json", {"meta", "tv", "pure:axn", {}}),
             QString("https://addon/meta/tv/pure%3Aaxn.json"));
}

void TransportTest::extrasJoinedWithAmpersand()
{
    QCOMPARE(resourceUrl("https://addon/manifest.json",
                         {"catalog", "tv", "guide", {{"date", "2026-07-02"}, {"skip", "1"}}}),
             QString("https://addon/catalog/tv/guide/date=2026-07-02&skip=1.json"));
}

void TransportTest::queryTokenPreserved()
{
    const QString transport = "https://addon.invalid/config/manifest.json?token=secret";
    QCOMPARE(resourceUrl(transport, {"subtitles", "series", "tt123:1:2", {}}),
             QString("https://addon.invalid/config/subtitles/series/tt123%3A1%3A2.json?token=secret"));
    QCOMPARE(resourceUrl(transport, {"catalog", "movie", "search", {{"search", "rock&roll"}}}),
             QString("https://addon.invalid/config/catalog/movie/search/search=rock%26roll.json?token=secret"));
    QCOMPARE(manifestUrl(transport), transport);
}

void TransportTest::configuredPathNotReencoded()
{
    // Configured paths carry encoded JSON/base64/emoji: never touched.
    const QString transport =
        "https://47623cd32ba8-stremio-addon-ratings.baby-beamup.club/mdbListApiKey=KEY&ratings=metacritic%2Ctomatoes"
        "&marker=%F0%9F%94%B4/manifest.json";
    QCOMPARE(resourceUrl(transport, {"stream", "movie", "tt0111161", {}}),
             QString("https://47623cd32ba8-stremio-addon-ratings.baby-beamup.club/mdbListApiKey=KEY&ratings=metacritic%2Ctomatoes"
                     "&marker=%F0%9F%94%B4/stream/movie/tt0111161.json"));
    const QString aio =
        "https://aiostreams.viren070.me/stremio/80060214-34a1-4bc6-943a-f20390c3a2ff/"
        "eyJpdiI6ImxEcDNJY3FvdEpBa0lrTkU1bjQ2M2c9PSIsImVuY3J5cHRlZCI6InNlMTNTRmJQV085M1hGcXRJa1J0R2VBN1Vvd1hLYVlEK1RldlZ6WHJXOGM9IiwidHlwZSI6ImFpb0VuY3J5cHQifQ/manifest.json";
    QCOMPARE(resourceUrl(aio, {"stream", "series", "tt0386676:5:1", {}}),
             QString(aio).replace("/manifest.json", "/stream/series/tt0386676%3A5%3A1.json"));
    QCOMPARE(resourceUrl("https://x.example/%7B%22a%22%3A1%7D/manifest.json", {"meta", "movie", "tt1", {}}),
             QString("https://x.example/%7B%22a%22%3A1%7D/meta/movie/tt1.json"));
}

void TransportTest::transportKinds()
{
    QCOMPARE(transportKind("https://a.b/manifest.json"), TransportKind::Http);
    QCOMPARE(transportKind("http://127.0.0.1:11470/local-addon/manifest.json"), TransportKind::Http);
    QCOMPARE(transportKind("https://opensubtitles.strem.io/stremio/v1"), TransportKind::Legacy);
    QCOMPARE(transportKind("https://a.b/addon"), TransportKind::Unsupported);
    QCOMPARE(transportKind("ftp://a.b/manifest.json"), TransportKind::Unsupported);
    QString error;
    QVERIFY(resourceUrl("https://a.b/addon", {"meta", "movie", "tt1", {}}, &error).isEmpty());
    QVERIFY(error.contains("manifest.json"));
}

void TransportTest::legacyCatalog()
{
    QCOMPARE(resourceUrl("https://stremio-mixer.schneider.ax/stremioget/stremio/v1",
                         {"catalog", "tv", "popularities.mixer", {}}),
             QString("https://stremio-mixer.schneider.ax/stremioget/stremio/v1/q.json?b=eyJpZCI6MSwianNvbnJwYyI6IjIuMCIsIm1ldGhvZCI6Im1ldGEuZmluZCIsInBhcmFtcyI6W251bGwseyJsaW1pdCI6MTAwLCJxdWVyeSI6eyJ0eXBlIjoidHYifSwic2tpcCI6MCwic29ydCI6eyJwb3B1bGFyaXRpZXMubWl4ZXIiOi0xLCJwb3B1bGFyaXR5IjotMX19XX0="));
}

void TransportTest::legacyStreamImdb()
{
    QCOMPARE(resourceUrl("https://legacywatchhub.strem.io/stremio/v1", {"stream", "series", "tt0386676:5:1", {}}),
             QString("https://legacywatchhub.strem.io/stremio/v1/q.json?b=eyJpZCI6MSwianNvbnJwYyI6IjIuMCIsIm1ldGhvZCI6InN0cmVhbS5maW5kIiwicGFyYW1zIjpbbnVsbCx7InF1ZXJ5Ijp7ImVwaXNvZGUiOjEsImltZGJfaWQiOiJ0dDAzODY2NzYiLCJzZWFzb24iOjUsInR5cGUiOiJzZXJpZXMifX1dfQ=="));
}

void TransportTest::legacySubtitles()
{
    QCOMPARE(resourceUrl("https://legacywatchhub.strem.io/stremio/v1", {"subtitles", "series", "tt0386676:5:1", {}}),
             QString("https://legacywatchhub.strem.io/stremio/v1/q.json?b=eyJpZCI6MSwianNvbnJwYyI6IjIuMCIsIm1ldGhvZCI6InN1YnRpdGxlcy5maW5kIiwicGFyYW1zIjpbbnVsbCx7InF1ZXJ5Ijp7Iml0ZW1IYXNoIjoidHQwMzg2Njc2IDUgMSJ9fV19"));
    Extra extra = extendOne({}, ExtraProp::named("videoHash"), QString("ffffffffff"));
    extra = extendOne(extra, ExtraProp::named("videoSize"), QString("1000000000"));
    QCOMPARE(resourceUrl("https://legacywatchhub.strem.io/stremio/v1", {"subtitles", "series", "tt0386676:5:1", extra}),
             QString("https://legacywatchhub.strem.io/stremio/v1/q.json?b=eyJpZCI6MSwianNvbnJwYyI6IjIuMCIsIm1ldGhvZCI6InN1YnRpdGxlcy5maW5kIiwicGFyYW1zIjpbbnVsbCx7InF1ZXJ5Ijp7Iml0ZW1IYXNoIjoidHQwMzg2Njc2IDUgMSIsInZpZGVvSGFzaCI6ImZmZmZmZmZmZmYiLCJ2aWRlb1NpemUiOjEwMDAwMDAwMDB9fV19"));
}

void TransportTest::legacyQueryFromId()
{
    QCOMPARE(legacy::queryFromId("tt0386676"), QJsonValue(QJsonObject{{"imdb_id", "tt0386676"}}));
    QCOMPARE(legacy::queryFromId("UC2312"), QJsonValue(QJsonObject{{"yt_id", "UC2312"}}));
    QCOMPARE(legacy::queryFromId("custom:test"), QJsonValue(QJsonObject{{"custom", "test"}}));
    QCOMPARE(legacy::queryFromId("tt0386676:5:2"),
             QJsonValue(QJsonObject{{"imdb_id", "tt0386676"}, {"season", 5}, {"episode", 2}}));
    QCOMPARE(legacy::queryFromId("yt_id:video"), QJsonValue(QJsonObject{{"yt_id", "video"}}));
    QCOMPARE(legacy::queryFromId("custom:test:vid"),
             QJsonValue(QJsonObject{{"custom", "test"}, {"video_id", "vid"}}));
}

void TransportTest::legacyManifestUrl()
{
    QCOMPARE(manifestUrl("https://opensubtitles.strem.io/stremio/v1"),
             QString("https://opensubtitles.strem.io/stremio/v1/q.json?b=eyJwYXJhbXMiOltdLCJtZXRob2QiOiJtZXRhIiwiaWQiOjEsImpzb25ycGMiOiIyLjAifQ=="));
}

void TransportTest::extendOneOrdering()
{
    // request.rs ExtraExt::extend_one: the changed property comes first.
    const ExtraProp skip = ExtraProp::named("skip");
    const Extra next = extendOne({{"search", "q"}}, skip, QString("100"));
    QCOMPARE(next, Extra({{"skip", "100"}, {"search", "q"}}));
    QCOMPARE(extendOne(next, skip, std::nullopt), Extra({{"search", "q"}}));

    ExtraProp multi = ExtraProp::named("genre", 2);
    Extra genres = extendOne({}, multi, QString("Action"));
    genres = extendOne(genres, multi, QString("Drama"));
    QCOMPARE(genres, Extra({{"genre", "Drama"}, {"genre", "Action"}}));
    genres = extendOne(genres, multi, QString("Comedy")); // limit 2
    QCOMPARE(genres, Extra({{"genre", "Comedy"}, {"genre", "Drama"}}));
    genres = extendOne(genres, multi, QString("Drama")); // toggles off
    QCOMPARE(genres, Extra({{"genre", "Comedy"}}));

    ExtraProp required = ExtraProp::named("genre");
    required.isRequired = true;
    QCOMPARE(extendOne({{"genre", "A"}}, required, std::nullopt), Extra({{"genre", "A"}}));
}

QTEST_GUILESS_MAIN(TransportTest)
#include "tst_transport.moc"
