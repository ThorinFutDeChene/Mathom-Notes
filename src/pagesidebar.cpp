/**
 * SPDX-FileCopyrightText: (C) 2026 Thorinux
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pagesidebar.h"

#include <algorithm>
#include <QAbstractItemModel>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <KLocalizedString>

#include "basketscene.h"
#include "bnpview.h"
#include "global.h"
#include "history.h"

namespace
{
constexpr int PageIdRole = Qt::UserRole + 1;
}

PageSidebar::PageSidebar(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(170);
    setMaximumWidth(360);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto *title = new QLabel(i18n("Pages"), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(4);

    auto *newPage = new QPushButton(i18n("New Page"), this);
    buttons->addWidget(newPage, 1);

    m_sortButton = new QToolButton(this);
    m_sortButton->setText(i18n("Sort"));
    m_sortButton->setPopupMode(QToolButton::InstantPopup);

    auto *sortMenu = new QMenu(m_sortButton);
    auto *sortGroup = new QActionGroup(sortMenu);
    sortGroup->setExclusive(true);

    const auto addSortAction = [this, sortMenu, sortGroup](const QString &text, SortMode mode) {
        QAction *action = sortMenu->addAction(text);
        action->setCheckable(true);
        action->setData(static_cast<int>(mode));
        sortGroup->addAction(action);

        if (mode == SortMode::Manual)
            action->setChecked(true);

        connect(action, &QAction::triggered, this, [this, mode]() {
            setSortMode(mode);
        });
    };

    addSortAction(i18n("Manual order"), SortMode::Manual);
    sortMenu->addSeparator();
    addSortAction(i18n("Date - oldest first"), SortMode::DateAscending);
    addSortAction(i18n("Date - newest first"), SortMode::DateDescending);
    addSortAction(i18n("Name - A to Z"), SortMode::NameAscending);
    addSortAction(i18n("Name - Z to A"), SortMode::NameDescending);

    m_sortButton->setMenu(sortMenu);
    buttons->addWidget(m_sortButton);

    layout->addLayout(buttons);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setDropIndicatorShown(true);
    layout->addWidget(m_list, 1);

    connect(newPage, &QPushButton::clicked, this, [this]() {
        if (!m_basket)
            return;

        const QString pageId = m_basket->createPage();
        rebuild();
        selectPage(pageId);
    });

    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current, QListWidgetItem *) {
        if (m_rebuilding || !m_basket || !current)
            return;

        m_basket->setCurrentPageId(current->data(PageIdRole).toString());
    });

    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_rebuilding || !m_basket || !item)
            return;

        const QString pageId =
            item->data(PageIdRole).toString();

        const QString newTitle =
            item->text().trimmed();

        QString oldTitle;

        for (const BasketScene::PageInfo &page :
             m_basket->pages()) {
            if (page.id == pageId) {
                oldTitle = page.title;
                break;
            }
        }

        if (oldTitle.isEmpty()
            || newTitle.isEmpty()
            || oldTitle == newTitle) {
            rebuild();
            return;
        }

        if (Global::bnpView
            && Global::bnpView->globalUndoStack()) {
            Global::bnpView
                ->globalUndoStack()
                ->push(
                    new PageRenameCommand(
                        m_basket,
                        pageId,
                        oldTitle,
                        newTitle));
        } else {
            m_basket->renamePage(
                pageId,
                newTitle);
        }
    });

    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int, int, const QModelIndex &, int) {
                if (m_rebuilding
                    || m_sortMode != SortMode::Manual
                    || !m_basket)
                    return;

                QStringList oldOrder;
                oldOrder.reserve(
                    m_basket->pages().size());

                for (const BasketScene::PageInfo &page :
                     m_basket->pages()) {
                    oldOrder.append(page.id);
                }

                QStringList newOrder;
                newOrder.reserve(m_list->count());

                for (int row = 0;
                     row < m_list->count();
                     ++row) {
                    newOrder.append(
                        m_list
                            ->item(row)
                            ->data(PageIdRole)
                            .toString());
                }

                if (oldOrder == newOrder)
                    return;

                if (Global::bnpView
                    && Global::bnpView->globalUndoStack()) {
                    Global::bnpView
                        ->globalUndoStack()
                        ->push(
                            new PageReorderCommand(
                                m_basket,
                                oldOrder,
                                newOrder));
                } else {
                    m_basket->reorderPages(
                        newOrder);
                }
            });
}

void PageSidebar::setBasket(BasketScene *basket)
{
    if (m_basket == basket) {
        rebuild();
        return;
    }

    if (m_basket)
        disconnect(m_basket, nullptr, this, nullptr);

    m_basket = basket;

    if (m_basket) {
        connect(m_basket, &BasketScene::pagesChanged, this, &PageSidebar::rebuild);
        connect(m_basket, &BasketScene::currentPageChanged, this, [this](const QString &pageId) {
            selectPage(pageId);
        });
    }

    rebuild();
}

void PageSidebar::setSortMode(SortMode mode)
{
    m_sortMode = mode;

    const bool manual = mode == SortMode::Manual;
    m_list->setDragDropMode(
        manual
            ? QAbstractItemView::InternalMove
            : QAbstractItemView::NoDragDrop);

    rebuild();
}

void PageSidebar::rebuild()
{
    m_rebuilding = true;
    const QSignalBlocker blocker(m_list);

    m_list->clear();

    if (!m_basket) {
        m_rebuilding = false;
        return;
    }

    QList<BasketScene::PageInfo> pages = m_basket->pages();

    switch (m_sortMode) {
    case SortMode::Manual:
        break;
    case SortMode::DateAscending:
        std::stable_sort(pages.begin(), pages.end(), [](const auto &left, const auto &right) {
            return left.dayKey < right.dayKey;
        });
        break;
    case SortMode::DateDescending:
        std::stable_sort(pages.begin(), pages.end(), [](const auto &left, const auto &right) {
            return left.dayKey > right.dayKey;
        });
        break;
    case SortMode::NameAscending:
        std::stable_sort(pages.begin(), pages.end(), [](const auto &left, const auto &right) {
            return QString::localeAwareCompare(left.title, right.title) < 0;
        });
        break;
    case SortMode::NameDescending:
        std::stable_sort(pages.begin(), pages.end(), [](const auto &left, const auto &right) {
            return QString::localeAwareCompare(left.title, right.title) > 0;
        });
        break;
    }

    for (const BasketScene::PageInfo &page : pages) {
        auto *item = new QListWidgetItem(page.title, m_list);
        item->setData(PageIdRole, page.id);
        item->setFlags(
            item->flags()
            | Qt::ItemIsEditable
            | Qt::ItemIsDragEnabled
            | Qt::ItemIsDropEnabled);

        if (page.id == m_basket->currentPageId())
            m_list->setCurrentItem(item);
    }

    m_rebuilding = false;
}

void PageSidebar::selectPage(const QString &pageId)
{
    if (pageId.isEmpty())
        return;

    const QSignalBlocker blocker(m_list);

    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);

        if (item->data(PageIdRole).toString() == pageId) {
            m_list->setCurrentItem(item);
            m_list->scrollToItem(item);
            return;
        }
    }
}

void PageSidebar::syncManualOrder()
{
    if (!m_basket)
        return;

    QStringList pageIds;
    pageIds.reserve(m_list->count());

    for (int row = 0; row < m_list->count(); ++row)
        pageIds.append(m_list->item(row)->data(PageIdRole).toString());

    m_basket->reorderPages(pageIds);
}

#include "moc_pagesidebar.cpp"
