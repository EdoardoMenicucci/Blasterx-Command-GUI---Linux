#include "translator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>

Translator::Translator(const QString &languageFile, const QString &catalogDirectory, QObject *parent)
    : QTranslator(parent),
      m_languageFile(languageFile.isEmpty() ? QDir::homePath() + "/.config/omarchy/language" : languageFile),
      m_catalogDirectory(catalogDirectory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Translator::reload);
    reload();
}

// One registry code on the first line; anything else, or no file, is empty,
// and the language comes from the system locale instead.
QString Translator::readLanguage(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QString code = QString::fromUtf8(file.readLine()).trimmed();
    static const QRegularExpression valid("^[a-z]{2,3}(-[A-Za-z]{2})?$");
    return valid.match(code).hasMatch() ? code : QString();
}

// A POSIX locale name as a language code: it_IT.UTF-8@euro is it-IT. Empty
// for C, POSIX and anything that is not a locale name.
static QString languageCode(const QString &localeName) {
    static const QRegularExpression name("^([A-Za-z]{2,3})(?:_([A-Za-z]{2}))?(?:[.@].*)?$");
    const auto match = name.match(localeName.trimmed());
    if (!match.hasMatch())
        return {};
    const QString language = match.captured(1).toLower(), region = match.captured(2).toUpper();
    return region.isEmpty() ? language : language + "-" + region;
}

// The language of the system locale, read the way gettext does: LC_ALL,
// LC_MESSAGES, then LANG name the locale, and LANGUAGE lists preferences
// ahead of it. The first one with a catalog wins, so LANGUAGE=de:it shows
// Italian while there is no German; with no catalog at all the first is kept,
// for its numbers and dates. The C locale, or none, is English.
QString Translator::systemLanguage(const QString &catalogDirectory) {
    QString locale;
    for (const char *variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        locale = qEnvironmentVariable(variable);
        if (!locale.isEmpty())
            break;
    }
    const QString localeCode = languageCode(locale);
    if (localeCode.isEmpty())
        return "en";

    QStringList candidates;
    for (const auto &entry : qEnvironmentVariable("LANGUAGE").split(':', Qt::SkipEmptyParts)) {
        const QString code = languageCode(entry);
        if (!code.isEmpty())
            candidates << code;
    }
    candidates << localeCode;

    for (const auto &code : candidates) {
        const QString base = code.section('-', 0, 0);
        if (base == "en" || QFileInfo::exists(catalogDirectory + "/" + code + ".json")
            || QFileInfo::exists(catalogDirectory + "/" + base + ".json"))
            return code;
    }
    return candidates.first();
}

// CLDR cardinal plural categories for whole counts, for Omarchy's languages
// (the same rules as Omarchy's shell/Commons/Plural.js). A region Omarchy has
// no code for, as a system locale can give (it-IT, fr-CA), counts like its
// language.
QString Translator::pluralCategory(const QString &language, long long count) {
    static const QStringList regional{"zh-CN", "zh-TW", "pt-BR", "es-MX"};
    if (language.contains('-') && !regional.contains(language))
        return pluralCategory(language.section('-', 0, 0), count);

    const long long n = count < 0 ? -count : count, n10 = n % 10, n100 = n % 100;
    const bool millions = n != 0 && n % 1000000 == 0;
    const bool teens = n100 >= 12 && n100 <= 14;
    const QString code = language;
    auto in = [&](std::initializer_list<const char *> codes) {
        for (const char *c : codes) if (code == QLatin1String(c)) return true;
        return false;
    };
    if (in({"ja", "ko", "zh", "zh-CN", "zh-TW", "th", "vi", "id", "ms"})) return "other";
    if (in({"hi", "bn", "fa", "si"})) return n <= 1 ? "one" : "other";
    if (in({"fr", "pt-BR"})) return n <= 1 ? "one" : millions ? "many" : "other";
    if (in({"ca", "es", "es-MX", "it", "pt"})) return n == 1 ? "one" : millions ? "many" : "other";
    if (code == "fil") return (n10 == 4 || n10 == 6 || n10 == 9) ? "other" : "one";
    if (code == "is") return n10 == 1 && n100 != 11 ? "one" : "other";
    if (in({"cs", "sk"})) return n == 1 ? "one" : n >= 2 && n <= 4 ? "few" : "other";
    if (code == "pl") return n == 1 ? "one" : n10 >= 2 && n10 <= 4 && !teens ? "few" : "many";
    if (in({"ru", "uk"})) return n10 == 1 && n100 != 11 ? "one" : n10 >= 2 && n10 <= 4 && !teens ? "few" : "many";
    if (in({"hr", "sr"})) return n10 == 1 && n100 != 11 ? "one" : n10 >= 2 && n10 <= 4 && !teens ? "few" : "other";
    if (code == "sl") return n100 == 1 ? "one" : n100 == 2 ? "two" : n100 >= 3 && n100 <= 4 ? "few" : "other";
    if (code == "lt") return n100 >= 11 && n100 <= 19 ? "other" : n10 == 1 ? "one" : n10 >= 2 ? "few" : "other";
    if (code == "lv") return n10 == 0 || (n100 >= 11 && n100 <= 19) ? "zero" : n10 == 1 ? "one" : "other";
    if (code == "ro") return n == 1 ? "one" : n == 0 || (n100 >= 1 && n100 <= 19) ? "few" : "other";
    if (code == "ar") {
        if (n <= 2) return n == 0 ? "zero" : n == 1 ? "one" : "two";
        return n100 >= 3 && n100 <= 10 ? "few" : n100 >= 11 ? "many" : "other";
    }
    if (code == "he") return n == 1 ? "one" : n == 2 ? "two" : "other";
    if (code == "ga") return n == 1 ? "one" : n == 2 ? "two" : n >= 3 && n <= 6 ? "few" : n >= 7 && n <= 10 ? "many" : "other";
    return n == 1 ? "one" : "other";
}

// The locale Omarchy formats this language with, from its registry, so the
// app's numbers and dates match the desktop's (Arabic there uses Western
// digits); without Omarchy, Qt's own locale for the code.
QLocale Translator::formatLocale(const QString &language) {
    QFile registry(qEnvironmentVariable("OMARCHY_PATH") + "/default/i18n/locales.json");
    if (registry.open(QIODevice::ReadOnly)) {
        const QString name = QJsonDocument::fromJson(registry.readAll()).object()
                                 .value(language).toObject().value("formatLocale").toString();
        if (!name.isEmpty())
            return QLocale(name);
    }
    return QLocale(language);
}

QString Translator::translate(const char *, const char *sourceText, const char *, int n) const {
    // A null string tells Qt there is no translation, so it shows the English.
    const QJsonValue value = m_strings.value(QString::fromUtf8(sourceText));
    if (value.isString())
        return value.toString();
    if (value.isObject()) {
        const QJsonObject forms = value.toObject();
        const QString form = forms.value(n < 0 ? "other" : pluralCategory(m_language, n)).toString();
        return form.isEmpty() ? forms.value("other").toString() : form;
    }
    return {};
}

void Translator::reload() {
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // omarchy-language replaces the file, which drops it from the watch, and a
    // first pick creates it; watch its directory too and re-arm every time.
    const QString directory = QFileInfo(m_languageFile).absolutePath();
    if (QFileInfo::exists(directory))
        m_watcher.addPath(directory);
    if (QFileInfo::exists(m_languageFile))
        m_watcher.addPath(m_languageFile);

    // Catalogs are compiled in, so only a different language needs a reload.
    // Omarchy's choice first; without one, the system locale.
    QString language = readLanguage(m_languageFile);
    if (language.isEmpty())
        language = systemLanguage(m_catalogDirectory);
    if (language == m_language)
        return;

    // English first (it carries the plural forms of counted strings), then
    // the base language and the regional file over it (pt, then pt-BR), so a
    // missing string falls back toward English one step at a time.
    QHash<QString, QJsonValue> strings;
    QStringList codes{"en", language.section('-', 0, 0), language};
    codes.removeDuplicates();
    for (const auto &code : codes) {
        QFile catalog(m_catalogDirectory + "/" + code + ".json");
        if (!catalog.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject object = QJsonDocument::fromJson(catalog.readAll()).object();
        for (auto it = object.begin(); it != object.end(); ++it)
            if ((it.value().isString() && !it.value().toString().isEmpty()) || it.value().isObject())
                strings.insert(it.key(), it.value());
    }

    m_language = language;
    m_strings = strings;
    // Dates, numbers, and Qt's own day and month names follow the language
    // too, and right-to-left languages mirror the window.
    const QLocale locale = formatLocale(language);
    QLocale::setDefault(locale);
    QGuiApplication::setLayoutDirection(locale.textDirection());
    emit changed();
}
