# Stremio addon compatibility log

Each entry records an incompatibility or a disputed behaviour, what Stremio does,
what LAMBDA does, the reference used, and the test that guards it.

Real-world evidence sources: Stremio's GitHub issue trackers (`stremio-bugs`,
`stremio-core`, `stremio-addon-sdk`) and the compatibility notes of other clients.
Reddit (`r/Stremio`, `r/StremioAddons`) could not be read from the development
environment: the Reddit API answered 403 and page fetches were refused, so no
Reddit thread is cited here.

## Decisions taken during research

### C-001 — Subtitle addons and `idPrefixes`
- **Addon:** any subtitle addon whose manifest limits `idPrefixes` (e.g. OpenSubtitles v3: `["tt"]`).
- **Resource:** `subtitles`.
- **Stremio:** `AllOfResource` → `Manifest::is_resource_supported`, so `types` and `idPrefixes` both filter subtitle requests (stremio-core `models/player.rs::subtitles_update`).
- **Other clients:** Nuvio filters the same way (`SubtitleRepository.kt::supportsSubtitleType`). Debrify deliberately ignores `types`/`idPrefixes` for subtitles (`stremio_subtitle_service.dart`, `StremioSubtitleService.kt`) because "many addons misconfigure it".
- **LAMBDA:** follows Stremio core. A misconfigured addon behaves the same as in Stremio.
- **Test:** `tst_capabilities::subtitlesRespectIdPrefixes`.

### C-002 — Full resource without `idPrefixes`
- **Stremio:** a `{name, types}` resource with no `idPrefixes` accepts every id; it does **not** fall back to the manifest's top-level `idPrefixes` (code in `manifest.rs`, despite the doc comment). Empty arrays also accept every id. stremio-bugs#1469 shows a client that turned a missing `idPrefixes` into `[]` on install and broke stream requests.
- **Nuvio** falls back to the top-level list; **stremio-addon-client** treats `[]` as "no ids".
- **LAMBDA:** stores `idPrefixes` as optional (missing ≠ empty) and uses core's rule: missing or empty → all ids.
- **Test:** `tst_capabilities::fullResourceWithoutPrefixesAcceptsAll`, `tst_manifest::idPrefixesOptionalPreserved`.

### C-003 — Configured manifest URLs with query tokens
- **Stremio:** request URL = transport URL with `/manifest.json` replaced, so `?token=…` stays at the end.
- **Raffi** strips `/manifest.json` from the URL end only, so `…/manifest.json?token=x` keeps the query in the middle and later appends `/manifest.json` after it — broken requests.
- **LAMBDA:** keeps the full transport URL; replaces only the `/manifest.json` path suffix and preserves the query byte-for-byte; never re-encodes the configured path (stremio-bugs#1188: long `key=value` paths with emoji). Debrify's `stremio_url_test.dart` cases are reproduced.
- **Test:** `tst_transport::queryTokenPreserved`, `tst_transport::configuredPathNotReencoded`.

### C-004 — Multiple extras in a catalog path
- **Stremio/SDK:** extras are one path segment joined with `&` (`search=x&skip=100`).
- **Raffi** joins them with `/` (`/search=x/skip=100`) — incompatible.
- **LAMBDA:** follows Stremio.
- **Test:** `tst_transport::extrasJoinedWithAmpersand`.

### C-005 — Stream ids for episodes
- **Stremio:** the stream request id is the `Video.id` from the addon's meta (`meta_details.rs`), the type is the meta type.
- **Raffi** builds `imdb:season:episode` itself, which fails for non-IMDb catalogs (kitsu, tmdb, custom ids).
- **LAMBDA:** always uses the canonical `Video.id`.
- **Test:** `tst_streams::episodeStreamUsesVideoId`; runtime: the local test add-on received `/stream/series/lambdatest%3Ashow%3A2%3A3.json` for S2 E3.

### C-006 — Streams embedded in meta videos
- **Stremio:** if the selected `Video` has `streams`, the UI lists only those (`serialize_meta_details.rs`); YouTube-style ids `yt_id:CHANNEL:VIDEO` produce a YouTube stream.
- **LAMBDA:** same.
- **Test:** runtime (logic in `AddonsBridge::loadStreams`): a test-add-on episode with embedded `streams` listed only that stream and no `stream/` request was sent.

### C-007 — Inline `stream.subtitles`
- **Stremio desktop** ignores subtitles embedded in a stream object (stremio-bugs#2348, open). They are part of the SDK stream spec.
- **LAMBDA:** lists them in the subtitle picker, attributed to the stream's addon, and loads them through mpv `sub-add` on selection.
- **Test:** `tst_resources::streamInlineSubtitles`.

### C-008 — Duplicate subtitle downloads
- **Evidence:** stremio-bugs#2292 — clients requesting the same slow, generated subtitle URL twice caused failures.
- **LAMBDA:** an addon subtitle is only downloaded when the user selects it, once, by mpv; re-selecting reuses the loaded track.
- **Test:** runtime: selecting an add-on subtitle made exactly one request for its file (test add-on log).

### C-009 — `proxyHeaders` without the streaming server
- **Stremio:** headers are applied only by rewriting the URL to the streaming server's `/proxy/` endpoint; without the server the headers are dropped.
- **Nuvio (libmpv on iOS)** sets mpv `http-header-fields` from `proxyHeaders.request` (skips `Range`, escapes `,` and `\`, clears it for every new file).
- **LAMBDA:** uses Nuvio's libmpv method for request headers (works with or without the server); response headers only matter to browsers and are ignored, like Nuvio.
- **Test:** `tst_streams::proxyHeadersToMpvOption`, `tst_streams::directUrlNeedsNoServer`; runtime: a header-protected test stream played and every request carried the header.

### C-010 — Install URL forms
- **Stremio web:** accepts only a parseable absolute URL and later requires the transport URL to end in `/manifest.json`; `stremio://` is mapped to `https://`.
- **Debrify / Nuvio:** also accept a missing scheme (→ `https://`) and append `/manifest.json` when missing (never for `/stremio/v1` legacy URLs).
- **LAMBDA:** accepts the superset (Stremio-valid URLs are unchanged); detects `X-Stremio-Addon` headers and JSON-array collections like stremio-addon-client.
- **Test:** `tst_addonurl::*`.

### C-011 — Subtitles without `id`
- **Evidence:** the SDK's own protocol example (`docs/protocol.md`) returns `{url, lang}` without `id`; stremio-core's `Subtitles` requires `id`, so such subtitles are dropped by Stremio.
- **LAMBDA:** like Nuvio (`SubtitleRepository.kt`), a missing `id` falls back to the URL; `lang` and `url` stay required.
- **Test:** `tst_resources::subtitlesResponse`.

### C-012 — Explicit `null` for defaulted fields
- **Stremio:** serde applies a default only when a key is missing; an explicit `null` for e.g. a boolean behaviour hint rejects the whole object.
- **LAMBDA:** `null` is read as the default for fields that have one (booleans, lists, optional strings). Structural requirements (required ids, URL validity of sources, semver) are kept exactly.
- **Test:** `tst_manifest::idPrefixesOptionalPreserved`, `tst_resources::nullAndSkippedItems`.

### C-013 — Subtitle hash without the streaming server
- **Stremio:** `videoHash`/`videoSize` come from the streaming server (`/opensubHash`); without it only `filename` is sent.
- **LAMBDA:** uses the server when it runs; otherwise computes the same OpenSubtitles hash from two HTTP range requests (with the stream's request headers). Servers that ignore `Range` are not downloaded (the request is aborted).
- **Test:** `tst_streams::openSubtitlesHash`, `videoParamsComputedLocally`, `videoParamsNoRangeSupport`, `videoParamsFollowRedirects`; runtime: `videoHash=4e63ff4da5d016d8` for the archive.org Big Buck Bunny file, confirmed with an independent Python computation.

### C-014 — Redirecting media hosts
- **Found in runtime testing:** archive.org answers ranged requests with two redirects; the first version aborted on the 302 and sent subtitle requests without the hash.
- **Fix:** redirects are followed; only a final non-206 answer is aborted.
- **Test:** `tst_streams::videoParamsFollowRedirects`.

### C-015 — Legacy OpenSubtitles (`/stremio/v1`)
- **Observed:** `https://opensubtitles.strem.io/stremio/v1` still serves its manifest and answers `subtitles.find`, but returns no subtitles (also for well-known titles), and it answers HTTP 403 to non-browser user agents such as Python's default.
- **LAMBDA:** installs and queries it through the legacy transport; its empty answers are an empty slot. LAMBDA sends a `Mozilla/5.0 … LAMBDA-Player/<version>` user agent, which it accepts.
- **Test:** `tst_manifest::legacyManifestConversion`, `tst_addonclient::legacyTransportRequest`; runtime install.

### C-016 — Catalogs with required extras on Home
- **Stremio:** the Board plans `AllCatalogs{extra: []}`, so a catalog with a required extra (Cinemeta "New", whose `genre` is required) is not a Home row; Discover lists it with its first option selected.
- **LAMBDA:** same (users may notice "New" only under Discover).
- **Test:** `tst_capabilities::boardPlanOrderAndRequiredExtras`, `tst_capabilities::discoverSelectable`; runtime with the live Cinemeta manifest.

### C-017 — Order of the default add-ons
- **Stremio:** Cinemeta is installed before OpenSubtitles v3 (stremio-official-addons order).
- **Found in runtime testing:** installing both concurrently appended whichever answered first.
- **Fix:** they are installed one after the other.

### C-018 — Newly opened video stays paused after Home (LAMBDA, pre-existing)
- **Found in runtime testing:** `showHome()` pauses mpv and mpv keeps `pause` across `loadfile`, so the next opened file (local or add-on) did not start although the UI showed it playing. Present since v0.2.2.
- **Fix:** `pause=no` before `loadfile`, as Stremio's mpv shell (`ShellVideo.js`) does after its `loadfile`.

## Built-in streaming engine (standalone playback)

### C-019 — Torrent `/create` also when `fileIdx` is known
- **Stremio:** `createTorrent.js` skips `/{infoHash}/create` when the stream has a `fileIdx` and no trackers, and goes straight to `/{infoHash}/{fileIdx}`. A dead torrent then shows up as a player that never starts.
- **LAMBDA:** always calls `/create` and asks the server to resolve the file list (`guessFileIdx: {}`; the addon's `fileIdx` still wins over the guess). The answer is checked before mpv is started: no files → "no peers are sharing it", `fileIdx` past the end → "file is not in the torrent", `error` → shown as is. The file's name and size are kept for subtitle requests.
- **Test:** `tst_streams::torrentViaStreamingServer`, `tst_streams::torrentCreateErrors`.

### C-020 — YouTube streams
- **Stremio:** `ytId` streams play through the streaming server's `/yt/{id}` route; without a server, Stremio opens the YouTube page (`Stream::download_url`).
- **LAMBDA:** always opens the YouTube page in the browser (also for YouTube links in `url` streams); LAMBDA does not bundle a YouTube extractor.
- **Test:** `tst_streams::youTubeStreams`.

### C-021 — Links that are web pages
- **Stremio:** hands any `url` stream to the player.
- **LAMBDA:** a `url` whose path does not name a media file is checked with one ranged request first. `text/html` opens in the browser ("This link is a web page"), 401/403/404/410 are reported, anything else goes to mpv.
- **Test:** `tst_streams::webPageOpensExternally`.

### C-022 — `/opensubHash` answers with `"error": null`
- **stream-server** (`routes/subtitles.rs`) answers `{"error": null, "result": {...}}` on success; the first LAMBDA version treated any `error` key as a failure and lost the hash.
- **LAMBDA:** only a non-null `error` is a failure. Only streams that the server serves are hashed by it; other links are hashed locally so their `proxyHeaders` are sent.
- **Test:** `tst_streams::videoParamsFromServer`.

### C-023 — Archive streams in stream-server
- **Found in runtime testing:** the pinned stream-server downloads an HTTP archive to an extension-less temp file and then detects the format by extension (every remote archive failed with "Failed to select archive file"), and its ZIP/TAR readers read into a `ReadBuf::take` view without advancing the buffer (responses with the right headers and no body). Downloaded archives are also left in the temp folder.
- **LAMBDA:** carries two small patches (`tools/stream-server/patches/`) and points the engine's TMP/TEMP into the stream cache, so "Clear cache" removes downloaded archives.
- **Test:** runtime — a ZIP stream from the local test add-on plays.
