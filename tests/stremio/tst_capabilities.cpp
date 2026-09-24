// Which addon receives which request: Manifest::is_resource_supported,
// AggrRequest::plan and CatalogWithFilters selectable_update (stremio-core).

#include "stremio/capabilities.h"
#include "testutil.h"

#include <QtTest>

using namespace stremio;

namespace {

Descriptor addon(const char *manifestJson, const QString &url)
{
    Descriptor descriptor;
    descriptor.manifest = *Manifest::fromJson(jsonObject(manifestJson));
    descriptor.transportUrl = url;
    return descriptor;
}

} // namespace

class CapabilitiesTest : public QObject
{
    Q_OBJECT

private slots:
    void shortResourceUsesManifestTypesAndPrefixes();
    void fullResourceWithoutPrefixesAcceptsAll();
    void subtitlesRespectIdPrefixes();
    void firstResourceWithNameDecides();
    void catalogRequestsUseManifestCatalogs();
    void boardPlanOrderAndRequiredExtras();
    void typePriorities();
    void discoverSelectable();
};

void CapabilitiesTest::shortResourceUsesManifestTypesAndPrefixes()
{
    const Descriptor a = addon(R"({"id":"a","version":"1.0.0","name":"A","types":["movie","series"],
        "resources":["stream"],"idPrefixes":["tt"]})", "https://a/manifest.json");
    QVERIFY(a.manifest.isResourceSupported({"stream", "movie", "tt123", {}}));
    QVERIFY(a.manifest.isResourceSupported({"stream", "series", "tt123:1:2", {}}));
    QVERIFY(!a.manifest.isResourceSupported({"stream", "movie", "kitsu:1", {}}));
    QVERIFY(!a.manifest.isResourceSupported({"stream", "channel", "tt123", {}}));
    QVERIFY(!a.manifest.isResourceSupported({"meta", "movie", "tt123", {}}));
}

void CapabilitiesTest::fullResourceWithoutPrefixesAcceptsAll()
{
    // Full resource: its own types; missing idPrefixes = every id, even when
    // the manifest has a top-level idPrefixes (manifest.rs code path).
    const Descriptor a = addon(R"({"id":"a","version":"1.0.0","name":"A","types":[],"idPrefixes":["tt"],
        "resources":[{"name":"stream","types":["movie","series","anime"]},
                     {"name":"meta","types":["movie"],"idPrefixes":[]},
                     {"name":"subtitles","idPrefixes":["tt"]}]})", "https://a/manifest.json");
    QVERIFY(a.manifest.isResourceSupported({"stream", "anime", "kitsu:1", {}}));
    QVERIFY(a.manifest.isResourceSupported({"meta", "movie", "anything", {}}));
    // Full resource without types supports no type.
    QVERIFY(!a.manifest.isResourceSupported({"subtitles", "movie", "tt1", {}}));
}

void CapabilitiesTest::subtitlesRespectIdPrefixes()
{
    const Descriptor openSubtitles = addon(R"({"id":"org.stremio.opensubtitlesv3","version":"1.0.0",
        "name":"OpenSubtitles v3","types":["movie","series"],"resources":["subtitles"],"idPrefixes":["tt"]})",
        "https://opensubtitles-v3.strem.io/manifest.json");
    const Descriptor any = addon(R"({"id":"any","version":"1.0.0","name":"Any","types":["movie","series"],
        "resources":["subtitles"],"idPrefixes":[]})", "https://any/manifest.json");
    const QList<Descriptor> addons{openSubtitles, any};
    const ResourcePath kitsu{"subtitles", "series", "kitsu:1:5", {}};
    const QList<PlannedRequest> planned = planAllOfResource(addons, kitsu);
    QCOMPARE(planned.size(), 1);
    QCOMPARE(planned[0].addonIndex, 1);
    QCOMPARE(planAllOfResource(addons, {"subtitles", "series", "tt1:1:5", {}}).size(), 2);
}

void CapabilitiesTest::firstResourceWithNameDecides()
{
    const Descriptor a = addon(R"({"id":"a","version":"1.0.0","name":"A","types":["movie"],
        "resources":[{"name":"stream","types":["series"]},"stream"]})", "https://a/manifest.json");
    QVERIFY(!a.manifest.isResourceSupported({"stream", "movie", "tt1", {}}));
}

void CapabilitiesTest::catalogRequestsUseManifestCatalogs()
{
    const Descriptor a = addon(R"({"id":"a","version":"1.0.0","name":"A","types":["movie"],"resources":[],
        "catalogs":[{"type":"movie","id":"top","extra":[{"name":"search"},{"name":"skip"}]}]})", "https://a/manifest.json");
    QVERIFY(a.manifest.isResourceSupported({"catalog", "movie", "top", {}}));
    QVERIFY(a.manifest.isResourceSupported({"catalog", "movie", "top", {{"search", "x"}, {"skip", "100"}}}));
    QVERIFY(!a.manifest.isResourceSupported({"catalog", "movie", "top", {{"genre", "x"}}}));
    QVERIFY(!a.manifest.isResourceSupported({"catalog", "series", "top", {}}));
}

void CapabilitiesTest::boardPlanOrderAndRequiredExtras()
{
    const Descriptor cinemeta = addon(R"({"id":"c","version":"1.0.0","name":"C","types":["movie","series"],
        "resources":["catalog"],"catalogs":[
          {"type":"movie","id":"top","extra":[{"name":"genre","options":["Action"]},{"name":"search"},{"name":"skip"}]},
          {"type":"series","id":"top","extra":[{"name":"search"},{"name":"skip"}]},
          {"type":"series","id":"last-videos","extra":[{"name":"lastVideosIds","isRequired":true,"optionsLimit":100}]},
          {"type":"movie","id":"searchonly","extra":[{"name":"search","isRequired":true}]}
        ]})", "https://c/manifest.json");
    const Descriptor other = addon(R"({"id":"o","version":"1.0.0","name":"O","types":["anime"],
        "resources":["catalog"],"catalogs":[{"type":"anime","id":"trending","extraSupported":["skip"]}]})",
        "https://o/manifest.json");
    const QList<Descriptor> addons{cinemeta, other};

    const QList<PlannedRequest> board = planAllCatalogs(addons, {});
    QCOMPARE(board.size(), 3);
    QCOMPARE(board[0].request.path.id, QString("top"));
    QCOMPARE(board[0].request.path.type, QString("movie"));
    QCOMPARE(board[1].request.path.type, QString("series"));
    QCOMPARE(board[2].request.base, QString("https://o/manifest.json"));

    const QList<PlannedRequest> search = planAllCatalogs(addons, {{"search", "matrix"}});
    QCOMPARE(search.size(), 3);
    QCOMPARE(search[2].request.path.id, QString("searchonly"));
    QCOMPARE(search[0].request.path.extra, Extra({{"search", "matrix"}}));

    QCOMPARE(planAllCatalogs(addons, {}, QString("anime")).size(), 1);
}

void CapabilitiesTest::typePriorities()
{
    QStringList types{"other", "anime", "tv", "series", "all", "channel", "movie", "documentary"};
    std::stable_sort(types.begin(), types.end(), [](const QString &a, const QString &b) {
        return compareTypes(a, b) < 0;
    });
    QCOMPARE(types, (QStringList{"all", "movie", "series", "channel", "tv", "anime", "documentary", "other"}));
}

void CapabilitiesTest::discoverSelectable()
{
    const Descriptor a = addon(R"({"id":"a","version":"1.0.0","name":"A","types":["movie","series"],"resources":[],
        "catalogs":[
          {"type":"series","id":"top","name":"Popular","extra":[{"name":"genre","options":["Drama","Comedy"]},{"name":"skip"}]},
          {"type":"movie","id":"top","name":"Popular","extra":[{"name":"genre","options":["Action"],"isRequired":true},{"name":"skip"}]},
          {"type":"movie","id":"needs-search","extra":[{"name":"search","isRequired":true}]}
        ]})", "https://a/manifest.json");
    const QList<Descriptor> addons{a};

    const std::optional<ResourceRequest> first = defaultDiscoverRequest(addons);
    QVERIFY(first);
    QCOMPARE(first->path.type, QString("movie")); // movie outranks series
    QCOMPARE(first->path.extra, Extra({{"genre", "Action"}})); // default_required_extra

    const DiscoverSelectable movie = stremio::discoverSelectable(addons, first, {});
    QCOMPARE(movie.types, (QStringList{"movie", "series"}));
    QCOMPARE(movie.catalogs.size(), 1); // required search without options is not selectable
    QCOMPARE(movie.extra.size(), 1);
    QCOMPARE(movie.extra[0].options.size(), 1); // required: no "none" option
    QVERIFY(!movie.nextPage); // nothing loaded yet

    const DiscoverSelectable paged = stremio::discoverSelectable(addons, first, {100, 100});
    QVERIFY(paged.nextPage);
    QCOMPARE(paged.nextPage->path.extra, Extra({{"skip", "200"}, {"genre", "Action"}}));
    QVERIFY(!stremio::discoverSelectable(addons, first, {100, 0}).nextPage); // empty page ends paging

    const ResourceRequest series{"https://a/manifest.json", {"catalog", "series", "top", {}}};
    const DiscoverSelectable seriesSel = stremio::discoverSelectable(addons, series, {});
    QCOMPARE(seriesSel.extra[0].options.size(), 3); // none + 2 genres
    QVERIFY(seriesSel.extra[0].options[0].selected);
    QVERIFY(!seriesSel.extra[0].options[0].value);
    QCOMPARE(seriesSel.extra[0].options[2].request.path.extra, Extra({{"genre", "Comedy"}}));
}

QTEST_GUILESS_MAIN(CapabilitiesTest)
#include "tst_capabilities.moc"
