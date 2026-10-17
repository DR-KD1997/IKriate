# IKriate codebase review: updates and optimisations

Review of the Krita 5.3 base (branch `krita/6.0`, commit `b5dc03a`, 16 Oct 2026) made while
starting IKriate. Items marked **Done** are changed in this repository; the rest are ranked
by expected value.

## Done in the first milestone

| Area | Finding | Change |
|---|---|---|
| Data isolation | Krita stores settings in `kritarc` and resources in `…/krita`. A fork using the same names would read and overwrite a user's real Krita setup. | Main config is `ikriaterc` (`KConfig::setMainConfigName`), display config `ikriatedisplayrc`, logs `ikriate.log`, single-instance key `IKriate5…`, default resource folder `…/ikriate` (`IkPaths::userDataLocation`). Krita's existing "custom resource folder" logic then also hides Krita's own user files from IKriate. |
| Update checks | `ENABLE_UPDATERS=ON` checks krita.org for *Krita* releases and would offer to download Krita. | Default `OFF`. |
| Android store flow | On startup the Android build shows the Krita Foundation's supporter dialog and initialises Play Billing for the Foundation's products. These do not exist under another app ID. | Disabled behind `IKRIATE_ENABLE_KRITA_DONATIONS`; interface-scale prompt now runs on its own timer instead of after that dialog. Donation menu entries removed. |
| App identity | Android app ID, labels, launcher icons; Windows version info, installer product name, registry and uninstall keys; desktop and AppStream names; 100 dialog titles. | All rebranded. Java/JNI namespace (`org.krita.android`) deliberately unchanged, since renaming it means renaming every JNI symbol. |
| JPEG speed | The Linux build did not find `libjpeg-turbo`'s TurboJPEG API, so the JPEG/TIFF filters fell back to plain libjpeg paths. | `libturbojpeg0-dev` added to Linux build instructions and CI. |
| Release builds | No link-time optimisation option. | `IKRIATE_ENABLE_LTO` (off until measured on all targets). |

## Recommended next

1. **Plan the Qt 6 move.** Qt 5.15 is past its standard support period (open-source users
   depend on KDE's patch collection), and upstream Krita's roadmap expects the Qt 6 build to
   stop being experimental by the end of 2026. IKriate tracks the
   combined `krita/6.0` branch, so the same source builds with `-DBUILD_WITH_QT6=ON`.
   The IKriate code avoids Qt5-only APIs or guards them (`QT_VERSION` checks), so the cost is
   mostly in dependencies and CI. Doing it soon also lines IKriate up with upstream's QML
   tablet work (the `kritatablet` prototype and QML tool options).
2. **Use patched Qt on Linux.** Krita carries Qt patches for tablet enter/leave events,
   key press/release pairing and frame-rate-limited updates (`KRITA_QT_HAS_*_PATCH`). Distro
   Qt lacks them (`QT_HAS_ENTER_LEAVE_PATCH - not found` in the configure log). The Windows
   and Android CI jobs get patched Qt from KDE's dependency packages; the Linux AppImage
   should switch to the same packages (upstream's `build-appimage.py`) before release.
3. **Newer xsimd.** Krita looks for xsimd 14, then 13, then 12; the Ubuntu 24.04 build uses
   12.1.1. xsimd drives Krita's SIMD pixel compositing, so the release builds should use the
   newest version KDE's dependency packages ship (they do on Windows/Android).
4. **Build times.** `KRITA_ENABLE_PCH` is off by default. Turning it on in CI and keeping
   `ccache` (already in the Linux workflow) are the cheapest wins; measure before enabling LTO.
5. **Binary and data-folder names.** The executable is still `krita`/`krita.exe` and installed
   data still lives in `share/krita`, because ~150 install rules and packaging scripts assume
   it. Portable zips, AppImages and APKs are unaffected; a system-wide Linux package would
   conflict with a Krita package. Rename in one pass when packaging for distributions.
6. **Windows shell extension.** The thumbnail/property shell extension uses Krita's COM
   class IDs. The CI produces a portable zip, which does not register it; before shipping an
   installer, give the extension new CLSIDs or drop it.
7. **Crash and bug reports.** KCrash/DrKonqi on Linux and the bug-report dialog are wired to
   KDE's tracker for "krita". Point them at IKriate's issue tracker (the About data already
   does) or disable DrKonqi integration.
8. **Translations.** New IKriate strings use `i18n()` but are not in the `po/` catalogs yet,
   so they show in English everywhere. Add them to the translation template.
9. **Windows Python scripting.** The Windows CI build configures without Python scripting
   (the Python in KDE's dependency packages is missing `setuptools`/`sipconfig` at configure
   time), so Krita's Python plugins are not in the zip. Bootstrap `setuptools` into that
   Python before configuring, as upstream's VM image does.
10. **AppStream metainfo.** `org.kde.krita.appdata.xml` still carries Krita's description and
    translated paragraphs; current `appstreamcli` rejects them, so the AppImage is built with
    `NO_APPSTREAM=1`. Write an IKriate description and drop the inherited translations.
11. **Android scripting.** Python plugins are unavailable in the Android build (no embedded
   interpreter). This matters for IKriate because tablet users can't use script-based
   add-ons; it is a large upstream change, so track it rather than fork it.

## Measurements

Reference build in a 2-core, 7 GB Linux workspace (Ubuntu 24.04, distro Qt 5.15.13):

| Metric | Value |
|---|---|
| Build steps (Release, tests off) | 3,509 |
| Full build, cold ccache | about 4.5 hours |
| Rebuild after an IKriate UI change | 2-4 minutes (only `kritaui` and the executable relink) |
| Install size (binaries, plugins, resources) | 231 MB |

Verified by running the build headless (Xvfb, QPainter canvas): first-run defaults,
Classic → Radial → Classic round trip, all four bundled skins, tint, runtime layout and skin
switching from Interface & Skins, the radial palette, Brush Studio.
