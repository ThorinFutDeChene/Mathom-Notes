/**
 * SPDX-FileCopyrightText: (C) 2003 by Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "basketfactory.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTime>
#include <QtXml/QDomDocument>

#include <KLocalizedString>
#include <KMessageBox>

#include "bnpview.h"
#include "global.h"

QString BasketFactory::newFolderName()
{
    QString folderName;

    int i =
        QDir(Global::basketsFolder()).count();

    const QString time =
        QTime::currentTime().toString(
            QStringLiteral("hhmmss"));

    for (;; ++i) {
        folderName =
            QStringLiteral("basket%1-%2/")
                .arg(i)
                .arg(time);

        if (!QDir(
                Global::basketsFolder()
                + folderName)
                 .exists()) {
            break;
        }
    }

    return folderName;
}

BasketScene *BasketFactory::newBasket(
    const QString &icon,
    const QString &name,
    BasketScene *parent)
{
    const QString folderName =
        newFolderName();

    const QString fullPath =
        Global::basketsFolder()
        + folderName;

    QDir dir;

    if (!dir.mkpath(fullPath)) {
        KMessageBox::error(
            nullptr,
            i18n(
                "Sorry, but the folder creation for this new location has failed."),
            i18n("Location Creation Failed"));
        return nullptr;
    }

    QDomDocument document(
        QStringLiteral("basket"));

    QDomElement basket =
        document.createElement(
            QStringLiteral("basket"));

    document.appendChild(basket);

    QDomElement properties =
        document.createElement(
            QStringLiteral("properties"));

    basket.appendChild(properties);

    QDomElement nameElement =
        document.createElement(
            QStringLiteral("name"));

    nameElement.appendChild(
        document.createTextNode(name));

    properties.appendChild(nameElement);

    QDomElement iconElement =
        document.createElement(
            QStringLiteral("icon"));

    iconElement.appendChild(
        document.createTextNode(icon));

    properties.appendChild(iconElement);

    basket.appendChild(
        document.createElement(
            QStringLiteral("notes")));

    QFile file(
        fullPath
        + QStringLiteral("/.basket"));

    if (!file.open(
            QIODevice::WriteOnly
            | QIODevice::Text)) {
        KMessageBox::error(
            nullptr,
            i18n(
                "Sorry, but the creation of this new location has failed."),
            i18n("Location Creation Failed"));

        dir.removeRecursively();
        return nullptr;
    }

    QTextStream stream(&file);
    document.save(stream, 2);
    file.close();

    return Global::bnpView->loadNewBasket(
        folderName,
        properties,
        parent);
}
