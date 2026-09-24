#pragma once

#include <QString>

namespace stremio {

// Display name for a subtitle `lang` value. Addons send ISO 639-1 ("en"),
// ISO 639-2 ("eng", "fre"/"fra"), region forms ("pt-BR", "pt_br") or free
// text; unknown values are shown as given.
QString languageName(const QString &code);

} // namespace stremio
