/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKBRUSHLIBRARY_H
#define IKBRUSHLIBRARY_H

#include <QPointer>
#include <QWidget>

#include <KoResource.h>

#include "kritaui_export.h"

class IkInterfaceManager;
class QListView;
class QStandardItemModel;
class IkCategoryList;
class IkPresetModel;

/**
 * The brush library: categories on the left (New, Recent, Favorites and the
 * brush categories), brushes with a sample stroke on the right.
 *
 * Categories are resource tags, so brushes users tag themselves show up here
 * too. "New" lists brushes the user created or imported; "Recent" the last
 * brushes used.
 */
class KRITAUI_EXPORT IkBrushLibrary : public QWidget
{
    Q_OBJECT
public:
    explicit IkBrushLibrary(IkInterfaceManager *manager, QWidget *parent = nullptr);
    ~IkBrushLibrary() override;

    /// Narrow mode: categories as icons only (used in the docked Kriate panel
    /// on small screens).
    void setCompact(bool compact);
    QSize sizeHint() const override;

    /// Remembers @p preset as recently used (called whenever a brush is picked).
    static void noteRecent(const QString &presetName);
    static QStringList recentPresets();

public Q_SLOTS:
    void reload();
    void selectCategory(int row);

Q_SIGNALS:
    void presetActivated();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void slotPresetClicked(const QModelIndex &index);
    void slotCurrentPresetChanged();
    void slotContextMenu(const QModelIndex &index, const QPoint &globalPos);

private:
    void populatePresets();
    void toggleFavourite(KoResourceSP resource);

    struct Private;
    Private *d;
};

#endif // IKBRUSHLIBRARY_H
