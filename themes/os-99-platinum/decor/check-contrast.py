#!/usr/bin/env python3
"""Audit a theme's colors.toml against WCAG 2.1 contrast.

    python3 check-contrast.py [theme-dir ...]

Foreground colours are checked against the theme's own `background` and, for
light schemes, against the #DDDDDD chrome as well -- ANSI colours show up on
both. Targets: 4.5:1 (AA, normal text) on the document background, 3:1 on
chrome. The palette's "normal" ANSI entries are deliberately held to 7:1 so
they stay clearly apart from their "bright" partners; fitting both to one
target makes each pair indistinguishable.
"""
import re, sys, pathlib

def _lin(c):
    c /= 255.0
    return c/12.92 if c <= 0.04045 else ((c + 0.055)/1.055) ** 2.4
def lum(rgb):
    r, g, b = (_lin(v) for v in rgb)
    return 0.2126*r + 0.7152*g + 0.0722*b
def ratio(a, b):
    la, lb = lum(a), lum(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)
def hexs(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))

SKIP = {"background","dark_background","darker_background","lighter_background",
        "foreground","bright_foreground","light_foreground","selection"}

def audit(d):
    p = pathlib.Path(d) / "colors.toml"
    s = p.read_text()
    bg = re.search(r'^background\s*=\s*"(#[0-9A-Fa-f]{6})"', s, re.M).group(1)
    light = re.search(r'^mode\s*=\s*"(\w+)"', s, re.M).group(1) == "light"
    print(f"\n{p.parent.name}  (mode {'light' if light else 'dark'}, background {bg})")
    bad = 0
    for m in re.finditer(r'^(\w+)\s*=\s*"(#[0-9A-Fa-f]{6})"', s, re.M):
        n, h = m.group(1), m.group(2)
        if n in SKIP:
            continue
        r_bg = ratio(hexs(h), hexs(bg))
        line = f"  {n:16} {h}  bg {r_bg:5.2f}"
        fail = r_bg < 4.5
        if light:
            r_ch = ratio(hexs(h), hexs("#DDDDDD"))
            line += f"  chrome {r_ch:5.2f}"
            fail = fail or r_ch < 3.0
        if fail:
            line += "   FAIL"
            bad += 1
        print(line)
    print(f"  -> {'all pass' if not bad else str(bad) + ' failing'}")
    return bad

if __name__ == "__main__":
    dirs = sys.argv[1:] or [str(pathlib.Path(__file__).resolve().parents[1])]
    sys.exit(1 if sum(audit(d) for d in dirs) else 0)
