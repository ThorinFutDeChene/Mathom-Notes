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
#include <QMessageBox>
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

    m_addButton = new QToolButton(this);
    m_addButton->setText(QStringLiteral("+"));
    m_addButton->setToolTip(i18n("New Page"));
    m_addButton->setAccessibleName(i18n("New Page"));
    buttons->addWidget(m_addButton);

    m_removeButton = new QToolButton(this);
    m_removeButton->setText(QStringLiteral("-"));
    m_removeButton->setToolTip(i18n("Delete Page"));
    m_removeButton->setAccessibleName(i18n("Delete Page"));
    buttons->addWidget(m_removeButton);

    buttons->addStretch(1);

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
    m_list->setContextMenuPolicy(
        Qt::CustomContextMenu);
    layout->addWidget(m_list, 1);

    connect(
        m_list,
        &QListWidget::customContextMenuRequested,
        this,
        [this](const QPoint &pos) {
            if (!m_basket
                || !Global::bnpView) {
                return;
            }

            QListWidgetItem *item =
                m_list->itemAt(pos);

            if (!item)
                return;

            m_list->setCurrentItem(item);

            const QString pageId =
                item->data(
                    PageIdRole)
                    .toString();

            if (pageId.isEmpty())
                return;

            QMenu menu(this);

            QAction *toShelf =
                menu.addAction(
                    i18n("Convert to Shelf"));

            connect(
                toShelf,
                &QAction::triggered,
                this,
                [this, pageId]() {
                    if (Global::bnpView) {
                        Global::bnpView
                            ->convertPageToShelf(
                                m_basket,
                                pageId);
                    }
                });

            QAction *toHouse =
                menu.addAction(
                    i18n("Convert to Mathom-House"));

            connect(
                toHouse,
                &QAction::triggered,
                this,
                [this, pageId]() {
                    if (Global::bnpView) {
                        Global::bnpView
                            ->convertPageToMathomHouse(
                                m_basket,
                                pageId);
                    }
                });

            menu.exec(
                m_list->viewport()
                    ->mapToGlobal(pos));
        });

    connect(m_addButton, &QToolButton::clicked, this, [this]() {
        if (!m_basket)
            return;

        const QString previousPageId =
            m_basket->currentPageId();

        const QString pageId =
            m_basket->createPage();

        if (Global::bnpView
            && Global::bnpView->globalUndoStack()) {
            Global::bnpView
                ->globalUndoStack()
                ->push(
                    new PageCreateCommand(
                        m_basket,
                        pageId,
                        previousPageId));
        }

        rebuild();
        selectPage(pageId);
    });

    connect(m_removeButton, &QToolButton::clicked, this, [this]() {
        if (!m_basket
            || m_basket->pages().size() <= 1) {
            return;
        }

        QListWidgetItem *current =
            m_list->currentItem();

        if (!current)
            return;

        const QString pageId =
            current->data(PageIdRole).toString();

        const QString pageTitle =
            current->text();

        const QMessageBox::StandardButton answer =
            QMessageBox::question(
                this,
                i18n("Delete Page?"),
                i18n(
                    "Delete the Page \"%1\" and all Mathoms it contains?",
                    pageTitle),
                QMessageBox::Yes
                    | QMessageBox::No,
                QMessageBox::No);

        if (answer != QMessageBox::Yes)
            return;

        if (Global::bnpView
            && Global::bnpView->globalUndoStack()) {
            Global::bnpView
                ->globalUndoStack()
                ->push(
                    new PageDeleteCommand(
                        m_basket,
                        pageId));
        } else {
            m_basket->deletePage(pageId);
        }
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

    m_addButton->setEnabled(
        m_basket != nullptr);

    m_removeButton->setEnabled(
        m_basket
        && m_basket->pages().size() > 1);

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
