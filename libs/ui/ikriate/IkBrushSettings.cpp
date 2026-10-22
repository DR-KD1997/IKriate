/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkBrushSettings.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QDomDocument>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <QScrollArea>
#include <QScroller>
#include <QStackedWidget>
#include <QTimer>
#include <QtMath>

#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <KoCompositeOpRegistry.h>
#include <KoToolBase.h>
#include <KoToolManager.h>
#include <klocalizedstring.h>

#include <brushengine/kis_locked_properties_proxy.h>
#include <brushengine/kis_locked_properties_server.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_settings.h>

#include "KisViewManager.h"
#include "kis_canvas2.h"
#include "kis_canvas_resource_provider.h"

#include "IkInterfaceManager.h"
#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
// ---- reading and writing settings ---------------------------------------------

KisPaintOpSettingsSP currentSettings(IkInterfaceManager *m)
{
    KisViewManager *vm = m ? m->viewManager() : nullptr;
    if (!vm || !vm->canvasResourceProvider()) return KisPaintOpSettingsSP();
    KisPaintOpPresetSP preset = vm->canvasResourceProvider()->currentPreset();
    return preset ? preset->settings() : KisPaintOpSettingsSP();
}

void writeProperty(KisPaintOpSettingsSP s, const QString &key, const QVariant &value)
{
    KisLockedPropertiesProxySP proxy(KisLockedPropertiesServer::instance()->createLockedPropertiesProxy(s.data()));
    proxy->setProperty(key, value);
}

bool isAutoBrush(KisPaintOpSettingsSP s)
{
    if (!s || !s->hasProperty(QStringLiteral("brush_definition"))) return false;
    QDomDocument doc;
    doc.setContent(s->getString(QStringLiteral("brush_definition")));
    return doc.documentElement().attribute(QStringLiteral("type")) == QLatin1String("auto_brush");
}

bool hasBrushDefinition(KisPaintOpSettingsSP s)
{
    return s && s->hasProperty(QStringLiteral("brush_definition"));
}

/// Attribute of the <Brush> element (@p mask false) or its <MaskGenerator>.
QString brushAttr(KisPaintOpSettingsSP s, bool mask, const QString &attr)
{
    if (!hasBrushDefinition(s)) return QString();
    QDomDocument doc;
    doc.setContent(s->getString(QStringLiteral("brush_definition")));
    QDomElement e = doc.documentElement();
    if (mask) e = e.elementsByTagName(QStringLiteral("MaskGenerator")).item(0).toElement();
    return e.attribute(attr);
}

void setBrushAttrs(KisPaintOpSettingsSP s, bool mask, const QList<QPair<QString, QString>> &attrs)
{
    if (!hasBrushDefinition(s)) return;
    QDomDocument doc;
    doc.setContent(s->getString(QStringLiteral("brush_definition")));
    QDomElement e = doc.documentElement();
    if (mask) e = e.elementsByTagName(QStringLiteral("MaskGenerator")).item(0).toElement();
    if (e.isNull()) return;
    for (const auto &a : attrs) e.setAttribute(a.first, a.second);
    writeProperty(s, QStringLiteral("brush_definition"), doc.toString());
}

/// Lowest size under the lightest pressure, as a fraction of the full size
/// (1 = pressure does not change the size).
qreal sizeRange(KisPaintOpSettingsSP s)
{
    if (!s->getBool(QStringLiteral("PressureSize"), false)) return 1.0;
    QDomDocument doc;
    doc.setContent(s->getString(QStringLiteral("SizeSensor")));
    const QDomNodeList curves = doc.elementsByTagName(QStringLiteral("curve"));
    if (curves.isEmpty()) return 0.0;
    const QString first = curves.item(0).toElement().text().section(QLatin1Char(';'), 0, 0);
    return qBound(0.0, first.section(QLatin1Char(','), 1, 1).toDouble(), 1.0);
}

void setSizeRange(KisPaintOpSettingsSP s, qreal minimum)
{
    if (minimum >= 0.995) {
        writeProperty(s, QStringLiteral("PressureSize"), false);
        return;
    }
    const QString curve = QStringLiteral("0,%1;1,1;").arg(minimum, 0, 'f', 3);
    QDomDocument doc;
    doc.setContent(s->getString(QStringLiteral("SizeSensor")));
    // Edit the pressure sensor's curve, whichever sensor layout is used.
    QDomElement pressure;
    const QDomNodeList all = doc.elementsByTagName(QStringLiteral("*"));
    if (doc.documentElement().attribute(QStringLiteral("id")) == QLatin1String("pressure")) pressure = doc.documentElement();
    for (int i = 0; pressure.isNull() && i < all.size(); ++i) {
        QDomElement e = all.item(i).toElement();
        if (e.attribute(QStringLiteral("id")) == QLatin1String("pressure")) pressure = e;
    }
    QString xml;
    if (pressure.isNull()) {
        xml = QStringLiteral("<!DOCTYPE params>\n<params id=\"pressure\">\n <curve>%1</curve>\n</params>\n").arg(curve);
    } else {
        QDomElement c = pressure.firstChildElement(QStringLiteral("curve"));
        if (c.isNull()) {
            c = doc.createElement(QStringLiteral("curve"));
            pressure.appendChild(c);
        }
        while (c.hasChildNodes()) c.removeChild(c.firstChild());
        c.appendChild(doc.createTextNode(curve));
        xml = doc.toString();
    }
    writeProperty(s, QStringLiteral("SizeSensor"), xml);
    writeProperty(s, QStringLiteral("SizeUseCurve"), true);
    writeProperty(s, QStringLiteral("PressureSize"), true);
}

KoToolBase *brushTool(IkInterfaceManager *m)
{
    KisViewManager *vm = m ? m->viewManager() : nullptr;
    if (!vm || !vm->canvasBase()) return nullptr;
    return KoToolManager::instance()->toolById(vm->canvasBase(), QStringLiteral("KritaShape/KisToolBrush"));
}

// ---- rows -----------------------------------------------------------------------

/// "Label            value" over a slider.
class IkSliderRow : public QWidget
{
public:
    IkSliderRow(const QString &label, QWidget *parent) : QWidget(parent)
    {
        QVBoxLayout *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 2, 0, 2);
        l->setSpacing(2);
        QHBoxLayout *top = new QHBoxLayout();
        QLabel *name = new QLabel(label, this);
        name->setFont(IkStyle::font(0.95));
        top->addWidget(name);
        top->addStretch(1);
        value = new QLabel(this);
        value->setFont(IkStyle::font(0.9));
        value->setForegroundRole(QPalette::PlaceholderText);
        top->addWidget(value);
        l->addLayout(top);
        slider = new IkSlider(this);
        l->addWidget(slider);
    }
    IkSlider *slider {nullptr};
    QLabel *value {nullptr};
};

class IkSwitchRow : public QWidget
{
public:
    IkSwitchRow(const QString &label, QWidget *parent) : QWidget(parent)
    {
        QHBoxLayout *l = new QHBoxLayout(this);
        l->setContentsMargins(0, 4, 0, 4);
        QLabel *name = new QLabel(label, this);
        name->setFont(IkStyle::font(0.95));
        l->addWidget(name, 1);
        toggle = new IkSwitch(this);
        l->addWidget(toggle);
    }
    IkSwitch *toggle {nullptr};
};

class IkPickRow : public QWidget
{
public:
    IkPickRow(const QString &label, QWidget *parent) : QWidget(parent)
    {
        QHBoxLayout *l = new QHBoxLayout(this);
        l->setContentsMargins(0, 4, 0, 4);
        QLabel *name = new QLabel(label, this);
        name->setFont(IkStyle::font(0.95));
        l->addWidget(name);
        l->addStretch(1);
        choice = new IkChoiceRow(QString(), this);
        choice->setCentered(true);
        choice->setMinimumWidth(IkStyle::scaled(160));
        choice->setMinimumHeight(IkStyle::touchSize() - 4);
        l->addWidget(choice);
    }
    IkChoiceRow *choice {nullptr};
};

struct Binding {
    QWidget *row {nullptr};
    std::function<bool(KisPaintOpSettingsSP )> applies;
    std::function<void(KisPaintOpSettingsSP )> read;
};
}

struct IkBrushSettingsPanel::Private
{
    QPointer<IkInterfaceManager> manager;
    QPointer<QWidget> editor;
    QWidget *previewHost {nullptr};
    QStackedWidget *pages {nullptr};
    IkTabBar *tabs {nullptr};
    QList<Binding> bindings;
    bool reading {false};
};

IkBrushSettingsPanel::IkBrushSettingsPanel(IkInterfaceManager *manager, QWidget *editor, QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->manager = manager;
    d->editor = editor;
    setObjectName(QStringLiteral("IkBrushSettingsPanel"));
    QVBoxLayout *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);

    // ---- header: close, save as new, reset, save -------------------------------
    IkPanel *header = new IkPanel(this);
    header->setRole(IkStyle::Raised);
    header->setRadius(0);
    QVBoxLayout *hl = new QVBoxLayout(header);
    hl->setContentsMargins(10, 6, 10, 6);
    QHBoxLayout *buttons = new QHBoxLayout();
    auto click = [this](const char *name) {
        if (!d->editor) return;
        if (QAbstractButton *b = d->editor->findChild<QAbstractButton *>(QString::fromLatin1(name))) b->click();
    };
    IkIconButton *close = new IkIconButton(QStringLiteral("close"), i18n("Close"), header);
    connect(close, &QAbstractButton::clicked, this, [this]() {
        if (d->editor) d->editor->hide();
    });
    buttons->addWidget(close);
    buttons->addStretch(1);
    IkIconButton *saveNew = new IkIconButton(QStringLiteral("share"), i18n("Save as a new brush"), header);
    connect(saveNew, &QAbstractButton::clicked, this, [click]() { click("saveNewBrushPresetButton"); });
    buttons->addWidget(saveNew);
    IkIconButton *reset = new IkIconButton(QStringLiteral("wand"), i18n("Undo all changes to this brush"), header);
    connect(reset, &QAbstractButton::clicked, this, [this, click]() {
        click("reloadPresetButton");
        QTimer::singleShot(0, this, [this]() { reload(); });
    });
    buttons->addWidget(reset);
    IkIconButton *save = new IkIconButton(QStringLiteral("check"), i18n("Save changes to this brush"), header);
    connect(save, &QAbstractButton::clicked, this, [click]() { click("saveBrushPresetButton"); });
    buttons->addWidget(save);
    hl->addLayout(buttons);
    d->previewHost = new QWidget(header);
    QVBoxLayout *ph = new QVBoxLayout(d->previewHost);
    ph->setContentsMargins(0, 0, 0, 0);
    d->previewHost->setMinimumHeight(IkStyle::scaled(70));
    hl->addWidget(d->previewHost);
    l->addWidget(header);

    // ---- pages ------------------------------------------------------------------
    d->pages = new QStackedWidget(this);
    l->addWidget(d->pages, 1);
    auto page = [this](QVBoxLayout *&layout) {
        QScrollArea *scroll = new QScrollArea(d->pages);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        IkStyle::makeTransparent(scroll);
        QScroller::grabGesture(scroll->viewport(), IkStyle::touchFirst() ? QScroller::LeftMouseButtonGesture : QScroller::TouchGesture);
        QWidget *w = new QWidget(scroll);
        layout = new QVBoxLayout(w);
        layout->setContentsMargins(IkStyle::scaled(16), IkStyle::scaled(12), IkStyle::scaled(16), IkStyle::scaled(12));
        layout->setSpacing(IkStyle::scaled(12));
        scroll->setWidget(w);
        d->pages->addWidget(scroll);
        return w;
    };

    // A slider bound to a value: t in 0..1 <-> value via the two maps.
    auto addSlider = [this](QVBoxLayout *pl, QWidget *host, const QString &label,
                            std::function<bool(KisPaintOpSettingsSP )> applies,
                            std::function<qreal(KisPaintOpSettingsSP )> get,
                            std::function<void(KisPaintOpSettingsSP , qreal)> set,
                            std::function<qreal(qreal)> toSlider, std::function<qreal(qreal)> fromSlider,
                            std::function<QString(qreal)> text) {
        IkSliderRow *row = new IkSliderRow(label, host);
        pl->addWidget(row);
        connect(row->slider, &IkSlider::valueEdited, this, [this, row, set, fromSlider, text](qreal t) {
            if (d->reading) return;
            const qreal v = fromSlider(t);
            row->value->setText(text(v));
            if (KisPaintOpSettingsSP s = currentSettings(d->manager)) set(s, v);
        });
        d->bindings << Binding{row, applies, [row, get, toSlider, text](KisPaintOpSettingsSP s) {
                                   const qreal v = get(s);
                                   row->slider->setValue(toSlider(v));
                                   row->value->setText(text(v));
                               }};
    };
    auto addSwitch = [this](QVBoxLayout *pl, QWidget *host, const QString &label,
                            std::function<bool(KisPaintOpSettingsSP )> applies,
                            std::function<bool(KisPaintOpSettingsSP )> get,
                            std::function<void(KisPaintOpSettingsSP , bool)> set) {
        IkSwitchRow *row = new IkSwitchRow(label, host);
        row->toggle->setCheckable(true);
        pl->addWidget(row);
        connect(row->toggle, &QAbstractButton::toggled, this, [this, set](bool on) {
            if (d->reading) return;
            if (KisPaintOpSettingsSP s = currentSettings(d->manager)) set(s, on);
        });
        d->bindings << Binding{row, applies, [row, get](KisPaintOpSettingsSP s) { row->toggle->setChecked(get(s)); }};
    };
    auto addChoice = [this](QVBoxLayout *pl, QWidget *host, const QString &label, const QStringList &names,
                            std::function<bool(KisPaintOpSettingsSP )> applies,
                            std::function<int(KisPaintOpSettingsSP )> get,
                            std::function<void(KisPaintOpSettingsSP , int)> set) {
        IkPickRow *row = new IkPickRow(label, host);
        row->choice->setChoices(names, 0);
        pl->addWidget(row);
        connect(row->choice, &IkChoiceRow::chosen, this, [this, set](int i) {
            if (d->reading) return;
            if (KisPaintOpSettingsSP s = currentSettings(d->manager)) set(s, i);
        });
        d->bindings << Binding{row, applies, [row, get](KisPaintOpSettingsSP s) { row->choice->setCurrentIndex(qMax(0, get(s))); }};
    };

    auto always = [](KisPaintOpSettingsSP ) { return true; };
    auto brushBased = [](KisPaintOpSettingsSP s) { return hasBrushDefinition(s); };
    auto autoBrush = [](KisPaintOpSettingsSP s) { return isAutoBrush(s); };
    auto has = [](const char *key) {
        const QString k = QString::fromLatin1(key);
        return [k](KisPaintOpSettingsSP s) { return s->hasProperty(k); };
    };
    auto linear = [](qreal lo, qreal hi) {
        return std::make_pair(std::function<qreal(qreal)>([lo, hi](qreal v) { return (v - lo) / (hi - lo); }),
                              std::function<qreal(qreal)>([lo, hi](qreal t) { return lo + t * (hi - lo); }));
    };
    auto percent = [](qreal v) { return QStringLiteral("%1").arg(qRound(v * 100)); };
    auto attrGet = [](bool mask, const char *attr, qreal def) {
        const QString a = QString::fromLatin1(attr);
        return std::function<qreal(KisPaintOpSettingsSP )>([mask, a, def](KisPaintOpSettingsSP s) {
            bool ok = false;
            const qreal v = brushAttr(s, mask, a).toDouble(&ok);
            return ok ? v : def;
        });
    };
    auto attrSet = [](bool mask, const char *attr) {
        const QString a = QString::fromLatin1(attr);
        return std::function<void(KisPaintOpSettingsSP , qreal)>([mask, a](KisPaintOpSettingsSP s, qreal v) {
            setBrushAttrs(s, mask, {{a, QString::number(v, 'g', 6)}});
        });
    };

    // ---- Stroke -----------------------------------------------------------------
    QVBoxLayout *pl = nullptr;
    QWidget *w = page(pl);
    addSlider(pl, w, i18n("Size"), always, [](KisPaintOpSettingsSP s) { return s->paintOpSize(); },
              [](KisPaintOpSettingsSP s, qreal v) { s->setPaintOpSize(v); },
              [](qreal v) { return qBound(0.0, std::log(qMax(1.0, v)) / std::log(1000.0), 1.0); },
              [](qreal t) { return qMax<qreal>(1.0, std::pow(1000.0, t)); },
              [](qreal v) { return v < 10 ? QStringLiteral("%1px").arg(v, 0, 'f', 1) : QStringLiteral("%1px").arg(qRound(v)); });
    {
        auto lin = linear(0.0, 1.0);
        addSlider(pl, w, i18n("Opacity"), always, [](KisPaintOpSettingsSP s) { return s->paintOpOpacity(); },
                  [](KisPaintOpSettingsSP s, qreal v) { s->setPaintOpOpacity(v); }, lin.first, lin.second, percent);
        addSlider(pl, w, i18n("Flow"), has("FlowValue"), [](KisPaintOpSettingsSP s) { return s->paintOpFlow(); },
                  [](KisPaintOpSettingsSP s, qreal v) { s->setPaintOpFlow(v); }, lin.first, lin.second, percent);
    }
    addSlider(pl, w, i18n("Smoothness"), always,
              [this](KisPaintOpSettingsSP ) {
                  KoToolBase *t = brushTool(d->manager);
                  if (!t || t->property("smoothingType").toInt() != 2) return 0.0;
                  return qBound(0.0, t->property("smoothnessQualityMax").toInt() / 300.0, 1.0);
              },
              [this](KisPaintOpSettingsSP , qreal v) {
                  KoToolBase *t = brushTool(d->manager);
                  if (!t) return;
                  // 0: Krita's light default smoothing; above: weighted, longer distance.
                  t->setProperty("smoothingType", v < 0.01 ? 1 : 2);
                  if (v >= 0.01) {
                      t->setProperty("smoothnessQualityMin", qRound(v * 300));
                      t->setProperty("smoothnessQualityMax", qRound(v * 300));
                  }
              },
              [](qreal v) { return v; }, [](qreal t) { return t; }, percent);
    addSlider(pl, w, i18n("Size range"), has("SizeSensor"), sizeRange, setSizeRange, [](qreal v) { return 1.0 - v; },
              [](qreal t) { return 1.0 - t; }, [](qreal v) { return v >= 0.995 ? i18n("Off") : QStringLiteral("%1×").arg(v, 0, 'f', 2); });
    addSlider(pl, w, i18n("Spacing"), brushBased, attrGet(false, "spacing", 0.1),
              [](KisPaintOpSettingsSP s, qreal v) {
                  setBrushAttrs(s, false, {{QStringLiteral("spacing"), QString::number(v, 'g', 4)},
                                           {QStringLiteral("useAutoSpacing"), QStringLiteral("0")}});
              },
              [](qreal v) { return qBound(0.0, std::log(v / 0.02) / std::log(5.0 / 0.02), 1.0); },
              [](qreal t) { return 0.02 * std::pow(5.0 / 0.02, t); }, [](qreal v) { return QString::number(v, 'f', 2); });
    pl->addStretch(1);

    // ---- Head -----------------------------------------------------------------
    w = page(pl);
    addChoice(pl, w, i18n("Shape"), {i18n("Circle"), i18n("Square")}, autoBrush,
              [](KisPaintOpSettingsSP s) { return brushAttr(s, true, QStringLiteral("type")) == QLatin1String("rect") ? 1 : 0; },
              [](KisPaintOpSettingsSP s, int i) {
                  setBrushAttrs(s, true, {{QStringLiteral("type"), i == 1 ? QStringLiteral("rect") : QStringLiteral("circle")}});
              });
    addChoice(pl, w, i18n("Mask Type"), {i18n("Default"), i18n("Soft"), i18n("Gaussian")}, autoBrush,
              [](KisPaintOpSettingsSP s) {
                  const QString id = brushAttr(s, true, QStringLiteral("id"));
                  return id == QLatin1String("soft") ? 1 : id == QLatin1String("gauss") ? 2 : 0;
              },
              [](KisPaintOpSettingsSP s, int i) {
                  const char *ids[] = {"default", "soft", "gauss"};
                  setBrushAttrs(s, true, {{QStringLiteral("id"), QString::fromLatin1(ids[qBound(0, i, 2)])}});
              });
    addSlider(pl, w, i18n("Angle"), brushBased, [](KisPaintOpSettingsSP s) { return s->paintOpAngle(); },
              [](KisPaintOpSettingsSP s, qreal v) { s->setPaintOpAngle(v); }, [](qreal v) { return qBound(0.0, v / 360.0, 1.0); },
              [](qreal t) { return qRound(t * 360.0) * 1.0; }, [](qreal v) { return QStringLiteral("%1°").arg(qRound(v)); });
    {
        auto lin = linear(0.01, 1.0);
        addSlider(pl, w, i18n("Ratio"), autoBrush, attrGet(true, "ratio", 1.0), attrSet(true, "ratio"), lin.first, lin.second,
                  [](qreal v) { return QString::number(v, 'f', 2); });
    }
    {
        auto lin = linear(0.0, 1.0);
        addSlider(pl, w, i18n("Hardness"), autoBrush, attrGet(true, "hfade", 1.0),
                  [](KisPaintOpSettingsSP s, qreal v) {
                      setBrushAttrs(s, true, {{QStringLiteral("hfade"), QString::number(v, 'g', 4)},
                                              {QStringLiteral("vfade"), QString::number(v, 'g', 4)}});
                  },
                  lin.first, lin.second, percent);
        addSlider(pl, w, i18n("Density"), brushBased, attrGet(false, "density", 1.0), attrSet(false, "density"), lin.first, lin.second, percent);
        addSlider(pl, w, i18n("Randomness"), brushBased, attrGet(false, "randomness", 0.0), attrSet(false, "randomness"), lin.first,
                  lin.second, percent);
    }
    {
        auto lin = linear(2, 20);
        addSlider(pl, w, i18n("Spikes"), autoBrush, attrGet(true, "spikes", 2), [](KisPaintOpSettingsSP s, qreal v) {
                      setBrushAttrs(s, true, {{QStringLiteral("spikes"), QString::number(qRound(v))}});
                  },
                  lin.first, [lin](qreal t) { return qRound(lin.second(t)) * 1.0; }, [](qreal v) { return QString::number(qRound(v)); });
    }
    addSwitch(pl, w, i18n("Anti-aliasing"), autoBrush,
              [](KisPaintOpSettingsSP s) { return brushAttr(s, true, QStringLiteral("antialiasEdges")) != QLatin1String("0"); },
              [](KisPaintOpSettingsSP s, bool on) {
                  setBrushAttrs(s, true, {{QStringLiteral("antialiasEdges"), on ? QStringLiteral("1") : QStringLiteral("0")}});
              });
    pl->addStretch(1);

    // ---- Texture ----------------------------------------------------------------
    w = page(pl);
    auto textured = [](KisPaintOpSettingsSP s) {
        return s->hasProperty(QStringLiteral("Texture/Pattern/Enabled"));
    };
    addSwitch(pl, w, i18n("Paper texture"), textured,
              [](KisPaintOpSettingsSP s) { return s->getBool(QStringLiteral("Texture/Pattern/Enabled"), false); },
              [](KisPaintOpSettingsSP s, bool on) { writeProperty(s, QStringLiteral("Texture/Pattern/Enabled"), on); });
    {
        auto scale = linear(0.1, 2.0);
        addSlider(pl, w, i18n("Scale"), textured,
                  [](KisPaintOpSettingsSP s) { return s->getDouble(QStringLiteral("Texture/Pattern/Scale"), 1.0); },
                  [](KisPaintOpSettingsSP s, qreal v) { writeProperty(s, QStringLiteral("Texture/Pattern/Scale"), v); },
                  scale.first, scale.second, [](qreal v) { return QStringLiteral("%1×").arg(v, 0, 'f', 2); });
        auto signedLin = linear(-1.0, 1.0);
        addSlider(pl, w, i18n("Brightness"), textured,
                  [](KisPaintOpSettingsSP s) { return s->getDouble(QStringLiteral("Texture/Pattern/Brightness"), 0.0); },
                  [](KisPaintOpSettingsSP s, qreal v) { writeProperty(s, QStringLiteral("Texture/Pattern/Brightness"), v); },
                  signedLin.first, signedLin.second, [](qreal v) { return QString::number(v, 'f', 2); });
        auto contrast = linear(0.0, 2.0);
        addSlider(pl, w, i18n("Contrast"), textured,
                  [](KisPaintOpSettingsSP s) { return s->getDouble(QStringLiteral("Texture/Pattern/Contrast"), 1.0); },
                  [](KisPaintOpSettingsSP s, qreal v) { writeProperty(s, QStringLiteral("Texture/Pattern/Contrast"), v); },
                  contrast.first, contrast.second, [](qreal v) { return QString::number(v, 'f', 2); });
    }
    addSwitch(pl, w, i18n("Invert texture"), textured,
              [](KisPaintOpSettingsSP s) { return s->getBool(QStringLiteral("Texture/Pattern/Invert"), false); },
              [](KisPaintOpSettingsSP s, bool on) { writeProperty(s, QStringLiteral("Texture/Pattern/Invert"), on); });
    QLabel *textureNote = new QLabel(i18n("Brushes without a paper texture show no texture settings."), w);
    textureNote->setWordWrap(true);
    textureNote->setForegroundRole(QPalette::PlaceholderText);
    pl->addWidget(textureNote);
    pl->addStretch(1);

    // ---- Paint ------------------------------------------------------------------
    w = page(pl);
    const QStringList ops = {COMPOSITE_OVER, COMPOSITE_MULT, COMPOSITE_SCREEN, COMPOSITE_OVERLAY, COMPOSITE_DARKEN,
                             COMPOSITE_LIGHTEN, COMPOSITE_DODGE, COMPOSITE_BURN, COMPOSITE_ADD, COMPOSITE_COLOR, COMPOSITE_BEHIND};
    QStringList opNames;
    for (const QString &op : ops) opNames << KoCompositeOpRegistry::instance().getKoID(op).name();
    addChoice(pl, w, i18n("Blending"), opNames, always,
              [ops](KisPaintOpSettingsSP s) { return ops.indexOf(s->getString(QStringLiteral("CompositeOp"), COMPOSITE_OVER)); },
              [ops](KisPaintOpSettingsSP s, int i) { s->setPaintOpCompositeOp(ops.value(i, COMPOSITE_OVER)); });
    addChoice(pl, w, i18n("Painting mode"), {i18n("Build up"), i18n("Wash")}, has("PaintOpAction"),
              [](KisPaintOpSettingsSP s) { return s->getInt(QStringLiteral("PaintOpAction"), 1) == 2 ? 1 : 0; },
              [](KisPaintOpSettingsSP s, int i) { writeProperty(s, QStringLiteral("PaintOpAction"), i == 1 ? 2 : 1); });
    addSwitch(pl, w, i18n("Pressure changes opacity"), has("OpacityUseCurve"),
              [](KisPaintOpSettingsSP s) { return s->getBool(QStringLiteral("OpacityUseCurve"), false); },
              [](KisPaintOpSettingsSP s, bool on) { writeProperty(s, QStringLiteral("OpacityUseCurve"), on); });
    addSwitch(pl, w, i18n("Pressure changes flow"), has("FlowUseCurve"),
              [](KisPaintOpSettingsSP s) { return s->getBool(QStringLiteral("FlowUseCurve"), false); },
              [](KisPaintOpSettingsSP s, bool on) { writeProperty(s, QStringLiteral("FlowUseCurve"), on); });
    addSwitch(pl, w, i18n("Airbrush"), has("PaintOpSettings/isAirbrushing"),
              [](KisPaintOpSettingsSP s) { return s->getBool(QStringLiteral("PaintOpSettings/isAirbrushing"), false); },
              [](KisPaintOpSettingsSP s, bool on) { writeProperty(s, QStringLiteral("PaintOpSettings/isAirbrushing"), on); });
    {
        auto lin = linear(1, 100);
        addSlider(pl, w, i18n("Airbrush rate"), has("PaintOpSettings/rate"),
                  [](KisPaintOpSettingsSP s) { return s->getDouble(QStringLiteral("PaintOpSettings/rate"), 20); },
                  [](KisPaintOpSettingsSP s, qreal v) { writeProperty(s, QStringLiteral("PaintOpSettings/rate"), v); },
                  lin.first, lin.second, [](qreal v) { return i18n("%1/s", qRound(v)); });
    }
    pl->addStretch(1);

    // ---- Special ----------------------------------------------------------------
    w = page(pl);
    {
        auto lin = linear(0.0, 5.0);
        addSlider(pl, w, i18n("Scatter"), has("ScatterValue"),
                  [](KisPaintOpSettingsSP s) {
                      return s->getBool(QStringLiteral("PressureScatter"), false) ? s->getDouble(QStringLiteral("ScatterValue"), 0) : 0.0;
                  },
                  [](KisPaintOpSettingsSP s, qreal v) { s->setPaintOpScatter(v); }, lin.first, lin.second,
                  [](qreal v) { return v < 0.01 ? i18n("Off") : QString::number(v, 'f', 2); });
    }
    addSwitch(pl, w, i18n("Mirror left and right"), has("HorizontalMirrorEnabled"),
              [](KisPaintOpSettingsSP s) {
                  return s->getBool(QStringLiteral("PressureMirror"), false) && s->getBool(QStringLiteral("HorizontalMirrorEnabled"), false);
              },
              [](KisPaintOpSettingsSP s, bool on) {
                  writeProperty(s, QStringLiteral("HorizontalMirrorEnabled"), on);
                  writeProperty(s, QStringLiteral("PressureMirror"),
                                on || s->getBool(QStringLiteral("VerticalMirrorEnabled"), false));
              });
    addSwitch(pl, w, i18n("Mirror top and bottom"), has("VerticalMirrorEnabled"),
              [](KisPaintOpSettingsSP s) {
                  return s->getBool(QStringLiteral("PressureMirror"), false) && s->getBool(QStringLiteral("VerticalMirrorEnabled"), false);
              },
              [](KisPaintOpSettingsSP s, bool on) {
                  writeProperty(s, QStringLiteral("VerticalMirrorEnabled"), on);
                  writeProperty(s, QStringLiteral("PressureMirror"),
                                on || s->getBool(QStringLiteral("HorizontalMirrorEnabled"), false));
              });
    addSwitch(pl, w, i18n("Erase with this brush"), always,
              [](KisPaintOpSettingsSP s) { return s->getBool(QStringLiteral("EraserMode"), false); },
              [this](KisPaintOpSettingsSP , bool on) {
                  if (d->manager) d->manager->setPaintMode(on ? IkInterfaceManager::Erase : IkInterfaceManager::Draw);
              });
    pl->addStretch(1);

    // ---- tabs at the bottom ------------------------------------------------------------
    d->tabs = new IkTabBar(this);
    d->tabs->addTab(QStringLiteral("stroke"), i18n("Stroke"));
    d->tabs->addTab(QStringLiteral("brush-head"), i18n("Head"));
    d->tabs->addTab(QStringLiteral("texture"), i18n("Texture"));
    d->tabs->addTab(QStringLiteral("paint-drop"), i18n("Paint"));
    d->tabs->addTab(QStringLiteral("sparkles"), i18n("Special"));
    connect(d->tabs, &IkTabBar::currentChanged, d->pages, &QStackedWidget::setCurrentIndex);
    l->addWidget(d->tabs);

    // Follow the current brush.
    if (KisViewManager *vm = manager ? manager->viewManager() : nullptr) {
        if (vm->canvasResourceProvider() && vm->canvasResourceProvider()->resourceManager()) {
            connect(vm->canvasResourceProvider()->resourceManager(), &KoCanvasResourceProvider::canvasResourceChanged, this,
                    [this](int key, const QVariant &) {
                        if (key == KoCanvasResource::CurrentPaintOpPreset || key == KoCanvasResource::Size) {
                            if (isVisible()) QTimer::singleShot(0, this, [this]() { reload(); });
                        }
                    });
        }
    }
}

IkBrushSettingsPanel::~IkBrushSettingsPanel()
{
    delete d;
}

QWidget *IkBrushSettingsPanel::previewHost() const
{
    return d->previewHost;
}

void IkBrushSettingsPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    reload();
}

void IkBrushSettingsPanel::reload()
{
    KisPaintOpSettingsSP s = currentSettings(d->manager);
    d->reading = true;
    for (const Binding &b : d->bindings) {
        const bool applies = s && b.applies(s);
        b.row->setVisible(applies);
        if (applies) b.read(s);
    }
    d->reading = false;
}

namespace IkBrushSettings
{
void adoptEditor(QWidget *editor, IkInterfaceManager *manager, bool on)
{
    if (!editor) return;
    IkBrushSettingsPanel *panel = editor->findChild<IkBrushSettingsPanel *>(QStringLiteral("IkBrushSettingsPanel"));
    const bool adopted = panel && panel->isVisibleTo(editor);
    if (on == adopted) {
        if (panel && on) panel->reload();
        return;
    }
    // Krita's parts the IKriate panel replaces or makes redundant.
    const QStringList hidden = {
        QStringLiteral("frmOptionWidgetContainer"), QStringLiteral("eraserBrushSizeCheckBox"),
        QStringLiteral("eraserBrushOpacityCheckBox"), QStringLiteral("dirtyPresetCheckBox"),
        QStringLiteral("wdgLodAvailability"), QStringLiteral("viewOptionButton"),
        QStringLiteral("currentBrushNameLabel"), QStringLiteral("renameBrushPresetButton"),
        QStringLiteral("currentBrushEngineIcon"), QStringLiteral("currentBrushEngineLabel"),
        QStringLiteral("reloadPresetButton"), QStringLiteral("dirtyPresetIndicatorButton"),
        QStringLiteral("saveNewBrushPresetButton"), QStringLiteral("saveBrushPresetButton"),
        QStringLiteral("engineFilterLabel"), QStringLiteral("brushEngineComboBox"),
    };
    for (const QString &name : hidden) {
        if (QWidget *w = editor->findChild<QWidget *>(name)) w->setVisible(!on);
    }
    QWidget *preview = editor->findChild<QWidget *>(QStringLiteral("liveBrushPreviewView"));
    QLayout *dashboard = editor->findChild<QLayout *>(QStringLiteral("brushEditorDashboard"));
    QBoxLayout *settingsLayout = editor->findChild<QBoxLayout *>(QStringLiteral("brushEditorLayout"));
    if (!panel && on && settingsLayout) {
        panel = new IkBrushSettingsPanel(manager, editor, settingsLayout->parentWidget());
        settingsLayout->insertWidget(0, panel, 1);
    }
    if (panel) panel->setVisible(on);
    if (preview && panel) {
        if (on) {
            panel->previewHost()->layout()->addWidget(preview);
        } else if (QBoxLayout *dash = qobject_cast<QBoxLayout *>(dashboard)) {
            dash->insertWidget(1, preview, 1);
        }
        preview->show();
    }
    if (panel && on) panel->reload();
}
}
