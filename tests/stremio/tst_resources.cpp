// Addon response parsing (stremio-core response.rs, meta_item.rs, stream.rs).

#include "stremio/resources.h"
#include "testutil.h"

#include <QtTest>

using namespace stremio;

class ResourcesTest : public QObject
{
    Q_OBJECT

private slots:
    void responseNeedsExactlyOneKey();
    void nullAndSkippedItems();
    void metaPreviewLegacyFields();
    void seriesVideosSortedAndUnique();
    void movieGuessesStreamId();
    void invalidVideoFailsMeta();
    void streamSources();
    void malformedStreams_data();
    void malformedStreams();
    void streamsResponseSkipsBadItems();
    void streamInlineSubtitles();
    void subtitlesResponse();
    void youtubeVideoIds();
    void addonCatalogResponse();
};

void ResourcesTest::responseNeedsExactlyOneKey()
{
    QString error;
    QVERIFY(!ResourceResponse::fromJson(jsonValue("256"), &error));
    QVERIFY(error.contains("expected an Object"));
    QVERIFY(!ResourceResponse::fromJson(jsonValue(R"({"unknownVariant":{"test":1}})"), &error));
    QVERIFY(error.contains("didn't contain any of the required keys"));
    QVERIFY(!ResourceResponse::fromJson(jsonValue(R"({"metas":[],"metasDetailed":[]})"), &error));
    QVERIFY(error.contains("more than 1"));
    QVERIFY(!ResourceResponse::fromJson(jsonValue(R"({"metas":{"object_key":"value"}})"), &error));
}

void ResourcesTest::nullAndSkippedItems()
{
    const auto nullMetas = ResourceResponse::fromJson(jsonValue(R"({"metas":null})"));
    QVERIFY(nullMetas);
    QCOMPARE(nullMetas->itemCount(), 0);

    const auto metas = ResourceResponse::fromJson(jsonValue(R"({"metas":[
        {"id":"tt1","type":"movie","name":"Good"},
        {"type":"movie","name":"No id"},
        {"id":"tt2","type":"movie","poster":"not a url"},
        {"id":"tt3","type":"movie"}
    ],"cacheMaxAge":3600,"staleRevalidate":14400,"staleError":604800})"));
    QVERIFY(metas);
    QCOMPARE(metas->metas.size(), 2);
    QCOMPARE(metas->skippedItems, 2);
    QCOMPARE(*metas->cacheMaxAge, 3600);
    QCOMPARE(*metas->staleError, 604800);
    QCOMPARE(metas->metas[1].name, QString()); // name defaults to ""
}

void ResourcesTest::metaPreviewLegacyFields()
{
    const auto meta = MetaItemPreview::fromJson(jsonValue(R"({
        "id":"tt1254207","type":"movie","name":"Big Buck Bunny","releaseInfo":2008,"runtime":10,
        "imdbRating":6.5,"genres":["Animation","Comedy"],"posterShape":"regular",
        "released":"2008-05-20T00:00:00.000Z",
        "trailers":[{"source":"YE7VzlLtp-4","type":"Trailer"},{"source":"x","type":"Clip"}],
        "behaviorHints":{"defaultVideoId":"tt1254207"}
    })"));
    QVERIFY(meta);
    QCOMPARE(*meta->releaseInfo, QString("2008"));
    QCOMPARE(*meta->runtime, QString("10"));
    QCOMPARE(*meta->imdbRating, QString("6.5"));
    QCOMPARE(meta->genres, (QStringList{"Animation", "Comedy"}));
    QCOMPARE(meta->posterShape, QString("poster")); // unknown -> poster
    QCOMPARE(meta->trailerStreams.size(), 1);
    QCOMPARE(meta->trailerStreams[0].source.ytId, QString("YE7VzlLtp-4"));
    QVERIFY(meta->released);
    QCOMPARE(meta->released->date(), QDate(2008, 5, 20));
    QCOMPARE(*meta->behaviorHints.defaultVideoId, QString("tt1254207"));

    const auto linked = MetaItemPreview::fromJson(jsonValue(R"({"id":"x","type":"series","released":1211241600000,
        "links":[{"name":"8.1","category":"imdb","url":"https://imdb.com/title/x"},
                 {"name":"Drama","category":"Genres","url":"stremio:///discover/x"}],
        "genres":["ignored because links exist"]})"));
    QVERIFY(linked);
    QCOMPARE(linked->genres, QStringList{"Drama"});
    QCOMPARE(*linked->imdbRating, QString("8.1"));
    QVERIFY(linked->released);
}

void ResourcesTest::seriesVideosSortedAndUnique()
{
    const auto meta = MetaItem::fromJson(jsonValue(R"({"id":"tt1","type":"series","name":"Show","videos":[
        {"id":"tt1:2:1","title":"S2E1","season":2,"episode":1},
        {"id":"tt1:0:1","name":"Special","season":0,"episode":1},
        {"id":"tt1:1:2","title":"S1E2","season":1,"episode":2,"thumbnail":"https://t/2.jpg","released":"2020-01-08T00:00:00Z"},
        {"id":"tt1:1:1","title":"S1E1","season":1,"episode":1},
        {"id":"tt1:1:1","title":"duplicate","season":1,"episode":1},
        {"id":"trailer-no-series-info","title":"Extra"}
    ]})"));
    QVERIFY(meta);
    QStringList ids;
    for (const Video &video : meta->videos) ids << video.id;
    QCOMPARE(ids, (QStringList{"tt1:1:1", "tt1:1:2", "tt1:2:1", "tt1:0:1", "trailer-no-series-info"}));
    QCOMPARE(meta->videos[0].title, QString("S1E1")); // first duplicate kept
    QCOMPARE(meta->videos[3].title, QString("Special")); // "name" alias
    QVERIFY(!meta->guessStreamVideoId()); // series: the user picks an episode
    QVERIFY(meta->video("tt1:1:2")->thumbnail);
}

void ResourcesTest::movieGuessesStreamId()
{
    const auto movie = MetaItem::fromJson(jsonValue(R"({"id":"tt2","type":"movie","name":"M"})"));
    QCOMPARE(*movie->guessStreamVideoId(), QString("tt2"));
    const auto defaultVideo = MetaItem::fromJson(jsonValue(R"({"id":"yt_id:UC1","type":"channel",
        "behaviorHints":{"defaultVideoId":"yt_id:UC1:v1"},"videos":[{"id":"yt_id:UC1:v1"}]})"));
    QCOMPARE(*defaultVideo->guessStreamVideoId(), QString("yt_id:UC1:v1"));
    const auto live = MetaItem::fromJson(jsonValue(R"({"id":"pure:axn","type":"tv","videos":[{"id":"prog1"}]})"));
    QCOMPARE(*live->guessStreamVideoId(), QString("pure:axn"));
}

void ResourcesTest::invalidVideoFailsMeta()
{
    QString error;
    QVERIFY(!MetaItem::fromJson(jsonValue(R"({"id":"x","type":"series","videos":[{"title":"no id"}]})"), &error));
    QVERIFY(error.startsWith("video:"));
}

void ResourcesTest::streamSources()
{
    const auto url = Stream::fromJson(jsonValue(R"({"url":"https://example.com/v.mp4","name":"Addon\n4k",
        "title":"Legacy description","behaviorHints":{"bingeGroup":"g","notWebReady":true,
        "proxyHeaders":{"request":{"User-Agent":"UA","Referer":"https://r"}},"filename":"v.mp4","videoSize":123}})"));
    QVERIFY(url);
    QCOMPARE(url->source.kind, StreamSourceKind::Url);
    QCOMPARE(*url->description, QString("Legacy description"));
    QVERIFY(url->behaviorHints.hasProxyHeaders);
    QCOMPARE(url->behaviorHints.proxyRequestHeaders.size(), 2);
    QCOMPARE(*url->behaviorHints.videoSize, 123);

    const auto torrent = Stream::fromJson(jsonValue(R"({"infoHash":"24C8802E2624E17D46CD555F364DEBD949C2C392",
        "fileIdx":2,"sources":["tracker:udp://t.example:80","dht:24c8802e2624e17d46cd555f364debd949c2c392"]})"));
    QVERIFY(torrent);
    QCOMPARE(torrent->source.kind, StreamSourceKind::Torrent);
    QCOMPARE(torrent->source.infoHash, QString("24c8802e2624e17d46cd555f364debd949c2c392"));
    QCOMPARE(*torrent->source.fileIdx, 2);
    QCOMPARE(torrent->source.announce.size(), 2);

    // url wins over infoHash (untagged enum order), like Stremio.
    const auto both = Stream::fromJson(jsonValue(R"({"url":"https://x/y.mkv","infoHash":"24c8802e2624e17d46cd555f364debd949c2c392"})"));
    QCOMPARE(both->source.kind, StreamSourceKind::Url);

    QCOMPARE(Stream::fromJson(jsonValue(R"({"ytId":"abc"})"))->source.kind, StreamSourceKind::YouTube);
    QCOMPARE(Stream::fromJson(jsonValue(R"({"externalUrl":"https://netflix.com/title/1"})"))->source.kind,
             StreamSourceKind::External);
    QCOMPARE(Stream::fromJson(jsonValue(R"({"playerFrameUrl":"https://embed/x"})"))->source.kind,
             StreamSourceKind::PlayerFrame);
    const auto rar = Stream::fromJson(jsonValue(R"({"rarUrls":[["https://a/f.rar",10000],["https://b/f2.rar",null]],"fileIdx":1})"));
    QCOMPARE(rar->source.kind, StreamSourceKind::Rar);
    QCOMPARE(*rar->source.archiveUrls[0].bytes, 10000);
    QVERIFY(!rar->source.archiveUrls[1].bytes);
    QCOMPARE(Stream::fromJson(jsonValue(R"({"7zipUrls":[["https://a/f.7z"]]})"))->source.kind, StreamSourceKind::Zip7);
    QCOMPARE(Stream::fromJson(jsonValue(R"({"nzbUrl":"https://n/x.nzb","servers":["nntps://u:p@news.example:563/4"]})"))->source.kind,
             StreamSourceKind::Nzb);
    const auto magnet = Stream::fromJson(jsonValue(R"({"url":"magnet:?xt=urn:btih:24c8802e2624e17d46cd555f364debd949c2c392"})"));
    QVERIFY(magnet->source.isMagnet());
}

void ResourcesTest::malformedStreams_data()
{
    QTest::addColumn<QString>("json");
    QTest::newRow("no source") << R"({"name":"x"})";
    QTest::newRow("short hash") << R"({"infoHash":"abc"})";
    QTest::newRow("negative fileIdx") << R"({"infoHash":"24c8802e2624e17d46cd555f364debd949c2c392","fileIdx":-1})";
    QTest::newRow("relative url") << R"({"url":"/video.mp4"})";
    QTest::newRow("bad hints") << R"({"url":"https://x/y","behaviorHints":{"videoSize":"big"}})";
}

void ResourcesTest::malformedStreams()
{
    QFETCH(QString, json);
    QVERIFY(!Stream::fromJson(QJsonDocument::fromJson(("[" + json + "]").toUtf8()).array().first()));
}

void ResourcesTest::streamsResponseSkipsBadItems()
{
    const auto response = ResourceResponse::fromJson(jsonValue(R"({"streams":[
        {"url":"https://ok/1.mp4"}, {"name":"broken"}, 7, {"ytId":"v"}
    ]})"));
    QVERIFY(response);
    QCOMPARE(response->streams.size(), 2);
    QCOMPARE(response->skippedItems, 2);
}

void ResourcesTest::streamInlineSubtitles()
{
    const auto stream = Stream::fromJson(jsonValue(R"({"url":"https://example.com/video.mp4",
        "subtitles":[{"id":"test-1","url":"https://example.com/subs.vtt","lang":"eng"},{"lang":"x"}]})"));
    QVERIFY(stream);
    QCOMPARE(stream->subtitles.size(), 1);
    QCOMPARE(stream->subtitles[0].lang, QString("eng"));
}

void ResourcesTest::subtitlesResponse()
{
    const auto response = ResourceResponse::fromJson(jsonValue(R"({"subtitles":[
        {"id":"1","url":"https://s/1.srt","lang":"eng","label":"English [SDH]"},
        {"url":"https://mkvtoolnix.download/samples/vsshort-en.srt","lang":"en"},
        {"id":"3","lang":"fre"}
    ]})"));
    QVERIFY(response);
    QCOMPARE(response->subtitles.size(), 2);
    QCOMPARE(*response->subtitles[0].label, QString("English [SDH]"));
    // No id (as in the SDK protocol example): the URL is used (C-011).
    QCOMPARE(response->subtitles[1].id, QString("https://mkvtoolnix.download/samples/vsshort-en.srt"));
}

void ResourcesTest::youtubeVideoIds()
{
    const auto stream = Stream::youtubeFromVideoId("yt_id:UCchannel:dQw4w9WgXcQ");
    QVERIFY(stream);
    QCOMPARE(stream->source.ytId, QString("dQw4w9WgXcQ"));
    QVERIFY(!Stream::youtubeFromVideoId("tt1:1:1"));
}

void ResourcesTest::addonCatalogResponse()
{
    const auto response = ResourceResponse::fromJson(jsonValue(R"({"addons":[
        {"transportUrl":"https://a/manifest.json","manifest":{"id":"a","version":"1.0.0","name":"A","types":["movie"],"resources":["stream"]}},
        {"transportUrl":"https://b/manifest.json","manifest":{"id":"b"}}
    ]})"));
    QVERIFY(response);
    QCOMPARE(response->addons.size(), 1);
}

QTEST_GUILESS_MAIN(ResourcesTest)
#include "tst_resources.moc"
