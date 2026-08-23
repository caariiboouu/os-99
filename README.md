# OS 99

A Hyprland theme for people who peaked in 1999.

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

That core is **compositor-generic**. The plugin has no idea Omarchy exists.
Omarchy just adds somewhere convenient to put the art and hooks to apply it, so
the theme also ships colours for Omarchy's bar, menus and notifications.

## Requirements

- Hyprland (developed against **0.56.2**)
- `python3` ≥ 3.11 — standard library only, no pip install
- A font. It tries **Charcoal**, then **ChicagoFLF**, then **Chicago Kare**, then
  gives up and uses `sans-serif`. Charcoal is Apple's and is deliberately not
  bundled here; `ttf-chicagoflf` (public domain) is a metric drop-in:

      yay -S ttf-chicagoflf

## Install

### 1. The plugin (everyone)

```
hyprpm add https://github.com/caariiboouu/os99-hyprbars
hyprpm enable hyprbars-os99
hyprpm reload -n
```

`hyprpm` builds it against your own Hyprland, so there is no ABI mismatch to
manage. Note it reports itself to Hyprland as `hyprbars` and therefore cannot be
loaded alongside upstream hyprbars — pick one.

### 2a. On Omarchy

```
git clone https://github.com/caariiboouu/os-99
cd os-99 && ./bin/os99-install
omarchy theme set "OS 99 Platinum"
```

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
