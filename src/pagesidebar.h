/**
 * SPDX-FileCopyrightText: (C) 2026 Thorinux
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PAGESIDEBAR_H
#define PAGESIDEBAR_H

#include <QWidget>

class BasketScene;
class QListWidget;
class QListWidgetItem;
class QToolButton;

class PageSidebar final : public QWidget
{
    Q_OBJECT

public:
    enum class SortMode {
        Manual,
        DateAscending,
        DateDescending,
        NameAscending,
        NameDescending
    };

    explicit PageSidebar(QWidget *parent = nullptr);

    void setBasket(BasketScene *basket);

private:
    void rebuild();
    void setSortMode(SortMode mode);
    void syncManualOrder();
    void selectPage(const QString &pageId);

    BasketScene *m_basket = nullptr;
    QListWidget *m_list = nullptr;
    QToolButton *m_addButton = nullptr;
    QToolButton *m_removeButton = nullptr;
    QToolButton *m_sortButton = nullptr;
    SortMode m_sortMode = SortMode::Manual;
    bool m_rebuilding = false;
};

#endif // PAGESIDEBAR_H
