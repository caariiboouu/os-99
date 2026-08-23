import QtQuick
import Quickshell.Io
import qs.Commons
import qs.Ui

// shell.toml's [font] section only parses integers -- there is no family key,
// and Style.fontFamily is hardcoded to the "monospace" fontconfig alias. This
// widget draws nothing and exists only to retarget that property, so the OS 9
// menu bar can use Charcoal without touching the global monospace alias (which
// would drag every terminal and browser code block along with it).
//
// Charcoal, not Chicago: Chicago was the System 7 system font, and OS 8.5
// replaced it with Charcoal. The window titles already draw in Charcoal, so
// this keeps the menu bar and the title bars on one face.
//
// The probe runs once at construction, so the shell is restarted by
// hooks/theme-set.d/os9-gtk.sh whenever a theme switch crosses the OS 9 boundary.
BarWidget {
  id: root
  moduleName: "j.os99-font"

  implicitWidth: 1
  implicitHeight: 1

  Process {
    running: true
    command: ["sh", "-c",
      "test -f \"$HOME/.local/state/omarchy/current/theme/os99.marker\" && echo os9 || echo other"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        Style.fontFamily = (String(text).trim() === "os9") ? "Charcoal" : "monospace"
      }
    }
  }
}
