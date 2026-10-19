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
 * Favorites, Gallery and Settings tabs, the gallery of recent documents and a
 * floating "+" button for New, Template and Open.
 *
 * Replaces Krita's welcome page in the main window.
 */
class KRITAUI_EXPORT IkHomeScreen : public QWidget
{
    Q_OBJECT
public:
    enum Page { FavoritesPage = 0, GalleryPage = 1, SettingsPage = 2 };

    explicit IkHomeScreen(KisMainWindow *mainWindow);
    ~IkHomeScreen() override;

    void setManager(IkInterfaceManager *manager);
    void showPage(Page page);

    /// Documents the user starred (shown under the star tab).
    static QList<QUrl> favoriteDocuments();
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

    struct Private;
    Private *d;
};

#endif // IKHOMESCREEN_H
