# Why this fork exists

A one-line change to Omarchy's OSD — the toast at the bottom of the screen.

Upstream draws the panel and its text from two independent sources:

```qml
color: Util.alpha(Color.background, 0.97)   // colors.toml
color: Color.popups.text                     // shell.popups.toml
```

Those only agree by luck. They do not agree in a theme whose *content* is dark
but whose *chrome* is light — which is exactly what OS 99 Noir is. Its
`colors.toml` background is `#2B2B2B`, so the black popup text landed at
**1.48:1**. Unreadable.

## Why this can't be fixed in theme data

Three surfaces in Noir pair these values differently:

| surface | text | background |
|---|---|---|
| OSD toast | `popups.text` | `colors.toml` background (dark) |
| `Ui/Dropdown`, `MultiSelect`, `SearchableDropdown` | `popups.text` | `popups.background` |
| Bar tray menus (`PopupCard`) | the **bar's** text (black) | `popups.background` |

So `popups.text` must be **light** for the first row and **dark** for the
second. One value, two contradictory requirements — and whichever you pick, a
third surface breaks:

- popups dark → toast and dropdowns fine, **tray menus go black-on-#4A4A4A (2.37:1)**
- popups light → dropdowns and tray menus fine, **toast goes black-on-#2B2B2B (1.48:1)**

Both were tried. Neither works.

## What this changes

Reading `Color.popups.background` instead of `Color.background` makes the OSD
pair with the same surface every other popup uses. The contradiction disappears
and all three land at **15.46:1**.

## Upstream

This belongs upstream: the OSD should pair `popups.text` with
`popups.background`, as every other consumer in the shell already does. Any
theme with a dark `colors.toml` background and a light popup surface hits this.
If Omarchy fixes it, delete this plugin and run `omarchy plugin enable omarchy.osd`.
