/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pagepropertiesdialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

#include <KLocalizedString>

#include "backgroundmanager.h"
#include "basketscene.h"
#include "gitwrapper.h"
#include "global.h"
#include "kcolorcombo2.h"

PagePropertiesDialog::PagePropertiesDialog(
    BasketScene *basket,
    QWidget *parent)
    : QDialog(parent)
    , m_basket(basket)
{
    setWindowTitle(i18n("Page Properties"));
    setModal(true);
    setMinimumWidth(500);

    auto *mainLayout = new QVBoxLayout(this);

    auto *appearanceGroup =
        new QGroupBox(
            i18n("Appearance"),
            this);

    auto *appearanceLayout =
        new QFormLayout(appearanceGroup);

    m_backgroundImage =
        new QComboBox(appearanceGroup);

    m_backgroundImage->setIconSize(
        QSize(100, 75));

    m_backgroundImagesMap.insert(
        0,
        QString());

    m_backgroundImage->addItem(
        i18n("(None)"));

    const QString currentBackground =
        m_basket->currentPageBackgroundImageName();

    const QStringList backgrounds =
        Global::backgroundManager->imageNames();

    int index = 1;

    for (const QString &background : backgrounds) {
        QPixmap *preview =
            Global::backgroundManager->preview(
                background);

        if (!preview)
            continue;

        m_backgroundImagesMap.insert(
            index,
            background);

        m_backgroundImage->insertItem(
            index,
            background);

        m_backgroundImage->setItemData(
            index,
            *preview,
            Qt::DecorationRole);

        if (background == currentBackground)
            m_backgroundImage->setCurrentIndex(
                index);

        ++index;
    }

    const int buttonMargin =
        qApp->style()->pixelMetric(
            QStyle::PM_ButtonMargin);

    m_backgroundImage->setMinimumHeight(
        75 + 2 * buttonMargin);

    m_backgroundImage->setMaxVisibleItems(50);

    m_backgroundColor =
        new KColorCombo2(
            appearanceGroup);

    m_backgroundColor->setDefaultColor(
        palette().color(QPalette::Base));

    m_backgroundColor->setColor(
        m_basket
            ->currentPageBackgroundColorSetting());

    m_textColor =
        new KColorCombo2(
            appearanceGroup);

    m_textColor->setDefaultColor(
        palette().color(QPalette::Text));

    m_textColor->setColor(
        m_basket
            ->currentPageTextColorSetting());

    appearanceLayout->addRow(
        i18n("Background image:"),
        m_backgroundImage);

    appearanceLayout->addRow(
        i18n("Background color:"),
        m_backgroundColor);

    appearanceLayout->addRow(
        i18n("Text color:"),
        m_textColor);

    mainLayout->addWidget(
        appearanceGroup);

    auto *dispositionGroup =
        new QGroupBox(
            i18n("Disposition"),
            this);

    auto *dispositionLayout =
        new QGridLayout(
            dispositionGroup);

    m_columnForm =
        new QRadioButton(
            i18n("Columns:"),
            dispositionGroup);

    m_columnCount =
        new QSpinBox(
            dispositionGroup);

    m_columnCount->setRange(
        1,
        20);

    m_columnCount->setValue(
        m_basket
            ->currentPageColumnCountSetting());

    m_freeForm =
        new QRadioButton(
            i18n("Free-form"),
            dispositionGroup);

    if (m_basket
            ->currentPageFreeLayoutSetting()) {
        m_freeForm->setChecked(true);
    } else {
        m_columnForm->setChecked(true);
    }

    dispositionLayout->addWidget(
        m_columnForm,
        0,
        0);

    dispositionLayout->addWidget(
        m_columnCount,
        0,
        1);

    dispositionLayout->addWidget(
        m_freeForm,
        1,
        0,
        1,
        2);

    connect(
        m_columnCount,
        &QSpinBox::valueChanged,
        this,
        [this](int) {
            m_columnForm->setChecked(true);
        });

    mainLayout->addWidget(
        dispositionGroup);

    auto *buttonBox =
        new QDialogButtonBox(
            QDialogButtonBox::Ok
                | QDialogButtonBox::Apply
                | QDialogButtonBox::Cancel,
            this);

    mainLayout->addWidget(buttonBox);

    connect(
        buttonBox,
        &QDialogButtonBox::accepted,
        this,
        [this]() {
            applyChanges();
            accept();
        });

    connect(
        buttonBox,
        &QDialogButtonBox::rejected,
        this,
        &QDialog::reject);

    connect(
        buttonBox->button(
            QDialogButtonBox::Apply),
        &QPushButton::clicked,
        this,
        [this]() {
            applyChanges();
        });
}

void PagePropertiesDialog::applyChanges()
{
    m_basket->setCurrentPageAppearance(
        m_backgroundImagesMap.value(
            m_backgroundImage->currentIndex()),
        m_backgroundColor->color(),
        m_textColor->color());

    m_basket->setCurrentPageDisposition(
        m_freeForm->isChecked(),
        m_columnCount->value());

    GitWrapper::commitBasket(m_basket);
}
