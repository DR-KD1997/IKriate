#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 IKriate contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Trims the IKriate tablet package to what it uses.

KDE's prebuilt Android dependency packages are installed in full into the same
prefix as IKriate, and the APK packaging copies every library in <prefix>/lib.
This script, run between the build and the packaging:

1. Replaces ICU (about 36 MB with its data) with a small stub that forwards
   Qt Core's 22 ICU calls to Android's own ICU where it is public (Android 12
   and later) and lets Qt fall back to its built-in code elsewhere (see
   icu-stub/ikicu.c). Needs the NDK (--ndk, or $ANDROID_NDK_ROOT); skipped
   with a notice otherwise.
2. Removes Qt plugins and QML modules IKriate does not use: QML debugging,
   the Quick Controls 2 styles other than the default one, Particles,
   LocalStorage, StateMachine, Graphical Effects, the bearer plugin, and the
   ICNS, WBMP and EPS image formats.
3. Removes every third-party library in <prefix>/lib that nothing left links
   to (DT_NEEDED), from the libraries, Krita plugins, Qt plugins and QML
   modules that stay. With FFmpeg and MLT no longer built for the tablet
   package this drops FFmpeg, MLT, SDL2 and their codecs, gettext's tools,
   unused second names of libraries (libOpenEXR next to libOpenEXR-3_4) and
   whatever else only came along with KDE's packages. Libraries loaded with
   dlopen() at run time (OpenSSL for Qt's network module) are kept.
4. Bundled fonts (when fontconfig also reads /system/fonts), documentation,
   and byte-identical copies of a library under a second name.

Usage: prune-tablet-libs.py <install prefix> [--ndk <path>] [--dry-run]
"""
import fnmatch
import glob
import os
import re
import shutil
import struct
import subprocess
import sys

# Never removed: IKriate itself, its plugins, Qt/KDE Frameworks (checked by
# androiddeployqt), and libraries opened by name at run time.
KEEP = [
    "libkrita*", "krita*.so", "libKF5*", "libKF6*", "libc++_shared.so",
    "libcrypto_1_1*.so", "libssl_1_1*.so", "libcrypto_3*.so", "libssl_3*.so",
    "preloadable_libintl.so", "libikicu.so",
]
# Qt libraries may go when nothing links them (Help, Test, QuickTest, the
# Particles module once its QML plugin is gone).
QT_REMOVABLE = ["libQt5Help*", "libQt5Test*", "libQt5QuickTest*", "libQt5QuickParticles*",
                "libQt6Help*", "libQt6Test*", "libQt6QuickTest*", "libQt6QuickParticles*"]
# Directories whose libraries go with the library that loads them.
PLUGIN_DIRS = ["lib/mlt-7", "lib/libheif"]

UNUSED_PLUGINS = [
    "plugins/qmltooling", "plugins/bearer",
    "plugins/imageformats/*qicns*", "plugins/imageformats/*qwbmp*", "plugins/imageformats/*kimg_eps*",
    "qml/QtQuick/Controls.2/Imagine", "qml/QtQuick/Controls.2/Material",
    "qml/QtQuick/Controls.2/Universal", "qml/QtQuick/Controls.2/Fusion",
    "qml/QtQuick/Particles.2", "qml/QtQuick/LocalStorage", "qml/QtQml/StateMachine",
    "qml/QtGraphicalEffects", "qml/QtTest",
]

ICU_LIBS = ("libicuuc.so", "libicui18n.so", "libicudata.so")
ICU_SYMBOL = re.compile(r"^(?:u|ucnv|ucol|UCNV)_\w+?_(\d+)$")
# ELF e_machine -> NDK clang target
NDK_TARGETS = {183: "aarch64-linux-android", 40: "armv7a-linux-androideabi", 62: "x86_64-linux-android",
               3: "i686-linux-android"}
MIN_API = 24  # minSdkVersion in apk/build.gradle

DT_NEEDED = 1


def needed_libs(path):
    """DT_NEEDED entries of an ELF shared object (32/64-bit, little endian)."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return []
    if data[:4] != b"\x7fELF":
        return []
    is64 = data[4] == 2
    if data[5] != 1:
        return []
    if is64:
        e_phoff, = struct.unpack_from("<Q", data, 0x20)
        e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x36)
    else:
        e_phoff, = struct.unpack_from("<I", data, 0x1C)
        e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x2A)
    loads, dynamic = [], None
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if is64:
            p_type, _, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<IIQQQQ", data, off)
        else:
            p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<IIIII", data, off)
        if p_type == 1:
            loads.append((p_vaddr, p_offset, p_filesz))
        elif p_type == 2:
            dynamic = (p_offset, p_filesz)
    if not dynamic:
        return []

    def vaddr_to_off(v):
        for vaddr, offset, size in loads:
            if vaddr <= v < vaddr + size:
                return v - vaddr + offset
        return None

    entries, strtab = [], None
    step = 16 if is64 else 8
    fmt = "<qQ" if is64 else "<iI"
    for off in range(dynamic[0], dynamic[0] + dynamic[1], step):
        tag, val = struct.unpack_from(fmt, data, off)
        if tag == 0:
            break
        if tag == 5:  # DT_STRTAB
            strtab = vaddr_to_off(val)
        elif tag == DT_NEEDED:
            entries.append(val)
    if strtab is None:
        return []
    out = []
    for e in entries:
        end = data.index(b"\0", strtab + e)
        out.append(data[strtab + e:end].decode())
    return out


def is_elf(path):
    try:
        with open(path, "rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False


def human(n):
    return f"{n / 1e6:.1f} MB"


def tree_size(path):
    if os.path.isfile(path):
        return os.path.getsize(path)
    return sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(path) for f in fs
               if not os.path.islink(os.path.join(r, f)))


def remove(path, dry):
    if dry:
        return
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path)
    else:
        os.remove(path)


def elf_machine(path):
    with open(path, "rb") as f:
        head = f.read(20)
    return struct.unpack_from("<H", head, 18)[0]


def rewrite_needed(path, mapping, dry):
    """Renames DT_NEEDED entries in place (new names no longer than the old,
    padded with NULs): no relinking or patchelf, nothing else moves."""
    with open(path, "rb") as f:
        data = bytearray(f.read())
    is64 = data[4] == 2
    if is64:
        e_phoff, = struct.unpack_from("<Q", data, 0x20)
        e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x36)
    else:
        e_phoff, = struct.unpack_from("<I", data, 0x1C)
        e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x2A)
    loads, dynamic = [], None
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if is64:
            p_type, _, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<IIQQQQ", data, off)
        else:
            p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<IIIII", data, off)
        if p_type == 1:
            loads.append((p_vaddr, p_offset, p_filesz))
        elif p_type == 2:
            dynamic = (p_offset, p_filesz)
    step, fmt = (16, "<qQ") if is64 else (8, "<iI")
    strtab, needed = None, []
    for off in range(dynamic[0], dynamic[0] + dynamic[1], step):
        tag, val = struct.unpack_from(fmt, data, off)
        if tag == 0:
            break
        if tag == 5:
            strtab = next(val - va + o for va, o, sz in loads if va <= val < va + sz)
        elif tag == DT_NEEDED:
            needed.append(val)
    changed = []
    for val in needed:
        start = strtab + val
        end = data.index(b"\0", start)
        name = data[start:end].decode()
        if name in mapping:
            new = mapping[name].encode()
            assert len(new) <= end - start
            data[start:end] = new + b"\0" * (end - start - len(new))
            changed.append(name)
    if changed and not dry:
        with open(path, "wb") as f:
            f.write(data)
    return changed


def find_ndk(argv):
    if "--ndk" in argv:
        return argv[argv.index("--ndk") + 1]
    for var in ("ANDROID_NDK_ROOT", "ANDROID_NDK_HOME", "ANDROID_NDK"):
        if os.environ.get(var):
            return os.environ[var]
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if sdk:
        found = sorted(glob.glob(os.path.join(sdk, "ndk", "*")))
        if found:
            return found[-1]
    return None


def nm(ndk_bin, path, flag):
    out = subprocess.run([os.path.join(ndk_bin, "llvm-nm"), "-D", flag, path],
                         capture_output=True, text=True, check=True).stdout
    return {line.split()[-1] for line in out.splitlines() if line.strip()}


def install_icu_stub(libdir, ndk, dry):
    """Builds icu-stub/ikicu.c for Qt Core's ICU version and points Qt Core at
    it. Returns a message; ICU stays in place whenever anything doesn't fit."""
    qtcore = sorted(glob.glob(os.path.join(libdir, "libQt5Core*.so")) +
                    glob.glob(os.path.join(libdir, "libQt6Core*.so")))
    if not qtcore:
        return "ICU stub: no Qt Core found, ICU kept"
    qtcore = qtcore[0]
    if not set(needed_libs(qtcore)) & set(ICU_LIBS):
        return "ICU stub: Qt Core does not use ICU"
    if not ndk:
        return "ICU stub: no NDK given (--ndk or $ANDROID_NDK_ROOT), ICU kept"
    ndk_bin = os.path.join(ndk, "toolchains", "llvm", "prebuilt", "linux-x86_64", "bin")
    target = NDK_TARGETS.get(elf_machine(qtcore))
    clang = os.path.join(ndk_bin, f"{target}{MIN_API}-clang") if target else None
    if not clang or not os.path.exists(clang):
        return f"ICU stub: no NDK compiler for this ABI in {ndk_bin}, ICU kept"
    imports = {s for s in nm(ndk_bin, qtcore, "--undefined-only") if ICU_SYMBOL.match(s)}
    versions = {ICU_SYMBOL.match(s).group(1) for s in imports}
    if len(versions) != 1:
        return f"ICU stub: unexpected ICU symbols in Qt Core ({sorted(imports)[:5]}), ICU kept"
    stub = os.path.join(libdir, "libikicu.so")
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icu-stub", "ikicu.c")
    try:
        subprocess.run([clang, "-shared", "-fPIC", "-O2", "-fvisibility=hidden", "-DICU_MAJOR=" + versions.pop(),
                        "-Wl,-soname,libikicu.so", "-Wl,-z,max-page-size=16384", "-o", stub, src, "-ldl"],
                       check=True)
        missing = imports - nm(ndk_bin, stub, "--defined-only")
    except (OSError, subprocess.CalledProcessError) as e:
        if os.path.exists(stub):
            os.remove(stub)
        return f"ICU stub: could not be built ({e}), ICU kept"
    if missing:
        os.remove(stub)
        return f"ICU stub: Qt Core needs ICU functions the stub lacks ({', '.join(sorted(missing))}), ICU kept"
    mapping = {name: "libikicu.so" for name in ICU_LIBS}
    changed = rewrite_needed(qtcore, mapping, dry)
    if dry:
        os.remove(stub)
    return (f"ICU stub: {os.path.basename(qtcore)} now uses libikicu.so instead of {', '.join(changed)} "
            f"({len(imports)} functions)")


def main():
    args = [a for i, a in enumerate(sys.argv[1:], 1)
            if not a.startswith("--") and sys.argv[i - 1] != "--ndk"]
    dry = "--dry-run" in sys.argv
    if not args:
        sys.exit(__doc__)
    prefix = os.path.abspath(args[0])
    libdir = os.path.join(prefix, "lib")
    total = 0

    # 1. ICU
    print(install_icu_stub(libdir, find_ndk(sys.argv), dry))

    # 2. Unused Qt plugins and QML modules
    for pattern in UNUSED_PLUGINS:
        for path in sorted(glob.glob(os.path.join(prefix, pattern))):
            size = tree_size(path)
            total += size
            print(f"remove {os.path.relpath(path, prefix)} ({human(size)})")
            remove(path, dry)

    # 3. Libraries nothing uses
    plugin_dirs = [os.path.join(prefix, d) for d in PLUGIN_DIRS]

    def candidate(path):
        name = os.path.basename(path)
        if any(path.startswith(d + os.sep) for d in plugin_dirs):
            return True
        if os.path.dirname(path) != libdir:
            return False
        if any(fnmatch.fnmatch(name, pat) for pat in QT_REMOVABLE):
            return True
        if name.startswith(("libQt5", "libQt6")):
            return False
        if re.match(r"lib(crypto|ssl)\.so$", name) and not glob.glob(os.path.join(libdir, name[:-3] + "_*.so")):
            return False  # the only OpenSSL there is: Qt opens it by name
        return not any(fnmatch.fnmatch(name, pat) for pat in KEEP)

    elves = {}
    for dirpath, dirs, files in os.walk(prefix):
        rel = os.path.relpath(dirpath, prefix).split(os.sep)[0]
        if rel in ("bin", "libexec", "share", "include"):
            dirs[:] = []
            continue
        for f in files:
            p = os.path.join(dirpath, f)
            if not os.path.islink(p) and (".so" in f or os.access(p, os.X_OK)) and is_elf(p):
                elves[p] = needed_libs(p)
    if dry:
        # Show the effect of the ICU stub without having written it.
        for p, needs in elves.items():
            if os.path.basename(p).startswith(("libQt5Core", "libQt6Core")):
                elves[p] = [("libikicu.so" if n in ICU_LIBS else n) for n in needs]

    by_name = {}
    for p in elves:
        by_name.setdefault(os.path.basename(p), p)
    reachable, stack = set(), [p for p in elves if not candidate(p)]
    while stack:
        p = stack.pop()
        if p in reachable:
            continue
        reachable.add(p)
        stack.extend(by_name[n] for n in elves[p] if n in by_name)
    gone = sorted(p for p in elves if p not in reachable)
    for p in gone:
        size = os.path.getsize(p)
        total += size
        print(f"remove {os.path.relpath(p, prefix)} ({human(size)})")
        remove(p, dry)
        del elves[p]
    if not dry:
        # versioned symlinks left pointing at nothing
        for f in os.listdir(libdir):
            q = os.path.join(libdir, f)
            if os.path.islink(q) and not os.path.exists(q):
                os.remove(q)
    # Plugin folders and data of libraries that went
    for d in plugin_dirs:
        if os.path.isdir(d) and not any(p.startswith(d + os.sep) for p in elves):
            size = tree_size(d)
            total += size
            print(f"remove {os.path.relpath(d, prefix)}/ ({human(size)})")
            remove(d, dry)
    if not any(os.path.basename(p).startswith("libmlt") for p in elves):
        for extra in ("share/mlt-7", "jar/SDL2Android.jar"):
            path = os.path.join(prefix, extra)
            if os.path.exists(path) and not (extra.endswith(".jar") and
                                             any("SDL2" in os.path.basename(p) for p in elves)):
                size = tree_size(path)
                total += size
                print(f"remove {extra} ({human(size)})")
                remove(path, dry)

    # 4a. Bundled fonts, only if fontconfig is set up to read the system fonts too.
    confs = []
    for dirpath, _, files in os.walk(os.path.join(prefix, "etc", "fonts")):
        confs += [os.path.join(dirpath, f) for f in files if f.endswith(".conf")]
    uses_system = any("/system/fonts" in open(c, encoding="utf8", errors="replace").read() for c in confs)
    fonts = []
    for sub in ("share/fonts", "fonts", "etc/fonts"):
        for dirpath, _, files in os.walk(os.path.join(prefix, sub)):
            fonts += [os.path.join(dirpath, f) for f in files if re.search(r"\.(ttf|otf|ttc|pfb)$", f, re.I)]
    if fonts:
        size = sum(os.path.getsize(f) for f in fonts)
        if uses_system:
            print(f"remove {len(fonts)} bundled font files ({human(size)}); fontconfig reads /system/fonts")
            total += size
            for f in fonts:
                remove(f, dry)
        else:
            print(f"keep {len(fonts)} bundled font files ({human(size)}): fontconfig does not read /system/fonts")

    # 4b. Documentation the dependency packages install; share/ ends up in the
    # APK's assets. (Not the CMake/pkg-config files: the packaging step re-runs
    # CMake against this prefix; build.gradle leaves them out of the assets.)
    for sub in ("share/doc", "share/info", "share/man", "share/gtk-doc", "share/gettext",
                "share/bash-completion"):
        d = os.path.join(prefix, sub)
        if os.path.isdir(d):
            size = tree_size(d)
            total += size
            print(f"remove {sub}/ ({human(size)})")
            remove(d, dry)

    # 4c. Byte-identical copies of a library under a second name that nothing
    # links against by that name.
    import hashlib
    by_hash = {}
    for p in sorted(elves):
        if os.path.dirname(p) == libdir and not re.match(r"lib(crypto|ssl)", os.path.basename(p)):
            with open(p, "rb") as f:
                by_hash.setdefault(hashlib.md5(f.read()).hexdigest(), []).append(p)
    wanted = set()
    for needs in elves.values():
        wanted.update(needs)
    for paths in by_hash.values():
        if len(paths) < 2:
            continue
        linked = [p for p in paths if os.path.basename(p) in wanted]
        keep = linked or [min(paths, key=lambda p: len(os.path.basename(p)))]
        for p in paths:
            if p in keep:
                continue
            size = os.path.getsize(p)
            total += size
            print(f"remove duplicate {os.path.relpath(p, prefix)} ({human(size)}) of {os.path.basename(keep[0])}")
            remove(p, dry)

    print(f"{'would free' if dry else 'freed'} {human(total)} (uncompressed)")


if __name__ == "__main__":
    main()
