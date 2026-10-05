#ifndef SECONDARYLANGUAGESPAGE_H
#define SECONDARYLANGUAGESPAGE_H

#include "settings.h"
#include "secondarylanguagesettings.h"

#include <QString>
#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QWidget;

class BASKET_EXPORT SecondaryLanguagesPage : public AbstractSettingsPage
{
    Q_OBJECT

public:
    explicit SecondaryLanguagesPage(
        QObject *parent,
        const KPluginMetaData &data = KPluginMetaData());

    void load() override;
    void save() override;
    void defaults() override;
    void cancel() override;

private:
    struct LanguageRow
    {
        QString id;
        QWidget *widget = nullptr;
        QLineEdit *trigger = nullptr;
    };

    void chooseLanguage();
    void addLanguage(
        const QString &id,
        const QString &trigger);
    void removeLanguage(
        const QString &id);
    void clearRows();
    void updateAddButton();
    void updateConflictWarning();

    QVector<SecondaryLanguageSelection>
    currentSelections() const;

    QVector<LanguageRow> m_rows;

    QVBoxLayout *m_rowsLayout = nullptr;
    QPushButton *m_addButton = nullptr;
    QLabel *m_conflictLabel = nullptr;
};

#endif
