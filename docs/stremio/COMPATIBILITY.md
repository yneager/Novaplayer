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
- **Test:** `tst_services::episodeStreamUsesVideoId`.

### C-006 — Streams embedded in meta videos
- **Stremio:** if the selected `Video` has `streams`, the UI lists only those (`serialize_meta_details.rs`); YouTube-style ids `yt_id:CHANNEL:VIDEO` produce a YouTube stream.
- **LAMBDA:** same.
- **Test:** `tst_services::embeddedVideoStreamsReplaceAddonStreams`.

### C-007 — Inline `stream.subtitles`
- **Stremio desktop** ignores subtitles embedded in a stream object (stremio-bugs#2348, open). They are part of the SDK stream spec.
- **LAMBDA:** lists them in the subtitle picker, attributed to the stream's addon, and loads them through mpv `sub-add` on selection.
- **Test:** `tst_resources::streamInlineSubtitles`.

### C-008 — Duplicate subtitle downloads
- **Evidence:** stremio-bugs#2292 — clients requesting the same slow, generated subtitle URL twice caused failures.
- **LAMBDA:** an addon subtitle is only downloaded when the user selects it, once, by mpv; re-selecting reuses the loaded track.
- **Test:** covered by `tst_subtitles::selectionLoadsOnce` (logic) and manual runtime check.

### C-009 — `proxyHeaders` without the streaming server
- **Stremio:** headers are applied only by rewriting the URL to the streaming server's `/proxy/` endpoint; without the server the headers are dropped.
- **Nuvio (libmpv on iOS)** sets mpv `http-header-fields` from `proxyHeaders.request` (skips `Range`, escapes `,` and `\`, clears it for every new file).
- **LAMBDA:** uses Nuvio's libmpv method for request headers (works with or without the server); response headers only matter to browsers and are ignored, like Nuvio.
- **Test:** `tst_streamresolver::proxyHeadersToMpvOption`.

### C-010 — Install URL forms
- **Stremio web:** accepts only a parseable absolute URL and later requires the transport URL to end in `/manifest.json`; `stremio://` is mapped to `https://`.
- **Debrify / Nuvio:** also accept a missing scheme (→ `https://`) and append `/manifest.json` when missing (never for `/stremio/v1` legacy URLs).
- **LAMBDA:** accepts the superset (Stremio-valid URLs are unchanged); detects `X-Stremio-Addon` headers and JSON-array collections like stremio-addon-client.
- **Test:** `tst_addonurl::*`.
