/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKSKINMANAGER_H
#define IKSKINMANAGER_H

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>

#include "kritaui_export.h"

/**
 * One selectable look for the interface.
 *
 * Skins are KDE colour-scheme files: either IKriate's own *.ikskin files
 * (a .colors file plus an optional [IKriate] group) or any of the *.colors
 * themes shipped with Krita. Users can drop their own .ikskin or .colors
 * files into the skins folder.
 */
struct KRITAUI_EXPORT IkSkin
{
    QString id;          ///< stable key, the file's base name
    QString name;        ///< display name
    QString description;
    QString filePath;
    bool userSkin {false};
    QColor accent;       ///< skin's own accent (selection colour)
    int chromeRadius {14};
    int panelOpacity {240};
    QString stylesheetPath; ///< optional Qt stylesheet shipped with the skin
};

/**
 * Applies skins, accent colour overrides and colour tinting.
 *
 * Applying a skin generates one effective colour scheme
 * ("IKriate Active") inside the user resource folder and asks the main
 * window's theme manager to switch to it. Because the result is an ordinary
 * colour scheme, every part of Krita that reads the palette or KColorScheme
 * picks up the skin, the accent and the tint consistently.
 */
class KRITAUI_EXPORT IkSkinManager : public QObject
{
    Q_OBJECT
public:
    struct Settings {
        QString skinId;
        bool overrideAccent {false};
        QColor accent;
        QColor tint;
        int tintStrength {0}; ///< 0..100
        bool operator==(const Settings &o) const;
    };

    static IkSkinManager *instance();

    /// All skins: bundled, user-installed and Krita's colour schemes.
    QList<IkSkin> availableSkins() const;
    IkSkin skin(const QString &id) const;
    QString defaultSkinId() const;

    Settings settings() const;
    void setSettings(const Settings &settings, bool apply = true);

    /// Applies the stored settings. Called once when the first window opens.
    void applyStoredSettings();
    /// Re-generates the active scheme from current settings and activates it.
    void apply();

    /// Copies a skin file into the user skins folder. Returns the new skin id
    /// or an empty string on failure, with a reason in @p error.
    QString importSkin(const QString &filePath, QString *error = nullptr);
    QString userSkinsFolder() const;

    /// Name of the generated theme as listed in Settings > Themes.
    static QString activeThemeName();

    // Values used by the floating chrome.
    int chromeRadius() const;
    int panelOpacity() const;

    /// Pure colour helpers, exposed for unit tests.
    static QColor tinted(const QColor &base, const QColor &tint, int strength);
    static QColor readableTextOn(const QColor &background);

Q_SIGNALS:
    /// The generated scheme at @p schemePath is ready; activate theme @p themeName.
    void sigActivateTheme(const QString &themeName, const QString &schemePath);
    void sigSkinChanged();

private:
    IkSkinManager();
    QString generateScheme(const IkSkin &skin, const Settings &settings, QString *error) const;
    void applyStylesheet(const IkSkin &skin) const;
    void saveSettings() const;

    Settings m_settings;
    int m_chromeRadius {14};
    int m_panelOpacity {240};
};

#endif // IKSKINMANAGER_H
