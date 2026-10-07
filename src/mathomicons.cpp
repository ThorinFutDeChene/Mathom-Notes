/**
 * SPDX-FileCopyrightText: 2026 Thorinux Systems
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mathomicons.h"

#include "global.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>

namespace
{
QString resourceForName(const QString &name)
{
    static const QHash<QString, QString> resources = {
        {QStringLiteral("fr.thorinux.mathom"), QStringLiteral(":/mathom/icons/app.png")},
        {QStringLiteral("mathom-app"), QStringLiteral(":/mathom/icons/app.png")},
        {QStringLiteral("mathom-house"), QStringLiteral(":/mathom/icons/house.png")},
        {QStringLiteral("mathom-shelf"), QStringLiteral(":/mathom/icons/shelf.png")},
        {QStringLiteral("tag_checkbox"), QStringLiteral(":/tags/16-actions-tag_checkbox.png")},
        {QStringLiteral("tag_checkbox_checked"), QStringLiteral(":/tags/16-actions-tag_checkbox_checked.png")},
        {QStringLiteral("tag_for_later"), QStringLiteral(":/tags/sc-actions-tag_for_later.svgz")},
        {QStringLiteral("tag_fun"), QStringLiteral(":/tags/sc-actions-tag_fun.svgz")},
        {QStringLiteral("tag_important"), QStringLiteral(":/tags/sc-actions-tag_important.svgz")},
        {QStringLiteral("tag_preference_bad"), QStringLiteral(":/tags/sc-actions-tag_preference_bad.svgz")},
        {QStringLiteral("tag_preference_excellent"), QStringLiteral(":/tags/sc-actions-tag_preference_excellent.svgz")},
        {QStringLiteral("tag_preference_good"), QStringLiteral(":/tags/sc-actions-tag_preference_good.svgz")},
        {QStringLiteral("tag_priority_high"), QStringLiteral(":/tags/sc-actions-tag_priority_high.svgz")},
        {QStringLiteral("tag_priority_low"), QStringLiteral(":/tags/sc-actions-tag_priority_low.svgz")},
        {QStringLiteral("tag_priority_medium"), QStringLiteral(":/tags/sc-actions-tag_priority_medium.svgz")},
        {QStringLiteral("tag_progress_000"), QStringLiteral(":/tags/sc-actions-tag_progress_000.svgz")},
        {QStringLiteral("tag_progress_025"), QStringLiteral(":/tags/sc-actions-tag_progress_025.svgz")},
        {QStringLiteral("tag_progress_050"), QStringLiteral(":/tags/sc-actions-tag_progress_050.svgz")},
        {QStringLiteral("tag_progress_075"), QStringLiteral(":/tags/sc-actions-tag_progress_075.svgz")},
        {QStringLiteral("tag_progress_100"), QStringLiteral(":/tags/sc-actions-tag_progress_100.svgz")},
    };

    return resources.value(name);
}

QString legacyCustomIconsFolder()
{
    return QDir::homePath() + QStringLiteral("/.local/share/basket/basket-icons/");
}
}

QIcon MathomIcons::application()
{
    return QIcon(QStringLiteral(":/mathom/icons/app.png"));
}

QString MathomIcons::customIconsFolder()
{
    return Global::savesFolder() + QStringLiteral("icons/");
}

QString MathomIcons::resolveCustomPath(const QString &nameOrPath)
{
    if (nameOrPath.isEmpty())
        return {};

    const QFileInfo direct(nameOrPath);
    if (direct.isFile())
        return direct.absoluteFilePath();

    const QString baseName = direct.fileName();
    if (baseName.isEmpty())
        return {};

    const QString mathomPath = customIconsFolder() + baseName;
    if (QFileInfo::exists(mathomPath))
        return mathomPath;

    const QString previousMathomPath =
        Global::savesFolder() + QStringLiteral("basket-icons/") + baseName;
    if (QFileInfo::exists(previousMathomPath))
        return previousMathomPath;

    const QString legacyPath = legacyCustomIconsFolder() + baseName;
    if (QFileInfo::exists(legacyPath))
        return legacyPath;

    return {};
}

QIcon MathomIcons::icon(const QString &nameOrPath)
{
    if (nameOrPath.isEmpty())
        return {};

    const QString bundled = resourceForName(nameOrPath);
    if (!bundled.isEmpty())
        return QIcon(bundled);

    const QString customPath = resolveCustomPath(nameOrPath);
    if (!customPath.isEmpty())
        return QIcon(customPath);

    return QIcon::fromTheme(nameOrPath);
}

QString MathomIcons::canonicalHierarchyName(const QString &storedIcon, bool topLevel)
{
    const bool legacyDefault =
        storedIcon.isEmpty()
        || storedIcon == QStringLiteral("basket")
        || storedIcon == QStringLiteral("org.kde.basket")
        || storedIcon == QStringLiteral("fr.thorinux.mathom");

    if (legacyDefault)
        return topLevel ? QStringLiteral("mathom-house") : QStringLiteral("mathom-shelf");

    const QString customPath = resolveCustomPath(storedIcon);
    if (!customPath.isEmpty())
        return customPath;

    return storedIcon;
}

QIcon MathomIcons::hierarchy(const QString &storedIcon, bool topLevel)
{
    const QString canonical = canonicalHierarchyName(storedIcon, topLevel);
    QIcon result = icon(canonical);

    if (!result.isNull())
        return result;

    return topLevel
        ? icon(QStringLiteral("mathom-house"))
        : icon(QStringLiteral("mathom-shelf"));
}

bool MathomIcons::isDefaultHierarchyIcon(
    const QString &storedIcon)
{
    return storedIcon.isEmpty()
        || storedIcon == QStringLiteral("basket")
        || storedIcon == QStringLiteral("org.kde.basket")
        || storedIcon == QStringLiteral("fr.thorinux.mathom")
        || storedIcon == QStringLiteral("mathom-house")
        || storedIcon == QStringLiteral("mathom-shelf");
}

QString MathomIcons::convertedHierarchyName(
    const QString &storedIcon,
    bool topLevel)
{
    if (isDefaultHierarchyIcon(storedIcon)) {
        return topLevel
            ? QStringLiteral("mathom-house")
            : QStringLiteral("mathom-shelf");
    }

    const QString customPath =
        resolveCustomPath(storedIcon);

    if (!customPath.isEmpty())
        return customPath;

    /*
     * Theme icons explicitly chosen by the user are custom choices too.
     * Keep them unchanged when the hierarchy level changes.
     */
    return storedIcon;
}
