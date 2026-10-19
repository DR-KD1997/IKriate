/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkRecycleBin.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#include <klocalizedstring.h>

#include <IkPaths.h>

namespace
{
QString indexFile()
{
    return IkRecycleBin::folder() + QStringLiteral("/index.ini");
}
}

QString IkRecycleBin::folder()
{
    const QString f = IkPaths::userDataLocation() + QStringLiteral("/recycle-bin");
    QDir().mkpath(f);
    return f;
}

bool IkRecycleBin::moveToBin(const QString &path, QString *error)
{
    const QFileInfo fi(path);
    if (!fi.exists()) {
        if (error) *error = i18n("The file no longer exists.");
        return false;
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const QString target = folder() + '/' + stamp + '_' + fi.fileName();
    // rename() fails across drives; fall back to copy + remove.
    if (!QFile::rename(path, target)) {
        if (!QFile::copy(path, target) || !QFile::remove(path)) {
            QFile::remove(target);
            if (error) *error = i18n("Could not move \"%1\" to the recycle bin.", fi.fileName());
            return false;
        }
    }
    QSettings index(indexFile(), QSettings::IniFormat);
    index.beginGroup(QFileInfo(target).fileName());
    index.setValue(QStringLiteral("original"), fi.absoluteFilePath());
    index.setValue(QStringLiteral("deleted"), QDateTime::currentDateTime());
    index.endGroup();
    return true;
}

QList<IkRecycleBin::Entry> IkRecycleBin::entries()
{
    QList<Entry> result;
    QSettings index(indexFile(), QSettings::IniFormat);
    const QDir dir(folder());
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time);
    for (const QFileInfo &fi : files) {
        if (fi.fileName() == QLatin1String("index.ini")) continue;
        Entry e;
        e.binPath = fi.absoluteFilePath();
        index.beginGroup(fi.fileName());
        e.originalPath = index.value(QStringLiteral("original")).toString();
        e.deleted = index.value(QStringLiteral("deleted"), fi.lastModified()).toDateTime();
        index.endGroup();
        e.size = fi.size();
        result << e;
    }
    return result;
}

bool IkRecycleBin::restore(const Entry &entry, QString *error)
{
    QString target = entry.originalPath;
    if (target.isEmpty()) {
        if (error) *error = i18n("The original location is unknown.");
        return false;
    }
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (QFileInfo::exists(target)) {
        const QFileInfo fi(target);
        target = fi.absolutePath() + '/' + fi.completeBaseName() + i18nc("suffix for a restored file", " (restored)") + '.' + fi.suffix();
    }
    if (!QFile::rename(entry.binPath, target)) {
        if (!QFile::copy(entry.binPath, target) || !QFile::remove(entry.binPath)) {
            if (error) *error = i18n("Could not restore the file to %1.", target);
            return false;
        }
    }
    QSettings index(indexFile(), QSettings::IniFormat);
    index.remove(QFileInfo(entry.binPath).fileName());
    return true;
}

bool IkRecycleBin::remove(const Entry &entry)
{
    QSettings index(indexFile(), QSettings::IniFormat);
    index.remove(QFileInfo(entry.binPath).fileName());
    return QFile::remove(entry.binPath);
}

void IkRecycleBin::empty()
{
    for (const Entry &e : entries()) remove(e);
}
