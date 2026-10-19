# Building IKriate

The source tracks Krita's `krita/6.0` branch, which builds either with Qt 5 (Krita 5.3) or
with Qt 6 (Krita 6.0, `-DBUILD_WITH_QT6=ON -DALLOW_UNSTABLE=QT6`). IKriate builds **Windows and
Linux with Qt 6** and **Android with Qt 5**: KDE publishes Qt 6 dependency packages for
Windows, Linux and macOS (branch `transition.now/qt6`), but not yet for Android. All three
builds come from the same source.

## GitHub Actions (all three platforms)

The Qt 5 versions of all three produced working packages on 17 Oct 2026 (Linux AppImage
156 MB, Windows zip 219 MB, Android APK 180 MB). Download them from the run's **Artifacts**
section. Each run's
summary page lists the package size and its largest parts; `IKRIATE-OPTIMISATION.md` has the
size and memory study.

| Workflow | Output | Dependencies |
|---|---|---|
| `build-linux.yml` | `IKriate-*-x86_64.AppImage` (Qt 6) | Ubuntu 26.04 container: Qt 6.10, KDE Frameworks 6, Immer/Zug/Lager packages. Needs glibc 2.42+ on the user's system |
| `build-windows.yml` | `ikriate-x64-*.zip` (portable, Qt 6) | Krita's prebuilt `transition.now/qt6` packages from KDE's registry, llvm-mingw |
| `build-android.yml` | `ikriate-arm64-v8a-*.apk` (debug-signed, Qt 5) | Krita's prebuilt packages from KDE's registry, NDK r27 |

The Windows and Android jobs reuse Krita's own CI tooling
([krita-deps-management](https://invent.kde.org/packaging/krita-deps-management),
[krita-ci-utilities](https://invent.kde.org/packaging/krita-ci-utilities)), which downloads
~90 dependencies already built by KDE. That avoids a multi-hour dependency build, but it
ties the compiler versions to KDE's: if a Windows build fails at link time with C++ library
errors, update `LLVM_MINGW_VERSION` to the llvm-mingw release KDE's Windows image uses.

Release signing (not configured yet):
- **Android**: create a keystore, store it and its passwords as repository secrets, set
  `KRITACI_ANDROID_PACKAGE_TYPE: release` and sign the APK with `apksigner`.
- **Windows**: the zip is unsigned; SmartScreen will warn on first launch until it is signed.

## Linux, locally (Qt 6, Ubuntu 26.04)

```sh
sudo apt install build-essential lld ccache ninja-build cmake extra-cmake-modules gettext pkg-config \
  qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-declarative-private-dev qt6-svg-dev \
  qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools qt6-5compat-dev qt6-multimedia-dev qt6-wayland-dev \
  qml6-module-qtqml-workerscript qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-shapes qml6-module-qtquick-templates qml6-module-qtquick-window \
  libkf6colorscheme-dev libkf6completion-dev libkf6config-dev libkf6coreaddons-dev libkf6crash-dev \
  libkf6guiaddons-dev libkf6i18n-dev libkf6itemmodels-dev libkf6itemviews-dev libkf6widgetsaddons-dev \
  libkf6windowsystem-dev libkdcrawqt6-dev libimmer-dev libzug-dev liblager-dev libkseexpr-dev \
  libboost-dev libeigen3-dev libexiv2-dev liblcms2-dev libfftw3-dev libgsl-dev libheif-dev libjxl-dev \
  libopenexr-dev libopenjp2-7-dev libpng-dev libtiff-dev libwebp-dev libgif-dev libjpeg-dev \
  libturbojpeg0-dev libpoppler-qt6-dev libraw-dev libquazip1-qt6-dev libmypaint-dev \
  libopencolorio-dev libharfbuzz-dev libfreetype-dev libfontconfig-dev libunibreak-dev libxsimd-dev \
  libsdl2-dev libfribidi-dev libx11-dev libxi-dev zlib1g-dev

cmake -S ikriate -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_WITH_QT6=ON -DALLOW_UNSTABLE=QT6 -DIKRIATE_ICF=safe \
  -DCMAKE_INSTALL_PREFIX=$HOME/ikriate-install -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DBUILD_TESTING=OFF
cmake --build build && cmake --install build --strip
$HOME/ikriate-install/bin/krita
```

Older distributions without Qt 6.5+ and KDE Frameworks 6 can still build the Qt 5 variant
(drop the two Qt 6 options and install the Qt 5 / KF5 `-dev` packages instead; Immer, Zug and
Lager then have to be built from source, pinned in the git history of `build-linux.yml`).

`CMAKE_INSTALL_LIBDIR=lib` matters on Debian/Ubuntu: Krita looks for its QML modules in
`<prefix>/lib/qml`, and the multiarch default (`lib/x86_64-linux-gnu`) hides them from the
Text tool's panels.

A full build takes 3-4 hours on 2 cores and about 40 minutes on 8. Distribution Qt lacks a
few tablet patches Krita carries; the AppImage/Windows/Android builds from KDE's dependency
packages include them.

## Build options added by IKriate

| Option | Default | Effect |
|---|---|---|
| `BRANDING` | `IKriate` | Icons and splash. Krita's own artwork stays under `krita/pics/branding/{default,Beta,Plus,Next}`. |
| `IKRIATE_ENABLE_LTO` | `OFF` | Link-time optimisation for Release/RelWithDebInfo. |
| `IKRIATE_ICF` | `OFF` (`safe` in CI) | Identical code folding at link time (`safe` or `all`; uses lld, or gold, with GCC). See `IKRIATE-OPTIMISATION.md`. |
| `IKRIATE_INSTALL_KRITA3_BUNDLE` | `OFF` | Krita 3's legacy resource bundle (16 MB, disabled by default in Krita; never installed on Android). |
| `ENABLE_UPDATERS` | `OFF` (was `ON`) | Krita's updater checks krita.org for *Krita* releases, so it is off. |
| `IKRIATE_ENABLE_KRITA_DONATIONS` (define) | unset | Menu entries of Krita's Android supporter flow. The flow itself (Google Play billing for the Krita Foundation's products) has been removed from IKriate. |

## Regenerating artwork

```sh
pip install cairosvg pillow
python3 packaging/branding/generate-assets.py        # icons, splash, Android launcher
python3 krita/data/ikriate/skins/generate-skins.py   # bundled skins
python3 packaging/branding/make-bundles.py           # IKriate_Brushes.bundle and brush categories
python3 packaging/branding/rebrand-strings.py        # product name in translatable text and po/
```

`rebrand-strings.py` is idempotent: run it again after merging new Krita code or translations.
It keeps accurate references ("Krita Foundation", "Krita 4", "based on Krita") and skips files
marked `IKRIATE-REBRAND: skip` (the About dialog, which credits Krita by name).

## Where the IKriate code lives

| Path | What |
|---|---|
| `libs/ui/ikriate/IkInterfaceManager.*` | Layout switching, platform defaults, draw/erase/smudge mode, menus, hiding the desktop chrome |
| `libs/ui/ikriate/IkLayouts.*`, `IkDesktopLayout.cpp`, `IkKriateLayout.cpp`, `IkMinimalLayout.cpp` | The three canvas layouts |
| `libs/ui/ikriate/IkHomeScreen.*`, `IkNewCanvasDialog.*`, `IkRecycleBin.*` | Gallery, create menu, New Canvas and Template sheets, recycle bin |
| `libs/ui/ikriate/IkSettingsPage.*` | Settings (theme, layout, language, advanced, clean-up) |
| `libs/ui/ikriate/IkBrushLibrary.*`, `IkStrokePreview.*` | Brush library and stroke previews |
| `libs/ui/ikriate/IkColorPanel.*`, `IkLayersPanel.*`, `IkAdjustmentsPanel.*` | Panels of the Desktop layout (also used in pop-overs) |
| `libs/ui/ikriate/IkWidgets.*`, `IkStyle.*`, `icons/` | Controls, palette-derived colours, Lucide line icons (ISC licence) |
| `libs/ui/ikriate/IkSkinManager.*` | Skins, light/dark/follow-system, accent and tint |
| `libs/ui/ikriate/IkLaunchScreen.*` | Keeps the Android launch screen up until the gallery is shown |
| `libs/resources/IkPaths.h` | Separate user data location |
| `krita/data/ikriate/`, `krita/data/input/ikriatetablet.profile` | Bundled skins, touch profile |
| `packaging/branding/` | Icon and splash sources, bundle and rebranding scripts |

Changes to upstream files are kept small and marked with `IKriate` comments, to make merging
new Krita releases easier.
