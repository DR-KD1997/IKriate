# Building IKriate

IKriate builds the same way Krita 5.3 does (Qt 5.15, KDE Frameworks 5, CMake). The source
tracks Krita's `krita/6.0` branch, which is the 5.3 release built with Qt 5 by default.

## GitHub Actions (all three platforms)

All three produced working packages on 17 Oct 2026 (Linux AppImage 156 MB, Windows zip
219 MB, Android APK 180 MB). Download them from the run's **Artifacts** section. Each run's
summary page lists the package size and its largest parts; `IKRIATE-OPTIMISATION.md` has the
size and memory study.

| Workflow | Output | Dependencies |
|---|---|---|
| `build-linux.yml` | `IKriate-*-x86_64.AppImage` | Ubuntu 24.04 packages + Immer/Zug/Lager |
| `build-windows.yml` | `ikriate-x64-*.zip` (portable) | Krita's prebuilt packages from KDE's registry, llvm-mingw |
| `build-android.yml` | `ikriate-arm64-v8a-*.apk` (debug-signed) | Krita's prebuilt packages from KDE's registry, NDK r27 |

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

## Linux, locally (Ubuntu 24.04 / Debian 13)

```sh
sudo apt install ccache ninja-build cmake extra-cmake-modules gettext \
  qtbase5-dev qtbase5-private-dev qtdeclarative5-dev qtquickcontrols2-5-dev libqt5svg5-dev \
  qttools5-dev libqt5x11extras5-dev qtmultimedia5-dev libqt5opengl5-dev \
  libkf5config-dev libkf5coreaddons-dev libkf5widgetsaddons-dev libkf5completion-dev \
  libkf5guiaddons-dev libkf5i18n-dev libkf5itemmodels-dev libkf5itemviews-dev \
  libkf5windowsystem-dev libkf5crash-dev libkf5kdcraw-dev libboost-system-dev libeigen3-dev \
  libexiv2-dev liblcms2-dev libfftw3-dev libgsl-dev libheif-dev libjxl-dev libopenexr-dev \
  libopenjp2-7-dev libpng-dev libtiff-dev libwebp-dev libgif-dev libjpeg-dev libturbojpeg0-dev \
  libpoppler-qt5-dev libraw-dev libquazip5-dev libmypaint-dev libopencolorio-dev \
  libharfbuzz-dev libfreetype-dev libfontconfig-dev libunibreak-dev libxsimd-dev \
  libsdl2-dev libfribidi-dev

# Immer, Zug and Lager are header-only and not packaged; the commits are pinned in
# .github/workflows/build-linux.yml.
for r in immer zug lager; do
  git clone https://github.com/arximboldi/$r.git
  cmake -S $r -B build-$r -G Ninja -DCMAKE_INSTALL_PREFIX=/opt/kdeps -DCMAKE_PREFIX_PATH=/opt/kdeps
  cmake --build build-$r && sudo cmake --install build-$r
done

cmake -S ikriate -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=$HOME/ikriate-install -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH=/opt/kdeps \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DBUILD_TESTING=OFF
cmake --build build && cmake --install build --strip
$HOME/ikriate-install/bin/krita
```

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
| `IKRIATE_ICF` | `OFF` | Identical code folding at link time (`safe` or `all`; uses gold with GCC). See `IKRIATE-OPTIMISATION.md`. |
| `IKRIATE_INSTALL_KRITA3_BUNDLE` | `OFF` | Krita 3's legacy resource bundle (16 MB, disabled by default in Krita; never installed on Android). |
| `ENABLE_UPDATERS` | `OFF` (was `ON`) | Krita's updater checks krita.org for *Krita* releases, so it is off. |
| `IKRIATE_ENABLE_KRITA_DONATIONS` (define) | unset | Krita Foundation's Android supporter/donation flow. Off: it sells the Foundation's Play Store products. |

## Regenerating artwork

```sh
pip install cairosvg pillow
python3 packaging/branding/generate-assets.py        # icons, splash, Android launcher
python3 krita/data/ikriate/skins/generate-skins.py   # bundled skins
```

## Where the IKriate code lives

| Path | What |
|---|---|
| `libs/ui/ikriate/IkInterfaceManager.*` | Layout switching, platform defaults, menus, radial palette, layers panel |
| `libs/ui/ikriate/IkChromeOverlays.*` | Minimal and Radial floating chrome |
| `libs/ui/ikriate/IkChromeWidgets.*` | Touch-sized buttons, pill sliders, swatches |
| `libs/ui/ikriate/IkBrushStudio.*` | Brush Studio sheet |
| `libs/ui/ikriate/IkSkinManager.*`, `IkAppearanceDialog.*` | Skins, accent, tint and the settings dialog |
| `libs/resources/IkPaths.h` | Separate user data location |
| `krita/data/ikriate/`, `krita/data/input/ikriatetablet.profile` | Bundled skins, touch profile |
| `packaging/branding/` | Icon and splash sources |

Changes to upstream files are kept small and marked with `IKriate` comments, to make merging
new Krita releases easier.
