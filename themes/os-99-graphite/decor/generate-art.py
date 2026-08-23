import zlib, struct, os, sys

def png(path, w, h, px):
    """px: dict[(x,y)] = (r,g,b,a); missing = transparent."""
    raw = b""
    for y in range(h):
        raw += b"\x00" + b"".join(
            bytes(px.get((x, y), (0, 0, 0, 0))) for x in range(w))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    out = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(out)

def C(h, a=255):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)

BLACK, WHITE = C("000000"), C("FFFFFF")

def mix(a, b, t=0.5):
    """Blend two palette colours. Used to step a bevel inward so a 2px wall
    reads as a dish rather than a trench."""
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3)) + (255,)

# ---------------------------------------------------------------------------
# Everything tunable lives in ~/.config/omarchy/os99-theme.toml so it can be
# nudged by eye without touching this file. Run os9-theme-reload after editing.
# ---------------------------------------------------------------------------
import tomllib

# WHERE THE CONFIG COMES FROM. A theme repo has to be self-contained -- someone
# installing it has no ~/.config/omarchy/os99-theme.toml and never will unless a
# separate installer put one there. So each theme ships its own copy next to the
# generator, and the user's file, if it exists, wins over it. That way the theme
# works on a bare machine and still respects local tuning on this one.
_HERE = os.path.dirname(os.path.abspath(__file__))
for _candidate in (os.environ.get("OS99_CONFIG"),
                   os.path.expanduser("~/.config/omarchy/os99-theme.toml"),
                   os.path.expanduser("~/.config/os99/os99-theme.toml"),
                   os.path.expanduser("~/.config/hypr/os99-theme.toml"),
                   os.path.join(_HERE, "os99-theme.toml")):
    if _candidate and os.path.isfile(_candidate):
        CFG_PATH = _candidate
        break
else:
    sys.exit("no os99-theme.toml found (looked at $OS99_CONFIG, ~/.config/{omarchy,os99,hypr}/ and beside this script)")
with open(CFG_PATH, "rb") as fh:
    CFG = tomllib.load(fh)

# --- where the art goes ------------------------------------------------------
# With no arguments this writes all three themes, which is what you want while
# tuning locally. --out/--palette write ONE decor directory, which is what the
# theme-set hook uses: it regenerates the LIVE theme in place, so a theme
# installed on its own still gets art matching that machine's display.
import argparse
_ap = argparse.ArgumentParser(description="Draw the OS 9 Platinum theme art.")
_ap.add_argument("--out", metavar="DIR", help="write one decor directory instead of all three")
_ap.add_argument("--palette", choices=("light", "dark"), default="light",
                 help="which palette from the toml to draw with (only with --out)")
_ap.add_argument("--quiet", action="store_true")
ARGS = _ap.parse_args()

_bar, _frame = CFG["bar"], CFG["frame"]
_btn, _txt   = CFG["buttons"], CFG["text"]

# Everything below is authored in DEVICE pixels: logical size * display scale.
# The plugin blits the result 1:1 (frame_texture_unscaled), so the compositor
# never resamples the art and a fine hatch stays exact. All the toml values stay
# LOGICAL so they remain meaningful if the scale changes.
def _detect_scale():
    """Ask the compositor. The art is authored at DEVICE resolution, so a wrong
    scale does not merely look slightly off -- the frame renders at the wrong
    SIZE, because frame_texture_unscaled blits it 1:1."""
    import json, subprocess
    try:
        mons = json.loads(subprocess.run(
            ["hyprctl", "monitors", "-j"], capture_output=True, text=True,
            timeout=5, check=True).stdout)
    except Exception:
        return None, "hyprctl unavailable"
    if not mons:
        return None, "no monitors reported"
    focused = next((m for m in mons if m.get("focused")), mons[0])
    scales = {round(float(m.get("scale", 1.0)), 6) for m in mons}
    note = f"{focused.get('name','?')}"
    if len(scales) > 1:
        # One set of art, one scale. Nothing here can serve two at once.
        note += f"  (WARNING: monitors disagree {sorted(scales)}; using the focused one)"
    return round(float(focused.get("scale", 1.0)), 6), note

_scale_cfg = CFG.get("display", {}).get("scale", "auto")
SCALE_NOTE = "from os99-theme.toml"
if isinstance(_scale_cfg, str) and _scale_cfg.strip().lower() == "auto":
    _detected, _note = _detect_scale()
    if _detected is None:
        SCALE, SCALE_NOTE = 1.0, f"auto FAILED ({_note}) -- assuming 1.0"
    else:
        SCALE, SCALE_NOTE = _detected, f"auto-detected from {_note}"
else:
    SCALE = float(_scale_cfg)

def dev(v):
    return int(round(v * SCALE))


# ---------------------------------------------------------------------------
# THE FONT. OS 9 used Charcoal, but Charcoal is Apple's and cannot be shipped,
# so the theme has to survive its absence on someone else's machine. The chain
# below is ordered by how little the layout moves when it is taken, measured by
# rendering each face at the bar's real device size:
#
#            ascent  descent  cap top  cap bottom  advance("Documents")
#   Charcoal     15        4        4          15                  84.1
#   ChicagoFLF   15        3        3          15                  90.0
#   ChicagoKare  12        4        3          12                  67.5
#
# ChicagoFLF (public domain) is a drop-in: identical ascent, identical baseline,
# identical descender depth, 7% wider. Chicago Kare (MIT) is drawn on a smaller
# em, so at the same nominal size it renders about a fifth too small -- usable,
# but only with the size compensation recorded beside it.
#
# Note for later: ChicagoFLF's caps sit 1px LOWER than Charcoal's, which is the
# high-baseline complaint about Charcoal, unforced.
FONT_CHAIN = [
    # family           size x   licence
    ("Charcoal",       1.00,   "Apple, not redistributable"),
    ("ChicagoFLF",     1.00,   "public domain"),
    ("Chicago Kare",   1.25,   "MIT"),
]

def _installed_families():
    import subprocess
    try:
        out = subprocess.run(["fc-list", ":", "family"], capture_output=True,
                             text=True, timeout=10, check=True).stdout
    except Exception:
        return None                      # no fontconfig: trust the toml blindly
    fams = set()
    for line in out.splitlines():
        for alias in line.split(","):
            fams.add(alias.strip())
    return fams

def resolve_font(preferred):
    """First installed family in the chain, preferred one first."""
    chain = [(preferred, 1.00, "named in os99-theme.toml")] + \
            [c for c in FONT_CHAIN if c[0] != preferred]
    have = _installed_families()
    if have is None:
        return chain[0][0], chain[0][1], "fc-list unavailable -- not verified"
    for fam, ratio, lic in chain:
        if fam in have:
            note = f"{lic}"
            if fam != preferred:
                note = f"{preferred} not installed -> {fam} ({lic})"
            return fam, ratio, note
    return "sans-serif", 1.00, "NONE of the OS 9 faces installed -- falling back to sans-serif"


RIDGES      = int(_bar["ridges"])
L_HI        = float(_bar.get("ridge_hi", 1))
L_LO        = float(_bar.get("ridge_lo", 1))
L_GAP       = float(_bar["ridge_gap"])
L_MTOP      = float(_bar["margin_top"])
L_MBOT      = float(_bar["margin_bottom"])
CLEAR_T     = int(_bar.get("clear_inset_top", 0))
CLEAR_B     = int(_bar.get("clear_inset_bottom", 0))

# Scale the PERIOD, never the parts: rounding each line separately loses a pixel
# per ridge. A period of 4 logical at scale 1.25 must come out 5 device rows,
# not round(2)+round(2)=4.
L_PERIOD   = L_HI + L_LO + L_GAP
D_PERIOD   = max(2, int(round(L_PERIOD * SCALE)))
# With device-authored art the png is blitted 1:1, so any whole device period
# tiles exactly -- uniformity is guaranteed by construction. It is only
# logical art, which the compositor has to upscale, that can come out uneven.
# The drawn period is a whole number of device pixels and the art is blitted
# 1:1, so the hatch tiles exactly at every scale -- uniformity is now guaranteed
# by construction rather than something to check for. What CAN happen is that the
# requested logical period does not survive the trip (2.4 logical at scale 1.0
# can only be 2 device px), which changes the look slightly but stays uniform.
PERIOD_EXACT = abs(L_PERIOD * SCALE - D_PERIOD) < 1e-9
# The parts MUST sum to D_PERIOD. The hatch loop advances by hi+lo+gap while
# FIELD is RIDGES*D_PERIOD, so if those disagree the hatch overruns its field and
# eats the bottom margin. Rounding each part independently is exactly how they
# come to disagree: at scale 1.0, ridge_hi 1.6 rounds UP to 2 while the period
# 2.4 rounds DOWN to 2, leaving no room for the dark line at all. So round the
# period, then carve the parts OUT of it and give the remainder to the dark line.
RIDGE_GAP  = max(0, min(int(round(L_GAP * SCALE)), D_PERIOD - 2))
RIDGE_HI   = min(max(1, int(round(L_HI * SCALE))), D_PERIOD - RIDGE_GAP - 1)
RIDGE_LO   = D_PERIOD - RIDGE_HI - RIDGE_GAP
assert RIDGE_HI >= 1 and RIDGE_LO >= 1 and RIDGE_HI + RIDGE_LO + RIDGE_GAP == D_PERIOD
FIELD      = RIDGES * D_PERIOD

# Hairlines stay hairlines: a 1px rule wants to be 1 DEVICE pixel, not 1.25.
RULE_PX = 1

# Hyprland grows the window by `range` then shifts it by `offset`, so
#   inset = offset - range   and   width = offset + range.
# Solve that pair from the two numbers that actually describe the shadow.
# Hyprland grows the window box by `range` on every side and THEN translates it
# by `offset`, so per axis:
#     extent past the edge = offset + range
#     inset from the far corner = offset - range
# Two consequences worth stating plainly, because they bound what is expressible:
#
#   1. INSET CAN NEVER EXCEED EXTENT. Their difference is 2*range, which cannot
#      be negative. A 2px shadow inset by 4px is not a thing you can ask for.
#   2. Inset is therefore MAXIMISED at range = 0, where inset == extent exactly
#      and the shadow is a pure offset copy of the window -- which is precisely
#      what a classic Mac drop shadow is. Verified that range = 0 still renders.
#
# So range is pinned to 0 and the two offsets ARE the two extents. That also
# buys independent right and bottom depth, which the old single `shadow` key
# could not express: offset is a vec2, so the right strip and the bottom strip
# no longer have to be the same weight.
_SH_RIGHT  = int(_frame.get("shadow_right", _frame.get("shadow", 3)))
_SH_BOTTOM = int(_frame.get("shadow_bottom", _frame.get("shadow", 3)))
_SH_RANGE  = 0
_SH_OK     = _SH_RIGHT >= 0 and _SH_BOTTOM >= 0
# Percentages of black -> the alpha byte Hyprland wants.
# NOTE: Hyprland's rgba() is RRGGBBAA -- alpha LAST. Writing the alpha first
# gives a dark RED at zero opacity, which silently looks like "no shadow".
_SH_A      = max(0, min(255, round(float(_frame.get("shadow_opacity", 60)) * 255 / 100)))
_SH_AI     = max(0, min(255, round(float(_frame.get("shadow_opacity_inactive", 22)) * 255 / 100)))

# The bar height must divide exactly by the scale, because the plugin reserves
# it in LOGICAL pixels while the art is authored in device ones. Derive the
# logical height from the toml, convert once, then hand the remainder to the
# margins in the ratio the toml asked for -- that keeps the total exact instead
# of accumulating a rounding error per band.
BAR_LOGICAL = int(round(2 + L_MTOP + RIDGES * L_PERIOD + L_MBOT + 2))   # +2: shadow AND content rule

# ...but not every logical height survives the trip to device pixels. The plugin
# reserves bar_height in LOGICAL px while the art is authored in DEVICE px, so
# unless BAR_LOGICAL * SCALE is a whole number the two disagree by a fraction and
# the compositor stretches the art -- which destroys the pinstripe period, the
# one thing the device-resolution art exists to protect.
#
# Writing SCALE as p/q in lowest terms, BAR_LOGICAL * p / q is whole exactly when
# q divides BAR_LOGICAL. So round the height UP to the next multiple of q and let
# the margins absorb the extra rows. At 1.25 = 5/4 that means a multiple of 4
# (28 here); at 1.5 = 3/2, an even number; at 1.0 or 2.0, anything at all.
from fractions import Fraction
_q = Fraction(SCALE).limit_denominator(64).denominator
BAR_QUANTUM = _q
if BAR_LOGICAL % _q:
    BAR_LOGICAL += _q - (BAR_LOGICAL % _q)
BAR_H       = int(round(BAR_LOGICAL * SCALE))
EXACT_H     = abs(BAR_LOGICAL * SCALE - BAR_H) < 1e-9

_slack     = max(0, BAR_H - RULE_PX * 3 - FIELD)

# WHERE THE HATCH SITS. The boxes are not placed by this script -- the plugin
# centres them, hard, at (barHeight - boxSize) / 2 (buttonSlots in barDeco.cpp).
# So the hatch has to go and meet them, or the two bands sit at different heights
# and the bar reads as two stripes instead of one. Since the box is now exactly
# as tall as the hatch field, matching the bands means matching their tops:
#
#     hatch_top = (BAR_H - FIELD) / 2      <- the same centring the plugin does
#
# margin_top / margin_bottom in the toml still set the bar's HEIGHT (they feed
# BAR_LOGICAL above); what they no longer do is set the split, because the split
# is not free. Tuning it by eye at one scale is exactly how it came to be 1.5px
# out at 1.25 and further out everywhere else.
#
# hatch_nudge moves the band off centre deliberately, in device px, if a
# particular ridge count wants it. Leave it at 0 unless you can see why.
_centred   = int(round((BAR_H - FIELD) / 2.0)) - RULE_PX * 2
_nudge     = int(_bar.get("hatch_nudge", 0))
MARGIN_TOP = max(0, min(_slack, _centred + _nudge))
MARGIN_BOT = _slack - MARGIN_TOP

FONT_FAMILY, FONT_RATIO, FONT_NOTE = resolve_font(str(_txt["font"]))
TEXT_SIZE  = int(round(int(_txt["size"]) * FONT_RATIO))
UI_SIZE    = int(round(int(_txt.get("ui_size", 11)) * FONT_RATIO))

W          = 64
SIDE       = int(round(int(_frame["bezel"]) * SCALE))
TOP        = BAR_H
H          = TOP + 24 + SIDE
# THE BOX IS THE HATCH. A title-bar box is exactly as tall as the field of
# ridges beside it -- that is what makes the bar read as one band rather than
# two. `buttons.size` in the toml records the intent, but the number that has to
# be right is FIELD, so derive from that and treat the toml value as a check.
# The plugin takes the size in LOGICAL px, so round-trip through the scale to be
# sure the two agree on the same pixel.
BOX_LOGICAL = max(1, int(round(FIELD / SCALE)))
BOX_S       = int(round(BOX_LOGICAL * SCALE))
BOX_DRIFT   = BOX_LOGICAL - int(_btn["size"])
CAP        = 3          # cap width for the legacy standalone bar art

FACE = LIT = SHADOW = DEEP = PRESSED = OUTLINE = None
STRIPE = STRIPE_HI = STRIPE_LO = RULE = WELL_LO = WELL_HI = TEXTCOL = DISH = None

def use(name):
    global FACE, LIT, SHADOW, DEEP, PRESSED, OUTLINE
    global STRIPE, STRIPE_HI, STRIPE_LO, RULE, WELL_LO, WELL_HI, TEXTCOL, DISH
    q = CFG[name]
    FACE, LIT, SHADOW = C(q["face"]), C(q["lit"]), C(q["shadow"])
    DEEP, PRESSED = C(q["deep"]), C(q["pressed"])
    OUTLINE, RULE = C(q["outline"]), C(q["rule"])
    STRIPE_HI, STRIPE_LO = C(q["stripe_hi"]), C(q["stripe_lo"])
    STRIPE = STRIPE_LO                      # legacy name, used by the bar art
    WELL_LO, WELL_HI = C(q["well_lo"]), C(q["well_hi"])
    TEXTCOL = q["text"]
    # A colour, so it follows the scheme. Falls back to the shared [buttons]
    # value, then to a derived tone, for configs that predate this.
    _d = q.get("dish") or _btn.get("dish")
    DISH = C(_d) if _d else None

def frame(active):
    """The whole OS 9 window frame, as one nine-patch ring.

    Drawn as two concentric RECTANGLES rather than as separate row and column
    runs, which is what makes the corners join:

      outer ring   rule at the very edge, then a raised bevel
                   (highlight top-left, shadow bottom-right)
      face         the body of the frame; the title bar's hatch lives in the
                   top band, inset horizontally so the corners stay plain
      inner ring   the recessed well the content sits in -- bevel the other way
                   (shadow top-left, highlight bottom-right), then a rule

    Drawing the well as a rectangle is the whole point. Laying the title bar's
    closing shadow + rule across the FULL width, as this used to, ran them
    straight through the side bezels: a black line crossed the side's shadow
    column at the bar's foot and again at the bottom, and the frame stopped
    reading as one continuous piece. As a rectangle the bar's bottom edge and
    the sides' inner edges are literally the same four lines, so they meet.

    The centre is transparent -- the client covers it, and painting it would
    tint any translucent window.
    """
    p = {}
    def hline(y, x0, x1, c):
        for x in range(x0, x1 + 1): p[(x, y)] = c
    def vline(x, y0, y1, c):
        for y in range(y0, y1 + 1): p[(x, y)] = c
    def rect(x0, y0, x1, y1, top_left, bottom_right):
        """One bevelled rectangle outline, lit from the top-left."""
        hline(y0, x0, x1, top_left)          # top
        vline(x0, y0, y1, top_left)          # left
        hline(y1, x0, x1, bottom_right)      # bottom
        vline(x1, y0, y1, bottom_right)      # right
        # Where a LIT edge runs into a SHADED one the junction reads badly, so
        # neither wins -- the reference puts a single face pixel there. A rect
        # whose edges are the same tone (the rules) has no such junction and
        # must stay unbroken, or the window's outline gets notched corners.
        if top_left != bottom_right:
            p[(x1, y0)] = FACE
            p[(x0, y1)] = FACE

    # --- the frame body ----------------------------------------------------
    for y in range(TOP):
        hline(y, 0, W - 1, FACE)
    for y in range(TOP, H):
        for x in list(range(SIDE)) + list(range(W - SIDE, W)):
            p[(x, y)] = FACE
    for y in range(H - SIDE, H):
        hline(y, 0, W - 1, FACE)

    # --- the title bar hatch ----------------------------------------------
    # Only in the stretchable middle: the corner tiles stay plain face, which
    # is what stops the corner reading as two textures colliding.
    if active:
        y = RULE_PX * 2 + MARGIN_TOP
        for _ in range(RIDGES):
            for k in range(RIDGE_HI):
                hline(y + k, SIDE, W - SIDE - 1, STRIPE_HI)
            for k in range(RIDGE_LO):
                hline(y + RIDGE_HI + k, SIDE, W - SIDE - 1, STRIPE_LO)
            y += RIDGE_HI + RIDGE_LO + RIDGE_GAP

    # --- inner ring: the recessed well the content sits in ------------------
    # Bevel first, then the rule just inside it.
    rect(SIDE - 2, TOP - 2, W - SIDE + 1, H - SIDE + 1, SHADOW, LIT)
    rect(SIDE - 1, TOP - 1, W - SIDE,     H - SIDE,     RULE,   RULE)

    # --- outer ring: raised bevel, then the window's own rule ---------------
    rect(1, 1, W - 2, H - 2, LIT,  SHADOW)
    rect(0, 0, W - 1, H - 1, RULE, RULE)

    # --- the centre belongs to the client ----------------------------------
    for y in range(TOP, H - SIDE):
        for x in range(SIDE, W - SIDE):
            p.pop((x, y), None)

    return p, W, H


def checkbox(checked, S=13):
    """Platinum checkbox: bevelled square well, checkmark when set."""
    p = {}
    for y in range(S):
        for x in range(S):
            p[(x, y)] = FACE
    for i in range(S):                              # outline
        p[(i, 0)] = p[(i, S-1)] = p[(0, i)] = p[(S-1, i)] = OUTLINE
    # A checkbox is a WELL, so the bevel is inverted vs a push button:
    # shadow top-left, highlight bottom-right.
    for i in range(1, S-1):
        p[(i, 1)] = SHADOW; p[(1, i)] = SHADOW
        p[(i, S-2)] = LIT;  p[(S-2, i)] = LIT
    if checked:
        for (x, y) in [(3,6),(4,7),(4,8),(5,9),(6,8),(7,7),(8,5),(9,4),(6,7),(7,6),(8,4)]:
            p[(x, y)] = OUTLINE
            if x + 1 < S - 1:
                p[(x+1, y)] = OUTLINE
    return p, S


def radio(selected, S=13):
    """Platinum radio: round well. Bevel shades only the inner RING -- shading
    the whole disc reads as a diagonal stripe, not a bevel."""
    import math
    p = {}
    c = (S - 1) / 2.0
    for y in range(S):
        for x in range(S):
            dx, dy = x - c, y - c
            d = math.hypot(dx, dy)
            if d > c + 0.2:
                continue
            if d > c - 0.9:
                p[(x, y)] = OUTLINE
            elif d > c - 2.0:
                # inner ring: light falls from the top-left
                p[(x, y)] = SHADOW if (dx + dy) < 0 else LIT
            else:
                p[(x, y)] = FACE
    if selected:
        for y in range(S):
            for x in range(S):
                if math.hypot(x - c, y - c) <= 2.4:
                    p[(x, y)] = OUTLINE
    return p, S


def arrow(direction, S=13):
    """Scrollbar arrow button: bevelled face with a solid triangle."""
    p = {}
    for y in range(S):
        for x in range(S):
            p[(x, y)] = FACE
    for i in range(S):
        p[(i, 0)] = p[(i, S-1)] = p[(0, i)] = p[(S-1, i)] = OUTLINE
    for i in range(1, S-1):
        p[(i, 1)] = LIT; p[(1, i)] = LIT
        p[(i, S-2)] = SHADOW; p[(S-2, i)] = SHADOW
    mid = S // 2
    for step in range(4):
        for off in range(-step, step + 1):
            if direction == "up":    px, py = mid + off, mid - 2 + step
            elif direction == "down": px, py = mid + off, mid + 2 - step
            elif direction == "left": px, py = mid - 2 + step, mid + off
            else:                     px, py = mid + 2 - step, mid + off
            if 1 < px < S - 2 and 1 < py < S - 2:
                p[(px, py)] = OUTLINE
    return p, S



def titlebar(active, W=64, H=BAR_H):
    """OS 9 title bar as a horizontal 3-slice.

    The pattern is constant along x, so stretching the middle horizontally is
    lossless. Height matches the bar exactly so the middle never stretches
    vertically -- that is what would destroy the pinstripe period.
    """
    p = {}
    def row(y, c):
        for x in range(W):
            p[(x, y)] = c

    row(0, OUTLINE)                      # outer top rule
    row(1, LIT)                          # Platinum top highlight
    for y in range(2, H - 2):
        # Active windows are pinstriped, inactive flat -- OS 9 signalled focus
        # with texture, not colour.
        row(y, FACE if (not active or y % 2 == 0) else STRIPE)
    row(H - 2, DEEP)                     # shadow above the rule
    row(H - 1, OUTLINE)                  # rule separating bar from content

    # Side caps: black outer edge, then the bevel, lit from the left.
    for y in range(H):
        p[(0, y)] = OUTLINE
        p[(1, y)] = LIT
        p[(2, y)] = FACE
        p[(W - 3, y)] = FACE
        p[(W - 2, y)] = SHADOW
        p[(W - 1, y)] = OUTLINE
    # Keep the top and bottom rules unbroken across the caps.
    for x in (0, 1, 2, W - 3, W - 2, W - 1):
        p[(x, 0)] = OUTLINE
        p[(x, H - 1)] = OUTLINE
    return p, W, H


def box(kind, pressed, S=13):
    """OS 9 title-bar boxes, traced from Platinum9's close/maximize XPMs.

    Four concentric zones, reading outside in:

      0  dish      shadow on top+left, highlight on bottom+right -- the whole
                   button sits in a recess pressed INTO the title bar
      1  outline   a dark GREY (2D2D2D in the reference), never black
      2  lip       face on top+left, shadow on bottom+right
      3  interior  a 45-degree gradient, darkest at the top-left corner and
                   running to the lightest tone at the bottom-right

    That interior gradient is the entire "depressed button" effect, and it is
    LINEAR along x+y rather than radial -- anti-diagonals in the reference art
    are flat, which is what proves it. A flat fill with a bevel around it, which
    is what this drew before, can never produce the same dished look.

    Glyphs are drawn ON TOP of the gradient, which continues unbroken beneath
    them, exactly as in the reference.
    """
    p = {}

    lo, hi = WELL_LO, WELL_HI
    if pressed:
        # Pressing keeps the gradient's direction and drags the whole ramp
        # down; the reference goes A9->FF unpressed and 56->B8 pressed.
        lo, hi = mix(lo, RULE, 0.45), mix(hi, RULE, 0.28)

    # --- zone 3: the gradient interior -------------------------------------
    i0, i1 = 3, S - 4
    span   = max(1, (i1 - i0) * 2)
    for y in range(i0, i1 + 1):
        for x in range(i0, i1 + 1):
            p[(x, y)] = mix(lo, hi, ((x - i0) + (y - i0)) / span)

    # --- zone 2: the inner lip ---------------------------------------------
    dish = DISH if DISH else mix(SHADOW, DEEP, 0.5)
    for i in range(2, S - 2):
        p[(i, 2)] = FACE
        p[(2, i)] = FACE
        p[(i, S - 3)] = dish
        p[(S - 3, i)] = dish
    p[(2, 2)] = LIT                       # the corner facing the light

    # --- zone 1: the outline -----------------------------------------------
    for i in range(1, S - 1):
        p[(i, 1)] = p[(1, i)] = p[(i, S - 2)] = p[(S - 2, i)] = OUTLINE

    # --- zone 0: the dish it sits in ---------------------------------------
    # Same tone as the lip's dark side, so the button reads as one pressing.
    for i in range(S):
        p[(i, 0)] = dish
        p[(0, i)] = dish
        p[(i, S - 1)] = LIT
        p[(S - 1, i)] = LIT
    p[(S - 1, 0)] = FACE                  # where the two meet, neither wins
    p[(0, S - 1)] = FACE

    # --- glyphs ------------------------------------------------------------
    q = i0 + round((i1 - i0) * 0.55)
    if kind == "zoom":
        # Not a free-floating square: two lines that, together with the box's
        # own top and left edges, close a small square in the UPPER-LEFT.
        for y in range(2, q + 1):
            p[(q, y)] = OUTLINE
        for x in range(2, q + 1):
            p[(x, q)] = OUTLINE
    elif kind == "collapse":
        # Windowshade slats: TWO rules, and they run the full inner width right
        # into the outline rather than floating inside a margin.
        for m in (round(S * 0.43), round(S * 0.57)):
            for x in range(1, S - 1):
                p[(x, m)] = OUTLINE
    return p, S


# "noir" is a third Appearance: light chrome, dark documents. Its chrome is the
# LIGHT palette verbatim -- only what lives inside a window changes, and that is
# colors.toml's business, not this file's.
if ARGS.out:
    TARGETS = [(ARGS.palette, ARGS.out)]
else:
    # No-argument mode is a DEVELOPMENT convenience: draw all three Omarchy
    # themes at once while tuning the toml. It is not the general entry point --
    # on a machine without Omarchy those directories do not exist, and silently
    # creating them would leave a confusing mess nobody asked for. --out is the
    # portable way in, and is what the hook uses.
    _themes = os.path.expanduser("~/.config/omarchy/themes")
    if not os.path.isdir(_themes):
        sys.exit("no Omarchy themes directory found.\n"
                 "  This no-argument mode only exists to redraw the three bundled\n"
                 "  Omarchy themes while tuning. To draw the art anywhere else:\n"
                 "      generate-art.py --out <dir> --palette light|dark")
    TARGETS = [(pal, os.path.join(_themes, theme, "decor"))
               for pal, theme in (("light", "os-99-platinum"),
                                  ("dark",  "os-99-graphite"),
                                  ("light", "os-99-noir"))]

for pal, d in TARGETS:
    use(pal)
    os.makedirs(d, exist_ok=True)
    for act, name in ((True, "active"), (False, "inactive")):
        q, fw, fh = frame(act)
        png(f"{d}/frame_{name}.png", fw, fh, q)
    # 13px boxes: referenced by the GTK stylesheets, do not resize.
    for kind, name in (("close", "close"), ("zoom", "maximize"), ("collapse", "minimize")):
        for pressed, suffix in ((False, ""), (True, "_pressed")):
            q, S = box(kind, pressed)
            png(f"{d}/{name}{suffix}.png", S, S, q)
    # Bar-sized boxes: drawn by the hyprbars fork into the title bar.
    for kind in ("close", "zoom", "collapse"):
        for pressed, suffix in ((False, ""), (True, "_pressed")):
            q, S = box(kind, pressed, BOX_S)
            png(f"{d}/bar_{kind}{suffix}.png", S, S, q)
    for on, suffix in ((False, "off"), (True, "on")):
        q, S = checkbox(on); png(f"{d}/check_{suffix}.png", S, S, q)
        q, S = radio(on);    png(f"{d}/radio_{suffix}.png", S, S, q)
    for act, suffix in ((True, "active"), (False, "inactive")):
        q, bw, bh = titlebar(act)
        png(f"{d}/bar_{suffix}.png", bw, bh, q)
    for dirn in ("up", "down", "left", "right"):
        q, S = arrow(dirn);  png(f"{d}/arrow_{dirn}.png", S, S, q)
    # One source of truth: the hook sources this instead of hardcoding numbers
    # that would silently drift out of step with the art.
    with open(f"{d}/bar.env", "w") as fh:
        fh.write(f"""# Generated by generate-art.py from ~/.config/omarchy/os99-theme.toml
# Do not edit -- edit the toml and run os9-theme-reload.
# The hook compares this against the live display and redraws on a mismatch.
# Without it, art baked on the author's machine silently renders at the wrong
# SIZE on anyone else's, because it is blitted 1:1.
OS99_ART_SCALE={SCALE}
OS99_ART_PALETTE={pal}
OS99_BAR_HEIGHT={BAR_LOGICAL}
OS99_BEZEL={int(_frame["bezel"])}
OS99_SHADOW_RANGE={_SH_RANGE}\nOS99_SHADOW_OFFSET_X={_SH_RIGHT}\nOS99_SHADOW_OFFSET_Y={_SH_BOTTOM}\nOS99_SHADOW_COLOR="rgba(000000{_SH_A:02X})"\nOS99_SHADOW_COLOR_INACTIVE="rgba(000000{_SH_AI:02X})"
OS99_FRAME_UNSCALED={"true" if abs(SCALE - 1.0) > 1e-9 else "false"}
OS99_FRAME_OVER=true
OS99_BOX_SIZE={BOX_LOGICAL}
OS99_BTN_EDGE_PAD={int(_btn["edge_pad"])}
OS99_BTN_GAP={int(_btn["gap"])}
OS99_BTN_CLEAR_PAD={int(_btn["clear_pad"])}
OS99_TEXT_SIZE={TEXT_SIZE}
OS99_TEXT_FONT="{FONT_FAMILY}"
OS99_UI_FONT="{FONT_FAMILY} {UI_SIZE}"
OS99_TEXT_CLEAR_PAD={int(_txt["clear_pad"])}
OS99_CLEAR_INSET_TOP={CLEAR_T}
OS99_CLEAR_INSET_BOTTOM={CLEAR_B}
OS99_FACE="rgb({CFG[pal]["face"]})"
OS99_CLEAR_COLOR="rgb({_bar["clear_color"] if _bar.get("clear_color") else CFG[pal]["face"]})"
OS99_TEXT_COLOR="rgb({TEXTCOL})"
OS99_FRAME_BORDER="{TOP} {SIDE} {SIDE} {SIDE}"
OS99_FRAME_INSET="{BAR_LOGICAL} {int(_frame["bezel"])} {int(_frame["bezel"])} {int(_frame["bezel"])}"
""")
    # ---------------------------------------------------------------------
    # bars.lua -- the SAME settings as bar.env, but as Hyprland config, written
    # into the theme so that a config reload applies them directly.
    #
    # Why this exists: omarchy-theme-set runs `hyprctl reload` inside a parallel
    # batch and only calls the theme-set hook AFTER every command in that batch
    # finishes. One of them, omarchy-theme-set-tmux, takes ~19s on a machine
    # with a lot of saved sessions. A reload resets plugin config to defaults,
    # so if the frame is only restored by the hook, the title bar sits wrong for
    # as long as the slowest unrelated command takes.
    #
    # The theme directory is swapped BEFORE that batch runs, so a reload that
    # sources this file gets the NEW theme's colours immediately and never waits
    # on tmux, or on anything else in the batch.
    #
    # Paths come from the `os99_dir` global the caller sets, because the art is
    # read from wherever the theme is installed, not from where it was drawn.
    with open(f"{d}/bars.lua", "w") as fh:
        fh.write(f"""-- Generated by generate-art.py -- do not edit.
-- Expects the caller to set the global `os99_dir` to this directory.
local D = os99_dir

-- Compositor-level settings; no plugin required, so kept in their own pcall.
pcall(function()
  hl.config({{
    general = {{ border_size = 0 }},
    misc = {{ background_color = "rgb(54679B)" }},
    group = {{ groupbar = {{ font_family = "{FONT_FAMILY}", font_size = {TEXT_SIZE} }} }},
    decoration = {{
      shadow = {{ enabled = true, sharp = true, range = {_SH_RANGE}, render_power = 1,
                 offset = {{ {_SH_RIGHT}, {_SH_BOTTOM} }},
                 color = "rgba(000000{_SH_A:02X})", color_inactive = "rgba(000000{_SH_AI:02X})" }},
    }},
  }})
end)

-- The plugin's own keys. These only exist once hyprbars is loaded, and on a
-- cold start the config is parsed before that -- hence the pcall, so an unknown
-- key cannot take the rest of the user's config down with it.
pcall(function()
  hl.config({{ plugin = {{ hyprbars = {{
    bar_height = {BAR_LOGICAL},
    bar_color = "rgb({CFG[pal]["face"]})",
    bar_texture = D .. "/bar",
    bar_texture_border = "0 3 0 3",
    frame_texture = D .. "/frame",
    frame_texture_border = "{TOP} {SIDE} {SIDE} {SIDE}",
    frame_inset = "{BAR_LOGICAL} {int(_frame["bezel"])} {int(_frame["bezel"])} {int(_frame["bezel"])}",
    frame_texture_unscaled = {"true" if abs(SCALE - 1.0) > 1e-9 else "false"},
    frame_over_window = true,
    bar_clear_color = "rgb({_bar["clear_color"] if _bar.get("clear_color") else CFG[pal]["face"]})",
    bar_text_clear_pad = {int(_txt["clear_pad"])},
    bar_button_clear_pad = {int(CFG["buttons"]["clear_pad"])},
    bar_clear_inset_top = {CLEAR_T},
    bar_clear_inset_bottom = {CLEAR_B},
    bar_text_font = "{FONT_FAMILY}",
    bar_text_size = {TEXT_SIZE},
    bar_text_align = "center",
    bar_part_of_window = true,
    bar_precedence_over_border = true,
    bar_buttons_alignment = "left",
    bar_padding = {int(CFG["buttons"]["edge_pad"])},
    bar_button_padding = {int(CFG["buttons"]["gap"])},
    col = {{ text = "rgb({TEXTCOL})" }},
  }} }} }})
end)

-- The boxes. hyprbars clears its button list on every config reload and never
-- repopulates ones added through the Lua API, so they have to be redeclared
-- here or every reload empties the title bar. clear_buttons (a fork addition)
-- makes it idempotent. Close alone on the LEFT, collapse and zoom on the RIGHT;
-- buttons fill outward from their own edge, so zoom goes first to keep the
-- top-right corner it has held since System 7.
pcall(function()
  hl.plugin.hyprbars.clear_buttons()
  local box = {{ bg_color = "rgb({CFG[pal]["face"]})", fg_color = "rgb({TEXTCOL})", size = {BOX_LOGICAL}, icon = "" }}
  local function add(img, side, action)
    hl.plugin.hyprbars.add_button({{ bg_color = box.bg_color, fg_color = box.fg_color,
      size = box.size, icon = "", image = D .. img, side = side, action = action }})
  end
  add("/bar_close.png",    "left",  "hyprctl dispatch killactive")
  add("/bar_zoom.png",     "right", "hyprctl dispatch fullscreen 1")
  add("/bar_collapse.png", "right", "hyprctl dispatch togglefloating")
end)
""")

    if not ARGS.quiet:
        print(f"{pal:5} -> {d} ({len(os.listdir(d))} files)")
if ARGS.quiet:
    raise SystemExit(0)
print()
print(f"  scale       {SCALE}   {SCALE_NOTE}")
print(f"  font        {FONT_FAMILY} @ {TEXT_SIZE}px bar / {UI_SIZE}pt ui   ({FONT_NOTE})")
print(f"  bar height  {BAR_H}px device = {BAR_LOGICAL}px logical"
      + ("" if EXACT_H else "   <-- not a whole number of device px; art will stretch")
      + (f"   [quantised to a multiple of {BAR_QUANTUM} for scale {SCALE}]" if BAR_QUANTUM > 1 else "")
      + f"  [{RULE_PX} rule + {RULE_PX} highlight + {MARGIN_TOP} margin"
      + f" + {FIELD} hatch + {MARGIN_BOT} margin + {RULE_PX} shadow]")
_hatch = (RULE_PX * 2 + MARGIN_TOP, RULE_PX * 2 + MARGIN_TOP + FIELD - 1)
_btn   = ((BAR_H - BOX_S) // 2, (BAR_H - BOX_S) // 2 + BOX_S - 1)
print(f"  ridge       period {RIDGE_HI + RIDGE_LO}px ({RIDGE_HI} light + {RIDGE_LO} dark)"
      f" device from {L_PERIOD:g}px logical x {SCALE}"
      + ("   exact" if PERIOD_EXACT else f"   (rounded from {L_PERIOD * SCALE:g}; uniform, but not the requested ratio)"))
print(f"  hatch rows  {_hatch[0]}..{_hatch[1]} ({FIELD}px)"
      f"   buttons rows {_btn[0]}..{_btn[1]} ({BOX_S}px)"
      f"   {'ALIGNED' if abs(_hatch[0]-_btn[0]) <= 1 and abs(_hatch[1]-_btn[1]) <= 1 else '<-- MISALIGNED'}"
      f"  (offset {_hatch[0]-_btn[0]:+d})")
print(f"  bezel       {SIDE}px      box {BOX_S}px device = {BOX_LOGICAL}px logical"
      + ("" if BOX_DRIFT == 0 else
         f"   <-- derived from the hatch; buttons.size says {int(CFG['buttons']['size'])}"
         f" ({BOX_DRIFT:+d}). Set it to {BOX_LOGICAL} to keep the toml honest."))
print(f"  shadow      right {_SH_RIGHT}px / bottom {_SH_BOTTOM}px,"
      f" inset the same (range 0 -> inset == extent)"
      f"   alpha {_SH_A:02X} active / {_SH_AI:02X} inactive"
      f"   -> hyprland range {_SH_RANGE} offset ({_SH_RIGHT}, {_SH_BOTTOM})"
      + ("" if _SH_OK else "   <-- shadow_right/shadow_bottom must be >= 0"))
print(f"  nine-patch  top {TOP} / side {SIDE} / bottom {SIDE}   art {W}x{H}")
