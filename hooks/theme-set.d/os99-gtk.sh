#!/bin/bash
# OS-level only. The OS 9 look is the WINDOW FRAME -- hyprbars draws it for
# every toolkit -- plus the menu bar. Application interiors are deliberately
# left stock.
#
# What this used to do and no longer does, and why:
#
#   gtk-theme / gtk-4.0/gtk.css   Themed the inside of GTK apps. Chasing app
#                                 internals is endless (Nautilus's split header
#                                 alone never lined up) and the frame already
#                                 carries the theme.
#   document-font-name / monospace-font-name
#                                 Forced Geneva into documents and Monaco into
#                                 every terminal and code block.
#
# font-name IS still set. It is the SYSTEM font -- menus, buttons, labels --
# which is chrome, and it does not reach web page content: browsers take page
# defaults from fontconfig's generic families, which is exactly the layer that
# stays stock now.
#   fontconfig 60-os99-fonts.conf  The worst of them: it aliased the GENERIC
#                                 families, sans-serif -> Geneva, serif -> New
#                                 York, monospace -> Monaco. That reaches
#                                 WEBPAGES -- every site asking for sans-serif
#                                 got a 12px bitmap face.
#
# The chrome fonts do not come from any of that and are unaffected: the title
# bar gets Charcoal from plugin:hyprbars:bar_text_font, and the menu bar from
# the j.os99-font shell plugin. Both name the family directly.
#
# 99-os99-chicago.conf stays -- it only disables antialiasing FOR the Chicago and
# Charcoal families, so it fires on our chrome and on nothing else.

THEME_DIR="$HOME/.local/state/omarchy/current/theme"

# The system font is NOT hardcoded. Charcoal is Apple's, so it cannot be shipped
# with the theme and may simply be absent on someone else's machine; the
# generator resolves the best installed face (Charcoal -> ChicagoFLF -> Chicago
# Kare) and records the result, with any size compensation already applied, in
# bar.env. Read it from there so the menu font and the title-bar font can never
# disagree about which face exists.
OS99_UI_FONT="Charcoal 11"
[[ -r "$THEME_DIR/decor/bar.env" ]] && \
  OS99_UI_FONT=$(sed -n 's/^OS99_UI_FONT="\(.*\)"$/\1/p' "$THEME_DIR/decor/bar.env" | tail -1)
[[ -n $OS99_UI_FONT ]] || OS99_UI_FONT="Charcoal 11"
GTK4_CSS="$HOME/.config/gtk-4.0/gtk.css"
FC_LINK="$HOME/.config/fontconfig/conf.d/60-os99-fonts.conf"
GHOSTTY_FONT="$HOME/.config/ghostty/os99-font.conf"

# Retire anything an older version of this hook installed, whichever theme is
# active, so switching away is not the only way to get rid of it.
if [[ -L $GTK4_CSS ]] && readlink "$GTK4_CSS" | grep -q "OS9-Platinum"; then
  rm -f "$GTK4_CSS"
  [[ -e $GTK4_CSS.pre-os9 ]] && mv "$GTK4_CSS.pre-os9" "$GTK4_CSS"
fi
if [[ -L $FC_LINK ]] && readlink "$FC_LINK" | grep -q "fontconfig.conf"; then
  rm -f "$FC_LINK"
  fc-cache -f >/dev/null 2>&1
fi
rm -f "$GHOSTTY_FONT"
gsettings reset org.gnome.desktop.interface document-font-name
gsettings reset org.gnome.desktop.interface monospace-font-name
gsettings reset org.gnome.desktop.interface gtk-theme

if [[ -f $THEME_DIR/os99.marker ]]; then
  # The ONE thing worth setting: hyprbars draws close/zoom/collapse for every
  # window, so GTK drawing its own puts a second set inside the frame and
  # reserves width for them. An empty layout removes them entirely.
  gsettings set org.gnome.desktop.wm.preferences button-layout ":"
  # The system font: menus, buttons, labels. Chrome, not documents.
  gsettings set org.gnome.desktop.interface font-name "$OS99_UI_FONT"
else
  gsettings set org.gnome.desktop.wm.preferences button-layout "appmenu:close"
  gsettings reset org.gnome.desktop.interface font-name
fi

# The MENU BAR font comes from the j.os99-font shell plugin, which probes
# os99.marker once at construction -- a plugin hot-reload does not re-instantiate
# it, only a full restart does. So restart the shell when a switch actually
# crosses the OS 9 boundary, and the bar font follows the theme instead of
# sticking on whatever it had.
STATE="$HOME/.local/state/omarchy/os99-font-state"
if [[ -f $THEME_DIR/os99.marker ]]; then want=os9; else want=other; fi
have=$(cat "$STATE" 2>/dev/null || echo unset)
if [[ $want != "$have" ]]; then
  mkdir -p "$(dirname "$STATE")"
  printf '%s\n' "$want" >"$STATE"
  # Detached and delayed so it lands after omarchy-theme-set finishes its own
  # shell IPC rather than racing it.
  setsid bash -c 'sleep 2; omarchy restart shell' >/dev/null 2>&1 &
fi
