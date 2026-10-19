#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 IKriate contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Removes third-party libraries the IKriate tablet package no longer uses.

KDE's prebuilt Android dependency packages are installed in full into the same
prefix as IKriate, and the APK packaging copies every library from <prefix>/lib.
With IKRIATE_TABLET_PACKAGE, several features are not built (audio through MLT,
HEIF/AVIF, JPEG XL, camera RAW, OpenColorIO, SeExpr), so their libraries are
dead weight. This script deletes a library only when it matches one of the
patterns below AND no remaining library or plugin links to it (DT_NEEDED),
repeating until nothing more can go. Libraries loaded with dlopen() at run time
(OpenSSL for Qt's network module, Qt plugins) are never candidates.

Bundled font files are removed only when the packaged fontconfig setup also
reads the system fonts (/system/fonts), so the Text tool keeps a font list.

Usage: prune-tablet-libs.py <install prefix> [--dry-run]
"""
import fnmatch
import os
import re
import shutil
import struct
import sys

CANDIDATES = [
    # audio in animations (MLT and its SDL output)
    "libmlt*.so*", "libmlt++*.so*", "libSDL2*.so*",
    # HEIF/AVIF and their codecs
    "libheif*.so*", "libde265*.so*", "libaom*.so*", "libdav1d*.so*", "libx265*.so*", "libkvazaar*.so*",
    "libSvtAv1*.so*", "librav1e*.so*",
    # JPEG XL
    "libjxl*.so*", "libhwy*.so*", "libbrotli*.so*",
    # camera RAW
    "libraw*.so*", "libKF5KDcraw*.so*", "libKDcrawQt*.so*",
    # OpenColorIO (LUT docker)
    "libOpenColorIO*.so*", "libyaml-cpp*.so*", "libpystring*.so*", "libminizip*.so*",
    # SeExpr fill layer
    "libkseexpr*.so*", "libKSeExpr*.so*",
    # G'MIC and network transfer helpers that nothing links any more
    "libgmic*.so*", "libcurl*.so*",
]

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


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry = "--dry-run" in sys.argv
    if not args:
        sys.exit(__doc__)
    prefix = os.path.abspath(args[0])
    libdir = os.path.join(prefix, "lib")

    elves = {}
    for dirpath, _, files in os.walk(prefix):
        for f in files:
            p = os.path.join(dirpath, f)
            if not os.path.islink(p) and (".so" in f or os.access(p, os.X_OK)) and is_elf(p):
                elves[p] = needed_libs(p)

    def candidate(path):
        name = os.path.basename(path)
        return os.path.dirname(path) == libdir and any(fnmatch.fnmatch(name, pat) for pat in CANDIDATES)

    removed = {}
    while True:
        wanted = set()
        for p, needs in elves.items():
            wanted.update(needs)
        gone = [p for p in elves if candidate(p) and os.path.basename(p) not in wanted]
        if not gone:
            break
        for p in gone:
            removed[p] = os.path.getsize(p)
            del elves[p]

    for p in sorted(removed):
        print(f"remove {os.path.relpath(p, prefix)} ({human(removed[p])})")
        if not dry:
            os.remove(p)
            # versioned symlinks pointing at it
            base = os.path.basename(p).split(".so")[0]
            for f in os.listdir(libdir):
                q = os.path.join(libdir, f)
                if os.path.islink(q) and f.startswith(base + ".so") and not os.path.exists(q):
                    os.remove(q)

    total = sum(removed.values())
    kept = sorted(os.path.basename(p) for p in elves if candidate(p))
    if kept:
        print("kept (still linked):", ", ".join(kept))

    # MLT's plugin modules and data go with MLT itself.
    if not any(os.path.basename(p).startswith("libmlt") for p in elves):
        for d in (os.path.join(libdir, "mlt-7"), os.path.join(prefix, "share", "mlt-7")):
            if os.path.isdir(d):
                size = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(d) for f in fs)
                total += size
                print(f"remove {os.path.relpath(d, prefix)}/ ({human(size)})")
                if not dry:
                    shutil.rmtree(d)
        jar = os.path.join(prefix, "jar", "SDL2Android.jar")
        if os.path.exists(jar) and not any(os.path.basename(p).startswith("libSDL2") for p in elves):
            print("remove jar/SDL2Android.jar")
            if not dry:
                os.remove(jar)

    # libheif loads its codecs from a plugin folder.
    heif_plugins = os.path.join(libdir, "libheif")
    if os.path.isdir(heif_plugins) and not any(os.path.basename(p).startswith("libheif") for p in elves):
        size = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(heif_plugins) for f in fs)
        total += size
        print(f"remove lib/libheif/ ({human(size)})")
        if not dry:
            shutil.rmtree(heif_plugins)

    # Bundled fonts, only if fontconfig is set up to read the system fonts too.
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
            if not dry:
                for f in fonts:
                    os.remove(f)
        else:
            print(f"keep {len(fonts)} bundled font files ({human(size)}): fontconfig does not read /system/fonts")

    # Documentation, headers and build metadata the dependency packages install;
    # share/ ends up in the APK's assets.
    for sub in ("share/doc", "share/info", "share/man", "share/gtk-doc", "share/gettext",
                "share/aclocal", "share/bash-completion", "share/cmake", "share/pkgconfig",
                "include", "lib/pkgconfig", "lib/cmake"):
        d = os.path.join(prefix, sub)
        if os.path.isdir(d):
            size = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(d) for f in fs
                       if not os.path.islink(os.path.join(r, f)))
            total += size
            print(f"remove {sub}/ ({human(size)})")
            if not dry:
                shutil.rmtree(d)

    # Byte-identical copies of a library under a second name (versioned
    # aliases that were installed as files rather than symlinks), when no
    # library links against that second name.
    import hashlib
    by_hash = {}
    for p in sorted(elves):
        # OpenSSL is loaded by name at run time (Qt looks for the _1_1 names).
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
            if p in keep or p in linked:
                continue
            size = os.path.getsize(p)
            total += size
            print(f"remove duplicate {os.path.relpath(p, prefix)} ({human(size)}) of {os.path.basename(keep[0])}")
            if not dry:
                os.remove(p)

    print(f"{'would free' if dry else 'freed'} {human(total)} (uncompressed)")


if __name__ == "__main__":
    main()
