#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 IKriate contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds IKriate's resource bundles from the upstream Krita bundles.

Run after merging a new Krita release (needs `pip install pyoxipng`):

    python3 packaging/branding/make-bundles.py

What it does, compared with upstream:
- IKriate_Brushes.bundle replaces Krita_4_Default_Resources.bundle and also takes in
  the loose default presets, so every default brush is in one place;
- eraser-only presets are dropped (erasing is a mode of every brush in IKriate);
- brush tips and patterns that are byte-identical to files already installed in the
  default resource folders are dropped (6.7 MB of duplicate patterns);
- PNG images are re-optimised losslessly;
- every preset is tagged with an IKriate brush category (Pencils, Pens, ...), which the
  brush panel shows as its category list;
- bundle metadata no longer names or links to Krita; author credits and licences stay.
"""
import hashlib
import io
import re
import struct
import zipfile
import zlib
from pathlib import Path
from xml.sax.saxutils import escape

try:
    import oxipng
except ImportError:  # optional: images are copied unchanged without it
    oxipng = None

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "krita" / "data"
BUNDLES = DATA / "bundles"
UPSTREAM = BUNDLES / "upstream"   # untouched upstream bundles, kept for re-runs

# (category, regular expression on the preset name). First match wins.
CATEGORIES = [
    ("Pencils", r"Pencil|Sketching|Charcoal_Pencil"),
    ("Pens", r"Ink-[1-4]|Ink_pen|Fineliner|Precision|Gpen"),
    ("Calligraphy", r"Ink-[5-9]|Sumi|Chisel"),
    ("Markers", r"Marker"),
    ("Watercolor", r"WaterC|Watercolor|Waterpaint"),
    ("Sprayers", r"Airbrush|Spray|Splat|Snow|Starfield|Particles"),
    ("Charcoal & Chalk", r"Chalk|Charcoal"),
    ("Blenders", r"Blender|Smear|Knife"),
    ("Paint", r"Bristles|Wet_|Dry_|Paint|Roller|Glaze|RGBA"),
    ("Textures", r"Texture|Screentone|Impressionism|Pointillism|Hair|Wood|Reptile|Crackles|Noise|Spines|Webs"),
    ("Stamps", r"Stamp"),
    ("Shapes", r"Shapes"),
    ("Pixel Art", r"Pixel_Art"),
    ("Effects", r"Adjust|Filter|Distort|Clone|Normal_Map|Experimental|Leaky"),
    ("Basic", r"Basic"),
]

ERASER_RE = re.compile(r"eraser", re.I)


def category_of(name):
    for cat, rx in CATEGORIES:
        if re.search(rx, name):
            return cat
    return "Basic"


def preset_xml(data):
    """Settings XML embedded in a .kpp (PNG zTXt/tEXt 'preset' chunk)."""
    pos, out = 8, ""
    while pos + 8 <= len(data):
        ln, = struct.unpack(">I", data[pos:pos + 4])
        typ, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"zTXt" and body.startswith(b"preset\0"):
            out += zlib.decompress(body[8:]).decode("utf8", "replace")
        elif typ == b"tEXt" and body.startswith(b"preset\0"):
            out += body[7:].decode("utf8", "replace")
    return out


def is_eraser_preset(name, data):
    xml = preset_xml(data)
    erase_op = re.search(r'name="CompositeOp"><!\[CDATA\[erase\]\]>', xml)
    return bool(erase_op or ERASER_RE.search(name))


def optimise(path, data):
    if oxipng and path.lower().endswith(".png") and not path.endswith(".kpp"):
        try:
            return oxipng.optimize_from_memory(data, level=4)
        except Exception:
            pass
    return data


def md5(data):
    return hashlib.md5(data).hexdigest()


def loose_hashes():
    hashes = set()
    for sub in ("brushes", "patterns"):
        for f in (DATA / sub).rglob("*"):
            if f.is_file():
                hashes.add(md5(f.read_bytes()))
    return hashes


def write_bundle(out, files, tags, meta, preview):
    """files: {path: bytes}; tags: {path: [tag, ...]}"""
    manifest = ['<?xml version="1.0" encoding="UTF-8"?>',
                '<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.2">',
                ' <manifest:file-entry manifest:media-type="application/x-krita-resourcebundle" manifest:full-path="/"/>']
    for path in sorted(files):
        mtype = path.split("/")[0]
        entry = f' <manifest:file-entry manifest:media-type="{mtype}" manifest:full-path="{escape(path)}" manifest:md5sum="{md5(files[path])}"'
        if tags.get(path):
            manifest.append(entry + ">")
            manifest.append("  <manifest:tags>")
            manifest += [f"   <manifest:tag>{escape(t)}</manifest:tag>" for t in tags[path]]
            manifest.append("  </manifest:tags>")
            manifest.append(" </manifest:file-entry>")
        else:
            manifest.append(entry + "/>")
    manifest.append("</manifest:manifest>")
    with zipfile.ZipFile(out, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/x-krita-resourcebundle", compress_type=zipfile.ZIP_STORED)
        for path in sorted(files):
            z.writestr(path, files[path], compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
        z.writestr("META-INF/manifest.xml", "\n".join(manifest) + "\n", compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("meta.xml", meta, compress_type=zipfile.ZIP_DEFLATED)
        if preview:
            z.writestr("preview.png", preview, compress_type=zipfile.ZIP_STORED)
    print(f"{out.name}: {len(files)} resources, {out.stat().st_size / 1e6:.2f} MB")


def meta_xml(author, description, license_, date):
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<meta:meta>
 <meta:generator>IKriate</meta:generator>
 <meta:bundle-version>1</meta:bundle-version>
 <dc:author>{escape(author)}</dc:author>
 <dc:description>{escape(description)}</dc:description>
 <meta:initial-creator>{escape(author)}</meta:initial-creator>
 <dc:creator>{escape(author)}</dc:creator>
 <meta:creation-date>{date}</meta:creation-date>
 <meta:dc-date>{date}</meta:dc-date>
 <meta:meta-userdefined meta:name="email" meta:value=""/>
 <meta:meta-userdefined meta:name="license" meta:value="{escape(license_)}"/>
 <meta:meta-userdefined meta:name="website" meta:value=""/>
</meta:meta>
"""


def read_tags(manifest):
    tags = {}
    for m in re.finditer(r'manifest:full-path="([^"]+)"[^>]*?>\s*<manifest:tags>(.*?)</manifest:tags>', manifest, re.S):
        tags[m.group(1)] = re.findall(r"<manifest:tag>(.*?)</manifest:tag>", m.group(2))
    return tags


def build_brushes():
    src = zipfile.ZipFile(UPSTREAM / "Krita_4_Default_Resources.bundle")
    old_tags = read_tags(src.read("META-INF/manifest.xml").decode())
    dupes = loose_hashes()
    files, tags, dropped = {}, {}, {"eraser": [], "duplicate": 0}
    for info in src.infolist():
        name = info.filename
        if info.is_dir() or name in ("mimetype", "meta.xml", "preview.png") or name.startswith("META-INF"):
            continue
        data = src.read(info)
        kind = name.split("/")[0]
        if kind == "paintoppresets" and is_eraser_preset(name, data):
            dropped["eraser"].append(name)
            continue
        if kind in ("brushes", "patterns") and md5(data) in dupes:
            dropped["duplicate"] += 1
            continue
        files[name] = optimise(name, data)
    # The loose default presets join the bundle so they get categories too.
    for f in sorted((UPSTREAM / "loose-paintoppresets").glob("*.kpp")):
        data = f.read_bytes()
        if not is_eraser_preset(f.name, data):
            files["paintoppresets/" + f.name] = data
    for path in files:
        if path.startswith("paintoppresets/"):
            name = Path(path).stem
            favourite = [t for t in old_tags.get(path, []) if "Favorite" in t]
            tags[path] = [category_of(name)] + (["★ My Favorites"] if favourite else [])
    write_bundle(BUNDLES / "IKriate_Brushes.bundle", files, tags,
                 meta_xml("David Revoy (Deevad), with brushes derived from Ramon Miranda, Razvanc, Radian, "
                          "Wolthera van Hövell, Storm, Scottyp and others",
                          "The standard IKriate brushes, brush tips and patterns.", "CC-0", "2026-10-19"),
                 src.read("preview.png"))
    print("  dropped eraser presets:", ", ".join(Path(p).stem for p in dropped["eraser"]))
    print("  dropped duplicate tips/patterns:", dropped["duplicate"])


def rewrite_meta(upstream_name, new_name, author, description, license_):
    src = zipfile.ZipFile(UPSTREAM / upstream_name)
    manifest = src.read("META-INF/manifest.xml").decode()
    tags = read_tags(manifest)
    files = {}
    for info in src.infolist():
        name = info.filename
        if info.is_dir() or name in ("mimetype", "meta.xml", "preview.png") or name.startswith("META-INF"):
            continue
        files[name] = optimise(name, src.read(info))
    preview = src.read("preview.png") if "preview.png" in src.namelist() else None
    write_bundle(BUNDLES / new_name, files, tags, meta_xml(author, description, license_, "2026-10-19"), preview)


def write_category_tags():
    """One .tag file per brush category, so the tags exist even when empty.

    Presets installed loose (the MyPaint brushes) are assigned through the tag
    file's "Default Resources" list; bundled presets through the bundle manifest.
    """
    out = DATA / "paintoppresets"
    loose = sorted(p.name for p in (ROOT / "plugins/paintops/mypaint/brushes").glob("*.myb"))
    for old in out.glob("ikriate-*.tag"):
        old.unlink()
    for i, (cat, _) in enumerate(CATEGORIES):
        slug = re.sub(r"[^a-z]+", "-", cat.lower()).strip("-")
        members = [f for f in loose if category_of(Path(f).stem) == cat]
        text = ("[Desktop Entry]\nType=Tag\nResourceType=paintoppreset\n"
                f"Name={cat}\nComment=IKriate brush category\nURL={cat}\n")
        if members:
            text += "Default Resources=" + ",".join(members) + "\n"
        (out / f"ikriate-{i:02d}-{slug}.tag").write_text(text, encoding="utf8")


if __name__ == "__main__":
    write_category_tags()
    build_brushes()
    rewrite_meta("RGBA_brushes.bundle", "RGBA_Brushes.bundle", "Ramon Miranda",
                 "Thick paint and textured RGBA brushes, with animated brush tips.", "CC-0")
    rewrite_meta("Krita_Artists_SeExpr_examples.bundle", "SeExpr_Examples.bundle",
                 "Wolthera van Hövell, David Revoy",
                 "Community-contributed SeExpr scripts for the procedural fill layer.", "CC-BY-SA")
