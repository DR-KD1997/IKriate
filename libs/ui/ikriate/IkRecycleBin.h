/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKRECYCLEBIN_H
#define IKRECYCLEBIN_H

#include <QDateTime>
#include <QList>
#include <QString>

#include "kritaui_export.h"

/**
 * Artworks deleted from the gallery go to a recycle bin inside IKriate's
 * data folder first, so they can be restored. Settings › Recycle Bin lists,
 * restores and permanently deletes them.
 */
namespace IkRecycleBin
{
struct Entry {
    QString binPath;      ///< file inside the bin
    QString originalPath; ///< where it came from
    QDateTime deleted;
    qint64 size {0};
};

KRITAUI_EXPORT QString folder();
KRITAUI_EXPORT bool moveToBin(const QString &path, QString *error = nullptr);
KRITAUI_EXPORT QList<Entry> entries();
KRITAUI_EXPORT bool restore(const Entry &entry, QString *error = nullptr);
KRITAUI_EXPORT bool remove(const Entry &entry);
KRITAUI_EXPORT void empty();
}

#endif
