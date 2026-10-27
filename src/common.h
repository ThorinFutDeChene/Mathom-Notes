/**
 * SPDX-FileCopyrightText: (C) 2003 by Sébastien Laoût <slaout@linux62.org>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/** Common methods extracted here to simplify the dependency between classes
 */

#ifndef BASKET_COMMON_H
#define BASKET_COMMON_H

#include "basket_export.h"

class QByteArray;
class QString;

namespace FileStorage
{
BASKET_EXPORT bool loadFromFile(const QString &fullPath, QString *string);
BASKET_EXPORT bool loadFromFile(const QString &fullPath, QByteArray *array);
BASKET_EXPORT bool saveToFile(const QString &fullPath, const QString &string, bool isEncrypted = false);
BASKET_EXPORT bool saveToFile(const QString &fullPath, const QByteArray &array, bool isEncrypted = false); //[Encrypt and] save binary content
BASKET_EXPORT bool safelySaveToFile(const QString &fullPath, const QByteArray &array);
BASKET_EXPORT bool safelySaveToFile(const QString &fullPath, const QString &string);
}

#endif
