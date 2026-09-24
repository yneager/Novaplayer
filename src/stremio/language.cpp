#include "stremio/language.h"

#include <QLocale>
#include <QRegularExpression>

namespace stremio {

QString languageName(const QString &code)
{
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("Unknown");
    }
    const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("[-_]")), Qt::SkipEmptyParts);
    const QString base = parts.value(0).toLower();
    if (base.size() < 2 || base.size() > 3) {
        return trimmed;
    }
    const QLocale::Language language = QLocale::codeToLanguage(base, QLocale::AnyLanguageCode);
    if (language == QLocale::AnyLanguage || language == QLocale::C) {
        // OpenSubtitles' Brazilian Portuguese code.
        if (base == QLatin1String("pob")) {
            return QStringLiteral("Portuguese (Brazil)");
        }
        return trimmed;
    }
    QString name = QLocale::languageToString(language);
    if (parts.size() > 1) {
        const QLocale::Territory territory = QLocale::codeToTerritory(parts[1].toUpper());
        if (territory != QLocale::AnyTerritory) {
            name += QStringLiteral(" (%1)").arg(QLocale::territoryToString(territory));
        }
    }
    return name;
}

} // namespace stremio
