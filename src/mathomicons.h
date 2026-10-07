/**
 * SPDX-FileCopyrightText: 2026 Thorinux Systems
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MATHOMICONS_H
#define MATHOMICONS_H

#include "basket_export.h"

#include <QIcon>
#include <QString>

namespace MathomIcons
{
/**
 * Central icon catalogue for Mathom.
 *
 * Application-owned icons are resolved from the Qt resource bundle first.
 * User custom icons are resolved from Mathom's private icon directory and
 * legacy BasKet locations. KDE/Breeze is used only as a final fallback for
 * standard desktop action icons.
 */
BASKET_EXPORT QIcon application();
BASKET_EXPORT QIcon hierarchy(const QString &storedIcon, bool topLevel);
BASKET_EXPORT QIcon icon(const QString &nameOrPath);
BASKET_EXPORT QString resolveCustomPath(const QString &nameOrPath);
BASKET_EXPORT QString canonicalHierarchyName(const QString &storedIcon, bool topLevel);
BASKET_EXPORT bool isDefaultHierarchyIcon(const QString &storedIcon);
BASKET_EXPORT QString convertedHierarchyName(const QString &storedIcon, bool topLevel);
BASKET_EXPORT QString customIconsFolder();
}

#endif
