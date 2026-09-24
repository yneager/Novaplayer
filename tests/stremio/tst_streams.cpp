// Stream resolution, streaming-server interaction, subtitle context.
// References: stremio-core stream.rs, stremio-video withStreamingServer/*,
// lz-string test data, the OpenSubtitles hash algorithm.

#include "mockaddonserver.h"
#include "stremio/contentservice.h"
#include "stremio/lzstring.h"
#include "stremio/streamresolver.h"
#include "stremio/transport.h"
#include "stremio/videoparams.h"
#include "testutil.h"

#include <QFile>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QtTest>

using namespace stremio;

class StreamsTest : public QObject
{
    Q_OBJECT

private slots:
    void lzStringVectors_data();
    void lzStringVectors();
    void magnetParsing();
    void formEncoding();
    void torrentUrlsAndBodies();
    void archiveAndFtpUrls();
    void proxyHeadersToMpvOption();
    void directUrlNeedsNoServer();
    void externalStreams();
    void serverRequiredWithoutServer();
    void torrentViaStreamingServer();
    void magnetViaStreamingServer();
    void openSubtitlesHash();
    void videoParamsFromHints();
    void videoParamsComputedLocally();
    void videoParamsFromServer();
    void videoParamsNoRangeSupport();
    void subtitlePlanExtras();
    void boardSearchAndNextPage();
    void episodeStreamUsesVideoId();

private:
    static Stream stream(const char *json) { return *Stream::fromJson(jsonValue(json)); }
    PlaybackSource resolve(StreamResolver &resolver, const Stream &s, const ResolveContext &context = {});
    VideoParams params(VideoParamsFetcher &fetcher, const Stream &s, const PlaybackSource &source);
};

// moc cannot parse the raw-string fixtures below; it only needs the class.
#ifndef Q_MOC_RUN

namespace {

QByteArray readData(const QString &name)
{
    QFile file(QStringLiteral(LAMBDA_TEST_DATA "/lz-string/") + name);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("missing test data %s", qPrintable(name));
    }
    return file.readAll();
}

QByteArray testFile(int size)
{
    QByteArray data(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i) {
        data[i] = char((((i * 7 + 3) * (i >> 9)) + i) % 256);
    }
    return data;
}

const QByteArray kSettings = R"({"options":[],"values":{"cacheSize":2147483648},"baseUrl":""})";

Descriptor descriptor(const char *manifestJson, const QString &url)
{
    Descriptor d;
    d.manifest = *Manifest::fromJson(jsonObject(manifestJson));
    d.transportUrl = url;
    return d;
}

} // namespace

PlaybackSource StreamsTest::resolve(StreamResolver &resolver, const Stream &s, const ResolveContext &context)
{
    std::optional<PlaybackSource> out;
    resolver.resolve(s, context, this, [&out](const PlaybackSource &source) { out = source; });
    QTest::qWaitFor([&out] { return out.has_value(); }, 10000);
    return out.value_or(PlaybackSource{});
}

VideoParams StreamsTest::params(VideoParamsFetcher &fetcher, const Stream &s, const PlaybackSource &source)
{
    std::optional<VideoParams> out;
    fetcher.fetch(s, source, this, [&out](const VideoParams &p) { out = p; });
    QTest::qWaitFor([&out] { return out.has_value(); }, 10000);
    return out.value_or(VideoParams{});
}

void StreamsTest::lzStringVectors_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("hello_world") << "hello_world";
    QTest::newRow("all_ascii") << "all_ascii";
    QTest::newRow("lorem_ipsum") << "lorem_ipsum";
}

void StreamsTest::lzStringVectors()
{
    QFETCH(QString, name);
    // lz-string's own tests feed convertFromUint8Array(data.bin): each byte
    // pair becomes one UTF-16 code unit (big-endian), an odd last byte is
    // padded (src/Uint8Array/utils.ts).
    const QByteArray bytes = readData(name + ".txt");
    QString input;
    for (qsizetype i = 0; i + 1 < bytes.size(); i += 2) {
        input.append(QChar(ushort((uchar(bytes[i]) << 8) | uchar(bytes[i + 1]))));
    }
    if (bytes.size() & 1) {
        input.append(QChar(ushort(uchar(bytes.back()) << 8)));
    }
    const QString expected = QString::fromLatin1(readData(name + ".encodeduri.txt"));
    const QString actual = lzCompressToEncodedUriComponent(input);
    qsizetype firstDiff = 0;
    while (firstDiff < qMin(actual.size(), expected.size()) && actual[firstDiff] == expected[firstDiff]) {
        ++firstDiff;
    }
    QVERIFY2(actual == expected, qPrintable(QStringLiteral("first difference at %1 (lengths %2 / %3): %4 vs %5")
                                                .arg(firstDiff).arg(actual.size()).arg(expected.size())
                                                .arg(actual.mid(firstDiff, 20), expected.mid(firstDiff, 20))));
}

void StreamsTest::magnetParsing()
{
    QStringList trackers;
    const auto hex = StreamResolver::magnetInfoHash(
        "magnet:?xt=urn:btih:24C8802E2624E17D46CD555F364DEBD949C2C392&dn=Sintel&tr=udp%3A%2F%2Ftracker.example%3A1337&tr=wss%3A%2F%2Ft2",
        &trackers);
    QCOMPARE(*hex, QString("24c8802e2624e17d46cd555f364debd949c2c392"));
    QCOMPARE(trackers, (QStringList{"udp://tracker.example:1337", "wss://t2"}));
    // Base32 form of the same hash.
    QCOMPARE(*StreamResolver::magnetInfoHash("magnet:?xt=urn:btih:ETEIALRGETQX2RWNKVPTMTPL3FE4FQ4S"),
             QString("24c8802e2624e17d46cd555f364debd949c2c392"));
    QVERIFY(!StreamResolver::magnetInfoHash("magnet:?dn=nothing"));
    QVERIFY(!StreamResolver::magnetInfoHash("https://x/y"));
}

void StreamsTest::formEncoding()
{
    QCOMPARE(StreamResolver::formUrlEncode("tracker:udp://t.example:80/announce"),
             QString("tracker%3Audp%3A%2F%2Ft.example%3A80%2Fannounce"));
    QCOMPARE(StreamResolver::formUrlEncode("a b*-._~"), QString("a+b*-._%7E"));
}

void StreamsTest::torrentUrlsAndBodies()
{
    const QString hash = "24c8802e2624e17d46cd555f364debd949c2c392";
    QCOMPARE(StreamResolver::torrentUrl("http://127.0.0.1:11470/", hash, 3, {}),
             "http://127.0.0.1:11470/" + hash + "/3");
    QCOMPARE(StreamResolver::torrentUrl("http://127.0.0.1:11470", hash, -1, {"dht:" + hash, "tracker:udp://a:1"}),
             "http://127.0.0.1:11470/" + hash + "/-1?tr=dht%3A" + hash + "&tr=tracker%3Audp%3A%2F%2Fa%3A1");

    const QJsonObject guess = StreamResolver::createTorrentBody(hash, std::nullopt, {"udp://a:1", "tracker:udp://a:1"},
                                                                SeriesInfo{2, 5});
    QCOMPARE(guess.value("torrent").toObject().value("infoHash").toString(), hash);
    const QJsonObject peerSearch = guess.value("peerSearch").toObject();
    QCOMPARE(peerSearch.value("sources").toArray(), QJsonArray({"dht:" + hash, "tracker:udp://a:1"}));
    QCOMPARE(peerSearch.value("min").toInt(), 40);
    QCOMPARE(peerSearch.value("max").toInt(), 200);
    QCOMPARE(guess.value("guessFileIdx").toObject(), (QJsonObject{{"season", 2}, {"episode", 5}}));

    const QJsonObject known = StreamResolver::createTorrentBody(hash, 1, {}, std::nullopt);
    QVERIFY(!known.contains("peerSearch"));
    QCOMPARE(known.value("guessFileIdx"), QJsonValue(false));
}

void StreamsTest::archiveAndFtpUrls()
{
    const Stream rar = stream(R"({"rarUrls":[["https://a/f.rar",10],["ftp://h/dir/f2.rar"]],"fileIdx":1,"fileMustInclude":["S01E02"]})");
    const QString url = StreamResolver::archiveCreateUrl("http://127.0.0.1:11470/", rar);
    QVERIFY(url.startsWith("http://127.0.0.1:11470/rar/create?lz="));
    const QString ftp = StreamResolver::ftpUrl("http://127.0.0.1:11470/", "ftp://h/dir/f2.rar");
    QVERIFY(ftp.startsWith("http://127.0.0.1:11470/ftp/f2.rar?lz="));
    QCOMPARE(ftp.mid(ftp.indexOf("lz=") + 3), lzCompressToEncodedUriComponent(R"({"ftpUrl":"ftp://h/dir/f2.rar"})"));
    // The FTP archive part is proxied through the server inside the payload.
    const QString expectedPayload = QStringLiteral(R"({"fileIdx":1,"fileMustInclude":["S01E02"],"urls":[["https://a/f.rar",10],[")")
        + ftp + QStringLiteral(R"("]]})");
    QCOMPARE(url.mid(url.indexOf("lz=") + 3), lzCompressToEncodedUriComponent(expectedPayload));

    const Stream nzb = stream(R"({"nzbUrl":"https://n/x.nzb","servers":["nntps://u:p@news.example:563/4"]})");
    QVERIFY(StreamResolver::archiveCreateUrl("http://s/", nzb).startsWith("http://s/nzb/create?lz="));
}

void StreamsTest::proxyHeadersToMpvOption()
{
    const QList<QPair<QString, QString>> headers{
        {"User-Agent", "Mozilla/5.0 (X, Y)"}, {"range", "bytes=0-"}, {" Referer ", " https://site/ "}, {"Empty", " "}};
    QCOMPARE(StreamResolver::mpvHeaderFields(headers),
             (QStringList{"Referer: https://site/", "User-Agent: Mozilla/5.0 (X, Y)"}));
}

void StreamsTest::directUrlNeedsNoServer()
{
    QNetworkAccessManager network;
    StreamingServer server(&network);
    server.setUrl("http://127.0.0.1:1/"); // nothing listens there
    StreamResolver resolver(&network, &server);
    const PlaybackSource source = resolve(resolver, stream(R"({"url":"https://cdn.example/v.mkv",
        "behaviorHints":{"proxyHeaders":{"request":{"Referer":"https://site/"},"response":{"Content-Type":"video/mp4"}}}})"));
    QCOMPARE(source.kind, PlaybackSource::Kind::Play);
    QCOMPARE(source.url, QString("https://cdn.example/v.mkv"));
    QCOMPARE(source.httpHeaders.size(), 1);
    QVERIFY(!source.viaStreamingServer);
    // Pre-built streaming server URL: infoHash/fileIdx recovered for subtitles.
    const PlaybackSource prebuilt = resolve(resolver, stream(R"({"url":"http://127.0.0.1:11470/24c8802e2624e17d46cd555f364debd949c2c392/2"})"));
    QCOMPARE(prebuilt.infoHash, QString("24c8802e2624e17d46cd555f364debd949c2c392"));
    QCOMPARE(*prebuilt.fileIdx, 2);
}

void StreamsTest::externalStreams()
{
    QNetworkAccessManager network;
    StreamingServer server(&network);
    StreamResolver resolver(&network, &server);
    const PlaybackSource external = resolve(resolver, stream(R"({"externalUrl":"https://www.netflix.com/title/80057281"})"));
    QCOMPARE(external.kind, PlaybackSource::Kind::OpenExternally);
    QCOMPARE(external.url, QString("https://www.netflix.com/title/80057281"));
    QCOMPARE(resolve(resolver, stream(R"({"playerFrameUrl":"https://embed/x"})")).kind, PlaybackSource::Kind::OpenExternally);
    QCOMPARE(resolve(resolver, stream(R"({"tizenUrl":"tizen://x"})")).kind, PlaybackSource::Kind::Unsupported);
    QVERIFY(!StreamResolver::playsInPlayer(stream(R"({"externalUrl":"https://x/"})")));
}

void StreamsTest::serverRequiredWithoutServer()
{
    QNetworkAccessManager network;
    StreamingServer server(&network);
    server.setUrl("http://127.0.0.1:1/");
    StreamResolver resolver(&network, &server);
    const PlaybackSource torrent = resolve(resolver, stream(R"({"infoHash":"24c8802e2624e17d46cd555f364debd949c2c392","fileIdx":0})"));
    QCOMPARE(torrent.kind, PlaybackSource::Kind::Unsupported);
    QVERIFY(torrent.error.contains("streaming server"));
    // YouTube falls back to the watch page (Stream::download_url).
    const PlaybackSource youtube = resolve(resolver, stream(R"({"ytId":"dQw4w9WgXcQ"})"));
    QCOMPARE(youtube.kind, PlaybackSource::Kind::OpenExternally);
    QCOMPARE(youtube.url, QString("https://youtube.com/watch?v=dQw4w9WgXcQ"));
}

void StreamsTest::torrentViaStreamingServer()
{
    MockAddonServer mock;
    mock.route("/settings", kSettings);
    const QString hash = "24c8802e2624e17d46cd555f364debd949c2c392";
    mock.route(("/" + hash + "/create").toUtf8(), R"({"guessedFileIdx":4})");
    QNetworkAccessManager network;
    StreamingServer server(&network);
    server.setUrl(mock.base());
    StreamResolver resolver(&network, &server);

    // Known fileIdx, no trackers: no /create call.
    const PlaybackSource direct = resolve(resolver, stream(R"({"infoHash":"24c8802e2624e17d46cd555f364debd949c2c392","fileIdx":7})"));
    QCOMPARE(direct.kind, PlaybackSource::Kind::Play);
    QCOMPARE(direct.url, mock.base() + "/" + hash + "/7");
    QVERIFY(!mock.methods().contains("POST"));

    // Season pack without fileIdx: guessFileIdx with season/episode.
    ResolveContext context;
    context.seriesInfo = SeriesInfo{1, 3};
    const PlaybackSource guessed = resolve(resolver,
        stream(R"({"infoHash":"24c8802e2624e17d46cd555f364debd949c2c392","sources":["udp://t:1"]})"), context);
    QCOMPARE(guessed.kind, PlaybackSource::Kind::Play);
    QCOMPARE(*guessed.fileIdx, 4);
    QCOMPARE(guessed.url, mock.base() + "/" + hash + "/4?tr=dht%3A" + hash + "&tr=tracker%3Audp%3A%2F%2Ft%3A1");
    const int post = int(mock.methods().indexOf("POST"));
    QVERIFY(post >= 0);
    const QJsonObject body = QJsonDocument::fromJson(mock.bodies()[post]).object();
    QCOMPARE(body.value("guessFileIdx").toObject(), (QJsonObject{{"season", 1}, {"episode", 3}}));
}

void StreamsTest::magnetViaStreamingServer()
{
    MockAddonServer mock;
    mock.route("/settings", kSettings);
    mock.route("/24c8802e2624e17d46cd555f364debd949c2c392/create", R"({"guessedFileIdx":0})");
    QNetworkAccessManager network;
    StreamingServer server(&network);
    server.setUrl(mock.base() + "/");
    StreamResolver resolver(&network, &server);
    const PlaybackSource source = resolve(resolver,
        stream(R"({"url":"magnet:?xt=urn:btih:24c8802e2624e17d46cd555f364debd949c2c392&tr=udp%3A%2F%2Fa%3A1"})"));
    QCOMPARE(source.kind, PlaybackSource::Kind::Play);
    QVERIFY(source.url.contains("/24c8802e2624e17d46cd555f364debd949c2c392/0?tr="));
    QVERIFY(source.viaStreamingServer);
}

void StreamsTest::openSubtitlesHash()
{
    const QByteArray data = testFile(200000);
    QCOMPARE(stremio::openSubtitlesHash(data.size(), data.left(65536), data.right(65536)), QString("022042608122af40"));
    QByteArray small;
    for (int i = 0; i < 10; ++i) {
        for (int b = 0; b < 256; ++b) small.append(char(b));
    }
    QCOMPARE(stremio::openSubtitlesHash(small.size(), small, small), QString("c643c13ebc39c000"));
}

void StreamsTest::videoParamsFromHints()
{
    QNetworkAccessManager network;
    StreamingServer server(&network);
    server.setUrl("http://127.0.0.1:1/");
    VideoParamsFetcher fetcher(&network, &server);
    const Stream s = stream(R"({"url":"https://x/y.mkv","behaviorHints":{"videoHash":"abc","videoSize":42,"filename":"Show.S01E02.mkv"}})");
    PlaybackSource source;
    source.url = "https://x/y.mkv";
    const VideoParams p = params(fetcher, s, source);
    QCOMPARE(*p.hash, QString("abc"));
    QCOMPARE(*p.size, 42);
    QCOMPARE(*p.filename, QString("Show.S01E02.mkv"));
}

void StreamsTest::videoParamsComputedLocally()
{
    MockAddonServer mock;
    MockAddonServer::Route file;
    file.file = testFile(200000);
    mock.route("/media/The%20Movie%20(2020).mkv", file);
    QNetworkAccessManager network;
    StreamingServer server(&network);
    server.setUrl("http://127.0.0.1:1/");
    VideoParamsFetcher fetcher(&network, &server);
    PlaybackSource source;
    source.url = mock.base() + "/media/The%20Movie%20(2020).mkv";
    source.httpHeaders = {{"Referer", "https://site/"}};
    const VideoParams p = params(fetcher, stream(R"({"url":"https://placeholder/x"})"), source);
    QCOMPARE(*p.hash, QString("022042608122af40"));
    QCOMPARE(*p.size, 200000);
    QCOMPARE(*p.filename, QString("The Movie (2020).mkv"));
    // Stream headers are sent with the range requests.
    // (Qt sends lower-case header names.)
    QVERIFY(mock.requestHeaders().first().contains("referer: https://site/\r"));
}

void StreamsTest::videoParamsFromServer()
{
    MockAddonServer mock;
    mock.route("/settings", kSettings);
    mock.route("/opensubHash?videoUrl=https%3A%2F%2Fcdn%2Fv.mkv", R"({"result":{"hash":"0123456789abcdef","size":12345}})");
    QNetworkAccessManager network;
    StreamingServer server(&network);
    server.setUrl(mock.base());
    VideoParamsFetcher fetcher(&network, &server);
    PlaybackSource source;
    source.url = "https://cdn/v.mkv";
    const VideoParams p = params(fetcher, stream(R"({"url":"https://cdn/v.mkv","behaviorHints":{"videoSize":99}})"), source);
    QCOMPARE(*p.hash, QString("0123456789abcdef"));
    QCOMPARE(*p.size, 99); // hint wins
    QCOMPARE(*p.filename, QString("v.mkv"));
}

void StreamsTest::videoParamsNoRangeSupport()
{
    MockAddonServer mock;
    MockAddonServer::Route file;
    file.file = testFile(200000);
    file.ignoreRange = true;
    mock.route("/v.mkv", file);
    QNetworkAccessManager network;
    StreamingServer server(&network);
    server.setUrl("http://127.0.0.1:1/");
    VideoParamsFetcher fetcher(&network, &server);
    PlaybackSource source;
    source.url = mock.base() + "/v.mkv";
    const VideoParams p = params(fetcher, stream(R"({"url":"https://placeholder/x"})"), source);
    QVERIFY(!p.hash);
    QCOMPARE(*p.filename, QString("v.mkv"));
}

void StreamsTest::subtitlePlanExtras()
{
    QTemporaryDir dir;
    AddonClient client;
    AddonManager manager(&client, dir.filePath("addons.json"));
    manager.addDescriptor(descriptor(R"({"id":"os","version":"1.0.0","name":"OpenSubtitles v3","types":["movie","series"],
        "resources":["subtitles"],"idPrefixes":["tt"]})", "https://opensubtitles-v3.strem.io/manifest.json"));
    manager.addDescriptor(descriptor(R"({"id":"streams","version":"1.0.0","name":"S","types":["movie","series"],
        "resources":["stream"]})", "https://s/manifest.json"));
    ContentService service(&manager, &client);

    VideoParams p;
    p.hash = "8e245d9679d31e12";
    p.size = 12909756;
    p.filename = "breakdance.avi";
    const QList<PlannedRequest> plan = service.subtitlePlan("series", "tt0386676:5:1", p);
    QCOMPARE(plan.size(), 1);
    // extend_one order: the last added property comes first.
    QCOMPARE(plan[0].request.path.extra,
             Extra({{"filename", "breakdance.avi"}, {"videoSize", "12909756"}, {"videoHash", "8e245d9679d31e12"}}));
    QCOMPARE(resourceUrl(plan[0].request.base, plan[0].request.path),
             QString("https://opensubtitles-v3.strem.io/subtitles/series/tt0386676%3A5%3A1/"
                     "filename=breakdance.avi&videoSize=12909756&videoHash=8e245d9679d31e12.json"));

    VideoParams onlyName;
    onlyName.filename = "x.mkv";
    QCOMPARE(service.subtitlePlan("movie", "tt1", onlyName)[0].request.path.extra, Extra({{"filename", "x.mkv"}}));
    QVERIFY(service.subtitlePlan("movie", "tt1", VideoParams{}).isEmpty());
    QVERIFY(service.subtitlePlan("series", "kitsu:1:1", p).isEmpty()); // C-001
}

void StreamsTest::boardSearchAndNextPage()
{
    QTemporaryDir dir;
    AddonClient client;
    AddonManager manager(&client, dir.filePath("addons.json"));
    manager.addDescriptor(descriptor(R"({"id":"c","version":"1.0.0","name":"Cinemeta","types":["movie"],"resources":["catalog","meta"],
        "catalogs":[{"type":"movie","id":"top","name":"Popular","extra":[{"name":"search"},{"name":"skip"}]},
                    {"type":"movie","id":"noskip","extra":[{"name":"genre","options":["A"]}]}]})", "https://c/manifest.json"));
    manager.addDescriptor(descriptor(R"({"id":"d","version":"1.0.0","name":"Disabled","types":["movie"],"resources":["catalog"],
        "catalogs":[{"type":"movie","id":"x","extra":[{"name":"search"}]}]})", "https://d/manifest.json"));
    manager.setEnabled("https://d/manifest.json", false);
    ContentService service(&manager, &client);

    QCOMPARE(service.board().size(), 2);
    const QList<CatalogRowInfo> search = service.search("  matrix ");
    QCOMPARE(search.size(), 1); // disabled addon not searched, noskip lacks "search"
    QCOMPARE(search[0].request.path.extra, Extra({{"search", "matrix"}}));
    QVERIFY(service.search("   ").isEmpty());

    const auto next = service.nextPage(search[0].request, 100);
    QVERIFY(next);
    QCOMPARE(next->path.extra, Extra({{"skip", "100"}, {"search", "matrix"}}));
    const auto after = service.nextPage(*next, 37);
    QCOMPARE(after->path.extra, Extra({{"skip", "137"}, {"search", "matrix"}}));
    QVERIFY(!service.nextPage(*after, 0));
    QVERIFY(!service.nextPage(service.board()[1].request, 100)); // no "skip" declared

    const QJsonObject discover = service.discover(std::nullopt, {});
    QCOMPARE(discover.value("types").toArray(), QJsonArray({"movie"}));
    QCOMPARE(discover.value("catalogs").toArray().size(), 2);
}

void StreamsTest::episodeStreamUsesVideoId()
{
    QTemporaryDir dir;
    AddonClient client;
    AddonManager manager(&client, dir.filePath("addons.json"));
    manager.addDescriptor(descriptor(R"({"id":"t","version":"1.0.0","name":"Torrent addon","types":["movie","series","anime"],
        "resources":["stream"],"idPrefixes":["tt","kitsu"]})", "https://t/manifest.json"));
    manager.addDescriptor(descriptor(R"({"id":"k","version":"1.0.0","name":"Kitsu","types":["anime","series"],
        "resources":["catalog",{"name":"meta","types":["anime","series"],"idPrefixes":["kitsu"]}],"catalogs":[]})",
        "https://k/manifest.json"));
    ContentService service(&manager, &client);
    // The meta addon's own video id (kitsu:1:5) is used, typed with the meta type.
    const QList<PlannedRequest> streams = service.streamPlan("anime", "kitsu:11061:5");
    QCOMPARE(streams.size(), 1);
    QCOMPARE(resourceUrl(streams[0].request.base, streams[0].request.path),
             QString("https://t/stream/anime/kitsu%3A11061%3A5.json"));
    QCOMPARE(service.metaPlan("anime", "kitsu:11061").size(), 1);
    QCOMPARE(service.metaPlan("movie", "tt1").size(), 0);
}

#endif // Q_MOC_RUN

QTEST_GUILESS_MAIN(StreamsTest)
#include "tst_streams.moc"
