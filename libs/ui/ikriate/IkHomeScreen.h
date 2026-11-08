/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKHOMESCREEN_H
#define IKHOMESCREEN_H

#include <QUrl>
#include <QWidget>

#include "kritaui_export.h"

class KisMainWindow;
class IkInterfaceManager;

/**
 * IKriate's start screen, shown when no document is open: a header with
 * Online, Gallery and Settings tabs, the gallery of recent documents (sorted
 * with favourites first by default) and a floating "+" button for New,
 * Template and Open.
 *
 * Replaces Krita's welcome page in the main window.
 */
class KRITAUI_EXPORT IkHomeScreen : public QWidget
{
    Q_OBJECT
public:
    enum Page { OnlinePage = 0, GalleryPage = 1, SettingsPage = 2 };
    /// Gallery order. Favourites first: starred artworks, most recently
    /// opened first, then the rest the same way.
    enum SortMode { SortFavoritesFirst = 0, SortRecent = 1, SortNameAscending = 2, SortNameDescending = 3 };

    explicit IkHomeScreen(KisMainWindow *mainWindow);
    ~IkHomeScreen() override;

    void setManager(IkInterfaceManager *manager);
    void showPage(Page page);
    Page currentPage() const;
    /// Section shown on the settings page (Theme, Language, ...).
    int settingsSection() const;
    void showSettingsSection(int section);

    /// Documents the user starred (shown first in the gallery).
    static QList<QUrl> favoriteDocuments();
    static SortMode sortMode();
    void setSortMode(SortMode mode);
    static void setFavoriteDocument(const QUrl &url, bool favorite);

public Q_SLOTS:
    void reload();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void layoutFloatingButtons();
    void setCreateMenuOpen(bool open);
    void openDocument(const QUrl &url);
    void showDocumentMenu(const QUrl &url, const QPoint &globalPos);
    void showSortMenu();

    struct Private;
    Private *d;
};

#endif // IKHOMESCREEN_H
