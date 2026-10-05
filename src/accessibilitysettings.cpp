#include "accessibilitysettings.h"

#include "note.h"
#include "notecontent.h"

#include <QApplication>
#include <QBrush>
#include <QFont>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QVariant>
#include <QVector>
#include <QWidget>

#include <KConfigGroup>
#include <KSharedConfig>


namespace
{

void enable(
    AccessibilityConfiguration &config,
    AccessibilityModule module)
{
    config.modules |= module;
}


struct SyllableRange
{
    int start = 0;
    int length = 0;
};


/*
 * Voyelles graphiques utilisees pour reperer les noyaux
 * des syllabes ecrites.
 */
bool isFrenchVowelLetter(QChar character)
{
    static const QString vowels =
        QStringLiteral(
            "aeiouy"
            "àâä"
            "éèêë"
            "îï"
            "ôö"
            "ùûü"
            "ÿ"
            "æœ");

    return vowels.contains(
        character.toLower());
}


bool hasDiaeresis(QChar character)
{
    static const QString diaeresis =
        QStringLiteral("ëïüÿ");

    return diaeresis.contains(
        character.toLower());
}


/*
 * En francais, y peut jouer le role d'une consonne
 * entre deux voyelles :
 *
 * voyage -> vo-ya-ge
 * royaume -> ro-yau-me
 */
bool isConsonantalY(
    const QString &word,
    int position)
{
    if (position < 0
        || position >= word.length()) {

        return false;
    }

    if (word.at(position).toLower()
        != QLatin1Char('y')) {

        return false;
    }

    if (position == 0
        || position + 1 >= word.length()) {

        return false;
    }

    return isFrenchVowelLetter(
               word.at(position - 1))
        && isFrenchVowelLetter(
               word.at(position + 1));
}


/*
 * Le u de qu et de certains gu ne constitue pas
 * un noyau vocalique.
 *
 * qui     -> qui
 * liquide -> li-qui-de
 * guerre  -> guer-re
 * guide   -> gui-de
 */
bool isSilentGraphicU(
    const QString &word,
    int position)
{
    if (position <= 0
        || position + 1 >= word.length()) {

        return false;
    }

    if (word.at(position).toLower()
        != QLatin1Char('u')) {

        return false;
    }

    const QChar previous =
        word.at(position - 1).toLower();

    const QChar next =
        word.at(position + 1).toLower();


    /*
     * qu + voyelle
     */
    if (previous == QLatin1Char('q')
        && isFrenchVowelLetter(next)) {

        return true;
    }


    /*
     * gu devant e/i/y.
     *
     * Le trema est volontairement exclu :
     * il signale justement une prononciation particuliere.
     */
    if (previous == QLatin1Char('g')) {

        static const QString following =
            QStringLiteral(
                "eéèê"
                "iî"
                "y");

        if (following.contains(next))
            return true;
    }

    return false;
}


bool isEffectiveVowel(
    const QString &word,
    int position)
{
    if (position < 0
        || position >= word.length()) {

        return false;
    }

    if (!isFrenchVowelLetter(
            word.at(position))) {

        return false;
    }

    if (isConsonantalY(
            word,
            position)) {

        return false;
    }

    if (isSilentGraphicU(
            word,
            position)) {

        return false;
    }

    return true;
}


/*
 * Retourne la longueur d'un grapheme vocalique courant.
 *
 * Il ne s'agit PAS encore du moteur phonemique :
 * celui-ci constituera un autre module Mathom.
 */
int vowelGraphemeLength(
    const QString &word,
    int position)
{
    if (!isEffectiveVowel(
            word,
            position)) {

        return 0;
    }


    /*
     * Un trema interdit de fusionner cette voyelle
     * avec la precedente.
     *
     * mais -> "mais"
     * maïs -> "ma-ïs"
     */
    if (hasDiaeresis(
            word.at(position))) {

        return 1;
    }


    static const QStringList graphemes = {
        QStringLiteral("eau"),
        QStringLiteral("oeu"),
        QStringLiteral("œu"),

        QStringLiteral("ai"),
        QStringLiteral("ei"),
        QStringLiteral("au"),
        QStringLiteral("eu"),
        QStringLiteral("ou"),
        QStringLiteral("oi"),

        QStringLiteral("ay"),
        QStringLiteral("ey")
    };


    const QString lower =
        word.toLower();

    for (const QString &grapheme : graphemes) {

        if (position + grapheme.length()
            > word.length()) {

            continue;
        }

        if (lower.mid(
                position,
                grapheme.length())
            != grapheme) {

            continue;
        }


        bool valid = true;

        for (int offset = 0;
             offset < grapheme.length();
             ++offset) {

            const int current =
                position + offset;

            /*
             * Un trema au milieu d'un groupe impose
             * une nouvelle unite.
             */
            if (offset > 0
                && hasDiaeresis(
                    word.at(current))) {

                valid = false;
                break;
            }

            if (!isEffectiveVowel(
                    word,
                    current)) {

                valid = false;
                break;
            }
        }

        if (valid)
            return grapheme.length();
    }


    return 1;
}


bool isCommonFrenchOnset(
    const QString &cluster)
{
    static const QStringList onsets = {
        QStringLiteral("bl"),
        QStringLiteral("br"),
        QStringLiteral("cl"),
        QStringLiteral("cr"),
        QStringLiteral("dr"),
        QStringLiteral("fl"),
        QStringLiteral("fr"),
        QStringLiteral("gl"),
        QStringLiteral("gr"),
        QStringLiteral("pl"),
        QStringLiteral("pr"),
        QStringLiteral("tr"),
        QStringLiteral("vr"),

        QStringLiteral("ch"),
        QStringLiteral("ph"),
        QStringLiteral("th"),
        QStringLiteral("gn"),

        QStringLiteral("qu"),
        QStringLiteral("gu")
    };

    return onsets.contains(
        cluster.toLower());
}


/*
 * Segmentation en syllabes ecrites.
 *
 * Cette implementation est propre a Mathom.
 * Elle ne reprend pas le moteur de LireCouleur.
 *
 * Principes :
 *
 * - reconnaissance de plusieurs graphemes vocaliques courants ;
 * - distinction des hiatus ;
 * - gestion contextuelle du y ;
 * - gestion du u graphique de qu/gu ;
 * - une consonne entre deux noyaux rejoint la syllabe suivante ;
 * - les groupes consonantiques courants restent ensemble ;
 * - une consonne double est partagee entre les syllabes.
 *
 * Le futur module "Coloration des phonemes" utilisera un moteur
 * distinct et pourra traiter la prononciation proprement dite.
 */
QVector<SyllableRange> syllableRanges(
    const QString &word)
{
    struct Nucleus
    {
        int start = 0;
        int end = 0;
    };


    QVector<Nucleus> nuclei;

    int position = 0;

    while (position < word.length()) {

        const int graphemeLength =
            vowelGraphemeLength(
                word,
                position);

        if (graphemeLength <= 0) {
            ++position;
            continue;
        }

        nuclei.append(
            {
                position,
                position + graphemeLength
            });

        position += graphemeLength;
    }


    /*
     * Aucun ou un seul noyau :
     * aucune coupure necessaire.
     */
    if (nuclei.size() <= 1) {
        return {
            {0, static_cast<int>(word.length())}
        };
    }


    QVector<int> boundaries;


    for (int index = 0;
         index < nuclei.size() - 1;
         ++index) {

        const int consonantStart =
            nuclei.at(index).end;

        const int consonantEnd =
            nuclei.at(index + 1).start;

        const int consonantCount =
            consonantEnd
            - consonantStart;


        /*
         * Deux noyaux immediatement consecutifs :
         * hiatus.
         *
         * Exemple :
         * maïs -> ma-ïs
         */
        int boundary =
            consonantStart;


        /*
         * Une consonne entre deux noyaux :
         *
         * ma-man
         * li-re
         * vo-ya-ge
         */
        if (consonantCount == 1) {
            boundary =
                consonantStart;
        }


        /*
         * Plusieurs consonnes.
         *
         * On conserve une attaque consonantique valide
         * avec la syllabe suivante.
         *
         * ta-ble
         * a-près
         * ar-bre
         * li-qui-de
         *
         * Une consonne double est naturellement separee :
         *
         * pom-me
         * bel-le
         */
        else if (consonantCount >= 2) {

            int onsetLength = 1;

            const QString lastTwo =
                word.mid(
                    consonantEnd - 2,
                    2);

            if (isCommonFrenchOnset(
                    lastTwo)) {

                onsetLength = 2;
            }

            boundary =
                consonantEnd
                - onsetLength;
        }


        if (boundary <= 0
            || boundary >= word.length()) {

            continue;
        }

        if (boundaries.isEmpty()
            || boundaries.last()
                != boundary) {

            boundaries.append(
                boundary);
        }
    }


    QVector<SyllableRange> result;

    int start = 0;

    for (int boundary : boundaries) {

        if (boundary <= start)
            continue;

        result.append(
            {
                start,
                boundary - start
            });

        start = boundary;
    }


    if (start < word.length()) {

        result.append(
            {
                start,
                static_cast<int>(word.length()) - start
            });
    }


    return result;
}



/*
 * Premiere segmentation phonemique de Mathom.
 *
 * Cette couche reconnait volontairement les correspondances
 * graphemes-sons francaises les plus fiables.
 *
 * Elle reste independante du moteur syllabique.
 */
int phonemeGraphemeLength(
    const QString &word,
    int position)
{
    if (position < 0
        || position >= word.length()) {

        return 0;
    }

    const QString lower =
        word.toLower();

    auto matches =
        [&lower, position](
            const QString &sequence)
    {
        return position + sequence.length()
                <= lower.length()
            && lower.mid(
                   position,
                   sequence.length())
                == sequence;
    };


    /*
     * Voyelles nasales.
     *
     * On ne fusionne pas n/m lorsque la consonne
     * est suivie d'une voyelle ou lorsqu'elle est double.
     */
    static const QStringList nasalGroups = {
        QStringLiteral("ain"),
        QStringLiteral("ein"),
        QStringLiteral("aim"),
        QStringLiteral("eim"),
        QStringLiteral("oin"),

        QStringLiteral("an"),
        QStringLiteral("am"),
        QStringLiteral("en"),
        QStringLiteral("em"),
        QStringLiteral("on"),
        QStringLiteral("om"),
        QStringLiteral("in"),
        QStringLiteral("im"),
        QStringLiteral("un"),
        QStringLiteral("um"),
        QStringLiteral("yn"),
        QStringLiteral("ym")
    };

    for (const QString &group : nasalGroups) {

        if (!matches(group))
            continue;

        const int nextPosition =
            position + group.length();

        if (nextPosition < lower.length()) {

            const QChar next =
                lower.at(nextPosition);

            const QChar final =
                group.at(group.length() - 1);

            if (isFrenchVowelLetter(next)
                || next == final) {

                continue;
            }
        }

        return group.length();
    }


    /*
     * Graphemes vocaliques courants.
     */
    static const QStringList vowelGroups = {
        QStringLiteral("eau"),
        QStringLiteral("oeu"),
        QStringLiteral("œu"),
        QStringLiteral("ou"),
        QStringLiteral("oi"),
        QStringLiteral("au"),
        QStringLiteral("ai"),
        QStringLiteral("ei"),
        QStringLiteral("eu")
    };

    for (const QString &group : vowelGroups) {
        if (matches(group))
            return group.length();
    }


    /*
     * Graphemes consonantiques courants.
     */
    static const QStringList consonantGroups = {
        QStringLiteral("ch"),
        QStringLiteral("ph"),
        QStringLiteral("gn"),
        QStringLiteral("qu"),
        QStringLiteral("th"),
        QStringLiteral("sh"),
        QStringLiteral("ck")
    };

    for (const QString &group : consonantGroups) {
        if (matches(group))
            return group.length();
    }


    /*
     * gu devant e/i/y.
     */
    if (matches(QStringLiteral("gu"))
        && position + 2 < lower.length()) {

        static const QString following =
            QStringLiteral(
                "eéèêë"
                "iîï"
                "yÿ");

        if (following.contains(
                lower.at(position + 2))) {

            return 2;
        }
    }


    /*
     * Cas simple :
     * une lettre constitue une unite visuelle.
     */
    return 1;
}


QVector<SyllableRange> phonemeRanges(
    const QString &word)
{
    QVector<SyllableRange> result;

    int position = 0;

    while (position < word.length()) {

        const int length =
            phonemeGraphemeLength(
                word,
                position);

        if (length <= 0) {
            ++position;
            continue;
        }

        result.append(
            {
                position,
                length
            });

        position += length;
    }

    return result;
}



/*
 * Retourne les graphemes complexes reconnus par
 * le moteur phonemique.
 *
 * Les lettres simples ne sont pas mises en evidence :
 * l'objectif est de faire ressortir les groupes
 * graphiques representant une unite de lecture.
 */
QVector<SyllableRange> graphemeRanges(
    const QString &word)
{
    QVector<SyllableRange> result;

    int position = 0;

    while (position < word.length()) {

        const int length =
            phonemeGraphemeLength(
                word,
                position);

        if (length <= 0) {
            ++position;
            continue;
        }

        if (length > 1) {
            result.append(
                {
                    position,
                    length
                });
        }

        position += length;
    }

    return result;
}


void applyDyslexiaPreset(
    AccessibilityConfiguration &config)
{
    /*
     * Modules deja operationnels dans le profil Dyslexie.
     *
     * Les futurs modules LireCouleur seront ajoutes ici
     * progressivement lorsqu'ils seront implementes.
     */
    enable(config, AccessibilityModule::AdaptedFont);
    enable(config, AccessibilityModule::LargerText);
    enable(config, AccessibilityModule::LetterSpacing);
    enable(config, AccessibilityModule::WordSpacing);
    enable(config, AccessibilityModule::LineSpacing);
}


void applyCustomModules(
    AccessibilityConfiguration &config)
{
    auto sharedConfig =
        KSharedConfig::openConfig();

    KConfigGroup custom(
        sharedConfig,
        QStringLiteral("Accessibility Custom Modules"));

    auto readModule =
        [&custom, &config](
            const QString &key,
            AccessibilityModule module)
    {
        if (custom.readEntry(key, false))
            enable(config, module);
    };

    readModule(
        QStringLiteral("adaptedFont"),
        AccessibilityModule::AdaptedFont);

    readModule(
        QStringLiteral("largerText"),
        AccessibilityModule::LargerText);

    readModule(
        QStringLiteral("letterSpacing"),
        AccessibilityModule::LetterSpacing);

    readModule(
        QStringLiteral("wordSpacing"),
        AccessibilityModule::WordSpacing);

    readModule(
        QStringLiteral("lineSpacing"),
        AccessibilityModule::LineSpacing);

    readModule(
        QStringLiteral("paragraphSpacing"),
        AccessibilityModule::ParagraphSpacing);

    readModule(
        QStringLiteral("syllableColoring"),
        AccessibilityModule::SyllableColoring);

    readModule(
        QStringLiteral("phonemeColoring"),
        AccessibilityModule::PhonemeColoring);

    readModule(
        QStringLiteral("graphemeHighlight"),
        AccessibilityModule::GraphemeHighlight);

    readModule(
        QStringLiteral("confusableLetters"),
        AccessibilityModule::ConfusableLetters);

    readModule(
        QStringLiteral("alternatingLines"),
        AccessibilityModule::AlternatingLines);

    readModule(
        QStringLiteral("readingGuide"),
        AccessibilityModule::ReadingGuide);

    readModule(
        QStringLiteral("activeLineHighlight"),
        AccessibilityModule::ActiveLineHighlight);

    readModule(
        QStringLiteral("dimOtherLines"),
        AccessibilityModule::DimOtherLines);

    readModule(
        QStringLiteral("textToSpeech"),
        AccessibilityModule::TextToSpeech);

    readModule(
        QStringLiteral("speechTracking"),
        AccessibilityModule::SpeechTracking);

    readModule(
        QStringLiteral("reducedDistractions"),
        AccessibilityModule::ReducedDistractions);

    readModule(
        QStringLiteral("largerControls"),
        AccessibilityModule::LargerControls);


    /*
     * Valeurs personnalisables.
     *
     * L'interface Personnalise les exposera ensuite.
     */
    config.fontFamily =
        custom.readEntry(
            QStringLiteral("fontFamily"),
            config.fontFamily);

    config.fontPointSize =
        custom.readEntry(
            QStringLiteral("fontPointSize"),
            config.fontPointSize);

    config.letterSpacingPercent =
        custom.readEntry(
            QStringLiteral("letterSpacingPercent"),
            config.letterSpacingPercent);

    config.wordSpacing =
        custom.readEntry(
            QStringLiteral("wordSpacing"),
            config.wordSpacing);

    config.lineSpacingPercent =
        custom.readEntry(
            QStringLiteral("lineSpacingPercent"),
            config.lineSpacingPercent);

    config.paragraphSpacing =
        custom.readEntry(
            QStringLiteral("paragraphSpacingValue"),
            config.paragraphSpacing);
}


QFont accessibleFont(
    const QFont &base,
    const AccessibilityConfiguration &config)
{
    QFont font(base);

    if (config.has(AccessibilityModule::AdaptedFont))
        font.setFamily(config.fontFamily);

    if (config.has(AccessibilityModule::LargerText))
        font.setPointSizeF(config.fontPointSize);

    if (config.has(AccessibilityModule::LetterSpacing)) {
        font.setLetterSpacing(
            QFont::PercentageSpacing,
            config.letterSpacingPercent);
    }

    if (config.has(AccessibilityModule::WordSpacing))
        font.setWordSpacing(config.wordSpacing);

    return font;
}


class AccessibilityHighlighter : public QSyntaxHighlighter
{
public:
    explicit AccessibilityHighlighter(
        QTextDocument *document)
        : QSyntaxHighlighter(document)
    {
        setObjectName(
            QStringLiteral(
                "mathomAccessibilityHighlighter"));
    }

    void setConfiguration(
        const AccessibilityConfiguration &configuration)
    {
        m_configuration = configuration;
        rehighlight();
    }

protected:
    void highlightBlock(
        const QString &text) override
    {
        if (text.isEmpty())
            return;

        QTextCharFormat baseFormat;
        bool hasBaseFormatting = false;


        /*
         * Modules typographiques.
         */
        if (m_configuration.has(
                AccessibilityModule::AdaptedFont)) {

            baseFormat.setFontFamilies(
                QStringList{
                    m_configuration.fontFamily});

            hasBaseFormatting = true;
        }

        if (m_configuration.has(
                AccessibilityModule::LargerText)) {

            baseFormat.setFontPointSize(
                m_configuration.fontPointSize);

            hasBaseFormatting = true;
        }

        if (m_configuration.has(
                AccessibilityModule::LetterSpacing)) {

            baseFormat.setFontLetterSpacing(
                m_configuration
                    .letterSpacingPercent);

            hasBaseFormatting = true;
        }

        if (m_configuration.has(
                AccessibilityModule::WordSpacing)) {

            baseFormat.setFontWordSpacing(
                m_configuration.wordSpacing);

            hasBaseFormatting = true;
        }


        if (hasBaseFormatting) {
            setFormat(
                0,
                text.length(),
                baseFormat);
        }


        /*
         * Coloration syllabique.
         *
         * Couche purement visuelle :
         * aucune couleur n'est enregistree dans le HTML du Mathom.
         */
        if (m_configuration.has(
                AccessibilityModule::SyllableColoring)
            && !m_configuration.has(
                AccessibilityModule::PhonemeColoring)) {

            static const QRegularExpression wordExpression(
                QStringLiteral("\\p{L}+"),
                QRegularExpression::
                    UseUnicodePropertiesOption);

            auto matches =
                wordExpression.globalMatch(text);

            while (matches.hasNext()) {

                const QRegularExpressionMatch match =
                    matches.next();

                const QString word =
                    match.captured();

                const QVector<SyllableRange> ranges =
                    syllableRanges(word);

                for (int index = 0;
                     index < ranges.size();
                     ++index) {

                    const SyllableRange &range =
                        ranges.at(index);

                    QTextCharFormat format =
                        baseFormat;

                    const QColor color =
                        (index % 2 == 0)
                        ? m_configuration.syllableColor1
                        : m_configuration.syllableColor2;

                    format.setForeground(
                        QBrush(color));

                    setFormat(
                        match.capturedStart()
                            + range.start,
                        range.length,
                        format);
                }
            }
        }


        /*
         * Coloration phonemique.
         *
         * Si coloration syllabique et phonemique sont
         * actives simultanement, la couche phonemique
         * prend la priorite.
         */
        if (m_configuration.has(
                AccessibilityModule::PhonemeColoring)) {

            static const QRegularExpression wordExpression(
                QStringLiteral("\\p{L}+"),
                QRegularExpression::
                    UseUnicodePropertiesOption);

            auto matches =
                wordExpression.globalMatch(text);

            while (matches.hasNext()) {

                const QRegularExpressionMatch match =
                    matches.next();

                const QString word =
                    match.captured();

                const QVector<SyllableRange> ranges =
                    phonemeRanges(word);

                for (int index = 0;
                     index < ranges.size();
                     ++index) {

                    const SyllableRange &range =
                        ranges.at(index);

                    QTextCharFormat format =
                        baseFormat;

                    const QColor color =
                        (index % 2 == 0)
                        ? m_configuration.syllableColor1
                        : m_configuration.syllableColor2;

                    format.setForeground(
                        QBrush(color));

                    setFormat(
                        match.capturedStart()
                            + range.start,
                        range.length,
                        format);
                }
            }
        }


        /*
         * Mise en evidence des graphemes.
         *
         * Le soulignement est ajoute au format deja calcule
         * afin de conserver une eventuelle coloration
         * syllabique ou phonemique.
         */
        if (m_configuration.has(
                AccessibilityModule::GraphemeHighlight)) {

            static const QRegularExpression wordExpression(
                QStringLiteral("\\p{L}+"),
                QRegularExpression::
                    UseUnicodePropertiesOption);

            auto matches =
                wordExpression.globalMatch(text);

            while (matches.hasNext()) {

                const QRegularExpressionMatch match =
                    matches.next();

                const QString word =
                    match.captured();

                const QVector<SyllableRange> ranges =
                    graphemeRanges(word);

                for (const SyllableRange &range : ranges) {

                    const int start =
                        match.capturedStart()
                        + range.start;

                    QTextCharFormat graphemeFormat =
                        format(start);

                    graphemeFormat.setFontUnderline(true);
                    graphemeFormat.setFontWeight(
                        QFont::DemiBold);

                    setFormat(
                        start,
                        range.length,
                        graphemeFormat);
                }
            }
        }
    }

private:
    AccessibilityConfiguration m_configuration;
};


AccessibilityHighlighter *accessibilityHighlighter(
    QTextDocument *document)
{
    if (!document)
        return nullptr;

    QObject *object =
        document->findChild<QObject *>(
            QStringLiteral(
                "mathomAccessibilityHighlighter"),
            Qt::FindDirectChildrenOnly);

    auto *highlighter =
        dynamic_cast<AccessibilityHighlighter *>(
            object);

    if (!highlighter) {
        highlighter =
            new AccessibilityHighlighter(
                document);
    }

    return highlighter;
}

} // namespace


AccessibilityConfiguration
AccessibilitySettings::effectiveConfiguration()
{
    AccessibilityConfiguration configuration;

    auto config =
        KSharedConfig::openConfig();

    KConfigGroup profiles(
        config,
        QStringLiteral("Accessibility Profiles"));

    /*
     * Les profils sont des PRESETS.
     *
     * Ils ne modifient jamais directement l'affichage.
     */
    if (profiles.readEntry(
            QStringLiteral("dyslexia"),
            false)) {

        applyDyslexiaPreset(configuration);
    }

    /*
     * Les autres profils seront raccordes a leurs presets
     * au fur et a mesure de leur implementation.
     *
     * Les cases peuvent deja coexister sans conflit.
     */

    if (profiles.readEntry(
            QStringLiteral("custom"),
            false)) {

        applyCustomModules(configuration);
    }

    return configuration;
}


void AccessibilitySettings::applyToTextEditor(
    QTextEdit *editor)
{
    if (!editor)
        return;

    if (!editor->property(
            "mathomAccessibilityEditor").toBool())
        return;

    const AccessibilityConfiguration configuration =
        effectiveConfiguration();

    /*
     * Editeur HTML :
     * couche de presentation uniquement.
     */
    if (editor->property(
            "mathomAccessibilityRichText").toBool()) {

        AccessibilityHighlighter *highlighter =
            accessibilityHighlighter(
                editor->document());

        if (highlighter) {
            highlighter->setConfiguration(
                configuration);
        }

        editor->viewport()->update();
        return;
    }

    /*
     * Editeur texte brut.
     */
    if (!editor->property(
            "mathomOriginalFont").isValid()) {

        editor->setProperty(
            "mathomOriginalFont",
            QVariant::fromValue(
                editor->font()));
    }

    const QFont originalFont =
        editor->property(
            "mathomOriginalFont").value<QFont>();

    const QSignalBlocker editorBlocker(editor);
    const QSignalBlocker documentBlocker(
        editor->document());

    editor->setFont(
        accessibleFont(
            originalFont,
            configuration));

    editor->document()->setDefaultFont(
        accessibleFont(
            originalFont,
            configuration));

    for (QTextBlock block =
             editor->document()->begin();
         block.isValid();
         block = block.next()) {

        QTextCursor cursor(block);

        QTextBlockFormat format =
            cursor.blockFormat();

        if (configuration.has(
                AccessibilityModule::LineSpacing)) {

            format.setLineHeight(
                configuration.lineSpacingPercent,
                QTextBlockFormat::ProportionalHeight);

        } else {

            format.setLineHeight(
                100.0,
                QTextBlockFormat::SingleHeight);
        }

        if (configuration.has(
                AccessibilityModule::ParagraphSpacing)) {

            format.setBottomMargin(
                configuration.paragraphSpacing);
        }

        cursor.setBlockFormat(format);
    }

    /*
     * Les modules visuels tels que la coloration syllabique
     * utilisent egalement le highlighter dans l'editeur texte brut.
     */
    AccessibilityHighlighter *highlighter =
        accessibilityHighlighter(
            editor->document());

    if (highlighter) {
        highlighter->setConfiguration(
            configuration);
    }

    editor->viewport()->update();
}


void AccessibilitySettings::applyToGraphicsItem(
    QGraphicsItem *item,
    bool requestRelayout)
{
    if (!item)
        return;

    auto *note =
        dynamic_cast<Note *>(
            item->parentItem());

    if (!note || !note->content())
        return;

    if (note->content()->graphicsItem() != item)
        return;

    const AccessibilityConfiguration configuration =
        effectiveConfiguration();

    /*
     * Mathom HTML hors edition.
     */
    if (auto *rich =
            dynamic_cast<QGraphicsTextItem *>(
                item)) {

        AccessibilityHighlighter *highlighter =
            accessibilityHighlighter(
                rich->document());

        if (highlighter) {
            highlighter->setConfiguration(
                configuration);
        }

        rich->update();

        if (requestRelayout)
            note->requestRelayout();

        return;
    }

    /*
     * Ancien Mathom texte brut.
     */
    if (auto *plain =
            dynamic_cast<QGraphicsSimpleTextItem *>(
                item)) {

        plain->setFont(
            accessibleFont(
                note->font(),
                configuration));

        plain->update();

        if (requestRelayout)
            note->requestRelayout();
    }
}


void AccessibilitySettings::refreshAllDisplays()
{
    const auto widgets =
        QApplication::allWidgets();

    /*
     * Editeurs actifs.
     */
    for (QWidget *widget : widgets) {

        auto *editor =
            qobject_cast<QTextEdit *>(
                widget);

        if (!editor)
            continue;

        if (!editor->property(
                "mathomAccessibilityEditor").toBool())
            continue;

        applyToTextEditor(editor);
    }

    /*
     * Mathoms affiches dans toutes les scenes ouvertes.
     */
    QSet<QGraphicsScene *> processedScenes;

    for (QWidget *widget : widgets) {

        auto *view =
            qobject_cast<QGraphicsView *>(
                widget);

        if (!view || !view->scene())
            continue;

        QGraphicsScene *scene =
            view->scene();

        if (processedScenes.contains(scene))
            continue;

        processedScenes.insert(scene);

        const auto items =
            scene->items();

        for (QGraphicsItem *item : items)
            applyToGraphicsItem(item);
    }
}
