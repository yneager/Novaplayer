#pragma once

// lz-string compressToEncodedURIComponent, as used by stremio-core to build
// the ?lz= payload of streaming-server archive/NZB/FTP URLs
// (types/resource/stream.rs, lz_str::compress_to_encoded_uri_component).
// Line-by-line port of pieroxy/lz-string src/_compress.ts (MIT, (c) 2013
// Pieroxy); it works on UTF-16 code units exactly like JavaScript strings.

#include <QString>

namespace stremio {

QString lzCompressToEncodedUriComponent(const QString &input);

} // namespace stremio
