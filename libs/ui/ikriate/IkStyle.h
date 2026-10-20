/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKSTYLE_H
#define IKSTYLE_H

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QString>

#include "kritaui_export.h"

class QPainter;
class QRectF;

/**
 * Colours, sizes and icons shared by all IKriate screens.
 *
 * Every colour is derived from the application palette, which the skin
 * manager generates from the active skin, accent and tint. Nothing here is a
 * fixed colour, so all skins (including users' own) restyle the interface.
 */
namespace IkStyle
{
enum Role {
    Background,   ///< page background (home, settings, canvas surround)
    Chrome,       ///< darkest band: home header, desktop top bar
    Surface,      ///< cards and side panels
    Raised,       ///< controls inside cards (fields, pills, tiles)
    RaisedHover,
    Text,
    MutedText,
    FaintText,
    Accent,
    AccentText,   ///< text/icons drawn on the accent
    AccentSoft,   ///< translucent accent for selected rows
    Divider,
    AccentDivider,
    CanvasSurround,
};

KRITAUI_EXPORT QColor color(Role role, const QPalette &palette);
KRITAUI_EXPORT QColor color(Role role);

/// Interface scale chosen in Settings (1.0 = 100%). On Android the system
/// density does the scaling, so this stays 1.0 there.
KRITAUI_EXPORT qreal uiScale();
KRITAUI_EXPORT void setUiScale(qreal scale);
/// @p px multiplied by uiScale().
KRITAUI_EXPORT int scaled(int px);
/// Applies the interface font (custom family, if any) at the current scale.
KRITAUI_EXPORT void applyInterfaceFont();

/// Edge of a comfortable touch target (48 on tablets, 40 with a mouse), scaled.
KRITAUI_EXPORT int touchSize();
/// Base corner radius for panels; controls use smaller fractions of it.
KRITAUI_EXPORT int radius();
KRITAUI_EXPORT int spacing();
/// True when the interface should favour fingers (Android, or forced).
KRITAUI_EXPORT bool touchFirst();

KRITAUI_EXPORT QFont font(qreal pointScale = 1.0, int weight = -1);
KRITAUI_EXPORT QFont titleFont();
KRITAUI_EXPORT QFont sectionFont();

/// Icon from IKriate's line icon set (":/ikriate/icons/<name>.svg"), drawn
/// in @p color (text colour by default). Falls back to Krita's icon theme.
KRITAUI_EXPORT QIcon icon(const QString &name, const QColor &color = QColor());
KRITAUI_EXPORT QPixmap pixmap(const QString &name, int size, const QColor &color, qreal dpr = 1.0);
KRITAUI_EXPORT bool hasIcon(const QString &name);

/// Mix of @p a and @p b; t = 0 gives a, t = 1 gives b.
KRITAUI_EXPORT QColor mix(const QColor &a, const QColor &b, qreal t);
KRITAUI_EXPORT QColor withAlpha(const QColor &c, int alpha);

/// Soft drop shadow behind a rounded rectangle (cheap, no effects framework).
KRITAUI_EXPORT void drawShadow(QPainter &p, const QRectF &rect, qreal radius, int depth = 10, int alpha = 70);
/// Accent gradient used by the floating "+" button and active highlights.
KRITAUI_EXPORT QBrush accentGradient(const QRectF &rect, const QPalette &palette);

/// Removes the frame and background of a list or scroll area so it shows
/// the panel behind it (without a style sheet, which would freeze its palette).
KRITAUI_EXPORT void makeTransparent(QWidget *scrollArea);

/// Clears the icon cache (after the skin changes).
KRITAUI_EXPORT void clearCache();
}

#endif // IKSTYLE_H
