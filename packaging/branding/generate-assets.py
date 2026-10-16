#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 IKriate contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Regenerate every raster/platform icon asset for IKriate from the SVG masters
in this directory.

    pip install cairosvg pillow
    python3 packaging/branding/generate-assets.py

Outputs:
  krita/pics/branding/IKriate/      desktop icons (PNG sizes, .svgz, .ico)
  krita/data/splash/                splash banner and artwork
  packaging/android/apk/res/        adaptive-icon layers and legacy mipmaps
"""
import gzip
import io
import shutil
from pathlib import Path

import cairosvg
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

ICON = HERE / "ikriate-icon.svg"
ICON_SMALL = HERE / "ikriate-icon-small.svg"
BANNER = HERE / "ikriate-banner.svg"
SPLASH = HERE / "ikriate-splash-art.svg"

# Same palette and geometry as ikriate-icon.svg (108-unit adaptive grid).
BG = "#17181B"
DOTS = [
    (54, 28, 7, "#F2A541"), (72.4, 35.6, 5, "#E2643E"), (80, 54, 5, "#E58FB0"),
    (72.4, 72.4, 5, "#7B6CF0"), (54, 80, 5, "#5BA8E8"), (35.6, 72.4, 5, "#5FB59A"),
    (28, 54, 5, "#9DB39A"), (35.6, 35.6, 5, "#EFE3CF"),
]
TIP_PATH = "M54,40 C61,47 62,58 54,68 C46,58 47,47 54,40Z"
TIP_COLOR = "#EFE3CF"


def render(svg: Path, size: int) -> Image.Image:
    png = cairosvg.svg2png(url=str(svg), output_width=size, output_height=size)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def desktop_icons():
    out = ROOT / "krita/pics/branding/IKriate"
    out.mkdir(parents=True, exist_ok=True)
    sizes = [16, 22, 24, 32, 48, 64, 128, 256, 512, 1024]
    images = {}
    for s in sizes:
        img = render(ICON_SMALL if s <= 32 else ICON, s)
        img.save(out / f"{s}-apps-krita.png", optimize=True)
        images[s] = img
    with gzip.open(out / "sc-apps-krita.svgz", "wb", compresslevel=9) as f:
        f.write(ICON.read_bytes())
    images[256].save(out / "krita.ico",
                     sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    # Re-use upstream's CMake/qrc wiring; only the artwork differs.
    default = ROOT / "krita/pics/branding/default"
    for name in ("CMakeLists.txt", "branding.qrc"):
        shutil.copy(default / name, out / name)
    # The shell-extension .ico at the branding root is also shown in Explorer.
    shutil.copy(out / "krita.ico", ROOT / "krita/pics/branding/krita.ico")


def splash():
    out = ROOT / "krita/data/splash"
    shutil.copy(BANNER, out / "banner.svg")
    png = cairosvg.svg2png(url=str(SPLASH), output_width=2560, output_height=1440)
    Image.open(io.BytesIO(png)).convert("RGB").save(out / "ikriate_splash.png", optimize=True)
    hd = cairosvg.svg2png(url=str(SPLASH), output_width=1280, output_height=720)
    Image.open(io.BytesIO(hd)).convert("RGB").save(out / "ikriate_splash_HD.jpg", quality=90)


def android_icons():
    res = ROOT / "packaging/android/apk/res"
    (res / "values/ic_launcher_background.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
        f'    <color name="ic_launcher_background">{BG}</color>\n</resources>\n')
    (res / "drawable/ic_launcher_background.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
        '    android:width="108dp" android:height="108dp"\n'
        '    android:viewportWidth="108" android:viewportHeight="108">\n'
        f'  <path android:pathData="M0,0h108v108h-108z" android:fillColor="{BG}"/>\n'
        '</vector>\n')

    def circle(cx, cy, r):
        return (f"M{cx - r},{cy} a{r},{r} 0 1,0 {2 * r},0 a{r},{r} 0 1,0 {-2 * r},0")

    dots = "\n".join(
        f'  <path android:fillColor="{c}" android:pathData="{circle(x, y, r)}"/>'
        for x, y, r, c in DOTS)
    (res / "drawable/ic_launcher_foreground.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<!-- IKriate adaptive icon foreground: concept D "Radial" -->\n'
        '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
        '    android:width="108dp" android:height="108dp"\n'
        '    android:viewportWidth="108" android:viewportHeight="108">\n'
        f'{dots}\n'
        '  <group android:rotation="35" android:pivotX="54" android:pivotY="54">\n'
        f'    <path android:fillColor="{TIP_COLOR}" android:pathData="{TIP_PATH}"/>\n'
        '  </group>\n'
        '</vector>\n')

    densities = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
    for d, s in densities.items():
        img = render(ICON, s)
        img.save(res / f"mipmap-{d}/ic_launcher.webp", "WEBP", lossless=True)
        img.save(res / f"mipmap-{d}/ic_launcher_round.webp", "WEBP", lossless=True)


if __name__ == "__main__":
    desktop_icons()
    splash()
    android_icons()
    print("IKriate branding assets regenerated.")
