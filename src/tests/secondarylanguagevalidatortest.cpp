#include <QObject>
#include <QtTest/QtTest>

#include <secondarylanguagevalidator.h>

class SecondaryLanguageValidatorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void distinctTriggers();
    void sameTriggerConflict();
    void prefixTriggerConflict();
};

void SecondaryLanguageValidatorTest::distinctTriggers()
{
    const QVector<SecondaryLanguageSelection> languages = {
        {
            QStringLiteral("es"),
            QStringLiteral("x")
        },
        {
            QStringLiteral("eo"),
            QStringLiteral("$")
        },
        {
            QStringLiteral("de"),
            QStringLiteral("xx")
        }
    };

    /*
     * Espagnol x et Allemand xx se chevauchent.
     * Retirons donc l'allemand pour vérifier un cas
     * réellement sans conflit.
     */
    const QVector<SecondaryLanguageSelection> safeLanguages = {
        languages.at(0),
        languages.at(1)
    };

    QVERIFY(
        SecondaryLanguageValidator::isValid(
            safeLanguages));

    QVERIFY(
        SecondaryLanguageValidator::conflicts(
            safeLanguages)
            .isEmpty());
}

void SecondaryLanguageValidatorTest::sameTriggerConflict()
{
    const QVector<SecondaryLanguageSelection> languages = {
        {
            QStringLiteral("de"),
            QStringLiteral("xx")
        },
        {
            QStringLiteral("eo"),
            QStringLiteral("xx")
        }
    };

    const QVector<SecondaryLanguageConflict> conflicts =
        SecondaryLanguageValidator::conflicts(
            languages);

    QVERIFY(!conflicts.isEmpty());

    bool foundU = false;

    for (const SecondaryLanguageConflict &conflict :
         conflicts) {

        if (conflict.type
                == SecondaryLanguageConflictType::SameTrigger
            && conflict.sequence
                == QStringLiteral("uxx")) {

            foundU = true;
            break;
        }
    }

    QVERIFY(foundU);
}

void SecondaryLanguageValidatorTest::prefixTriggerConflict()
{
    const QVector<SecondaryLanguageSelection> languages = {
        {
            QStringLiteral("es"),
            QStringLiteral("x")
        },
        {
            QStringLiteral("de"),
            QStringLiteral("xx")
        }
    };

    const QVector<SecondaryLanguageConflict> conflicts =
        SecondaryLanguageValidator::conflicts(
            languages);

    QVERIFY(!conflicts.isEmpty());

    bool foundA = false;

    for (const SecondaryLanguageConflict &conflict :
         conflicts) {

        if (conflict.type
                == SecondaryLanguageConflictType::PrefixTrigger
            && conflict.sequence
                == QStringLiteral("axx")) {

            foundA = true;
            break;
        }
    }

    QVERIFY(foundA);
}

QTEST_APPLESS_MAIN(SecondaryLanguageValidatorTest)

#include "secondarylanguagevalidatortest.moc"
