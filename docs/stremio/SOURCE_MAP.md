# Stremio addon support — source map

Every protocol behaviour in `src/stremio/` is a port of, or is checked against, a
named open-source implementation. Stremio's own code is the authority; the other
clients are used for problems Stremio does not solve for a native libmpv player
and for confirming real-world edge cases.

Reference revisions studied (2026-09-24):

| Repository | Revision | Role |
|---|---|---|
| Stremio/stremio-core (`development`) | 88be65b | protocol authority: types, capability matching, transport, models |
| Stremio/stremio-core `stremio-core-web` | 88be65b | fetch semantics, MetaDetails serialisation |
| Stremio/stremio-web | 6a68fc5 | how the official UI consumes the models |
| Stremio/stremio-video | shallow HEAD | stream conversion, torrent creation, subtitle `videoParams` |
| Stremio/stremio-addon-sdk | ec4e0a4 | protocol docs (`docs/protocol.md`, `docs/api/**`) |
| Stremio/stremio-addon-client | 7c66830 | `detectFromURL`, collections, descriptor format |
| Stremio/stremio-official-addons | HEAD | default descriptors (Cinemeta, OpenSubtitles v3, …) |
| varunsalian/debrify | d670326 | configured URLs with query tokens, collection import formats, timeouts |
| NuvioMedia (hgomatheus/Nuvio `cmp-rewrite`) | 37203d1 | URL normalisation, per-addon stream groups, libmpv `http-header-fields` |
| kaleidal/raffi | 3406447 | counter-examples (see COMPATIBILITY.md) |
| coveninja/cove | HEAD | desktop mpv client, per-request timeout |
| stremio-native/stream-server | f585ab6 | bundled streaming engine (torrent, archive, FTP, NZB, `/opensubHash`, `/stats.json`), libtorrent backend |

| Feature | Authoritative / reference implementation | Exact repo / file | LAMBDA implementation |
|---|---|---|---|
| Manifest parsing | Stremio core `Manifest`, `ManifestResource` (short/full), `ManifestCatalog` + `ManifestExtra` (full `extra` or legacy `extraRequired`/`extraSupported`), `UniqueVec` (first of duplicate `(id,type)` kept), `ExtraPropValid` (`skip` normalised), `ManifestBehaviorHints` | stremio-core `src/types/addon/manifest.rs` | `src/stremio/manifest.{h,cpp}` |
| Legacy manifest | `LegacyManifest` → `Manifest` (methods, sorts, idProperty) | stremio-core `src/addon_transport/http_transport/legacy/legacy_manifest.rs` | `src/stremio/legacytransport.{h,cpp}` |
| Capability matching | `Manifest::is_resource_supported`, `ManifestCatalog::is_extra_supported`, `default_required_extra`, `AggrRequest::plan` (`AllCatalogs`, `AllOfResource`) | stremio-core `manifest.rs`, `src/types/addon/request.rs` | `src/stremio/capabilities.{h,cpp}` |
| URL construction | `AddonHTTPTransport::resource` — `/{resource}/{type}/{id}[/{extra}].json`, `URI_COMPONENT_ENCODE_SET`, `query_params_encode`, replace `/manifest.json` in the transport URL (query string kept) | stremio-core `http_transport.rs`, `query_params_encode.rs`, `constants.rs`; confirmed by Debrify `lib/utils/stremio_url.dart` + `test/stremio_url_test.dart`, Nuvio `AddonTransportUrls.kt` | `src/stremio/transport.{h,cpp}` |
| Legacy transport | JSON-RPC over `q.json?b=<base64>` | stremio-core `legacy/mod.rs` | `src/stremio/legacytransport.{h,cpp}` |
| Install URL handling | `stremio://` → `https://` (core `AddonDetails`), `new URL(input)` (stremio-web `Addons.js`); missing `/manifest.json` appended and scheme defaulted (Debrify `normalizeStremioManifestUri`, Nuvio `normalizeManifestUrl`); `X-Stremio-Addon` header + JSON-array collection detection (stremio-addon-client `detectFromURL.js`) | as listed | `src/stremio/addonurl.{h,cpp}`, `AddonManager::install` (`src/stremio/addonmanager.cpp`) |
| Response parsing | `ResourceResponse` (exactly one key; `null` → []; per-item `VecSkipError`), `MetaItemPreview` legacy fields, `MetaItem.videos` unique + sorted, `Video`, `Stream` untagged source order, `Subtitles` | stremio-core `response.rs`, `meta_item.rs`, `stream.rs`, `subtitles.rs` | `src/stremio/resources.{h,cpp}` |
| Catalog requests (Board) | `CatalogsWithExtra` — `AllCatalogs{extra: [], type: None}`, lazy range loading, `EmptyContent` rows hidden, errors shown | stremio-core `models/catalogs_with_extra.rs`; stremio-web `routes/Board` | `ContentService::board` (`src/stremio/contentservice.cpp`), Home rows in `resources/vui/stremio.js` |
| Discover | `CatalogWithFilters` — selectable catalogs need `default_required_extra`, `TYPE_PRIORITIES`, extras with options, `extend_one`, next page = sum of page sizes | stremio-core `models/catalog_with_filters.rs` | `discoverSelectable` (`src/stremio/capabilities.cpp`), `ContentService::discover`, Discover view |
| Pagination | `skip` only when the catalog declares it; `skip = previous skip + items` | `catalogs_with_extra.rs` `LoadNextPage`, `catalog_with_filters.rs` | `ContentService::nextPage`, `discoverSelectable` |
| Search | `AllCatalogs{extra: [search=q]}` — only catalogs whose extras include `search` and whose required extras are satisfied | `catalogs_with_extra.rs`; stremio-web `routes/Search/useSearch.js` | `ContentService::search` |
| Meta | `AllOfResource(meta/{type}/{id})`; UI uses first `Ready` in addon order, else first error, else loading | stremio-core `models/meta_details.rs`; `stremio-core-web/src/model/serialize_meta_details.rs` | `ContentService::metaPlan`, `AddonsBridge::loadMeta` (`src/addonsbridge.cpp`), `pickMeta` in `stremio.js` |
| Episodes | `Video` id/title/season/episode/released/thumbnail/overview/streams; sorting (season 0 last); `defaultVideoId` / `meta.id` guess | `meta_item.rs`, `meta_details.rs::selected_guess_stream_update` | `src/stremio/resources.cpp` (`normalizeVideos`, `guessStreamVideoId`), Details view |
| Streams | `AllOfResource(stream/{meta type}/{video id})`; streams embedded in the selected video replace addon streams in the UI | `meta_details.rs::streams_update/meta_streams_update`; `serialize_meta_details.rs` | `ContentService::streamPlan`, `AddonsBridge::loadStreams` |
| lz payloads | lz-string `compressToEncodedURIComponent` (used by core through `lz_str`) | pieroxy/lz-string `src/_compress.ts` + its test data | `src/stremio/lzstring.{h,cpp}` |
| Stream resolution | `Stream::convert` + stremio-video `convertStream`/`createTorrent`/`buildProxyUrl`: torrent, magnet, YouTube, archives, NZB, FTP go through a Stremio-compatible streaming server: LAMBDA's bundled engine by default (see *Streaming engine*), or an external one such as Stremio Service (detected with `GET /settings`); direct URLs to mpv (as stremio-shell-ng/ShellVideo does); `proxyHeaders.request` applied with mpv `http-header-fields` (Nuvio iOS `MPVPlayerBridge.swift`) | stremio-core `stream.rs`; stremio-video `withStreamingServer/*`, `ShellVideo.js`; Nuvio `iosApp/.../MPVPlayerBridge.swift` | `src/stremio/streamresolver.{h,cpp}` |
| Subtitles | `AllOfResource(subtitles/{meta type}/{video id})` with extras `videoHash`,`videoSize`,`filename` built by `extend_one` (hints first, then video params) | stremio-core `models/player.rs::subtitles_update`; stremio-web `usePlayer.js`; stremio-video `fetchVideoParams.js` | `ContentService::subtitlePlan`, `src/stremio/videoparams.{h,cpp}`, `MainWindow::fetchAddonSubtitles` |
| Configured addons | configure URL = `transportUrl.replace('manifest.json','configure')`; `configurationRequired` blocks install; config lives in the URL path/query | stremio-web `AddonDetailsModal.js`, `Addons.js`; core `update_profile.rs`; SDK `docs/advanced.md` | `AddonManager`, `src/addonconfigurewindow.{h,cpp}`, Add-ons view |
| Addon identity / order | identity = transport URL; reinstall replaces in place, new addon appended; order = array order (also how stremio-addon-manager reorders) | core `update_profile.rs`; stremio-addon-client `AddonCollection.js` | `src/stremio/addonmanager.{h,cpp}` |
| Collections / import | array of `{manifest, transportUrl, flags}`; wrappers `{addons}`, `{result:{addons}}`, `{addonCollection:{addons}}` | stremio-addon-client `AddonCollection.js`; Debrify `_extractAddonDescriptors` | `AddonManager::importCollection` |
| Addon catalogs | `addon_catalog` resource via `addonCatalogs` in the manifest (Cinemeta: `official`, `community`) returning `{addons:[descriptor]}` | core `catalog_with_filters.rs` (`DescriptorPreview` adapter), official-addons `index.json` | `ContentService::addonCatalogs`, Add-ons → Discover add-ons |
| Addon ordering in results | one independent result slot per planned request, in addon order | core `models/common/resource_loadable.rs` | one slot per `PlannedRequest` in `AddonsBridge` / `stremio.js` |
| Caching | HTTP cache honouring `Cache-Control` (SDK `cacheMaxAge`/`staleRevalidate`/`staleError` → headers); core reuses loaded slots unless `force` | SDK `docs/api/requests/*`; core `resources_update`; browser fetch | `AddonClient` (`src/stremio/addonclient.cpp`): `QNetworkDiskCache` + in-memory parsed responses |
| Errors / timeouts | status must be 200/201; per-addon error slot; no global failure; 15 s per request (Debrify `_requestTimeout`, Cove `ADDON_REQUEST_TIMEOUT_MS`) | `stremio-core-web/src/env.rs`; Debrify `stremio_service.dart`; Cove `AddonManager.kt` | `AddonClient` |
| Streaming engine | Stremio's closed `server.js` API, implemented openly by stream-server (`server/src/lib.rs` router, `routes/engine.rs` create/stats, `routes/stream.rs` ranged file streaming with piece prioritisation, `cache_cleaner.rs`, `ServerConfig::embedded`) | stremio-native/stream-server | `tools/stream-server/lambda_stream_host.rs` (127.0.0.1 host), `src/stremio/serverprocess.{h,cpp}` (child process), `StreamingServer::ensure` |
| YouTube | `Stream::download_url` (watch page in the browser) | stremio-core `stream.rs` | `StreamResolver::youTubeWatchUrl` |
