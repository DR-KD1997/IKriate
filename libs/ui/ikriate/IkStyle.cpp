/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkStyle.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QFrame>
#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QWindow>

#include <kis_icon_utils.h>

#include "IkSkinManager.h"

namespace
{
QHash<QString, QByteArray> &svgCache()
{
    static QHash<QString, QByteArray> cache;
    return cache;
}

QHash<QString, QPixmap> &pixmapCache()
{
    static QHash<QString, QPixmap> cache;
    return cache;
}

QByteArray svgData(const QString &name)
{
    QHash<QString, QByteArray> &cache = svgCache();
    auto it = cache.constFind(name);
    if (it != cache.constEnd()) return it.value();
    QFile f(QStringLiteral(":/ikriate/icons/%1.svg").arg(name));
    QByteArray data;
    if (f.open(QIODevice::ReadOnly)) data = f.readAll();
    cache.insert(name, data);
    return data;
}

bool isDark(const QPalette &pal)
{
    return pal.color(QPalette::Window).lightnessF() < 0.5;
}

/// Renders IKriate's SVG line icons crisply at any size, in a given colour.
class IkSvgIconEngine : public QIconEngine
{
public:
    IkSvgIconEngine(const QString &name, const QColor &color, bool followPalette)
        : m_name(name), m_color(color), m_followPalette(followPalette) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, IkStyle::pixmap(m_name, qMin(rect.width(), rect.height()), colorFor(mode), dpr));
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State) override
    {
        const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
        QPixmap pm = IkStyle::pixmap(m_name, qMin(size.width(), size.height()), colorFor(mode), dpr);
        return pm;
    }

    QIconEngine *clone() const override
    {
        return new IkSvgIconEngine(m_name, m_color, m_followPalette);
    }

    QString key() const override
    {
        return QStringLiteral("IkSvgIconEngine");
    }

private:
    QColor colorFor(QIcon::Mode mode) const
    {
        QColor c = m_followPalette ? IkStyle::color(IkStyle::Text) : m_color;
        if (mode == QIcon::Disabled) {
            c.setAlphaF(c.alphaF() * 0.38);
        } else if (mode == QIcon::Selected) {
            c = IkStyle::color(IkStyle::AccentText);
        }
        return c;
    }

    QString m_name;
    QColor m_color;
    bool m_followPalette;
};
}

namespace IkStyle
{
QColor mix(const QColor &a, const QColor &b, qreal t)
{
    t = qBound<qreal>(0.0, t, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

QColor withAlpha(const QColor &c, int alpha)
{
    QColor r = c;
    r.setAlpha(qBound(0, alpha, 255));
    return r;
}

QColor color(Role role, const QPalette &pal)
{
    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor accent = pal.color(QPalette::Highlight);
    const bool dark = isDark(pal);
    switch (role) {
    case Background: return window;
    case Chrome: return dark ? mix(window, Qt::black, 0.55) : mix(window, text, 0.04);
    case Surface: return mix(window, text, dark ? 0.065 : 0.045);
    case Raised: return mix(window, text, dark ? 0.13 : 0.09);
    case RaisedHover: return mix(window, text, dark ? 0.19 : 0.14);
    case Text: return text;
    case MutedText: return mix(window, text, 0.64);
    case FaintText: return mix(window, text, 0.42);
    case Accent: return accent;
    case AccentText: return pal.color(QPalette::HighlightedText);
    case AccentSoft: return withAlpha(accent, dark ? 64 : 48);
    case Divider: return withAlpha(text, dark ? 26 : 34);
    case AccentDivider: return withAlpha(accent, 150);
    case CanvasSurround: return dark ? mix(window, Qt::black, 0.18) : mix(window, text, 0.12);
    }
    return window;
}

QColor color(Role role)
{
    return color(role, qApp ? qApp->palette() : QPalette());
}

bool touchFirst()
{
#ifdef Q_OS_ANDROID
    return true;
#else
    static const bool forced = qEnvironmentVariableIsSet("IKRIATE_TOUCH_UI");
    return forced;
#endif
}

int touchSize()
{
    return touchFirst() ? 48 : 40;
}

int radius()
{
    return IkSkinManager::instance()->chromeRadius();
}

int spacing()
{
    return touchFirst() ? 12 : 10;
}

QFont font(qreal pointScale, int weight)
{
    QFont f = qApp ? qApp->font() : QFont();
    if (f.pointSizeF() > 0) {
        f.setPointSizeF(f.pointSizeF() * pointScale);
    } else if (f.pixelSize() > 0) {
        f.setPixelSize(qRound(f.pixelSize() * pointScale));
    }
    if (weight >= 0) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        f.setWeight(QFont::Weight(weight));
#else
        f.setWeight(weight);
#endif
    }
    return f;
}

QFont titleFont()
{
    return font(1.7, QFont::Bold);
}

QFont sectionFont()
{
    return font(1.35, QFont::Normal);
}

bool hasIcon(const QString &name)
{
    return !svgData(name).isEmpty();
}

QPixmap pixmap(const QString &name, int size, const QColor &color, qreal dpr)
{
    const int px = qMax(1, qRound(size * dpr));
    const QString key = QStringLiteral("%1|%2|%3").arg(name).arg(px).arg(color.rgba(), 0, 16);
    QHash<QString, QPixmap> &cache = pixmapCache();
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    QPixmap result;
    QByteArray data = svgData(name);
    if (!data.isEmpty()) {
        QColor solid = color;
        solid.setAlpha(255);
        data.replace("currentColor", solid.name(QColor::HexRgb).toLatin1());
        QSvgRenderer renderer(data);
        QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setOpacity(color.alphaF());
            renderer.render(&p, QRectF(0, 0, px, px));
        }
        result = QPixmap::fromImage(img);
    } else {
        // Not one of ours: use Krita's icon theme.
        result = KisIconUtils::loadIcon(name).pixmap(QSize(px, px));
    }
    result.setDevicePixelRatio(dpr);
    cache.insert(key, result);
    return result;
}

QIcon icon(const QString &name, const QColor &c)
{
    if (!hasIcon(name)) {
        return KisIconUtils::loadIcon(name);
    }
    return QIcon(new IkSvgIconEngine(name, c, !c.isValid()));
}

void makeTransparent(QWidget *w)
{
    if (QAbstractScrollArea *area = qobject_cast<QAbstractScrollArea *>(w)) {
        if (QFrame *f = qobject_cast<QFrame *>(area)) f->setFrameShape(QFrame::NoFrame);
        area->viewport()->setAutoFillBackground(false);
        QPalette vp = area->viewport()->palette();
        vp.setColor(QPalette::Base, Qt::transparent);
        vp.setColor(QPalette::Window, Qt::transparent);
        area->viewport()->setPalette(vp);
    }
    w->setAutoFillBackground(false);
    QPalette p = w->palette();
    p.setColor(QPalette::Base, Qt::transparent);
    w->setPalette(p);
}

void clearCache()
{
    pixmapCache().clear();
}

void drawShadow(QPainter &p, const QRectF &rect, qreal r, int depth, int alpha)
{
    p.save();
    p.setPen(Qt::NoPen);
    for (int i = depth; i > 0; i -= 2) {
        const qreal a = alpha * (1.0 - qreal(i) / depth) / (depth / 2.0);
        p.setBrush(QColor(0, 0, 0, qBound(0, qRound(a), 255)));
        p.drawRoundedRect(rect.adjusted(-i / 2.0, -i / 4.0, i / 2.0, i * 0.75), r + i / 2.0, r + i / 2.0);
    }
    p.restore();
}

QBrush accentGradient(const QRectF &rect, const QPalette &pal)
{
    const QColor accent = pal.color(QPalette::Highlight);
    auto shifted = [&](qreal dh, qreal dl) {
        qreal h = accent.hslHueF();
        if (h < 0) h = 0; // greys have no hue
        h += dh;
        while (h < 0) h += 1.0;
        while (h >= 1.0) h -= 1.0;
        return QColor::fromHslF(h, accent.hslSaturationF(), qBound<qreal>(0.0, accent.lightnessF() + dl, 1.0));
    };
    QLinearGradient g(rect.topLeft(), rect.bottomRight());
    g.setColorAt(0.0, shifted(0.05, 0.12));
    g.setColorAt(1.0, shifted(-0.08, -0.02));
    return QBrush(g);
}
}
