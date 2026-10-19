#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 IKriate contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Generates the bundled IKriate skins (*.ikskin).

An .ikskin file is a regular KDE colour scheme (.colors) with one extra
[IKriate] group. Anything that reads .colors files can read it, and users can
make their own skin by copying one of these files into the skins folder
(Settings > Interface & Skins > Open skins folder) and editing it.

[IKriate] keys:
  Accent        r,g,b   highlight colour for selections and active controls
  ChromeRadius  px      corner radius of the floating panels
  PanelOpacity  0-255   opacity of the floating panels over the canvas
  Stylesheet    file    optional Qt stylesheet next to the skin, applied app-wide
  Description   text    one line shown in the skin picker
"""
from pathlib import Path

HERE = Path(__file__).resolve().parent


def rgb(h):
    h = h.lstrip("#")
    return ",".join(str(int(h[i:i + 2], 16)) for i in (0, 2, 4))


def group(bg, alt, fg, inactive, accent, link, negative="#EE5334",
          neutral="#F7C64C", positive="#97CA40", active=None):
    return {
        "BackgroundNormal": rgb(bg),
        "BackgroundAlternate": rgb(alt),
        "ForegroundNormal": rgb(fg),
        "ForegroundInactive": rgb(inactive),
        "ForegroundActive": rgb(active or fg),
        "ForegroundLink": rgb(link),
        "ForegroundVisited": rgb(inactive),
        "ForegroundNegative": rgb(negative),
        "ForegroundNeutral": rgb(neutral),
        "ForegroundPositive": rgb(positive),
        "DecorationFocus": rgb(accent),
        "DecorationHover": rgb(accent),
    }


SKINS = [
    dict(file="ikriate-dark", name="IKriate Dark",
         description="Soft charcoal with a violet accent. The default.",
         window="#1C1C1F", window_alt="#2A2A2E", view="#141416", view_alt="#1F1F22",
         button="#2C2C31", button_alt="#36363C", fg="#ECEAF0", inactive="#A19FA8",
         accent="#C46FD0", accent_fg="#FFFFFF", link="#D38CDD", radius=16, opacity=244),
    dict(file="ikriate-light", name="IKriate Light",
         description="Bright neutral greys with a violet accent.",
         window="#F2F1F4", window_alt="#E6E4EA", view="#FBFAFC", view_alt="#EFEDF2",
         button="#E4E2E8", button_alt="#D9D6DE", fg="#211F24", inactive="#625E68",
         accent="#9C3FAE", accent_fg="#FFFFFF", link="#9C3FAE", radius=16, opacity=246,
         negative="#B3261E", neutral="#8A6100", positive="#2E7D32"),
    dict(file="ikriate-charcoal", name="IKriate Charcoal",
         description="Neutral dark grey with an amber accent.",
         window="#222327", window_alt="#2A2B30", view="#17181B", view_alt="#1E1F23",
         button="#2C2D32", button_alt="#34363C", fg="#ECE9E3", inactive="#A3A1A0",
         accent="#F2A541", accent_fg="#1A1A1A", link="#F2A541", radius=14, opacity=240),
    dict(file="ikriate-paper", name="IKriate Paper",
         description="Warm light theme for daylight and print work.",
         window="#EDEAE4", window_alt="#E4E0D8", view="#F7F5F1", view_alt="#EFECE6",
         button="#E2DED6", button_alt="#D8D3CA", fg="#24221F", inactive="#5E5A54",
         accent="#B4472A", accent_fg="#FFFFFF", link="#B4472A", radius=14, opacity=245,
         negative="#B3261E", neutral="#8A6100", positive="#2E7D32"),
    dict(file="ikriate-midnight", name="IKriate Midnight",
         description="Deep blue-violet for late sessions.",
         window="#1E1F33", window_alt="#25263D", view="#15162A", view_alt="#1B1C31",
         button="#2A2B45", button_alt="#33344F", fg="#E8E6F5", inactive="#A6A3C2",
         accent="#8E83F7", accent_fg="#111122", link="#A79FFA", radius=16, opacity=235),
    dict(file="ikriate-high-contrast", name="IKriate High Contrast",
         description="Maximum contrast: black panels, white text, yellow accent.",
         window="#000000", window_alt="#0D0D0D", view="#000000", view_alt="#0D0D0D",
         button="#1A1A1A", button_alt="#262626", fg="#FFFFFF", inactive="#D0D0D0",
         accent="#FFD400", accent_fg="#000000", link="#FFD400", radius=8, opacity=255,
         negative="#FF6B57", neutral="#FFD400", positive="#7CFC5A"),
]


def write(s):
    neg = s.get("negative", "#EE5334")
    neu = s.get("neutral", "#F7C64C")
    pos = s.get("positive", "#97CA40")
    groups = {
        "Colors:Window": group(s["window"], s["window_alt"], s["fg"], s["inactive"], s["accent"], s["link"], neg, neu, pos),
        "Colors:Header": group(s["window"], s["window_alt"], s["fg"], s["inactive"], s["accent"], s["link"], neg, neu, pos),
        "Colors:View": group(s["view"], s["view_alt"], s["fg"], s["inactive"], s["accent"], s["link"], neg, neu, pos),
        "Colors:Button": group(s["button"], s["button_alt"], s["fg"], s["inactive"], s["accent"], s["link"], neg, neu, pos),
        "Colors:Tooltip": group(s["button"], s["window"], s["fg"], s["inactive"], s["accent"], s["link"], neg, neu, pos),
        "Colors:Complementary": group(s["window"], s["window_alt"], s["fg"], s["inactive"], s["accent"], s["link"], neg, neu, pos),
        "Colors:Selection": group(s["accent"], s["accent"], s["accent_fg"], s["accent_fg"], s["accent"], s["accent_fg"], neg, neu, pos),
    }
    out = [f"# {s['name']} — bundled IKriate skin. Generated by generate-skins.py.", ""]
    out += ["[ColorEffects:Disabled]", "Color=56,56,56", "ColorAmount=0", "ColorEffect=0",
            "ContrastAmount=0.65", "ContrastEffect=1", "IntensityAmount=0.1", "IntensityEffect=2", ""]
    out += ["[ColorEffects:Inactive]", "ChangeSelectionColor=false", "Enable=false", ""]
    for name, keys in groups.items():
        out.append(f"[{name}]")
        out += [f"{k}={v}" for k, v in sorted(keys.items())]
        out.append("")
    out += ["[General]", f"ColorScheme={s['name']}", f"Name={s['name']}", "shadeSortColumn=true", ""]
    out += ["[KDE]", "contrast=4", ""]
    out += ["[WM]", f"activeBackground={rgb(s['window'])}", f"activeForeground={rgb(s['fg'])}",
            f"inactiveBackground={rgb(s['window'])}", f"inactiveForeground={rgb(s['inactive'])}", ""]
    out += ["[IKriate]", f"Accent={rgb(s['accent'])}", f"ChromeRadius={s['radius']}",
            f"PanelOpacity={s['opacity']}", f"Description={s['description']}", ""]
    (HERE / f"{s['file']}.ikskin").write_text("\n".join(out))


if __name__ == "__main__":
    for skin in SKINS:
        write(skin)
    print("wrote", len(SKINS), "skins")
