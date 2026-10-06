#include <QObject>
#include <QtTest/QtTest>

#include <secondarylanguageengine.h>
#include <secondarylanguagesettings.h>

class SecondaryLanguageEngineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void esperantoCustomTrigger();
    void spanishCycle();
    void germanDefaultTrigger();
    void ambiguousTransformation();
    void noTransformation();
    void azeriCaseHandling();
    void guaraniCombiningCharacter();
    void yorubaCombinedTone();
    void vietnameseStructuralThenTone();
    void dutchIjAccent();
    void africanLatinCharacters();
    void kurdishCharacters();
    void upperSorbianDigraph();
    void skoltSamiCharacters();
};

void SecondaryLanguageEngineTest::esperantoCustomTrigger()
{
    QVector<SecondaryLanguageSelection> languages;

    languages.append({
        QStringLiteral("eo"),
        QStringLiteral("$")
    });

    const QString input =
        QStringLiteral("au$");

    const SecondaryLanguageTransformation result =
        SecondaryLanguageEngine::transform(
            input,
            languages);

    QVERIFY(result.matched);
    QVERIFY(!result.ambiguous);

    QCOMPARE(
        result.replaceLength,
        2);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ŭ"));

    QString transformed = input;

    transformed.chop(
        result.replaceLength);

    transformed.append(
        result.replacement);

    QCOMPARE(
        transformed,
        QStringLiteral("aŭ"));
}

void SecondaryLanguageEngineTest::spanishCycle()
{
    QVector<SecondaryLanguageSelection> languages;

    languages.append({
        QStringLiteral("es"),
        QStringLiteral("x")
    });

    auto result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("ux"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ú"));

    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("úx"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ü"));

    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("üx"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("u"));
}

void SecondaryLanguageEngineTest::germanDefaultTrigger()
{
    QVector<SecondaryLanguageSelection> languages;

    languages.append({
        QStringLiteral("de"),
        QStringLiteral("xx")
    });

    const SecondaryLanguageTransformation result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("axx"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replaceLength,
        3);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ä"));
}

void SecondaryLanguageEngineTest::ambiguousTransformation()
{
    QVector<SecondaryLanguageSelection> languages;

    languages.append({
        QStringLiteral("de"),
        QStringLiteral("xx")
    });

    languages.append({
        QStringLiteral("eo"),
        QStringLiteral("xx")
    });

    const SecondaryLanguageTransformation result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("uxx"),
            languages);

    QVERIFY(!result.matched);
    QVERIFY(result.ambiguous);
}

void SecondaryLanguageEngineTest::noTransformation()
{
    QVector<SecondaryLanguageSelection> languages;

    languages.append({
        QStringLiteral("eo"),
        QStringLiteral("$")
    });

    const SecondaryLanguageTransformation result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("bonjour$"),
            languages);

    QVERIFY(!result.matched);
    QVERIFY(!result.ambiguous);
}


void SecondaryLanguageEngineTest::azeriCaseHandling()
{
    const QVector<SecondaryLanguageSelection> languages = {
        {
            QStringLiteral("az"),
            QStringLiteral("$")
        }
    };

    auto result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("i$"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ı"));

    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("I$"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("İ"));

    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("e$"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ə"));

    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("E$"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("Ə"));
}

void SecondaryLanguageEngineTest::guaraniCombiningCharacter()
{
    const QVector<SecondaryLanguageSelection> languages = {
        {
            QStringLiteral("gn"),
            QStringLiteral("$")
        }
    };

    const SecondaryLanguageTransformation result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("g$"),
            languages);

    QVERIFY(result.matched);

    /*
     * Le résultat visible est un seul graphème, mais il peut être
     * constitué de plusieurs points de code Unicode.
     */
    QCOMPARE(
        result.replacement,
        QStringLiteral("g̃"));
}


void SecondaryLanguageEngineTest::yorubaCombinedTone()
{
    const QVector<SecondaryLanguageSelection> languages = {
        {
            QStringLiteral("yo"),
            QStringLiteral("$")
        }
    };

    auto result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("ẹ$"),
            languages);

    QVERIFY(result.matched);
    QVERIFY(!result.ambiguous);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ẹ́"));

    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("ẹ́$"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ẹ̀"));
}

void SecondaryLanguageEngineTest::vietnameseStructuralThenTone()
{
    const QVector<SecondaryLanguageSelection> languages = {
        {
            QStringLiteral("vi"),
            QStringLiteral("xx")
        }
    };

    auto result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("awxx"),
            languages);

    QVERIFY(result.matched);
    QVERIFY(!result.ambiguous);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ă"));

    /*
     * La règle aw -> ă est non cyclique.
     * ăxx doit donc entrer dans le cycle tonal et non revenir à "aw".
     */
    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("ăxx"),
            languages);

    QVERIFY(result.matched);
    QVERIFY(!result.ambiguous);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ằ"));

    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("ằxx"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ẳ"));

    /*
     * Vérifier aussi la règle de séquence la plus longue :
     * aaxx doit produire â et non appliquer le cycle de "a".
     */
    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("aaxx"),
            languages);

    QVERIFY(result.matched);
    QVERIFY(!result.ambiguous);

    QCOMPARE(
        result.replacement,
        QStringLiteral("â"));
}


void SecondaryLanguageEngineTest::dutchIjAccent()
{
    const QVector<SecondaryLanguageSelection> languages = {
        {
            QStringLiteral("nl"),
            QStringLiteral("$")
        }
    };

    const SecondaryLanguageTransformation result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("ij$"),
            languages);

    QVERIFY(result.matched);
    QVERIFY(!result.ambiguous);

    QCOMPARE(
        result.replacement,
        QStringLiteral("íj́"));
}


void SecondaryLanguageEngineTest::africanLatinCharacters()
{
    {
        const QVector<SecondaryLanguageSelection> languages = {
            {
                QStringLiteral("ak"),
                QStringLiteral("$")
            }
        };

        auto result =
            SecondaryLanguageEngine::transform(
                QStringLiteral("e$"),
                languages);

        QVERIFY(result.matched);
        QCOMPARE(
            result.replacement,
            QStringLiteral("ɛ"));

        result =
            SecondaryLanguageEngine::transform(
                QStringLiteral("o$"),
                languages);

        QVERIFY(result.matched);
        QCOMPARE(
            result.replacement,
            QStringLiteral("ɔ"));
    }

    {
        const QVector<SecondaryLanguageSelection> languages = {
            {
                QStringLiteral("ee"),
                QStringLiteral("$")
            }
        };

        auto result =
            SecondaryLanguageEngine::transform(
                QStringLiteral("d$"),
                languages);

        QVERIFY(result.matched);
        QCOMPARE(
            result.replacement,
            QStringLiteral("ɖ"));

        result =
            SecondaryLanguageEngine::transform(
                QStringLiteral("f$"),
                languages);

        QVERIFY(result.matched);
        QCOMPARE(
            result.replacement,
            QStringLiteral("ƒ"));

        result =
            SecondaryLanguageEngine::transform(
                QStringLiteral("n$"),
                languages);

        QVERIFY(result.matched);
        QCOMPARE(
            result.replacement,
            QStringLiteral("ŋ"));
    }
}

void SecondaryLanguageEngineTest::kurdishCharacters()
{
    const QVector<SecondaryLanguageSelection> languages = {
        {
            QStringLiteral("ku"),
            QStringLiteral("$")
        }
    };

    auto result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("c$"),
            languages);

    QVERIFY(result.matched);
    QCOMPARE(
        result.replacement,
        QStringLiteral("ç"));

    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("s$"),
            languages);

    QVERIFY(result.matched);
    QCOMPARE(
        result.replacement,
        QStringLiteral("ş"));

    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("u$"),
            languages);

    QVERIFY(result.matched);
    QCOMPARE(
        result.replacement,
        QStringLiteral("û"));
}


void SecondaryLanguageEngineTest::upperSorbianDigraph()
{
    const QVector<SecondaryLanguageSelection> languages = {
        {
            QStringLiteral("hsb"),
            QStringLiteral("$")
        }
    };

    auto result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("dz$"),
            languages);

    QVERIFY(result.matched);
    QVERIFY(!result.ambiguous);

    QCOMPARE(
        result.replacement,
        QStringLiteral("dź"));

    /*
     * Vérification de la casse de début de mot.
     */
    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("Dz$"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("Dź"));
}

void SecondaryLanguageEngineTest::skoltSamiCharacters()
{
    const QVector<SecondaryLanguageSelection> languages = {
        {
            QStringLiteral("sms"),
            QStringLiteral("$")
        }
    };

    auto result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("g$"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ǧ"));

    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("ǧ$"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ǥ"));

    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("j$"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ʒ"));

    result =
        SecondaryLanguageEngine::transform(
            QStringLiteral("ʒ$"),
            languages);

    QVERIFY(result.matched);

    QCOMPARE(
        result.replacement,
        QStringLiteral("ǯ"));
}

QTEST_APPLESS_MAIN(SecondaryLanguageEngineTest)

#include "secondarylanguageenginetest.moc"
