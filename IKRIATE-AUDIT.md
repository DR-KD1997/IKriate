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

## Done in the second milestone (redesign)

| Area | Change |
|---|---|
| Qt 6 | Windows and Linux CI build with Qt 6 (`BUILD_WITH_QT6=ON`): Windows from KDE's `transition.now/qt6` packages, Linux in an Ubuntu 26.04 container (Qt 6.10, KF6). Android stays on Qt 5 until KDE ships Qt 6 Android packages. Verified with a full local Qt 6 build. |
| Interface | New home screen, gallery, New Canvas and Template sheets, settings page, and the Desktop, Kriate and Minimal layouts, built from palette-derived styles and Lucide line icons. Menus, toolbars, dockers and the status bar are hidden in all three layouts; the docker interface is opt-in under Settings › Advanced. |
| Erasing | Eraser presets removed; erase is a mode applied to any brush (and to the pen's eraser end). |
| Touch | Touch painting is off by default: one finger pans, only the pen draws. |
| Android start | The launch screen stays up (sticky splash) until the home screen has painted, replacing the black screen during start-up. |
| Branding | `rebrand-strings.py` renames the product in all translatable text and the 75 translation catalogues; About/splash credit Krita accurately and drop Krita's sponsors and donation links; Krita's colour themes renamed "Classic …"; Android billing code (the Krita Foundation's Play Store products) removed; AppStream metadata rewritten. File-format names (".kra, Krita document") and the Python API module name are kept, since they name Krita's format and API. |
| Size | Identical code folding (`IKRIATE_ICF=safe`) in Windows and Linux CI; brushes merged into one optimised bundle without duplicate tips and patterns. |

## Recommended next

1. **Qt 6 on Android.** Switch `build-android.yml` to Qt 6 once KDE publishes Qt 6 Android
   dependency packages (none on `transition.now/qt6` yet); the IKriate code already builds
   with both Qt versions.
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
10. **AppStream metainfo.** Rewritten for IKriate (English only); `NO_APPSTREAM=1` can be
    dropped from the AppImage step once `appstreamcli validate` passes in CI.
11. **Android scripting.** Python plugins are unavailable in the Android build (no embedded
   interpreter). This matters for IKriate because tablet users can't use script-based
   add-ons; it is a large upstream change, so track it rather than fork it.

12. **Package size and memory.** See `IKRIATE-OPTIMISATION.md`: stripped plugins, a
    smaller AppImage, Windows debug-info splitting, the Krita 3 bundle and the OpenGL
    tile pool are done; LTO/identical code folding and KDE dependency packages on Linux
    are next.

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
