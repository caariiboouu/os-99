import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons

// The only part of OS 99 that has to live inside omarchy-shell.
//
// The menu bar's face cannot be set from anywhere else. Style.fontFamily
// defaults to the "monospace" fontconfig alias; a theme's shell.toml has size
// tokens but no family key, and OMARCHY_MENU_FONT reaches the summoned popups
// and not the bar. So one small thing has to run in the shell, and this is it.
//
// It has no window, no bar entry and no popup. Everything OS 99 used to do with
// QML -- the minimized-window list, the box configuration menu -- is the `os99`
// command now. A terminal cannot tell you a window is parked without being
// asked, and that glance was the whole argument for a bar widget; weighed
// against QML that breaks silently when Omarchy moves a type underneath it, the
// glance lost.
//
// The face is READ rather than named here: generate-art.py records what the
// font chain actually resolved to (ChicagoFLF -> Charcoal -> Chicago Kare ->
// sans-serif) in bar.env, so the menu bar cannot end up on a different face
// from the title bars. os99-menubar-font holds that value to a plain font name
// before it leaves the shell script, and it is checked again here, because it
// is about to become a font family in a process that outlives every part of
// this.
QtObject {
  id: root

  // This file is <plugin>/shell-plugin/os99/Service.qml in a checkout, and
  // <plugin>/Service.qml when a package installs it flat. Two levels up is the
  // plugin root either way for the first, and the helpers are ordinary
  // programs in /usr/bin for the second.
  readonly property string pluginRoot: {
    var u = String(Qt.resolvedUrl("../../"))
    if (u.indexOf("file://") === 0)
      u = u.substring(7)
    return decodeURIComponent(u)
  }
  readonly property string helperDir:
    pluginRoot.indexOf("/usr/") === 0 ? "/usr/bin/" : pluginRoot + "bin/"

  // Bounded like every other helper call: os99-run holds a deadline, caps the
  // output, and takes the whole process group down if it overruns.
  readonly property var probe: Process {
    running: true
    command: [root.helperDir + "os99-run",
              "--deadline", "5", "--max-stdout", "256", "--max-stderr", "4096",
              "--", root.helperDir + "os99-menubar-font"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var f = String(text).trim().substring(0, 64)
        Style.fontFamily = /^[A-Za-z0-9][A-Za-z0-9 ._-]*$/.test(f) ? f : "monospace"
      }
    }
  }

  // The only reason this answers anything: a plugin that failed to load cannot
  // report that it failed to load, so os99-widget-check asks from outside and
  // a target that answers is the proof. See docs/security.md.
  readonly property var ipc: IpcHandler {
    target: "os99"
    function ping(): string { return "ok" }
  }
}
