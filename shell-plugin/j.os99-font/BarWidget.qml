import QtQuick
import Quickshell.Io
import qs.Commons
import qs.Ui

// shell.toml's [font] section only parses integers -- there is no family key,
// and Style.fontFamily is hardcoded to the "monospace" fontconfig alias. This
// widget draws nothing and exists only to retarget that property, so the OS 99
// menu bar can use the theme face without touching the global monospace alias
// (which would drag every terminal and browser code block along with it).
//
// It reads the face the font chain actually RESOLVED rather than naming one
// here. generate-art.py records that in bar.env after checking what is
// installed (ChicagoFLF -> Charcoal -> Chicago Kare -> sans-serif), so the menu
// bar cannot end up on a different face from the title bars -- which is exactly
// what a hardcoded name did on a machine missing that font.
//
// The probe runs once at construction, so the shell is restarted by
// hooks/theme-set.d/os99-gtk.sh whenever a theme switch crosses the OS 99
// boundary.
BarWidget {
  id: root
  moduleName: "j.os99-font"

  implicitWidth: 1
  implicitHeight: 1

  Process {
    running: true
    command: ["sh", "-c", "d=\"$HOME/.local/state/omarchy/current/theme\"; test -f \"$d/os99.marker\" || { echo monospace; exit 0; }; f=$(sed -n 's/^OS99_TEXT_FONT=\"\\(.*\\)\"$/\\1/p' \"$d/decor/bar.env\" 2>/dev/null | tail -1); echo \"${f:-ChicagoFLF}\""]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        Style.fontFamily = String(text).trim() || "monospace"
      }
    }
  }
}
