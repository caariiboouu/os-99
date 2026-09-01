# OS 99

A Hyprland theme for people who want a little Mac OS 9 back in their lives. A modern take on light, dark, and mixed modes.

Square corners, a chiselled bezel around the whole window, a pinstriped title
bar, and a hard offset drop shadow. It is an original reimplementation of a
late-90s desktop look — not a port, and not affiliated with or endorsed by
Apple.

| | |
|---|---|
| **OS 99 Platinum** | light chrome, light content |
| **OS 99 Graphite** | dark chrome, dark content |
| **OS 99 Noir** | light chrome, dark content |

![OS 99 Platinum](themes/os-99-platinum/preview.png)

## What it actually is

The look is one Hyprland plugin plus a directory of pixel art. The frame is a
nine-patch texture drawn around the *whole* window, with the title bar as its
top edge — Hyprland's own border can't do this, because it interpolates a single
gradient across the window and so can never put a crisp 1px highlight on the
top-left and a 1px shadow on the bottom-right.

## Requirements

- Hyprland (developed against **0.56.2**)
- `python3` ≥ 3.11 — standard library only, no pip install
- A font — optional. It tries **Charcoal**, then **ChicagoFLF**, then **Chicago
  Kare**, then falls back to `sans-serif`, so it works with none of them
  installed. Charcoal is Apple's and is deliberately not bundled here;
  **ChicagoFLF** (public domain) is a metric drop-in and the one to want.

      # Arch, via an AUR helper
      yay -S ttf-chicagoflf
      # anywhere else: drop ChicagoFLF.ttf in ~/.local/share/fonts
      fc-cache -f

  `os99-install` names the right command for your distro.

## Install

### 1. The plugin

```
hyprpm add https://github.com/caariiboouu/os-99
hyprpm enable hyprbars-os99
hyprpm reload -n
```

Same repository — the plugin source is in [`plugin/`](plugin/). `hyprpm` builds
it against your own Hyprland, so there is no ABI mismatch to manage.

It derives from [hyprbars](https://github.com/hyprwm/hyprland-plugins) and keeps
its BSD 3-Clause licence; [`plugin/README.md`](plugin/README.md) lists what was
changed and why. It still reports itself to Hyprland as `hyprbars`, so it cannot
be loaded alongside upstream hyprbars — pick one.

### 2a. On Omarchy

```
omarchy plugin add https://github.com/caariiboouu/os-99 --enable
~/.config/omarchy/plugins/io.github.caariiboouu.os-99/bin/os99-install
omarchy theme set "OS 99 Platinum"
```

The first line installs the bar widget the collapse box needs and leaves a
checkout of this repository in place; the second does the per-machine work.
A plain `git clone` works too — run `./bin/os99-install` from the checkout.

`os99-install` is safe to re-run, and re-running it is the fix for most things.
It checks prerequisites, matches the art to your display, resolves the font,
installs the hooks, and registers the menu-bar font widget. `--check` reports
without changing anything.

### 2b. On plain Hyprland

```
git clone https://github.com/caariiboouu/os-99
mkdir -p ~/.local/share/os99
python3 os-99/themes/os-99-platinum/decor/generate-art.py \
    --out ~/.local/share/os99/decor --palette light
~/.local/bin/os99-window-bars      # or run it from ./bin
```

Then source the generated config from your Hyprland config:

```lua
os99_dir = os.getenv("HOME") .. "/.local/share/os99/decor"
dofile(os99_dir .. "/bars.lua")
```

Set `OS99_DIR` to keep the art somewhere else, and `OS99_PALETTE=dark` for the
Graphite palette.

## Remove

Everything the install put down, in reverse:

```
hyprpm disable hyprbars-os99 && hyprpm remove https://github.com/caariiboouu/os-99
omarchy plugin remove io.github.caariiboouu.os-99
omarchy theme remove "OS 99 Platinum"     # and "OS 99 Graphite", "OS 99 Noir"
rm -f ~/.local/bin/os99-* \
      ~/.config/omarchy/hooks/theme-set.d/os99-*.sh \
      ~/.config/omarchy/hooks/post-boot.d/os99-*.sh \
      ~/.config/omarchy/os99-theme.toml
```

Switch to another theme first if one of these is active. On plain Hyprland,
delete `~/.local/share/os99` and the `dofile` line you added.

## The title bar boxes

The close box is on the left; zoom and collapse are on the right, as they were.
That is the default, not the limit — eight boxes exist, and which ones a bar
carries is yours to choose:

| box | glyph | does | latches |
|---|---|---|---|
| `close` | plain box | close the window | |
| `kill` | the X | force quit — sits beside close, the two ways to end a window | |
| `zoom` | square off the top-left corner | expand, keeping the title bar | ✓ |
| `collapse` | bar along the bottom | minimize to the bar widget | |
| `shade` | two slats | roll up to just the title bar | ✓ |
| `float` | two overlapping windows | toggle floating | ✓ |
| `pin` | solid square | pin a floating window — only shown while it floats | ✓ |
| `full` | four corner brackets | true fullscreen — takes the bar with it, `SUPER+F` returns | |

Rest on a box and it names what it does. A box that **latches** is drawn held
down while the window is already in that state, and then its tooltip names the
way back out rather than repeating the way in — "Unfloat", not "Float". The
names match the right-click menu's. The state is read off the window every
frame, so a change
made by a keybind or by the window itself shows up on the box too. Tooltips are
`plugin:hyprbars:bar_tooltips` and `bar_tooltip_delay` (ms) if you want them
gone or slower.

The two slats are the traced OS 9 windowshade box, so they sit on `shade`, the
box that actually rolls a window up. Minimize is not an OS 9 idea and gets its
own mark instead of borrowing that one.

`full` is the one box that cannot undo itself. True fullscreen hands the whole
output to the window and the compositor draws no decoration on it — the status
bar goes too — so the box you would click to come back is not there. **`SUPER+F`
toggles it back.** Holding the window at maximized and only telling the client
it is fullscreen would keep the bar, but on anything that does not restyle its
own chrome that is exactly what `zoom` already does, and two boxes doing one
thing is worse than one box with a caveat. If you would rather not have the
trap at all, `os99-buttons toggle full` takes it off the bar — `zoom` already
gives you a full-screen window that keeps its title bar.

**Right-click anywhere on the bar that is not a box** and an OS 9 menu opens
with the full list — click to toggle, checkmarks show what is on (Omarchy
shell only). The same switch from a terminal:

```
os99-buttons list
os99-buttons toggle float
```

Both edit `[buttons] left/right` in `os99-theme.toml` (corner-first per side)
and apply live. All eight boxes' art is drawn every run, so toggling never
waits on a redraw.

A floating window's title bar can also never end up under the status bar: the
status bar is a top layer, so a bar beneath it would be unclickable — the one
handle a floating window has, gone. The plugin nudges any such window back
below the reserved area, the way nothing was allowed over the OS 9 menu bar.

Collapse **minimizes**: the window is parked on a workspace called
`os99-minimized` and the bar widget offers it back. Hyprland has no minimize, and
every in-place imitation of one hides the title bar you would click to undo it,
so the window goes somewhere rather than shrinking in place.

A parked window is never hidden. It stays in `hyprctl clients`, which means there
are three independent ways back and no way to strand one:

```
os99-minimize list        # what is parked
os99-minimize restore     # bring back the most recent
os99-minimize restore-all
```

...the bar widget (Omarchy only), or simply switching to that workspace. If you would rather bind it than click it:

```lua
hl.bind("SUPER + M", hl.dsp.exec_cmd("hyprctl eval 'hl.plugin.hyprbars.minimize()'"))
```

Windowshade -- rolling the window up into just its bar, the way System 7 did --
is the `shade` box (or `hl.plugin.hyprbars.shade()` on a key). It is off by
default: rolling a window up means floating it, and Hyprland re-centres a
floated window. The plugin bounds the correction it applies when that happens,
so the bar can sit a little high in the worst case, but never off screen.

## Why the art is generated rather than shipped as final PNGs

The frame is authored at **device** resolution and blitted 1:1, so the
compositor never resamples it — that is what keeps a 1px pinstripe from turning
into a moiré on a fractional scale. The cost is that art baked for one display
scale is not slightly wrong on another, it is the **wrong size**.

So the art is regenerated to match your display. `bar.env` records the scale it
was drawn at, and the hook redraws (about 110 ms) whenever that disagrees with
the monitor. Change your monitor scale and it follows on the next config reload.

Not every scale is available, incidentally — Hyprland needs the logical size to
land on whole pixels in both axes, and silently snaps to the nearest one that
does. On 2560×1440 that means 1.5 is not a real option and quietly becomes 1.6.

## Tuning

Everything is in `os99-theme.toml` — ridge count and thickness, bezel depth,
shadow geometry, button sizes, and both palettes. Edit it and run:

```
os99-theme-reload
```

A few values are derived rather than free, and the generator prints its
reasoning when it runs. The bar height is quantised so it lands on whole device
pixels; the boxes are sized from the hatch field so the two bands match; and the
hatch is placed to meet the buttons, which the plugin centres. The shadow's
inset can never exceed its depth, because Hyprland builds a shadow by growing
the window box and then shifting it.

## Credits

- Built on [hyprbars](https://github.com/hyprwm/hyprland-plugins) by Vaxry and
  contributors (BSD 3-Clause). The fork adds nine-patch frame rendering,
  per-button sides and images, and teardown guards.
- The close/zoom box geometry was traced from the XPMs in
  [grassmunk/Platinum9](https://github.com/grassmunk/Platinum9).
- Colour values sampled from Platinum artwork; the dark "Graphite" palette
  mirrors its spacing rather than inventing one.

## Licence

MIT — see [LICENSE](LICENSE). The hyprbars fork keeps its original BSD 3-Clause
licence and lives in its own repository.
