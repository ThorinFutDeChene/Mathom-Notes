/**
 * SPDX-FileCopyrightText: (C) 2003 by Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BASKETFACTORY_H
#define BASKETFACTORY_H

#include <QString>

class BasketScene;

namespace BasketFactory
{

BasketScene *newBasket(
    const QString &icon,
    const QString &name,
    BasketScene *parent = nullptr);

QString newFolderName();

}

#endif // BASKETFACTORY_H
