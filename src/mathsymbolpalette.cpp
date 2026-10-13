/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mathsymbolpalette.h"

#include "focusedwidgets.h"
#include "mathsymbolcatalog.h"
#include "global.h"
#include "bnpview.h"

#include <KLocalizedString>

#include <QAbstractItemView>
#include <QAction>
#include <QTreeWidgetItem>
#include <QDialog>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QTextCursor>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

void insertInto(FocusedTextEdit *target, const QString &symbol)
{
    if (!target || target->isReadOnly() || symbol.isEmpty())
        return;
    QTextCursor cursor = target->textCursor();
    cursor.insertText(symbol);
    target->setTextCursor(cursor);
    target->setFocus();
}

}

void MathSymbolPalette::open(QWidget *parent, FocusedTextEdit *target)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(i18n("Mathematical symbols"));
    dialog->resize(570, 520);
    dialog->setModal(false);
    auto *layout = new QVBoxLayout(dialog);
    auto *description = new QLabel(
        target ? i18n("Select a symbol to insert at the cursor.")
               : i18n("Open a text Mathom to insert a symbol."), dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *search = new QLineEdit(dialog);
    search->setPlaceholderText(i18n("Search by symbol or name..."));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);

    auto *tree = new QTreeWidget(dialog);
    tree->setHeaderLabels({i18n("Symbol"), i18n("Meaning")});
    tree->setRootIsDecorated(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    QString previous;
    QTreeWidgetItem *group = nullptr;
    for (const MathSymbolDefinition &definition : MathSymbolCatalog::entries()) {
        if (!group || previous != definition.category) {
            previous = definition.category;
            group = new QTreeWidgetItem(tree, {definition.category});
            group->setFirstColumnSpanned(true);
            group->setFlags(Qt::ItemIsEnabled);
            QFont font = group->font(0);
            font.setBold(true);
            group->setFont(0, font);
        }
        auto *item = new QTreeWidgetItem(group, {definition.symbol, definition.label});
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    }
    tree->expandAll();
    layout->addWidget(tree, 1);

    auto *insert = new QPushButton(i18n("Insert symbol"), dialog);
    insert->setEnabled(false);
    layout->addWidget(insert);
    QPointer<FocusedTextEdit> safeTarget(target);

    QObject::connect(tree, &QTreeWidget::currentItemChanged, dialog,
        [tree, insert, safeTarget](QTreeWidgetItem *item, QTreeWidgetItem *) {
            insert->setEnabled(item && item->parent() && safeTarget && !safeTarget->isReadOnly());
        });
    QObject::connect(insert, &QPushButton::clicked, dialog,
        [tree, safeTarget]() {
            auto *item = tree->currentItem();
            if (item && item->parent())
                insertInto(safeTarget.data(), item->text(0));
        });
    QObject::connect(tree, &QTreeWidget::itemDoubleClicked, dialog,
        [safeTarget](QTreeWidgetItem *item, int) {
            if (item && item->parent())
                insertInto(safeTarget.data(), item->text(0));
        });
    QObject::connect(search, &QLineEdit::textChanged, dialog,
        [tree](const QString &value) {
            const QString needle = value.trimmed();
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                auto *category = tree->topLevelItem(i);
                const bool groupMatch = category->text(0).contains(needle, Qt::CaseInsensitive);
                bool hasVisible = false;
                for (int j = 0; j < category->childCount(); ++j) {
                    auto *item = category->child(j);
                    const bool visible = groupMatch
                        || item->text(0).contains(needle, Qt::CaseInsensitive)
                        || item->text(1).contains(needle, Qt::CaseInsensitive);
                    item->setHidden(!visible);
                    hasVisible |= visible;
                }
                category->setHidden(!hasVisible);
                if (!needle.isEmpty() && hasVisible)
                    category->setExpanded(true);
            }
        });
    dialog->show();
}

void MathSymbolPalette::populateMenu(QMenu *menu, FocusedTextEdit *target)
{
    if (!menu)
        return;
    QPointer<FocusedTextEdit> safeTarget(target);
    QAction *allSymbols = menu->addAction(i18n("Open symbol palette..."));
    QObject::connect(allSymbols, &QAction::triggered, menu,
        [menu, safeTarget]() {
            MathSymbolPalette::open(
                Global::bnpView ? Global::bnpView->window() : nullptr,
                safeTarget.data());
        });
    menu->addSeparator();
    QString previous;
    QMenu *category = nullptr;
    for (const MathSymbolDefinition &definition : MathSymbolCatalog::entries()) {
        if (!category || previous != definition.category) {
            previous = definition.category;
            category = menu->addMenu(definition.category);
        }
        auto *action = category->addAction(
            QStringLiteral("%1  %2").arg(definition.symbol, definition.label));
        const QString symbol = definition.symbol;
        QObject::connect(action, &QAction::triggered, category,
            [safeTarget, symbol]() { insertInto(safeTarget.data(), symbol); });
    }
}
