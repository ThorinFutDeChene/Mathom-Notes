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

QTEST_APPLESS_MAIN(SecondaryLanguageEngineTest)

#include "secondarylanguageenginetest.moc"
