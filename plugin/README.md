# hyprbars, forked for OS 99

A fork of [hyprbars](https://github.com/hyprwm/hyprland-plugins) carrying the
changes the [OS 99](https://github.com/caariiboouu/os-99) theme needs. BSD
3-Clause, same as upstream.

    hyprpm add https://github.com/caariiboouu/os99-hyprbars
    hyprpm enable hyprbars-os99
    hyprpm reload -n

It still reports itself to Hyprland as `hyprbars`, because the Lua API it
exposes is `hl.plugin.hyprbars.*` and the theme calls it by name. Hyprland
refuses to load two plugins with the same name, so this cannot run alongside
upstream hyprbars — pick one.

## What the fork adds

- **Nine-patch frame rendering.** `frame_texture` draws a bezel around the whole
  window with the title bar as its top edge. Upstream fills the bar with a solid
  rect, which cannot do pinstripes, and Hyprland's own border interpolates a
  single gradient across the window so it can never put a crisp 1px highlight on
  the top-left and a 1px shadow on the bottom-right.
- **`frame_texture_unscaled`** blits that art 1:1 as device pixels instead of
  scaling it, which is what stops a fine hatch turning into moiré on a
  fractional scale.
- **Per-button `side` and `image`** — upstream has one global side and draws
  buttons as a rounded rect plus a font glyph.
- **`clear_buttons()`**, so buttons declared through the Lua API can be
  redeclared idempotently. Upstream clears its button list on every config
  reload and never repopulates API-added ones, which silently empties the title
  bar on every reload.
- **`frame_force_square` / `frame_force_flat`** hold rounding at 0 and disable
  blur and inactive-dim on framed windows, so the look does not depend on the
  user's own `looknfeel` config winning an ordering race.
- **Teardown guards.** Unloading the plugin used to abort the compositor: a
  queued pass element would draw one more frame after the plugin's config values
  were destroyed, call `->value()` on a dead value, and throw out of the render
  pass — where an escaping exception is `std::terminate`. Every entry point now
  checks a shutting-down flag first.

## Compatibility

Built and tested against Hyprland **0.56.2**. `hyprpm` rebuilds it against
whatever Hyprland you run, and hyprbars refuses to load on a header/runtime hash
mismatch, so an upgrade gives you a build or load failure rather than silent
misbehaviour.
