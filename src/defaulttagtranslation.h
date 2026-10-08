/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Translation keys for Mathom's built-in tags, independent of user labels.
 */
#ifndef MATHOM_DEFAULT_TAG_TRANSLATION_H
#define MATHOM_DEFAULT_TAG_TRANSLATION_H

#include <KLocalizedString>
#include <QDomDocument>
#include <QSet>
#include <QStringList>

namespace DefaultTagTranslation {

struct NameKeys {
    const char *stateId;
    const char *tagName;
    const char *stateName;
};

// Keep these keys in sync with Tag::createDefaultTagsSet().
// Literal i18n() calls in that function provide gettext extraction.
inline const NameKeys *forStateId(const QString &id)
{
    static constexpr NameKeys entries[] = {
        {"todo_unchecked", "To Do", "Unchecked"},
        {"todo_done", "To Do", "Done"},
        {"progress_000", "Progress", "0 %"},
        {"progress_025", "Progress", "25 %"},
        {"progress_050", "Progress", "50 %"},
        {"progress_075", "Progress", "75 %"},
        {"progress_100", "Progress", "100 %"},
        {"priority_low", "Priority", "Low"},
        {"priority_medium", "Priority", "Medium"},
        {"priority_high", "Priority", "High"},
        {"preference_bad", "Preference", "Bad"},
        {"preference_good", "Preference", "Good"},
        {"preference_excellent", "Preference", "Excellent"},
        {"highlight", "Highlight", "Highlight"},
        {"important", "Important", "Important"},
        {"very_important", "Very Important", "Very Important"},
        {"information", "Information", "Information"},
        {"idea", "Idea", "Idea"},
        {"title", "Title", "Title"},
        {"code", "Code", "Code"},
        {"work", "Work", "Work"},
        {"personal", "Personal", "Personal"},
        {"funny", "Funny", "Funny"},
    };
    for (const NameKeys &entry : entries) {
        if (id == QLatin1StringView(entry.stateId))
            return &entry;
    }
    return nullptr;
}

inline const NameKeys *forTagElement(const QDomElement &tag)
{
    for (QDomElement state = tag.firstChildElement(QStringLiteral("state"));
         !state.isNull();
         state = state.nextSiblingElement(QStringLiteral("state"))) {
        if (const NameKeys *entry = forStateId(state.attribute(QStringLiteral("id"))))
            return entry;
    }
    return nullptr;
}

inline QString translated(const char *message, const QString &language = {})
{
    if (!message)
        return {};
    const KLocalizedString localized = ki18n(message);
    return language.isEmpty() ? localized.toString()
                              : localized.toString(QStringList{language});
}

// Legacy tags.xml has no metadata recording which locale produced its
// built-in names. Infer the language from several unchanged tag names,
// never from a single name. If evidence is weak, preserve everything.
inline QString legacyLanguage(const QDomElement &root)
{
    QSet<QString> languages = KLocalizedString::availableApplicationTranslations();
    languages.insert(QStringLiteral("en_US"));

    QString bestLanguage;
    int bestScore = 0;
    int secondScore = 0;

    for (const QString &language : languages) {
        int score = 0;
        for (QDomElement tag = root.firstChildElement(QStringLiteral("tag"));
             !tag.isNull();
             tag = tag.nextSiblingElement(QStringLiteral("tag"))) {
            if (tag.hasAttribute(QStringLiteral("automaticName")))
                continue;
            const NameKeys *entry = forTagElement(tag);
            if (!entry)
                continue; // user-created tag
            const QString stored = tag.firstChildElement(QStringLiteral("name")).text();
            const QString expected = translated(entry->tagName, language);
            // A missing catalog falls back to English. Do not count this
            // fallback as evidence for a non-English language.
            if (stored == expected &&
                (language == QLatin1StringView("en_US") ||
                 expected != QString::fromUtf8(entry->tagName))) {
                ++score;
            }
        }
        if (score > bestScore) {
            secondScore = bestScore;
            bestScore = score;
            bestLanguage = language;
        } else if (score > secondScore) {
            secondScore = score;
        }
    }
    return bestScore >= 5 && bestScore >= secondScore + 2 ? bestLanguage : QString();
}

inline bool automaticName(const QDomElement &element,
                          const QString &stored,
                          const char *defaultMessage,
                          const QString &legacyLanguage)
{
    if (element.hasAttribute(QStringLiteral("automaticName")))
        return element.attribute(QStringLiteral("automaticName")) == QLatin1StringView("true")
               && defaultMessage;
    // Backwards-compatible migration: only migrate a name that matches
    // the built-in label in the confidently inferred original language.
    return defaultMessage && !legacyLanguage.isEmpty() && !stored.isEmpty()
           && stored == translated(defaultMessage, legacyLanguage);
}

} // namespace DefaultTagTranslation

#endif // MATHOM_DEFAULT_TAG_TRANSLATION_H
