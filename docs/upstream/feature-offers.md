# hyprbars: three changes we can contribute

Written in ASD-STE100 (Simplified Technical English).

We keep a fork of hyprbars for a theme. Three of our changes are not specific to
that theme. We can send each one as a separate pull request. Tell us which ones
you want.

## 1. A side for each button

Today all buttons go to one side. The side comes from
`plugin:hyprbars:bar_buttons_alignment`.

Some window styles put one button on the left and the other buttons on the
right. You cannot make this layout with one global side.

The change adds an optional `side` field to a button. The value is `"left"` or
`"right"`. If the field is empty, the button uses the global alignment. This
keeps all existing configurations correct.

## 2. An image for each button

Today a button is a rounded rectangle with a font character on it. The character
comes from the `icon` field.

You cannot draw a pixel image with a rounded rectangle and a font character.

The change adds an optional `image` field to a button. The value is the path of
a PNG file. If the field is set, hyprbars draws the image in place of the
rectangle and the character. hyprbars reads the file again if its modification
time changes. A theme can then change its buttons without a plugin reload.

## 3. A function to remove all buttons

Today `add_button` adds a button to a list. `onPreConfigReload` empties the list
before Hyprland reads the configuration again. This is correct when the
`add_button` calls are in the configuration.

The calls are not always in the configuration. A script can add buttons at run
time with `hyprctl eval`. Hyprland does not keep these buttons after a
configuration reload. The script must add them again. If the script adds them
again, and the configuration also adds them, the bar shows each button twice.

The change adds `clear_buttons()`. A script calls it before it adds its buttons.
The result is the same each time.

## What we do not offer here

Our fork also draws a nine-patch frame around the full window. This is a large
change. We do not offer it in this list. Ask us if you want to discuss it.

## Contact

The fork is at https://github.com/caariiboouu/os-99 in `plugin/hyprbars`.
