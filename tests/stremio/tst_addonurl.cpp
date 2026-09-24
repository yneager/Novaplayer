// Install URL normalisation (stremio-core AddonDetails, Debrify
// normalizeStremioManifestUri, Nuvio normalizeManifestUrl).

#include "stremio/addonurl.h"

#include <QtTest>

using namespace stremio;

class AddonUrlTest : public QObject
{
    Q_OBJECT

private slots:
    void validUrlsUnchanged();
    void stremioScheme();
    void missingSchemeAndManifest();
    void legacyAndLocalhost();
    void collectionFiles();
    void rejected_data();
    void rejected();
    void configure();
};

void AddonUrlTest::validUrlsUnchanged()
{
    InstallUrl url;
    QVERIFY(normalizeInstallUrl("https://v3-cinemeta.strem.io/manifest.json", url));
    QCOMPARE(url.url, QString("https://v3-cinemeta.strem.io/manifest.json"));
    QCOMPARE(url.manifestUrl, url.url);
    QVERIFY(url.endsWithManifest);

    QVERIFY(normalizeInstallUrl("  https://addon.invalid/config/manifest.json?token=secret  ", url));
    QCOMPARE(url.manifestUrl, QString("https://addon.invalid/config/manifest.json?token=secret"));
}

void AddonUrlTest::stremioScheme()
{
    InstallUrl url;
    QVERIFY(normalizeInstallUrl("stremio://addon.invalid/config?token=secret", url));
    QCOMPARE(url.url, QString("https://addon.invalid/config?token=secret"));
    QCOMPARE(url.manifestUrl, QString("https://addon.invalid/config/manifest.json?token=secret"));
    QVERIFY(normalizeInstallUrl("stremio://catalog.example.com/manifest.json", url));
    QCOMPARE(url.manifestUrl, QString("https://catalog.example.com/manifest.json"));
}

void AddonUrlTest::missingSchemeAndManifest()
{
    InstallUrl url;
    QVERIFY(normalizeInstallUrl("torrentio.strem.fun/providers=yts%7Ceztv/", url));
    QCOMPARE(url.manifestUrl, QString("https://torrentio.strem.fun/providers=yts%7Ceztv/manifest.json"));
    QVERIFY(normalizeInstallUrl("https://host.example/", url));
    QCOMPARE(url.manifestUrl, QString("https://host.example/manifest.json"));
    QVERIFY(normalizeInstallUrl("https://host.example/manifest.json/#frag", url));
    QCOMPARE(url.manifestUrl, QString("https://host.example/manifest.json"));
}

void AddonUrlTest::legacyAndLocalhost()
{
    InstallUrl url;
    QVERIFY(normalizeInstallUrl("https://opensubtitles.strem.io/stremio/v1", url));
    QVERIFY(url.isLegacy);
    QCOMPARE(url.manifestUrl, QString("https://opensubtitles.strem.io/stremio/v1"));
    QVERIFY(normalizeInstallUrl("http://127.0.0.1:11470/local-addon/manifest.json", url));
    QCOMPARE(url.manifestUrl, QString("http://127.0.0.1:11470/local-addon/manifest.json"));
}

void AddonUrlTest::collectionFiles()
{
    InstallUrl url;
    QVERIFY(normalizeInstallUrl("https://example.com/my-addons.json", url));
    QVERIFY(url.looksLikeJsonFile);
}

void AddonUrlTest::rejected_data()
{
    QTest::addColumn<QString>("input");
    QTest::newRow("empty") << "  ";
    QTest::newRow("ftp") << "ftp://x.example/manifest.json";
    QTest::newRow("words") << "hello";
}

void AddonUrlTest::rejected()
{
    QFETCH(QString, input);
    InstallUrl url;
    QString error;
    QVERIFY(!normalizeInstallUrl(input, url, &error));
    QVERIFY(!error.isEmpty());
}

void AddonUrlTest::configure()
{
    QCOMPARE(configureUrl("https://torrentio.strem.fun/manifest.json"), QString("https://torrentio.strem.fun/configure"));
    QCOMPARE(configureUrl("https://a/cfg/manifest.json?x=1"), QString("https://a/cfg/configure?x=1"));
}

QTEST_GUILESS_MAIN(AddonUrlTest)
#include "tst_addonurl.moc"
