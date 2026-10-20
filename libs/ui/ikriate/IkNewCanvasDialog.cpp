/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkNewCanvasDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QDir>
#include <QGuiApplication>
#include <QAction>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <kactioncollection.h>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScroller>
#include <QUrl>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <KoResourcePaths.h>
#include <kis_image_config.h>

#include "KisDocument.h"
#include "KisMainWindow.h"
#include "KisPart.h"
#include "KisTemplate.h"
#include "KisTemplateGroup.h"
#include "KisTemplateTree.h"
#include "dialogs/KisDlgCreateNewDocument.h"
#include "kis_config.h"

#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
struct Preset {
    QString name;
    int w, h;
    double ppi;
};

QList<Preset> presets()
{
    QList<Preset> list;
    if (QScreen *s = QGuiApplication::primaryScreen()) {
        const QSize px = s->size() * s->devicePixelRatio();
        list << Preset{i18n("Screen Size"), qMax(px.width(), px.height()), qMin(px.width(), px.height()), 132};
    }
    list << Preset{i18n("Full HD"), 1920, 1080, 300}
         << Preset{i18n("4K UHD"), 3840, 2160, 300}
         << Preset{i18n("Square"), 2048, 2048, 300}
         << Preset{i18n("A4"), 2480, 3508, 300}
         << Preset{i18n("A5"), 1748, 2480, 300}
         << Preset{i18n("US Letter"), 2550, 3300, 300}
         << Preset{i18n("Comic Page"), 1988, 3075, 300}
         << Preset{i18n("Social Post (4:5)"), 1080, 1350, 72}
         << Preset{i18n("Phone Wallpaper"), 1440, 3120, 72};
    return list;
}

int maxCanvasSide()
{
    return IkStyle::touchFirst() ? 8192 : 16000;
}

/// "Title   [X]" bar of the sheets.
QWidget *sheetTitle(const QString &title, QWidget *parent, std::function<void()> onClose)
{
    QWidget *bar = new QWidget(parent);
    QHBoxLayout *l = new QHBoxLayout(bar);
    l->setContentsMargins(16, 6, 6, 6);
    l->addSpacing(36);
    QLabel *t = new QLabel(title, bar);
    t->setAlignment(Qt::AlignCenter);
    t->setFont(IkStyle::font(1.45));
    l->addWidget(t, 1);
    IkIconButton *close = new IkIconButton(QStringLiteral("close"), i18n("Close"), bar);
    QObject::connect(close, &QAbstractButton::clicked, bar, onClose);
    l->addWidget(close);
    return bar;
}

QWidget *divider(QWidget *parent)
{
    return new IkDivider(parent);
}

IkTextButton *confirmButton(QWidget *parent)
{
    IkTextButton *b = new IkTextButton(i18nc("button that creates the canvas", "CONFIRM"), IkTextButton::Plain, parent);
    b->setFontScale(1.25, QFont::Bold);
    b->setMinimumHeight(IkStyle::touchSize() + 12);
    return b;
}

/// Max-layers read-out: number above a layers icon.
class IkLayerBudget : public QWidget
{
public:
    explicit IkLayerBudget(QWidget *parent) : QWidget(parent)
    {
        setFixedSize(IkStyle::touchSize() + 40, IkStyle::touchSize() + 4);
        setToolTip(i18n("About how many layers of this size fit in the memory IKriate may use"));
    }
    int layers {0};

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        p.setPen(Qt::NoPen);
        p.setBrush(IkStyle::color(IkStyle::Raised, palette()));
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        p.setPen(IkStyle::color(IkStyle::Text, palette()));
        p.setFont(IkStyle::font(0.85, QFont::DemiBold));
        p.drawText(QRectF(r.left(), r.top() + 2, r.width(), r.height() / 2), Qt::AlignCenter,
                   layers > 9999 ? QStringLiteral("9999+") : QString::number(layers));
        const int s = qRound(r.height() * 0.38);
        p.drawPixmap(QRectF(r.center().x() - s / 2.0, r.center().y() + 1, s, s),
                     IkStyle::pixmap(QStringLiteral("layers"), s, IkStyle::color(IkStyle::Text, palette()), devicePixelRatioF()),
                     QRectF(0, 0, s * devicePixelRatioF(), s * devicePixelRatioF()));
    }
};

/// Background colour chip (white rectangle in the mockup); checkerboard when transparent.
/// Background colour button that also previews the canvas shape: a page
/// with the width/height ratio of the canvas, filled with the background.
class IkBackgroundChip : public QAbstractButton
{
public:
    explicit IkBackgroundChip(QWidget *parent) : QAbstractButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFixedSize(IkStyle::scaled(96), IkStyle::touchSize() + IkStyle::scaled(8));
        setToolTip(i18n("Background"));
    }
    QColor color {Qt::white};

    void setAspect(qreal widthOverHeight)
    {
        if (widthOverHeight > 0 && !qFuzzyCompare(m_aspect, widthOverHeight)) {
            m_aspect = widthOverHeight;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(3, 3, -3, -3);
        QSizeF s(box.height() * m_aspect, box.height());
        if (s.width() > box.width()) s = QSizeF(box.width(), box.width() / m_aspect);
        s = s.expandedTo(QSizeF(3, 3));
        QRectF r(QPointF(), s);
        r.moveCenter(box.center());
        r = QRectF(qRound(r.x()), qRound(r.y()), qRound(r.width()), qRound(r.height()));
        IkStyle::drawShadow(p, r, 2, 6, 60);
        if (color.alpha() < 255) {
            const int c = 6;
            p.save();
            p.setClipRect(r);
            for (qreal y = r.top(); y < r.bottom(); y += c)
                for (qreal x = r.left(); x < r.right(); x += c)
                    p.fillRect(QRectF(x, y, c, c), (int((x - r.left()) / c + (y - r.top()) / c) % 2) ? QColor(200, 200, 200) : QColor(245, 245, 245));
            p.restore();
        }
        p.fillRect(r, color);
        if (isDown() || underMouse()) p.fillRect(r, QColor(0, 0, 0, isDown() ? 40 : 15));
    }

private:
    qreal m_aspect {1.0};
};

class IkNewCanvasSheet : public QWidget
{
public:
    IkNewCanvasSheet(KisMainWindow *mw, IkPopover *popover)
        : QWidget(popover), m_mw(mw), m_popover(popover)
    {
        KisConfig cfg(true);
        QVBoxLayout *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        l->addWidget(sheetTitle(i18n("New Canvas"), this, [this]() { m_popover->dismiss(); }));
        l->addWidget(divider(this));

        QWidget *body = new QWidget(this);
        QVBoxLayout *b = new QVBoxLayout(body);
        b->setContentsMargins(18, 22, 18, 22);
        b->setSpacing(10);

        m_preset = new IkChoiceRow(i18n("Preset"), body);
        m_preset->setCentered(true);
        QStringList names;
        m_presets = presets();
        for (const Preset &p : m_presets) names << QStringLiteral("%1   %2 × %3").arg(p.name).arg(p.w).arg(p.h);
        const QImage clip = QApplication::clipboard()->image();
        if (!clip.isNull()) {
            m_presets << Preset{i18n("Clipboard Image"), clip.width(), clip.height(), 300};
            names << QStringLiteral("%1   %2 × %3").arg(i18n("Clipboard Image")).arg(clip.width()).arg(clip.height());
            m_clipboardIndex = m_presets.size() - 1;
        }
        m_preset->setChoices(names, -1);
        connect(m_preset, &IkChoiceRow::chosen, this, [this](int i) { applyPreset(i); });
        b->addWidget(m_preset);

        QLabel *range = new QLabel(i18n("Range (10 - %1px)", maxCanvasSide()), body);
        range->setFont(IkStyle::font(0.85, QFont::DemiBold));
        b->addWidget(range);

        QGridLayout *g = new QGridLayout();
        g->setHorizontalSpacing(12);
        g->setVerticalSpacing(14);
        m_width = new IkNumberRow(i18n("Width"), body);
        m_height = new IkNumberRow(i18n("Height"), body);
        g->addWidget(m_width, 0, 0);
        g->addWidget(m_height, 1, 0);
        m_unit = new IkChoiceRow(i18n("px"), body);
        m_unit->setCentered(true);
        m_unit->setChoices({QStringLiteral("px"), QStringLiteral("in"), QStringLiteral("cm"), QStringLiteral("mm")}, 0);
        m_unit->setFixedWidth(IkStyle::touchSize() + 40);
        connect(m_unit, &IkChoiceRow::chosen, this, [this](int i) {
            const int w = widthPx(), h = heightPx();
            m_unitIndex = i;
            static const char *u[4] = {"px", "in", "cm", "mm"};
            // The row's label is its value here.
            m_unit->setChoices({QStringLiteral("px"), QStringLiteral("in"), QStringLiteral("cm"), QStringLiteral("mm")}, i);
            Q_UNUSED(u);
            setSizePx(w, h);
        });
        g->addWidget(m_unit, 0, 1);
        m_budget = new IkLayerBudget(body);
        g->addWidget(m_budget, 1, 1, Qt::AlignCenter);
        b->addLayout(g);

        QLabel *ppiRange = new QLabel(i18n("Range (20 - 600)"), body);
        ppiRange->setFont(IkStyle::font(0.85, QFont::DemiBold));
        b->addSpacing(6);
        b->addWidget(ppiRange);
        QHBoxLayout *ppiRow = new QHBoxLayout();
        ppiRow->setSpacing(12);
        m_ppi = new IkNumberRow(i18n("PPI"), body);
        m_ppi->setRange(20, 600, 0);
        ppiRow->addWidget(m_ppi, 1);
        m_bg = new IkBackgroundChip(body);
        m_bg->color = cfg.defaultBackgroundColor();
        m_bg->color.setAlpha(cfg.defaultBackgroundOpacity());
        connect(m_bg, &QAbstractButton::clicked, this, [this]() {
            QMenu menu(this);
            QAction *white = menu.addAction(i18n("White"));
            QAction *transparent = menu.addAction(i18n("Transparent"));
            QAction *pick = menu.addAction(i18n("Choose Colour..."));
            QAction *picked = menu.exec(m_bg->mapToGlobal(QPoint(0, m_bg->height())));
            if (picked == white) m_bg->color = Qt::white;
            else if (picked == transparent) m_bg->color = QColor(255, 255, 255, 0);
            else if (picked == pick) {
                const QColor c = QColorDialog::getColor(m_bg->color, this, i18n("Background"));
                if (c.isValid()) m_bg->color = c;
            }
            m_bg->update();
        });
        ppiRow->addWidget(m_bg);
        b->addLayout(ppiRow);

        IkTextButton *advanced = new IkTextButton(i18n("More options (colour model, depth, profile)..."), IkTextButton::Link, body);
        advanced->setFontScale(0.9);
        connect(advanced, &QAbstractButton::clicked, this, [this]() {
            KisMainWindow *mw = m_mw;
            m_popover->dismiss();
            IkNewCanvasDialog::showAdvanced(mw);
        });
        b->addSpacing(6);
        b->addWidget(advanced);
        l->addWidget(body, 1);

        l->addWidget(divider(this));
        IkTextButton *confirm = confirmButton(this);
        connect(confirm, &QAbstractButton::clicked, this, [this]() { create(); });
        l->addWidget(confirm);

        for (IkNumberRow *r : {m_width, m_height}) {
            connect(r, &IkNumberRow::valueChanged, this, [this](double) { updateBudget(); });
        }
        connect(m_ppi, &IkNumberRow::valueChanged, this, [this](double) {
            // Keep the pixel size when the resolution changes in pixel units.
            if (m_unitIndex != 0) setSizePx(m_lastW, m_lastH);
        });

        m_ppi->setValue(qBound(20.0, cfg.defImageResolution() * 72.0, 600.0));
        m_unitIndex = 0;
        setSizePx(cfg.defImageWidth(), cfg.defImageHeight());
    }

    void applyPreset(int i)
    {
        if (i < 0 || i >= m_presets.size()) return;
        const Preset &p = m_presets[i];
        m_ppi->setValue(p.ppi);
        setSizePx(p.w, p.h);
    }

    double unitFactor() const
    {
        const double ppi = qMax(1.0, m_ppi->value());
        switch (m_unitIndex) {
        case 1: return ppi;              // px per inch
        case 2: return ppi / 2.54;       // px per cm
        case 3: return ppi / 25.4;       // px per mm
        default: return 1.0;
        }
    }

    int widthPx() const { return qBound(10, qRound(m_width->value() * unitFactor()), maxCanvasSide()); }
    int heightPx() const { return qBound(10, qRound(m_height->value() * unitFactor()), maxCanvasSide()); }

    void setSizePx(int w, int h)
    {
        m_lastW = w;
        m_lastH = h;
        const double f = unitFactor();
        const int decimals = m_unitIndex == 0 ? 0 : 2;
        m_width->setRange(10 / f, maxCanvasSide() / f, decimals);
        m_height->setRange(10 / f, maxCanvasSide() / f, decimals);
        m_width->setValue(w / f);
        m_height->setValue(h / f);
        updateBudget();
    }

    void updateBudget()
    {
        if (m_bg && heightPx() > 0) m_bg->setAspect(qreal(widthPx()) / heightPx());
        m_lastW = widthPx();
        m_lastH = heightPx();
        m_budget->layers = IkNewCanvasDialog::estimatedMaxLayers(m_lastW, m_lastH);
        m_budget->update();
    }

    void create()
    {
        const int w = widthPx(), h = heightPx();
        const double ppi = m_ppi->value();
        KisConfig cfg(false);
        cfg.defImageWidth(w);
        cfg.defImageHeight(h);
        cfg.defImageResolution(ppi / 72.0);
        cfg.setDefaultBackgroundColor(QColor(m_bg->color.rgb()));
        cfg.setDefaultBackgroundOpacity(quint8(m_bg->color.alpha()));

        const KoColorSpace *cs = KoColorSpaceRegistry::instance()->colorSpace(cfg.defColorModel(), cfg.defaultColorDepth(),
                                                                               cfg.defColorProfile());
        if (!cs) cs = KoColorSpaceRegistry::instance()->rgb8();
        KisDocument *doc = KisPart::instance()->createDocument();
        const KoColor bg(m_bg->color, cs);
        const bool transparent = m_bg->color.alpha() == 0;
        doc->newImage(i18n("Untitled"), w, h, cs, bg, transparent ? KisConfig::CANVAS_COLOR : KisConfig::RASTER_LAYER,
                      transparent ? 1 : qMax(1, cfg.numDefaultLayers()), QString(), ppi / 72.0);
        const bool fromClipboard = m_preset->currentIndex() == m_clipboardIndex && m_clipboardIndex >= 0;
        // Close the sheet first: opening the document restores the window
        // layout, which must not find focus inside a sheet that is going away.
        if (QWidget *f = QApplication::focusWidget()) {
            if (m_popover->isAncestorOf(f)) f->clearFocus();
        }
        m_popover->dismiss();
        if (fromClipboard) {
            // Clipboard preset: paste the clipboard into the new canvas.
            QPointer<KisMainWindow> mw = m_mw;
            KisPart::instance()->startCustomDocument(doc);
            if (mw) QMetaObject::invokeMethod(mw, [mw]() {
                if (QAction *paste = mw->actionCollection()->action(QStringLiteral("edit_paste"))) paste->trigger();
            }, Qt::QueuedConnection);
        } else {
            KisPart::instance()->startCustomDocument(doc);
        }
    }

private:
    QPointer<KisMainWindow> m_mw;
    IkPopover *m_popover;
    QList<Preset> m_presets;
    int m_clipboardIndex {-1};
    IkChoiceRow *m_preset {nullptr};
    IkNumberRow *m_width {nullptr};
    IkNumberRow *m_height {nullptr};
    IkChoiceRow *m_unit {nullptr};
    int m_unitIndex {0};
    IkNumberRow *m_ppi {nullptr};
    IkLayerBudget *m_budget {nullptr};
    IkBackgroundChip *m_bg {nullptr};
    int m_lastW {1920}, m_lastH {1080};
};

class IkTemplateSheet : public QWidget
{
public:
    IkTemplateSheet(IkPopover *popover)
        : QWidget(popover), m_popover(popover)
    {
        QVBoxLayout *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        l->addWidget(sheetTitle(i18n("Template"), this, [this]() { m_popover->dismiss(); }));
        l->addWidget(divider(this));

        QHBoxLayout *body = new QHBoxLayout();
        body->setSpacing(0);
        m_groups = new QListWidget(this);
        m_templates = new QListWidget(this);
        for (QListWidget *w : {m_groups, m_templates}) {
            w->setFrameShape(QFrame::NoFrame);
            w->setFont(IkStyle::font(1.2));
            w->setIconSize(QSize(26, 26));
            w->setSpacing(2);
            IkStyle::makeTransparent(w);
            QPalette lp = w->palette();
            lp.setColor(QPalette::Highlight, IkStyle::color(IkStyle::Raised));
            lp.setColor(QPalette::HighlightedText, IkStyle::color(IkStyle::Text));
            w->setPalette(lp);
            w->setUniformItemSizes(true);
            QScroller::grabGesture(w->viewport(), IkStyle::touchFirst() ? QScroller::LeftMouseButtonGesture : QScroller::TouchGesture);
        }
        m_groups->setFixedWidth(IkStyle::scaled(200));
        body->addWidget(m_groups);
        IkDivider *vline = new IkDivider(this);
        vline->setFixedHeight(QWIDGETSIZE_MAX);
        vline->setMinimumHeight(0);
        vline->setFixedWidth(1);
        body->addWidget(vline);
        body->addWidget(m_templates, 1);
        l->addLayout(body, 1);
        l->addWidget(divider(this));
        IkTextButton *confirm = confirmButton(this);
        connect(confirm, &QAbstractButton::clicked, this, [this]() { openSelected(); });
        l->addWidget(confirm);

        // Category icons by group name.
        auto groupIcon = [](const QString &name) {
            const QString n = name.toLower();
            if (n.contains(QLatin1String("anim"))) return QStringLiteral("rotate");
            if (n.contains(QLatin1String("comic"))) return QStringLiteral("text");
            if (n.contains(QLatin1String("design"))) return QStringLiteral("grid");
            if (n.contains(QLatin1String("dslr"))) return QStringLiteral("reference");
            if (n.contains(QLatin1String("texture"))) return QStringLiteral("texture");
            return QStringLiteral("template");
        };
        KisTemplateTree tree(QStringLiteral("templates/"), true);
        for (KisTemplateGroup *g : tree.groups()) {
            if (g->isHidden()) continue;
            QStringList names, files;
            for (KisTemplate *t : g->templates()) {
                if (t->isHidden()) continue;
                names << t->name();
                files << t->file();
            }
            if (names.isEmpty()) continue;
            QString title = g->name();
            title.remove(QRegularExpression(QStringLiteral("\\s*Templates?$"), QRegularExpression::CaseInsensitiveOption));
            QListWidgetItem *item = new QListWidgetItem(IkStyle::icon(groupIcon(g->name())), title, m_groups);
            item->setSizeHint(QSize(0, IkStyle::touchSize() + 4));
            item->setData(Qt::UserRole, names);
            item->setData(Qt::UserRole + 1, files);
        }
        connect(m_groups, &QListWidget::currentRowChanged, this, [this](int row) {
            m_templates->clear();
            QListWidgetItem *g = m_groups->item(row);
            if (!g) return;
            const QStringList names = g->data(Qt::UserRole).toStringList();
            const QStringList files = g->data(Qt::UserRole + 1).toStringList();
            for (int i = 0; i < names.size(); ++i) {
                QListWidgetItem *t = new QListWidgetItem(names[i], m_templates);
                t->setSizeHint(QSize(0, IkStyle::touchSize() + 4));
                t->setData(Qt::UserRole, files.value(i));
            }
            m_templates->setCurrentRow(0);
        });
        connect(m_templates, &QListWidget::itemDoubleClicked, this, [this]() { openSelected(); });
        m_groups->setCurrentRow(0);
    }

    void openSelected()
    {
        QListWidgetItem *t = m_templates->currentItem();
        if (!t) return;
        const QString file = t->data(Qt::UserRole).toString();
        m_popover->dismiss();
        KisPart::instance()->openTemplate(QUrl::fromLocalFile(file));
    }

private:
    IkPopover *m_popover;
    QListWidget *m_groups {nullptr};
    QListWidget *m_templates {nullptr};
};

IkPopover *makeSheet(KisMainWindow *mw)
{
    IkPopover *pop = new IkPopover(mw);
    pop->setDimBackground(true);
    pop->setRadius(14);
    QObject::connect(pop, &IkPopover::dismissed, pop, &QObject::deleteLater);
    return pop;
}
}

namespace IkNewCanvasDialog
{
int estimatedMaxLayers(int width, int height)
{
    const qint64 budgetMiB = KisImageConfig(true).tilesHardLimit();
    const qint64 layerBytes = qint64(width) * height * 4;
    if (layerBytes <= 0) return 0;
    // The projection and the undo history need roughly two more layers' worth.
    return int(qMax<qint64>(1, budgetMiB * 1024 * 1024 / layerBytes - 2));
}

void showNewCanvas(KisMainWindow *mw)
{
    if (!mw) return;
    IkPopover *pop = makeSheet(mw);
    pop->setContent(new IkNewCanvasSheet(mw, pop));
    pop->popup(nullptr, IkPopover::Center, QSize(400, 560));
}

void showTemplates(KisMainWindow *mw)
{
    if (!mw) return;
    IkPopover *pop = makeSheet(mw);
    pop->setContent(new IkTemplateSheet(pop));
    pop->popup(nullptr, IkPopover::Center, QSize(560, 560));
}

void showAdvanced(KisMainWindow *mw)
{
    KisDlgCreateNewDocument *dlg = new KisDlgCreateNewDocument(mw);
    dlg->exec();
    dlg->deleteLater();
}
}
