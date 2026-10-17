# IKriate size and memory study

How much smaller and leaner IKriate can get without dropping Krita features, and how far each
step moves the code away from upstream Krita. Measured on 17 Oct 2026 against the Linux build
(Ubuntu 24.04, distro Qt 5.15, Xvfb with Mesa's software OpenGL) and the CI packages from the
same day: Linux AppImage 156 MB, Windows zip 219 MB, Android APK 180 MB.

**Short answer.** About a quarter of the Linux package, and probably more of the Windows one,
can go with no feature loss and almost no divergence, because the waste is in packaging
rather than in Krita's code. Below that floor the remaining size is mostly Qt, third-party
libraries and resource bundles that are features in their own right. Memory at rest is
dominated by Qt, the graphics driver and the image itself. The code-level changes that would
save more (resource handling, colour-engine templates, plugin linking) all mean carrying
large patches against upstream.

## Where the size goes

Linux install, 231 MB before packaging:

| Part | Installed | Compressed (xz) | Notes |
|---|---|---|---|
| Krita libraries and 166 plugins | 122 MB | ~19 MB | 33 MB of it is symbol tables (plugins were not stripped) |
| `kritalcmsengine` alone | 24 MB | | ~8,500 blend-mode template instantiations (every colour model × depth × mode) |
| Translations, 75 languages | 33 MB | 4.3 MB | `.mo` files compress well, so they cost disk space far more than download |
| Resource bundles | 37 MB | ~35 MB | Already zipped. Krita 3's bundle (16 MB) ships *disabled by default* |
| Document templates | 19 MB | 2.8 MB | Stored with weak compression inside `.kra` |
| Other resources | ~16 MB | ~12 MB | Patterns, presets, icons |

Bundled third-party libraries in the AppImage (compressed): Qt 11 MB, ICU 9 MB, everything
else 18 MB. Several MB of "everything else" is a network and crypto stack (curl, NSS, GnuTLS,
Kerberos, LDAP, SSH) pulled in by Ubuntu's Poppler (PDF import) and CUPS (printing), not by
anything Krita uses directly.

On first run Krita also **copies** every bundle and loose resource into the user's resource
folder and indexes them into a SQLite cache (29-40 MB of thumbnails): 86 MB of user data
before the user has made anything.

## What was measured

| Change | Result | Feature cost |
|---|---|---|
| Strip plugin modules | lib folder 122 → 89 MB installed; −3 MB in the AppImage | None |
| Leave out Krita 3 bundle | −16 MB package, −16 MB user data, first run **33 s → 13 s** | None by default (it ships disabled) |
| AppImage zstd level 22, 1 MiB blocks (default: level 15, 128 KiB) | −7% | None; same decompression speed |
| AppImage xz, 1 MiB blocks | −16% | Slower start: xz decompresses several times slower |
| All of the above, simulated on a CI-equivalent AppDir | 153 → 116 MB (zstd) or 104 MB (xz) | None |
| Re-deflate templates (lossless, output identical when rendered) | 18.6 → 9.0 MB installed | None, but no download saving |
| Re-optimise PNGs in Krita 4 bundle | 17.5 → 15.6 MB | None |
| Colour engine `-Os` instead of `-O2` | 17.8 → 11.8 MB | **Blend modes 2.0-2.9× slower** (multiply, overlay, dodge; 8-, 16- and 32-bit). Rejected |
| Colour engine LTO | 17.8 → 14.5 MB (−19%) | None: same speed, bit-identical output |
| Colour engine identical code folding (`--icf`) | −11% (safe), −26% (all, gold), −12% (all, lld) | None measured: same speed, bit-identical output |
| `-ffunction-sections` + `--gc-sections` alone | −0.1% | Not worth it |

Speed and correctness were checked with a small benchmark that composites 8.4 Mpx per pass
through Krita's own composite ops and hashes the output.

Memory, same 1600×900 image, after start-up:

| | Resident |
|---|---|
| First run (indexing resources) | 547 MB, not released afterwards (`malloc_trim` frees only 2 MB, so it is live data, not fragmentation) |
| Later runs | 375 MB |
| …of which Mesa's software renderer (LLVM, gallium; replaced by the GPU driver on real hardware) | ~70 MB of code plus most of the 158 MB heap |
| …OpenGL tile upload pool, kept for the whole session below a 64-tile threshold | up to 38 MB; purging idle pools saved **7.5 MB** here |
| Image tiles for this picture | 13 MB |

Krita's own heap at rest is small. Memory grows with the picture: about 4 bytes per pixel
per paint layer (48 MB per layer at 4000×3000, 8-bit RGBA) plus projection and undo. Empty
areas of a layer cost almost nothing (tiles are shared), and tiles go to a swap file once
`memoryHardLimitPercent` (50% of RAM) is reached.

## Options by divergence from upstream

### Level 0: packaging and build settings only (no Krita source changes)

| Option | Saves | Status |
|---|---|---|
| `cmake --install --strip` on Linux | 33 MB installed, ~3 MB download | **Done** |
| AppImage zstd-22 with 1 MiB blocks | ~8 MB download | **Done** |
| Windows: split debug info off again (it was disabled because a missing `kritarunner.exe` stopped the step; KDE's dependency DLLs carry DWARF sections) | Likely tens of MB; the new size report will show it | **Done** (the skip is now per missing file) |
| Size report in every CI job summary | Makes the next steps measurable on Windows and Android | **Done** |
| LTO (`IKRIATE_ENABLE_LTO`) and identical code folding (`IKRIATE_ICF=safe`) in release CI | Up to ~20% of Krita's own binaries | Options ready; CI time and the Windows/Android linkers still to be validated |
| Build the Linux AppImage from KDE's dependency packages, like Windows and Android | Drops the curl/NSS/GnuTLS chain; brings Krita's patched Qt | To do. Also fixes a found issue: Ubuntu's libheif loads its HEIF/AVIF codecs from a fixed system folder, so the current AppImage relies on the host for them |

### Level 1: a few lines in Krita files, trivial to carry through merges

| Option | Saves | Status |
|---|---|---|
| Leave out the disabled Krita 3 bundle (`IKRIATE_INSTALL_KRITA3_BUNDLE`, default OFF; upstream already does this on Android) | 16 MB package + 16 MB user data, 20 s of first run | **Done** |
| Purge idle OpenGL tile pools of any size (`kis_texture_tile_info_pool.h`, one constant) | 7.5 MB measured, up to ~38 MB per open view on medium canvases | **Done** |
| Platform memory defaults: e.g. a lower hard limit and earlier swapping on Android, where the system kills apps that hold too much | Bounded peak on large canvases | Proposed; needs device testing, not settable blind |

### Level 2: packaging changes that move some content to optional downloads

| Option | Saves | Trade-off |
|---|---|---|
| Translations as per-language downloads (keep English and the system language) | ~30 MB installed, ~4 MB download | Language switching needs a download step |
| Rebuild the Krita 4 bundle without the 84 files that duplicate the default resource folders | 6.8 MB | Generated bundle differs from upstream's file; same resources available |
| Re-deflate templates at build time | 9.6 MB installed | No download saving |
| Legacy bundles (Krita 3, Krita 4) as optional downloads | 17.5 MB more | Krita 4 brushes become opt-in |

### Level 3: code changes with real maintenance cost

| Option | Saves | Why it is expensive |
|---|---|---|
| Use installed bundles in place instead of copying them on first run (desktop) | 21-37 MB user data per install, faster first run | `KisResourceLocator` and the resource database store storage paths relative to the user folder; needs schema and migration work. Not possible on Android, where bundles live inside the APK |
| Smaller thumbnails in the resource database | Part of 29-40 MB | Touches every resource type's thumbnail path |
| Fewer colour-engine template instantiations (e.g. route rare models/depths through a generic path) | Several MB of the 24 MB engine | Krita's pixel pipeline; speed regressions likely; large conflicts with upstream |
| Link plugins into fewer, larger libraries | Some size, faster start | Build system rewrite; conflicts with every upstream plugin change |
| Remove whole features (PDF import, EXR, HEIF/AVIF, JPEG XL, G'MIC bridge) | 1-3 MB compressed each | Each is a user-visible format or tool; poor value |

**Not recommended:** `-Os`/`-Oz` builds (measured 2-3× slower blending), xz-compressed
AppImages (slower start for 12 MB), and stopping Android from extracting native libraries
(`useLegacyPackaging`): Krita loads its plugins by file path, so they must exist on disk.
The Qt 6 move is worth doing for support reasons, but it will not make the package smaller.

## Suggested order

1. Read the size reports from the next CI run (Windows and Android especially) and confirm
   the Level 0 and 1 savings.
2. Turn on `IKRIATE_ICF=safe`, then LTO, in one CI run per platform; keep whichever links
   within the time limit and passes a smoke test.
3. Move the Linux build to KDE's dependency packages.
4. Decide on optional language packs and legacy bundles together, since both need a small
   download UI.
5. Treat Level 3 as upstream contributions rather than IKriate patches, so the fork does not
   carry them alone.
