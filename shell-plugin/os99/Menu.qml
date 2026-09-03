import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Commons
import qs.Ui

// The title bar's right-click menu: which boxes does this bar carry?
//
// The hyprbars fork spawns os99-bar-menu on a right-click that lands on bar
// face rather than on a box; that script reads the cursor position and calls
// in here over shell IPC. Everything the menu DOES goes back out through
// `os99-buttons toggle`, which edits os99-theme.toml and re-applies live --
// so the menu is only a face on the CLI, and the CLI keeps working if the
// menu never loads.
//
// Like the bar widget, this names its helpers by an absolute path derived from
// this file's own location and runs them under bin/os99-run, which caps their
// output, holds them to a deadline, and takes down the whole process group if
// they overrun. Nothing here builds a shell command line, so there is no
// composition to get wrong, and everything shown is rendered as plain text.
//
// Styled as an OS 9 menu: popup face, 1px rule, a hard offset shadow with no
// blur (blur is the tell that something is not classic Mac), square corners,
// the chrome font, and a checkmark column. Colours come from the theme's
// popup palette so Graphite gets its dark chrome without a special case.
Item {
  id: root

  property bool opened: false
  property var entries: []
  property real menuX: 0
  property real menuY: 0
  property string windowAddr: ""
  property string failure: ""

  readonly property int itemH: Style.space(26)
  readonly property int checkW: Style.space(20)
  readonly property int padX: Style.space(12)
  readonly property int menuW: Style.space(170)
  readonly property int shadowOff: Style.space(4)

  // This file is <plugin>/shell-plugin/os99/Menu.qml; the helpers ship in the
  // same install, two levels up in bin/.
  readonly property string pluginRoot: {
    var u = String(Qt.resolvedUrl("../../"))
    if (u.indexOf("file://") === 0)
      u = u.substring(7)
    return decodeURIComponent(u)
  }
  // Where the helpers are. A plugin installed from git is a checkout, so they
  // sit in its own bin/. A packaged install puts this QML under /usr/share and
  // the helpers become ordinary programs in /usr/bin. The prefix decides it --
  // no probing, because QML cannot ask whether a file exists.
  readonly property string helperDir:
    pluginRoot.indexOf("/usr/") === 0 ? "/usr/bin/" : pluginRoot + "bin/"
  readonly property string runner: helperDir + "os99-run"
  readonly property string buttonsCli: helperDir + "os99-buttons"

  // ------------------------------------------------------- the menu-bar font
  //
  // The one thing OS 99 has to do inside omarchy-shell, and the reason this
  // plugin is keepLoaded: Style.fontFamily defaults to the "monospace"
  // fontconfig alias, a theme's shell.toml carries size tokens but no family
  // key, and OMARCHY_MENU_FONT reaches the summoned popups and not the bar. So
  // the menu bar can only get its OS 9 face from in here.
  //
  // The face is READ rather than named: generate-art.py records what the font
  // chain actually resolved to (ChicagoFLF -> Charcoal -> Chicago Kare ->
  // sans-serif) in bar.env, so the menu bar cannot end up on a different face
  // from the title bars. os99-menubar-font holds it to a plain font name, and
  // it is checked again here, because it becomes a font family in a process
  // that outlives every part of this.
  //
  // It runs at construction, and the panel is constructed at shell start
  // because of keepLoaded -- opening the menu has nothing to do with it.
  readonly property var fontProbe: Process {
    running: true
    command: [root.runner,
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

  readonly property int deadline: 5
  // Toggling a box is not just an edit: it redraws every piece of art and
  // re-applies the plugin settings, so it gets a deadline that fits the work
  // rather than the one that fits a question. Killed halfway through a redraw
  // is the one outcome worth avoiding here.
  readonly property int toggleDeadline: 30
  // Eight boxes and their labels. A few hundred bytes; this is generous.
  readonly property int maxStdout: 16384
  readonly property int maxStderr: 4096
  readonly property int maxEntries: 32
  readonly property int maxLabel: 40

  function helperArgv(args, maxOut, seconds) {
    return [root.runner,
            "--deadline", String(seconds === undefined ? root.deadline : seconds),
            "--max-stdout", String(maxOut),
            "--max-stderr", String(root.maxStderr),
            "--", root.buttonsCli].concat(args)
  }

  function openAt(payloadJson) {
    var p
    try { p = JSON.parse(payloadJson || "{}") } catch (e) { p = {} }
    root.windowAddr = String(p.window || "").substring(0, 32)
    root.menuX = Number(p.x) || 0
    root.menuY = Number(p.y) || 0
    if (lister.running)
      return
    root.failure = ""
    root.listerStarted = false
    // Armed before the start: a helper that is not there never emits started,
    // and a watchdog hung on that signal would never fire.
    listerWatchdog.restart()
    lister.running = true
  }

  function close() { root.opened = false }

  function toggle(id) {
    // The id is our own list's, and it travels as an argv element rather than
    // as text in a command line, but it is still held to the names the art
    // actually has -- a list is only as trustworthy as the thing that made it.
    if (!/^[a-z]+$/.test(id))
      return
    if (toggler.running)
      return
    toggler.command = root.helperArgv(["toggle", id], 4096, root.toggleDeadline)
    togglerWatchdog.restart()
    toggler.running = true
    root.opened = false
  }

  // Screen containing the click, so the menu opens where the cursor is on a
  // multi-monitor layout instead of on whichever screen is "first".
  function screenAt(x, y) {
    var ss = Quickshell.screens
    for (var i = 0; i < ss.length; i++) {
      var s = ss[i]
      if (x >= s.x && x < s.x + s.width && y >= s.y && y < s.y + s.height)
        return s
    }
    return ss.length > 0 ? ss[0] : null
  }

  function clean(s) {
    return String(s).replace(/[\x00-\x1f\x7f]/g, " ").substring(0, root.maxLabel)
  }

  // TERM, then KILL. os99-run leads its own process group, so one signal
  // reaches everything it started.
  property var aborting: null
  function abort(p) {
    if (!p.running)
      return
    root.aborting = p
    p.signal(15)
    killer.restart()
  }

  Timer {
    id: killer
    interval: 2000
    onTriggered: {
      if (root.aborting && root.aborting.running)
        root.aborting.signal(9)
      root.aborting = null
    }
  }

  // See BarWidget.qml: a command that cannot be executed never emits started,
  // and never gets an exit code either. Catch it on the way back to idle.
  property bool listerStarted: false

  Process {
    id: lister
    command: root.helperArgv(["list", "--json"], root.maxStdout)
    onStarted: root.listerStarted = true
    onRunningChanged: {
      if (!lister.running && !root.listerStarted) {
        listerWatchdog.stop()
        root.entries = []
        root.failure = "OS 99 helpers not found in " + root.helperDir
        root.opened = true
      }
    }
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var rows = []
        try {
          rows = JSON.parse(String(text).substring(0, root.maxStdout)).buttons || []
        } catch (e) {
          rows = []
        }
        var out = []
        for (var i = 0; i < rows.length && out.length < root.maxEntries; i++) {
          var r = rows[i]
          if (!r || !/^[a-z]+$/.test(String(r.id)))
            continue
          out.push({ id: String(r.id), label: root.clean(r.label), on: r.on === true })
        }
        root.entries = out
      }
    }
    onExited: (code, status) => {
      listerWatchdog.stop()
      if (root.entries.length > 0) {
        root.failure = ""
        root.opened = true
      } else {
        // Nothing to show is never nothing to say: the menu is the only visible
        // sign that the boxes are configurable at all.
        root.failure = code === 125 || code === 126 || code === 127
                       ? "OS 99 helpers not found in " + root.helperDir
                       : (code === 124 ? "os99-buttons timed out"
                                       : "os99-buttons failed (" + code + ")")
        root.opened = true
      }
    }
  }

  Timer {
    id: listerWatchdog
    interval: (root.deadline + 3) * 1000
    onTriggered: {
      root.failure = "os99-buttons did not answer"
      root.entries = []
      root.opened = true
      root.abort(lister)
    }
  }

  Process {
    id: toggler
    onExited: (code, status) => togglerWatchdog.stop()
  }

  Timer {
    id: togglerWatchdog
    interval: (root.toggleDeadline + 3) * 1000
    onTriggered: root.abort(toggler)
  }

  IpcHandler {
    target: "os99"
    function menu(payloadJson: string): string {
      root.openAt(payloadJson)
      return "ok"
    }
    function close(): string { root.close(); return "ok" }
    function ping(): string { return "ok" }
  }

  PanelWindow {
    id: panel
    visible: root.opened
    screen: root.screenAt(root.menuX, root.menuY)
    anchors { top: true; bottom: true; left: true; right: true }
    color: "transparent"
    WlrLayershell.namespace: "os99-menu"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    exclusionMode: ExclusionMode.Ignore

    // Anywhere that is not the menu dismisses it, the way a menu dies when
    // the mouse gives up on it.
    MouseArea {
      anchors.fill: parent
      acceptedButtons: Qt.AllButtons
      onPressed: root.opened = false
    }

    Item {
      id: card
      // Clamp inside the screen: a menu summoned at the bottom edge walks up,
      // not off. Local coords: the panel covers the whole screen.
      x: Math.max(0, Math.min(root.menuX - (panel.screen ? panel.screen.x : 0),
                              panel.width - card.width - root.shadowOff))
      y: Math.max(0, Math.min(root.menuY - (panel.screen ? panel.screen.y : 0),
                              panel.height - card.height - root.shadowOff))
      width: root.menuW + root.shadowOff
      height: column.implicitHeight + Style.space(8) + root.shadowOff

      // The hard shadow first, offset and square. No blur, deliberately.
      Rectangle {
        x: root.shadowOff
        y: root.shadowOff
        width: root.menuW
        height: column.implicitHeight + Style.space(8)
        color: Qt.rgba(0, 0, 0, 0.45)
      }

      Rectangle {
        width: root.menuW
        height: column.implicitHeight + Style.space(8)
        color: Color.popups.background
        border.color: Color.popups.border
        border.width: 1
        radius: 0

        Column {
          id: column
          anchors.fill: parent
          anchors.margins: Style.space(4)

          // Says what went wrong and where it was looking, instead of opening
          // an empty menu that reads as "there is nothing to configure".
          Text {
            visible: root.failure !== ""
            width: column.width
            padding: Style.space(4)
            text: root.failure
            color: Color.popups.text
            font.family: Style.fontFamily
            font.pixelSize: Style.font.caption
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
          }

          Repeater {
            model: root.entries

            Rectangle {
              id: row
              required property var modelData

              width: column.width
              height: root.itemH
              radius: 0
              // OS 9 highlights the whole item and inverts the text; the
              // accent stands in for the classic highlight colour.
              color: rowHover.hovered ? Color.accent : "transparent"

              HoverHandler { id: rowHover }

              Text {
                x: 0
                width: root.checkW
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignHCenter
                text: row.modelData.on ? "✓" : ""
                color: rowHover.hovered ? Color.popups.background : Color.popups.text
                font.family: Style.fontFamily
                font.pixelSize: Style.font.body
                textFormat: Text.PlainText
              }

              Text {
                x: root.checkW
                width: parent.width - root.checkW - root.padX
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.label
                color: rowHover.hovered ? Color.popups.background : Color.popups.text
                font.family: Style.fontFamily
                font.pixelSize: Style.font.body
                textFormat: Text.PlainText
                elide: Text.ElideRight
              }

              MouseArea {
                anchors.fill: parent
                onClicked: root.toggle(row.modelData.id)
              }
            }
          }
        }
      }
    }
  }
}
