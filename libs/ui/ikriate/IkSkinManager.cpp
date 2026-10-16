/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkSkinManager.h"

#include <QApplication>
#include <cmath>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTextStream>

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include <IkPaths.h>
#include <KoResourcePaths.h>

namespace
{
const char *const kConfigGroup = "IKriate Skin";
const char *const kGeneratedFile = "ikriate-active.colors";
const char *const kStylesheetProperty = "ikriateSkinStylesheet";

// Groups whose background colours take the tint. Selection is left alone
// because it carries the accent.
const QStringList kTintedGroups = {
    QStringLiteral("Colors:Window"), QStringLiteral("Colors:View"),
    QStringLiteral("Colors:Button"), QStringLiteral("Colors:Tooltip"),
    QStringLiteral("Colors:Header"), QStringLiteral("Colors:Complementary"),
};

IkSkin readSkin(const QString &path, bool userSkin)
{
    IkSkin s;
    const QFileInfo fi(path);
    s.id = fi.completeBaseName();
    s.filePath = fi.absoluteFilePath();
    s.userSkin = userSkin;

    KConfig cfg(path, KConfig::SimpleConfig);
    const KConfigGroup general(&cfg, "General");
    s.name = general.readEntry("Name", s.id);

    const KConfigGroup ik(&cfg, "IKriate");
    s.description = ik.readEntry("Description", QString());
    s.accent = ik.readEntry("Accent", QColor());
    if (!s.accent.isValid()) {
        s.accent = KConfigGroup(&cfg, "Colors:Selection").readEntry("BackgroundNormal", QColor());
    }
    s.chromeRadius = qBound(0, ik.readEntry("ChromeRadius", 14), 32);
    s.panelOpacity = qBound(120, ik.readEntry("PanelOpacity", 240), 255);
    const QString qss = ik.readEntry("Stylesheet", QString());
    if (!qss.isEmpty()) {
        const QString qssPath = fi.dir().absoluteFilePath(qss);
        if (QFileInfo::exists(qssPath)) {
            s.stylesheetPath = qssPath;
        }
    }
    if (s.description.isEmpty() && fi.suffix() == QLatin1String("colors")) {
        s.description = i18n("Krita colour theme");
    }
    return s;
}

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF());
}
}

bool IkSkinManager::Settings::operator==(const Settings &o) const
{
    return skinId == o.skinId && overrideAccent == o.overrideAccent && accent == o.accent
        && tint == o.tint && tintStrength == o.tintStrength;
}

IkSkinManager *IkSkinManager::instance()
{
    // Lives for the whole process, like the other UI singletons.
    static IkSkinManager *s_instance = new IkSkinManager();
    return s_instance;
}

IkSkinManager::IkSkinManager()
{
    const KConfigGroup cfg(KSharedConfig::openConfig(), kConfigGroup);
    m_settings.skinId = cfg.readEntry("Skin", defaultSkinId());
    m_settings.overrideAccent = cfg.readEntry("OverrideAccent", false);
    m_settings.accent = cfg.readEntry("Accent", QColor(0xF2, 0xA5, 0x41));
    m_settings.tint = cfg.readEntry("Tint", QColor(0x5B, 0xA8, 0xE8));
    m_settings.tintStrength = qBound(0, cfg.readEntry("TintStrength", 0), 100);
}

QString IkSkinManager::defaultSkinId() const
{
    return QStringLiteral("ikriate-charcoal");
}

QString IkSkinManager::activeThemeName()
{
    return QStringLiteral("IKriate Active");
}

QString IkSkinManager::userSkinsFolder() const
{
    const QString folder = IkPaths::userSkinsLocation();
    QDir().mkpath(folder);
    return folder;
}

QList<IkSkin> IkSkinManager::availableSkins() const
{
    QMap<QString, IkSkin> byId;
    QStringList order;

    auto add = [&](const QString &path, bool user) {
        const IkSkin s = readSkin(path, user);
        if (s.id == QFileInfo(QString::fromLatin1(kGeneratedFile)).completeBaseName()) {
            return; // never offer our own generated output as a source
        }
        if (!byId.contains(s.id)) {
            order << s.id;
        }
        byId.insert(s.id, s); // later sources (user) override earlier (bundled)
    };

    // Bundled IKriate skins first, so they lead the list.
    QStringList bundled = KoResourcePaths::findAllAssets("data", "ikriate/skins/*.ikskin");
    bundled.sort();
    for (const QString &p : bundled) add(p, false);

    // Krita's colour themes are valid skins too.
    QStringList schemes = KoResourcePaths::findAllAssets("data", "color-schemes/*.colors");
    schemes += KoResourcePaths::findAllAssets("genericdata", "color-schemes/*.colors");
    schemes.removeDuplicates();
    schemes.sort();
    for (const QString &p : schemes) add(p, false);

    const QDir userDir(userSkinsFolder());
    const QStringList user = userDir.entryList({"*.ikskin", "*.colors"}, QDir::Files, QDir::Name);
    for (const QString &f : user) add(userDir.absoluteFilePath(f), true);

    QList<IkSkin> result;
    for (const QString &id : order) result << byId.value(id);
    return result;
}

IkSkin IkSkinManager::skin(const QString &id) const
{
    const QList<IkSkin> skins = availableSkins();
    for (const IkSkin &s : skins) {
        if (s.id == id) return s;
    }
    for (const IkSkin &s : skins) {
        if (s.id == defaultSkinId()) return s;
    }
    return skins.isEmpty() ? IkSkin() : skins.first();
}

IkSkinManager::Settings IkSkinManager::settings() const
{
    return m_settings;
}

void IkSkinManager::setSettings(const Settings &settings, bool applyNow)
{
    m_settings = settings;
    m_settings.tintStrength = qBound(0, m_settings.tintStrength, 100);
    saveSettings();
    if (applyNow) apply();
}

void IkSkinManager::saveSettings() const
{
    KConfigGroup cfg(KSharedConfig::openConfig(), kConfigGroup);
    cfg.writeEntry("Skin", m_settings.skinId);
    cfg.writeEntry("OverrideAccent", m_settings.overrideAccent);
    cfg.writeEntry("Accent", m_settings.accent);
    cfg.writeEntry("Tint", m_settings.tint);
    cfg.writeEntry("TintStrength", m_settings.tintStrength);
    cfg.sync();
}

void IkSkinManager::applyStoredSettings()
{
    static bool done = false;
    if (done) return;
    done = true;

    const KConfigGroup theme(KSharedConfig::openConfig(), "theme");
    const QString currentTheme = theme.readEntry("Theme", QString());
    // First run, or the user still uses the IKriate skin system: (re)generate.
    // If they picked another theme from Settings > Themes, respect that.
    if (currentTheme.isEmpty() || currentTheme == activeThemeName()) {
        apply();
    } else {
        const IkSkin s = skin(m_settings.skinId);
        m_chromeRadius = s.chromeRadius;
        m_panelOpacity = s.panelOpacity;
    }
}

void IkSkinManager::apply()
{
    const IkSkin s = skin(m_settings.skinId);
    if (s.filePath.isEmpty()) {
        qWarning("IKriate: no skins found, keeping the current theme");
        return;
    }
    QString error;
    const QString scheme = generateScheme(s, m_settings, &error);
    if (scheme.isEmpty()) {
        qWarning("IKriate: could not generate skin scheme: %s", qPrintable(error));
        return;
    }
    m_chromeRadius = s.chromeRadius;
    m_panelOpacity = s.panelOpacity;
    applyStylesheet(s);
    Q_EMIT sigActivateTheme(activeThemeName(), scheme);
    Q_EMIT sigSkinChanged();
}

QString IkSkinManager::generateScheme(const IkSkin &s, const Settings &st, QString *error) const
{
    const QString folder = KoResourcePaths::getAppDataLocation() + QStringLiteral("/color-schemes");
    if (!QDir().mkpath(folder)) {
        if (error) *error = i18n("Cannot create %1", folder);
        return QString();
    }
    const QString outPath = folder + '/' + QString::fromLatin1(kGeneratedFile);
    QFile::remove(outPath);

    KConfig src(s.filePath, KConfig::SimpleConfig);
    KConfig out(outPath, KConfig::SimpleConfig);

    // Copy the skin verbatim, then adjust.
    const QStringList groups = src.groupList();
    for (const QString &g : groups) {
        const KConfigGroup sg(&src, g);
        KConfigGroup og(&out, g);
        const QStringList keys = sg.keyList();
        for (const QString &k : keys) {
            og.writeEntry(k, sg.readEntry(k, QString()));
        }
    }

    KConfigGroup general(&out, "General");
    general.writeEntry("Name", activeThemeName());
    general.writeEntry("ColorScheme", activeThemeName());
    general.writeEntry("IKriateSource", s.id);

    QColor accent = st.overrideAccent && st.accent.isValid() ? st.accent : s.accent;
    if (accent.isValid()) {
        KConfigGroup sel(&out, "Colors:Selection");
        const QColor text = readableTextOn(accent);
        sel.writeEntry("BackgroundNormal", accent);
        sel.writeEntry("BackgroundAlternate", accent.darker(112));
        sel.writeEntry("ForegroundNormal", text);
        sel.writeEntry("ForegroundActive", text);
        sel.writeEntry("ForegroundInactive", text);
        for (const QString &g : out.groupList()) {
            if (!g.startsWith(QLatin1String("Colors:"))) continue;
            KConfigGroup cg(&out, g);
            cg.writeEntry("DecorationFocus", accent);
            cg.writeEntry("DecorationHover", accent);
        }
    }

    if (st.tint.isValid() && st.tintStrength > 0) {
        for (const QString &g : kTintedGroups) {
            if (!out.hasGroup(g)) continue;
            KConfigGroup cg(&out, g);
            for (const char *key : {"BackgroundNormal", "BackgroundAlternate"}) {
                const QColor c = cg.readEntry(key, QColor());
                if (c.isValid()) cg.writeEntry(key, tinted(c, st.tint, st.tintStrength));
            }
        }
        KConfigGroup wm(&out, "WM");
        for (const char *key : {"activeBackground", "inactiveBackground"}) {
            const QColor c = wm.readEntry(key, QColor());
            if (c.isValid()) wm.writeEntry(key, tinted(c, st.tint, st.tintStrength));
        }
    }

    if (!out.sync()) {
        if (error) *error = i18n("Cannot write %1", outPath);
        return QString();
    }
    return outPath;
}

void IkSkinManager::applyStylesheet(const IkSkin &s) const
{
    // Only touch the application stylesheet if a skin asks for one, and
    // only remove what a skin put there before.
    const bool hadSkinSheet = qApp->property(kStylesheetProperty).toBool();
    if (s.stylesheetPath.isEmpty()) {
        if (hadSkinSheet) {
            qApp->setStyleSheet(QString());
            qApp->setProperty(kStylesheetProperty, false);
        }
        return;
    }
    QFile f(s.stylesheetPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(f.readAll()));
        qApp->setProperty(kStylesheetProperty, true);
    }
}

QString IkSkinManager::importSkin(const QString &filePath, QString *error)
{
    const QFileInfo fi(filePath);
    const QString suffix = fi.suffix().toLower();
    if (suffix != QLatin1String("ikskin") && suffix != QLatin1String("colors")) {
        if (error) *error = i18n("Skins are .ikskin or .colors files.");
        return QString();
    }
    {
        KConfig probe(filePath, KConfig::SimpleConfig);
        if (!probe.hasGroup("Colors:Window") || !probe.hasGroup("Colors:View")) {
            if (error) *error = i18n("This file is not a colour scheme (missing [Colors:Window] or [Colors:View]).");
            return QString();
        }
    }
    const QString target = QDir(userSkinsFolder()).absoluteFilePath(fi.fileName());
    if (QFileInfo(target).absoluteFilePath() != fi.absoluteFilePath()) {
        QFile::remove(target);
        if (!QFile::copy(filePath, target)) {
            if (error) *error = i18n("Could not copy the skin into %1.", userSkinsFolder());
            return QString();
        }
    }
    // Bring along a stylesheet the skin refers to, if it sits next to it.
    KConfig cfg(filePath, KConfig::SimpleConfig);
    const QString qss = KConfigGroup(&cfg, "IKriate").readEntry("Stylesheet", QString());
    if (!qss.isEmpty() && !qss.contains('/') && !qss.contains('\\')) {
        const QString from = fi.dir().absoluteFilePath(qss);
        const QString to = QDir(userSkinsFolder()).absoluteFilePath(qss);
        if (QFileInfo::exists(from) && from != to) {
            QFile::remove(to);
            QFile::copy(from, to);
        }
    }
    return fi.completeBaseName();
}

int IkSkinManager::chromeRadius() const
{
    return m_chromeRadius;
}

int IkSkinManager::panelOpacity() const
{
    return m_panelOpacity;
}

QColor IkSkinManager::tinted(const QColor &base, const QColor &tint, int strength)
{
    if (!base.isValid() || !tint.isValid() || strength <= 0) return base;
    // Re-colour towards the tint's hue while keeping the base lightness, so a
    // dark skin stays dark and contrast with text is preserved.
    const qreal sat = qBound<qreal>(0.25, tint.hslSaturationF(), 0.9);
    const qreal hue = tint.hslHueF() < 0 ? 0.0 : tint.hslHueF();
    const QColor target = QColor::fromHslF(hue, sat, base.lightnessF());
    const qreal amount = qBound(0, strength, 100) / 100.0 * 0.55;
    QColor result = mix(base, target, amount);
    result.setAlpha(base.alpha());
    return result;
}

QColor IkSkinManager::readableTextOn(const QColor &bg)
{
    auto lin = [](qreal c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    const qreal l = 0.2126 * lin(bg.redF()) + 0.7152 * lin(bg.greenF()) + 0.0722 * lin(bg.blueF());
    // Pick whichever gives more contrast (WCAG relative luminance).
    const qreal contrastBlack = (l + 0.05) / 0.05;
    const qreal contrastWhite = 1.05 / (l + 0.05);
    return contrastBlack >= contrastWhite ? QColor(0x1A, 0x1A, 0x1A) : QColor(Qt::white);
}
