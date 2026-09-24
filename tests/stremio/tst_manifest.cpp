// Manifest parsing (stremio-core src/types/addon/manifest.rs and its
// unit_tests/serde/manifest*.rs cases).

#include "stremio/legacytransport.h"
#include "stremio/manifest.h"
#include "testutil.h"

#include <QtTest>

using namespace stremio;

class ManifestTest : public QObject
{
    Q_OBJECT

private slots:
    void simpleManifest();
    void richResourceDeclarations();
    void idPrefixesOptionalPreserved();
    void catalogExtraForms();
    void behaviorHintsAndUrls();
    void malformedManifests_data();
    void malformedManifests();
    void officialAddonsParse();
    void descriptorRoundTrip();
    void legacyManifestConversion();
};

void ManifestTest::simpleManifest()
{
    QString error;
    const auto manifest = Manifest::fromJson(jsonObject(R"({
        "id": "org.myexampleaddon", "version": "1.0.0", "name": "simple Big Buck Bunny example",
        "types": ["movie"], "catalogs": [{"type": "movie", "id": "bbbcatalog"}],
        "resources": ["catalog", {"name": "stream", "types": ["movie"], "idPrefixes": ["tt"]}]
    })"), &error);
    QVERIFY2(manifest, qPrintable(error));
    QCOMPARE(manifest->id, QString("org.myexampleaddon"));
    QCOMPARE(manifest->resources.size(), 2);
    QVERIFY(!manifest->resources[0].full);
    QVERIFY(manifest->resources[1].full);
    QCOMPARE(*manifest->resources[1].idPrefixes, QStringList{"tt"});
    QCOMPARE(manifest->catalogs.size(), 1);
    QVERIFY(!manifest->catalogs[0].name);
    QCOMPARE(manifest->catalogs[0].displayName(), QString("bbbcatalog"));
    QVERIFY(!manifest->idPrefixes);
}

void ManifestTest::richResourceDeclarations()
{
    const auto manifest = Manifest::fromJson(jsonObject(R"({
        "id": "a", "version": "0.0.1", "name": "A", "types": [],
        "resources": [
            {"name": "catalog", "types": ["anime", "series", "movies"], "idPrefixes": ["tt", "kitsu"]},
            {"name": "no-types", "idPrefixes": []},
            {"name": "no-prefixes", "types": ["movie"]},
            "meta", "subtitles"
        ]
    })"));
    QVERIFY(manifest);
    QCOMPARE(manifest->resources.size(), 5);
    QVERIFY(!manifest->resources[1].types);
    QVERIFY(manifest->resources[1].idPrefixes && manifest->resources[1].idPrefixes->isEmpty());
    QVERIFY(!manifest->resources[2].idPrefixes);
}

void ManifestTest::idPrefixesOptionalPreserved()
{
    // Missing idPrefixes must stay "missing", not become [] (stremio-bugs#1469).
    const auto manifest = Manifest::fromJson(jsonObject(R"({
        "id": "a", "version": "1.0.0", "name": "A", "types": ["movie"],
        "resources": [{"name": "stream", "types": ["movie"], "idPrefixes": null}]
    })"));
    QVERIFY(manifest);
    QVERIFY(!manifest->resources[0].idPrefixes.has_value());
    QVERIFY(!manifest->idPrefixes.has_value());
}

void ManifestTest::catalogExtraForms()
{
    const auto manifest = Manifest::fromJson(jsonObject(R"({
        "id": "a", "version": "1.0.0", "name": "A", "types": ["movie", "series"], "resources": ["catalog"],
        "catalogs": [
            {"type": "movie", "id": "top", "name": "Popular", "extra": [
                {"name": "genre", "options": ["Action", "Drama"], "optionsLimit": 2},
                {"name": "search"},
                {"name": "skip", "isRequired": true, "options": ["0"]},
                {"name": "genre", "options": ["duplicate"]}
            ]},
            {"type": "series", "id": "legacy", "extraRequired": ["search"], "extraSupported": ["search", "genre"]},
            {"type": "movie", "id": "top", "name": "Duplicate id+type"},
            {"type": "series", "id": "top"},
            {"type": "movie", "id": "brokenextra", "extra": [{"name": "genre", "isRequired": "yes"}],
             "extraSupported": ["genre"]}
        ]
    })"));
    QVERIFY(manifest);
    // UniqueVec by (id, type): the first "movie/top" wins; "series/top" is distinct.
    QCOMPARE(manifest->catalogs.size(), 4);
    const ManifestCatalog &top = manifest->catalogs[0];
    QCOMPARE(top.displayName(), QString("Popular"));
    QCOMPARE(top.extra.size(), 3); // duplicate "genre" dropped
    QCOMPARE(top.extra[0].optionsLimit, 2);
    // A declared "skip" is always normalised to SKIP_EXTRA_PROP.
    QCOMPARE(top.extra[2].name, QString("skip"));
    QVERIFY(!top.extra[2].isRequired);
    QVERIFY(top.extra[2].options.isEmpty());

    const ManifestCatalog &legacy = manifest->catalogs[1];
    QVERIFY(legacy.shortExtraForm);
    QCOMPARE(legacy.extra.size(), 2);
    QVERIFY(legacy.extra[0].isRequired);
    QVERIFY(!legacy.extra[1].isRequired);

    // An invalid full `extra` falls through to the short form (untagged enum).
    const ManifestCatalog &broken = manifest->catalogs[3];
    QCOMPARE(broken.id, QString("brokenextra"));
    QVERIFY(broken.shortExtraForm);
    QCOMPARE(broken.extra.size(), 1);
}

void ManifestTest::behaviorHintsAndUrls()
{
    const auto manifest = Manifest::fromJson(jsonObject(R"({
        "id": "a", "version": "1.2.3-beta.1+build", "name": "A", "types": [], "resources": [],
        "logo": "not a url", "background": "",
        "behaviorHints": {"configurable": true, "configurationRequired": true, "p2p": true, "unknownHint": 5}
    })"));
    QVERIFY(manifest);
    QVERIFY(manifest->logo.isEmpty());       // DefaultOnError
    QVERIFY(manifest->background.isEmpty()); // NoneAsEmptyString
    QVERIFY(manifest->behaviorHints.configurable);
    QVERIFY(manifest->behaviorHints.configurationRequired);
    QVERIFY(manifest->behaviorHints.p2p);
    QVERIFY(!manifest->behaviorHints.adult);
}

void ManifestTest::malformedManifests_data()
{
    QTest::addColumn<QString>("json");
    QTest::newRow("missing id") << R"({"version":"1.0.0","name":"A","types":[],"resources":[]})";
    QTest::newRow("non semver") << R"({"id":"a","version":"1.0","name":"A","types":[],"resources":[]})";
    QTest::newRow("types not array") << R"({"id":"a","version":"1.0.0","name":"A","types":"movie","resources":[]})";
    QTest::newRow("resources missing") << R"({"id":"a","version":"1.0.0","name":"A","types":[]})";
    QTest::newRow("bad resource") << R"({"id":"a","version":"1.0.0","name":"A","types":[],"resources":[5]})";
    QTest::newRow("catalog without type") << R"({"id":"a","version":"1.0.0","name":"A","types":[],"resources":[],"catalogs":[{"id":"x"}]})";
    QTest::newRow("html instead of json") << R"({"html":"<html>"})";
}

void ManifestTest::malformedManifests()
{
    QFETCH(QString, json);
    QString error;
    QVERIFY(!Manifest::fromJson(QJsonDocument::fromJson(json.toUtf8()).object(), &error));
    QVERIFY(!error.isEmpty());
}

void ManifestTest::officialAddonsParse()
{
    // Real descriptors from Stremio/stremio-official-addons index.json (trimmed).
    const auto cinemeta = Manifest::fromJson(jsonObject(R"({
        "id":"com.linvo.cinemeta","version":"3.0.14","name":"Cinemeta","types":["movie","series"],
        "resources":["catalog","meta","addon_catalog"],"idPrefixes":["tt"],
        "catalogs":[
          {"type":"movie","id":"top","name":"Popular","extra":[{"name":"genre","options":["Action"]},{"name":"search"},{"name":"skip"}]},
          {"type":"series","id":"last-videos","name":"Last videos","extra":[{"name":"lastVideosIds","isRequired":true,"optionsLimit":100}]}
        ],
        "addonCatalogs":[{"type":"all","id":"official","name":"Official"},{"type":"all","id":"community","name":"Community"}],
        "behaviorHints":{"newEpisodeNotifications":true}
    })"));
    QVERIFY(cinemeta);
    QCOMPARE(cinemeta->addonCatalogs.size(), 2);
    QCOMPARE(cinemeta->catalogs[1].extra[0].optionsLimit, 100);

    const auto local = Manifest::fromJson(jsonObject(R"({
        "id":"org.stremio.local","version":"1.10.0","name":"Local Files","types":["movie","series","other"],
        "resources":[{"name":"meta","types":["other"],"idPrefixes":["local:","bt:"]},
                     {"name":"stream","types":["movie","series"],"idPrefixes":["tt"]}],
        "catalogs":[]
    })"));
    QVERIFY(local);
}

void ManifestTest::descriptorRoundTrip()
{
    const auto descriptor = Descriptor::fromJson(jsonObject(R"({
        "manifest": {"id":"a","version":"1.0.0","name":"A","types":["movie"],"resources":["stream"]},
        "transportUrl": "https://a.example/cfg/manifest.json?token=1",
        "flags": {"official": true, "protected": false}
    })"));
    QVERIFY(descriptor);
    QVERIFY(descriptor->flags.official);
    const auto again = Descriptor::fromJson(descriptor->toJson());
    QVERIFY(again);
    QCOMPARE(again->transportUrl, descriptor->transportUrl);
    QCOMPARE(again->manifest.raw, descriptor->manifest.raw);
}

void ManifestTest::legacyManifestConversion()
{
    const auto manifest = legacy::parseManifestResponse(jsonValue(R"({"result":{"manifest":{
        "id":"org.stremio.opensubtitles","name":"OpenSubtitles","version":"0.24.0",
        "methods":["subtitles.find","meta.find","meta.get"],"types":["series","movie"],
        "idProperty":["imdb_id","yt_id","kitsu_id"],
        "sorts":[{"prop":"popularities.x","name":"Popular","types":["movie"]}]
    }}})"));
    QVERIFY(manifest);
    QCOMPARE(manifest->catalogs.size(), 1);
    QCOMPARE(manifest->catalogs[0].id, QString("popularities.x"));
    QCOMPARE(*manifest->idPrefixes, (QStringList{"tt", "UC", "kitsu_id:"}));
    QVERIFY(manifest->hasResource("subtitles"));
    QVERIFY(manifest->hasResource("meta"));
    QVERIFY(!manifest->hasResource("stream"));

    QString error;
    QVERIFY(!legacy::parseManifestResponse(jsonValue(R"({"error":{"message":"boom","code":7}})"), &error));
    QCOMPARE(error, QString("rpc error 7: boom"));
}

QTEST_GUILESS_MAIN(ManifestTest)
#include "tst_manifest.moc"
