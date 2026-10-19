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

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>
#if defined(Q_OS_ANDROID) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtAndroid>
#include <QAndroidJniObject>
#endif

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
        s.description = i18n("Colour theme");
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
    m_settings.darkSkinId = cfg.readEntry("DarkSkin", QString());
    m_settings.lightSkinId = cfg.readEntry("LightSkin", QString());
    m_settings.themeMode = ThemeMode(qBound(0, cfg.readEntry("ThemeMode", int(FollowSystem)), 2));
    // Settings from before the light/dark split: one skin, used for both.
    const QString legacy = cfg.readEntry("Skin", QString());
    if (!legacy.isEmpty() && m_settings.darkSkinId.isEmpty() && m_settings.lightSkinId.isEmpty()) {
        const bool dark = isDarkSkin(skin(legacy));
        (dark ? m_settings.darkSkinId : m_settings.lightSkinId) = legacy;
        m_settings.themeMode = dark ? Dark : Light;
    }
    if (m_settings.darkSkinId.isEmpty()) m_settings.darkSkinId = defaultSkinId();
    if (m_settings.lightSkinId.isEmpty()) m_settings.lightSkinId = defaultLightSkinId();
    m_settings.overrideAccent = cfg.readEntry("OverrideAccent", false);
    m_settings.accent = cfg.readEntry("Accent", QColor(0xC4, 0x6F, 0xD0));
    m_settings.tint = cfg.readEntry("Tint", QColor(0x5B, 0xA8, 0xE8));
    m_settings.tintStrength = qBound(0, cfg.readEntry("TintStrength", 0), 100);
    m_settings.skinId = effectiveSkinId();
}

QString IkSkinManager::defaultSkinId() const
{
    return QStringLiteral("ikriate-dark");
}

QString IkSkinManager::defaultLightSkinId() const
{
    return QStringLiteral("ikriate-light");
}

bool IkSkinManager::isDarkSkin(const IkSkin &s)
{
    if (s.filePath.isEmpty()) return true;
    KConfig cfg(s.filePath, KConfig::SimpleConfig);
    const QColor bg = KConfigGroup(&cfg, "Colors:Window").readEntry("BackgroundNormal", QColor(Qt::black));
    return bg.lightnessF() < 0.5;
}

bool IkSkinManager::systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::instance()) {
        const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
        if (scheme != Qt::ColorScheme::Unknown) {
            return scheme == Qt::ColorScheme::Dark;
        }
    }
#endif
#if defined(Q_OS_ANDROID)
    // Configuration.uiMode & UI_MODE_NIGHT_MASK == UI_MODE_NIGHT_YES
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QAndroidJniObject context = QtAndroid::androidContext();
    if (context.isValid()) {
        QAndroidJniObject res = context.callObjectMethod("getResources", "()Landroid/content/res/Resources;");
        QAndroidJniObject conf = res.callObjectMethod("getConfiguration", "()Landroid/content/res/Configuration;");
        if (conf.isValid()) {
            const int uiMode = conf.getField<jint>("uiMode");
            return (uiMode & 0x30) == 0x20;
        }
    }
#endif
    return true;
#elif defined(Q_OS_WIN)
    QSettings personalize(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                          QSettings::NativeFormat);
    return personalize.value(QStringLiteral("AppsUseLightTheme"), 0).toInt() == 0;
#else
    // Linux desktops: the freedesktop "prefer-dark" setting reaches Qt 6.5+
    // above; otherwise default to dark, which suits painting.
    return true;
#endif
}

QString IkSkinManager::effectiveSkinId() const
{
    bool dark = true;
    switch (m_settings.themeMode) {
    case Light: dark = false; break;
    case Dark: dark = true; break;
    case FollowSystem:
    default: dark = systemPrefersDark(); break;
    }
    return dark ? m_settings.darkSkinId : m_settings.lightSkinId;
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
    Settings next = settings;
    // A directly chosen skin goes into the slot matching its brightness.
    if (!next.skinId.isEmpty() && next.skinId != m_settings.skinId) {
        const bool dark = isDarkSkin(skin(next.skinId));
        (dark ? next.darkSkinId : next.lightSkinId) = next.skinId;
        if (next.themeMode != FollowSystem) {
            next.themeMode = dark ? Dark : Light;
        }
    }
    m_settings = next;
    m_settings.tintStrength = qBound(0, m_settings.tintStrength, 100);
    m_settings.skinId = effectiveSkinId();
    saveSettings();
    if (applyNow) apply();
}

void IkSkinManager::saveSettings() const
{
    KConfigGroup cfg(KSharedConfig::openConfig(), kConfigGroup);
    cfg.deleteEntry("Skin");
    cfg.writeEntry("DarkSkin", m_settings.darkSkinId);
    cfg.writeEntry("LightSkin", m_settings.lightSkinId);
    cfg.writeEntry("ThemeMode", int(m_settings.themeMode));
    cfg.writeEntry("OverrideAccent", m_settings.overrideAccent);
    cfg.writeEntry("Accent", m_settings.accent);
    cfg.writeEntry("Tint", m_settings.tint);
    cfg.writeEntry("TintStrength", m_settings.tintStrength);
    cfg.sync();
}

void IkSkinManager::prepareActiveScheme()
{
    if (m_prepared) return;
    const KConfigGroup theme(KSharedConfig::openConfig(), "theme");
    const QString currentTheme = theme.readEntry("Theme", QString());
    if (!currentTheme.isEmpty() && currentTheme != activeThemeName()) return;

    const IkSkin s = skin(m_settings.skinId);
    if (s.filePath.isEmpty()) return;
    QString error;
    if (generateScheme(s, m_settings, &error).isEmpty()) {
        qWarning("IKriate: could not generate skin scheme: %s", qPrintable(error));
        return;
    }
    m_chromeRadius = s.chromeRadius;
    m_panelOpacity = s.panelOpacity;
    m_prepared = true;
}

void IkSkinManager::applyStoredSettings()
{
    static bool done = false;
    if (done) return;
    done = true;

    if (m_prepared) {
        // The scheme was written before the window was built and the theme
        // manager loaded it directly; only the optional stylesheet remains.
        applyStylesheet(skin(m_settings.skinId));
        return;
    }

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
