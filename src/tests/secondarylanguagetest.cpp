#include <QObject>
#include <QtTest/QtTest>

#include <secondarylanguagecatalog.h>
#include <secondarylanguageprofile.h>
#include <secondarylanguagesettings.h>
#include <global.h>

#include <KConfig>
#include <KSharedConfig>

#include <QTemporaryDir>

class SecondaryLanguageTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void loadEsperantoProfile();
    void loadCatalog();
    void findProfileById();
    void settingsRoundTrip();
};

void SecondaryLanguageTest::loadEsperantoProfile()
{
    QString error;

    const SecondaryLanguageProfile profile =
        SecondaryLanguageProfile::loadFromResource(
            QStringLiteral(
                ":/mathom/secondary-languages/eo.json"),
            &error);

    QVERIFY2(
        profile.isValid(),
        qPrintable(error));

    QCOMPARE(
        profile.id,
        QStringLiteral("eo"));

    QCOMPARE(
        profile.name,
        QStringLiteral("Espéranto"));

    QCOMPARE(
        profile.defaultTrigger,
        QStringLiteral("xx"));

    bool foundU = false;

    for (const SecondaryLanguageRule &rule :
         profile.rules) {
        if (rule.source == QStringLiteral("u")) {
            QCOMPARE(
                rule.variants,
                QStringList{
                    QStringLiteral("ŭ")
                });

            foundU = true;
            break;
        }
    }

    QVERIFY(foundU);
}

void SecondaryLanguageTest::loadCatalog()
{
    QStringList errors;

    const QVector<SecondaryLanguageProfile> profiles =
        SecondaryLanguageCatalog::profiles(
            &errors);

    QVERIFY2(
        errors.isEmpty(),
        qPrintable(
            errors.join(
                QLatin1Char('\n'))));

    QCOMPARE(
        profiles.size(),
        46);

    QStringList ids;

    for (const SecondaryLanguageProfile &profile :
         profiles) {
        ids.append(profile.id);
    }

    const QStringList requiredIds = {
        QStringLiteral("de"),
        QStringLiteral("es"),
        QStringLiteral("eo"),
        QStringLiteral("fr"),
        QStringLiteral("pl"),
        QStringLiteral("tr"),
        QStringLiteral("az"),
        QStringLiteral("mi"),
        QStringLiteral("haw"),
        QStringLiteral("gn"),
        QStringLiteral("ha"),
        QStringLiteral("ig"),
        QStringLiteral("se"),
        QStringLiteral("vi"),
        QStringLiteral("yo")
    };

    for (const QString &id : requiredIds) {
        QVERIFY2(
            ids.contains(id),
            qPrintable(
                QStringLiteral(
                    "Profil manquant : %1")
                    .arg(id)));
    }
}

void SecondaryLanguageTest::findProfileById()
{
    QString error;

    const SecondaryLanguageProfile spanish =
        SecondaryLanguageCatalog::profileById(
            QStringLiteral("es"),
            &error);

    QVERIFY2(
        spanish.isValid(),
        qPrintable(error));

    QCOMPARE(
        spanish.name,
        QStringLiteral("Espagnol"));
}


void SecondaryLanguageTest::settingsRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    Global::basketConfig =
        KSharedConfig::openConfig(
            directory.filePath(
                QStringLiteral("mathomtestrc")),
            KConfig::SimpleConfig);

    QVector<SecondaryLanguageSelection> expected;

    expected.append({
        QStringLiteral("es"),
        QStringLiteral("x")
    });

    expected.append({
        QStringLiteral("eo"),
        QStringLiteral("$")
    });

    expected.append({
        QStringLiteral("de"),
        QStringLiteral("xx")
    });

    SecondaryLanguageSettings::save(expected);

    const QVector<SecondaryLanguageSelection> loaded =
        SecondaryLanguageSettings::load();

    QCOMPARE(loaded.size(), 3);

    QCOMPARE(
        loaded.at(0).id,
        QStringLiteral("es"));
    QCOMPARE(
        loaded.at(0).trigger,
        QStringLiteral("x"));

    QCOMPARE(
        loaded.at(1).id,
        QStringLiteral("eo"));
    QCOMPARE(
        loaded.at(1).trigger,
        QStringLiteral("$"));

    QCOMPARE(
        loaded.at(2).id,
        QStringLiteral("de"));
    QCOMPARE(
        loaded.at(2).trigger,
        QStringLiteral("xx"));
}

QTEST_APPLESS_MAIN(SecondaryLanguageTest)

#include "secondarylanguagetest.moc"
