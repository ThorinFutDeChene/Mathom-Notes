/**
 * SPDX-FileCopyrightText: (C) 2003 Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "newbasketdialog.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <KComboBox>
#include <KIconButton>
#include <KIconLoader>
#include <KLocalizedString>
#include <KMainWindow>

#include <algorithm>

#include "basketfactory.h"
#include "basketlistview.h"
#include "basketscene.h"
#include "bnpview.h"
#include "global.h"
#include "history.h"
#include "mathomicons.h"
#include "tools.h"

NewBasketDialog::NewBasketDialog(
    BasketScene *parentBasket,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("New Mathom-House"));
    setObjectName(QStringLiteral("NewBasket"));
    setModal(true);

    auto *mainLayout = new QVBoxLayout(this);

    auto *identityLayout = new QHBoxLayout();

    m_icon = new KIconButton(this);
    m_icon->setIconType(
        KIconLoader::NoGroup,
        KIconLoader::Action);
    m_icon->setIconSize(16);
    m_icon->setIcon(QStringLiteral("mathom-house"));

    const int iconSize =
        std::max(
            m_icon->sizeHint().width(),
            m_icon->sizeHint().height());

    m_icon->setFixedSize(iconSize, iconSize);
    m_icon->setToolTip(i18n("Icon"));

    m_name = new QLineEdit(this);
    m_name->setMinimumWidth(
        m_name->fontMetrics().maxWidth() * 20);
    m_name->setToolTip(i18n("Name"));

    identityLayout->addWidget(m_icon);
    identityLayout->addWidget(m_name, 1);

    mainLayout->addLayout(identityLayout);

    auto *locationLayout = new QHBoxLayout();

    auto *locationLabel =
        new QLabel(i18n("C&reate in:"), this);

    m_createIn = new KComboBox(this);
    m_createIn->addItem(i18n("(Mathom-Houses)"));

    locationLabel->setBuddy(m_createIn);

    locationLayout->addWidget(locationLabel);
    locationLayout->addWidget(m_createIn, 1);

    mainLayout->addLayout(locationLayout);

    m_basketsMap.clear();
    m_basketsMap.insert(0, nullptr);

    int index = 1;

    for (int i = 0;
         i < Global::bnpView->topLevelItemCount();
         ++i) {
        index =
            populateBasketsList(
                Global::bnpView->topLevelItem(i),
                1,
                index);
    }

    connect(
        m_createIn,
        qOverload<int>(
            &QComboBox::currentIndexChanged),
        this,
        [this](int index) {
            setWindowTitle(
                index == 0
                    ? i18n("New Mathom-House")
                    : i18n("New Shelf"));

            const QString currentIcon =
                m_icon->icon();

            if (currentIcon == QStringLiteral("mathom-house")
                || currentIcon == QStringLiteral("mathom-shelf")) {
                m_icon->setIcon(
                    index == 0
                        ? QStringLiteral("mathom-house")
                        : QStringLiteral("mathom-shelf"));
            }
        });

    auto *buttonBox =
        new QDialogButtonBox(
            QDialogButtonBox::Ok
                | QDialogButtonBox::Cancel,
            this);

    okButton =
        buttonBox->button(QDialogButtonBox::Ok);

    okButton->setDefault(true);
    okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
    okButton->setEnabled(false);

    connect(
        m_name,
        &QLineEdit::textChanged,
        this,
        &NewBasketDialog::nameChanged);

    connect(
        okButton,
        &QPushButton::clicked,
        this,
        &NewBasketDialog::slotOk);

    connect(
        buttonBox,
        &QDialogButtonBox::accepted,
        this,
        &QDialog::accept);

    connect(
        buttonBox,
        &QDialogButtonBox::rejected,
        this,
        &QDialog::reject);

    mainLayout->addWidget(buttonBox);

    if (parentBasket) {
        int parentIndex = 0;

        for (auto it = m_basketsMap.constBegin();
             it != m_basketsMap.constEnd();
             ++it) {
            if (it.value() == parentBasket) {
                parentIndex = it.key();
                break;
            }
        }

        if (parentIndex > 0)
            m_createIn->setCurrentIndex(parentIndex);
    }

    m_name->setFocus();
}

NewBasketDialog::~NewBasketDialog() = default;

bool NewBasketDialog::event(QEvent *event)
{
    const bool result = QDialog::event(event);

    if (event->type() == QEvent::Polish)
        m_name->setFocus();

    return result;
}

void NewBasketDialog::returnPressed()
{
    okButton->animateClick();
}

void NewBasketDialog::nameChanged(
    const QString &newName)
{
    okButton->setEnabled(
        !newName.trimmed().isEmpty());
}

int NewBasketDialog::populateBasketsList(
    QTreeWidgetItem *item,
    int indent,
    int index)
{
    static const int ICON_SIZE = 16;

    BasketScene *basket =
        static_cast<BasketListViewItem *>(item)
            ->basket();

    QPixmap icon =
        MathomIcons::hierarchy(
            basket->icon(),
            item->parent() == nullptr)
            .pixmap(ICON_SIZE, ICON_SIZE);

    icon =
        Tools::indentPixmap(
            icon,
            indent,
            2 * ICON_SIZE / 3);

    m_createIn->addItem(
        icon,
        basket->basketName());

    m_basketsMap.insert(index, basket);
    ++index;

    for (int i = 0;
         i < item->childCount();
         ++i) {
        index =
            populateBasketsList(
                item->child(i),
                indent + 1,
                index);
    }

    return index;
}

void NewBasketDialog::slotOk()
{
    Global::bnpView->closeAllEditors();

    QString selectedIcon = m_icon->icon();

    if (selectedIcon.isEmpty()
        || selectedIcon
            == QStringLiteral("fr.thorinux.mathom")) {
        selectedIcon =
            m_createIn->currentIndex() == 0
                ? QStringLiteral("mathom-house")
                : QStringLiteral("mathom-shelf");
    }

    BasketScene *createdBasket =
        BasketFactory::newBasket(
            selectedIcon,
            m_name->text().trimmed(),
            m_basketsMap.value(
                m_createIn->currentIndex()));

    if (createdBasket
        && Global::bnpView
        && Global::bnpView->globalUndoStack()) {

        Global::bnpView
            ->globalUndoStack()
            ->push(
                new BasketCreateCommand(
                    Global::bnpView,
                    createdBasket));
    }

    if (Global::activeMainWindow())
        Global::activeMainWindow()->show();
}

#include "moc_newbasketdialog.cpp"
