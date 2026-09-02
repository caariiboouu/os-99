import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import qs.Commons
import qs.Ui

// The OS 99 bar widget: the way back from the collapse box, the menu-bar face,
// and -- on a machine where OS 99 has only just been installed -- the thing
// that finishes installing it.
//
// One widget because the marketplace lists one plugin per repository, and these
// only ever ship together.
//
// FINISHING THE INSTALL. `omarchy plugin add` clones and enables; by design it
// never runs a plugin's code, so it cannot copy a theme into place, draw art at
// your display's scale, or build the compositor plugin that draws the frames.
// Those steps used to live in the README, which meant the install was only
// complete for someone who read to the end of it. Instead this widget asks
// os99-install what is still outstanding and, when something is, says so in the
// bar and offers to do it. Nothing happens without a click: the outstanding
// work includes compiling a plugin into the compositor and switching the theme,
// which are not things to do to somebody quietly.
//
// MINIMIZED WINDOWS. OS 99's title bar minimises a window by parking it on the
// os99-minimized workspace (see bin/os99-minimize). A parked window has no bar
// on screen to click, so something that can see windows the user cannot has to
// offer the way back. That is this: a count in the bar, and a list you pick
// from. It is a convenience, not the mechanism. Everything here goes through
// os99-minimize, which works on its own from a terminal, and the park is a
// REGULAR workspace -- so if this widget never loads, the windows are still
// listed by `hyprctl clients` and still reachable by switching to that
// workspace. Nothing can be stranded by a QML error.
//
// MENU-BAR FONT. shell.toml's [font] section only parses integers -- there is
// no family key, and Style.fontFamily is hardcoded to the "monospace"
// fontconfig alias. The probe below retargets that property, so the OS 99 menu
// bar can use the theme face without touching the global monospace alias
// (which would drag every terminal and browser code block along with it). It
// reads the face the font chain actually RESOLVED rather than naming one here:
// generate-art.py records that in bar.env after checking what is installed
// (ChicagoFLF -> Charcoal -> Chicago Kare -> sans-serif), so the menu bar
// cannot end up on a different face from the title bars.
//
// HOW THIS TALKS TO THE HELPERS, and why it looks like this.
//
// Nothing here composes a shell command, and nothing here trusts $PATH. Every
// helper is named by an absolute path derived from THIS FILE's own location,
// so the widget uses the copy that shipped in the same install as itself, and
// there is no way for a different os99-minimize earlier in someone's PATH to be
// the one that runs. When a helper genuinely is not there, the widget says so
// in the bar rather than failing silently (see helperError).
//
// Every helper runs under bin/os99-run, which owns its lifetime: a wall-clock
// deadline, separate caps on stdout and stderr enforced as the bytes arrive,
// and a TERM/KILL of the whole process GROUP when the deadline passes, so a
// wedged hyprctl cannot outlive the call. That matters because the data coming
// back is window titles, which are written by whatever clients happen to be
// running. Titles are bounded again on the way in here, and are rendered as
// plain text -- never as rich text, whose auto-detection would happily read
// markup out of a window title and go fetch what it names.
BarWidget {
  id: root
  moduleName: "io.github.caariiboouu.os-99"

  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family

  // ---------------------------------------------------------------- helpers

  // This file is <plugin>/shell-plugin/os99/BarWidget.qml, so two levels up is
  // the plugin root, and the helpers sit in its bin/. `omarchy plugin add`
  // clones the whole repository into the plugins directory, so they arrive
  // with this file and stay with it.
  readonly property string pluginRoot: {
    var u = String(Qt.resolvedUrl("../../"))
    if (u.indexOf("file://") === 0)
      u = u.substring(7)
    return decodeURIComponent(u)
  }
  readonly property string runner: pluginRoot + "bin/os99-run"
  readonly property string minimizer: pluginRoot + "bin/os99-minimize"
  readonly property string fontProbe: pluginRoot + "bin/os99-menubar-font"
  readonly property string installer: pluginRoot + "bin/os99-install"

  // Seconds a helper may take, and bytes it may say. A list of parked windows
  // is a few hundred bytes; anything approaching these numbers is a fault, not
  // a big desktop.
  readonly property int deadline: 5
  // Restoring is more work than asking, and restore-all is more again: one
  // hyprctl dispatch per window. Reading gets the short deadline, acting gets
  // one sized to the act.
  readonly property int actDeadline: 20
  // Setup compiles a Hyprland plugin from source. Minutes, not seconds.
  readonly property int setupDeadline: 900
  readonly property int maxStdout: 65536
  readonly property int maxStderr: 4096
  // Rows a menu can show and a field length it can show them at. Bounded in
  // os99-minimize as well; bounded twice because the two bounds protect
  // different things -- that one keeps the pipe small, this one keeps a single
  // row from being able to consume the widget.
  readonly property int maxRows: 100
  readonly property int maxField: 120

  // The exact argv, never a command line. os99-run refuses anything that is
  // not one of the helpers sitting beside it, so a mistake here cannot become
  // a way to run something else.
  function helperArgv(script, args, maxOut, seconds) {
    return [root.runner,
            "--deadline", String(seconds === undefined ? root.deadline : seconds),
            "--max-stdout", String(maxOut),
            "--max-stderr", String(root.maxStderr),
            "--", script].concat(args)
  }

  // Rows of { address, appClass, title }, most recently minimized first.
  property var entries: []
  readonly property int count: entries.length
  property bool popupOpen: false

  // Empty while the helpers are answering normally. Anything else is shown in
  // the bar: a widget that quietly does nothing is the failure that costs
  // someone a window they cannot find.
  property string helperError: ""

  // Setup state, from os99-install --status.
  property var setupSteps: []
  property bool setupReady: true
  property bool setupRunning: false
  property string setupLine: ""

  // The bar stays quiet until it has news -- something minimized, something
  // wrong, or an install still to finish. The font probe still runs: a Process
  // does not care that its widget is collapsed to nothing.
  visible: count > 0 || helperError !== "" || !setupReady

  // A vertical bar stacks the mark over the count and grows downwards; a
  // horizontal one sets them side by side and grows sideways. This used to
  // hide itself outright on a vertical bar, which took the one control that
  // brings a parked window back out of exactly the layout where a parked
  // window is hardest to find.
  implicitWidth: vertical
    ? barSize
    : (visible ? content.implicitWidth + Style.spacing.controlPaddingX * 2 : 0)
  implicitHeight: vertical
    ? (visible ? content.implicitHeight + Style.spacing.controlPaddingY * 2 : 0)
    : barSize

  Behavior on implicitWidth {
    NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
  }

  Behavior on implicitHeight {
    NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
  }

  // ------------------------------------------------------------ the font probe

  Process {
    id: fontProber
    running: true
    command: root.helperArgv(root.fontProbe, [], 256)
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        // The helper already holds this to a plain font name; hold it again on
        // the way in, because it is about to become a font family in a shell
        // process that outlives every part of this.
        var f = String(text).trim().substring(0, 64)
        Style.fontFamily = /^[A-Za-z0-9][A-Za-z0-9 ._-]*$/.test(f) ? f : "monospace"
      }
    }
  }

  // ------------------------------------------------------- one poller only

  // A bar surface is built per monitor, so this widget is live once per screen
  // and every copy would ask the same question and get the same answer. Three
  // monitors would mean three hyprctl calls a tick for one list. So the
  // instances elect one poller -- the first in the host's own list of them, so
  // every instance names the same one -- and it hands the result to the rest.
  // Elected at each tick rather than once, so a monitor plugged in or pulled
  // out settles itself with nothing having to notify anything.
  function instances() {
    return bar && typeof bar.moduleWidgets === "function" ? bar.moduleWidgets(moduleName) : []
  }

  function isPoller() {
    var all = root.instances()
    // A bar that cannot list its own widgets is a bar we cannot coordinate on.
    // Then every instance polls, which is what this did before -- wasteful on
    // several monitors, but never silent, and silent is the worse failure.
    return all.length === 0 || all[0] === root
  }

  // Called ON a peer BY whichever instance learned something. Never starts
  // anything of its own.
  function adopt(state) {
    if (state.entries !== undefined) {
      root.entries = state.entries
      if (root.entries.length === 0)
        root.popupOpen = false
    }
    if (state.helperError !== undefined) root.helperError = state.helperError
    if (state.setupSteps !== undefined) root.setupSteps = state.setupSteps
    if (state.setupReady !== undefined) root.setupReady = state.setupReady
    if (state.setupRunning !== undefined) root.setupRunning = state.setupRunning
    if (state.setupLine !== undefined) root.setupLine = state.setupLine
  }

  function publish(state) {
    var all = root.instances()
    for (var i = 0; i < all.length; i++) {
      if (all[i] !== root && all[i] && typeof all[i].adopt === "function")
        all[i].adopt(state)
    }
  }

  // --------------------------------------------------------------- the list

  // Rows arrive one line at a time and are counted as they arrive, so a helper
  // that somehow produced a million of them is stopped rather than collected.
  property var pending: []
  property int rowsSeen: 0
  property string listerStderr: ""

  // PopupCard dismisses itself by calling close() on its owner when one exists,
  // and only falls back to writing its own `open` property when it does not --
  // and that write lands on a property BOUND to popupOpen, breaking the binding
  // and costing a click the next time round. Owning the close keeps the one
  // source of truth.
  function close() {
    root.popupOpen = false
  }

  function clean(s) {
    // Control characters out (a title carrying a newline or a tab could
    // otherwise forge a row), then cut to a length a menu can show. The escape
    // sequences go too: os99-install colours its output, and a bar is not a
    // terminal.
    return String(s).replace(/\x1b\[[0-9;]*m/g, "")
                    .replace(/[\x00-\x1f\x7f]/g, " ")
                    .substring(0, root.maxField)
  }

  function takeRow(line) {
    if (line.length === 0)
      return
    root.rowsSeen++
    if (root.rowsSeen > root.maxRows) {
      // Seen enough. Stop the helper rather than keep reading it.
      root.abort(lister)
      return
    }
    var f = line.split("\t")
    // N, address, class, title -- anything shorter is not a row we wrote.
    if (f.length < 4)
      return
    if (!/^0x[0-9a-fA-F]+$/.test(f[1]))
      return
    root.pending.push({ address: f[1], appClass: root.clean(f[2]), title: root.clean(f[3]) })
  }

  function refresh() {
    // One at a time. Hyprland emits several events for one minimize, and a
    // widget that started a helper per event would be racing itself.
    if (lister.running)
      return
    if (!root.isPoller())
      return
    root.pending = []
    root.rowsSeen = 0
    root.listerStderr = ""
    root.listerStarted = false
    // Armed BEFORE the start, not from onStarted: a helper that is not there
    // to run never emits started, and a watchdog waiting for that signal would
    // wait forever -- which is exactly the silent nothing this is here to
    // prevent.
    listerWatchdog.restart()
    lister.running = true
  }

  // TERM the helper, then KILL if it is still there. os99-run is a process
  // group leader, so the signal reaches its whole tree, not just the script.
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

  // What a helper's exit code means. os99-minimize exits 1 for "nothing is
  // minimized", which is news rather than a fault; os99-run exits 125 when the
  // helper is not there to run and 124 when it ran out of time.
  function helperFault(code) {
    if (code === 0 || code === 1)
      return ""
    if (code === 125 || code === 126 || code === 127)
      return "OS 99 helpers not found"
    if (code === 124)
      return "OS 99 helper timed out"
    return "OS 99 helper failed (" + code + ")"
  }

  // Did this run get as far as a process? Quickshell reports a command it
  // cannot execute by putting `running` back to false without ever emitting
  // `started`, and there is no exit code for a program that never ran. That
  // combination is the missing-helper case, and it is caught here rather than
  // left to the watchdog, so the bar says so at once instead of after a wait.
  property bool listerStarted: false

  Process {
    id: lister
    running: true
    command: root.helperArgv(root.minimizer, ["list"], root.maxStdout)
    onStarted: root.listerStarted = true
    onRunningChanged: {
      if (!lister.running && !root.listerStarted) {
        listerWatchdog.stop()
        root.helperError = "OS 99 helpers not found"
      }
    }
    stdout: SplitParser {
      splitMarker: "\n"
      onRead: line => root.takeRow(line)
    }
    stderr: SplitParser {
      splitMarker: "\n"
      onRead: line => {
        if (root.listerStderr.length < 200)
          root.listerStderr += root.clean(line)
      }
    }
    onExited: (code, status) => {
      listerWatchdog.stop()
      root.entries = root.pending
      root.pending = []
      if (root.entries.length === 0)
        root.popupOpen = false
      root.helperError = root.helperFault(code)
      root.publish({ entries: root.entries, helperError: root.helperError })
    }
  }

  // The backstop behind os99-run's own deadline: if the runner itself never
  // returns, this is what ends it.
  Timer {
    id: listerWatchdog
    interval: (root.deadline + 3) * 1000
    onTriggered: {
      root.helperError = "OS 99 helper did not answer"
      root.abort(lister)
    }
  }

  property bool actorStarted: false

  Process {
    id: actor
    onStarted: root.actorStarted = true
    onRunningChanged: {
      if (!actor.running && !root.actorStarted) {
        actorWatchdog.stop()
        root.helperError = "OS 99 helpers not found"
      }
    }
    onExited: (code, status) => {
      actorWatchdog.stop()
      var fault = root.helperFault(code)
      if (fault !== "")
        root.helperError = fault
      refreshSoon.restart()
    }
  }

  Timer {
    id: actorWatchdog
    interval: (root.actDeadline + 3) * 1000
    onTriggered: {
      root.helperError = "OS 99 helper did not answer"
      root.abort(actor)
    }
  }

  function act(args) {
    if (actor.running)
      return
    actor.command = root.helperArgv(root.minimizer, args, 4096, root.actDeadline)
    root.actorStarted = false
    actorWatchdog.restart()
    actor.running = true
    root.popupOpen = false
  }

  function restore(address) {
    // The address comes from our own list and never reaches a shell -- it is an
    // argv element -- but it is checked anyway, because it originated with the
    // compositor rather than with us.
    if (!/^0x[0-9a-fA-F]+$/.test(address))
      return
    root.act(["restore", address])
  }

  // --------------------------------------------------------------- the setup

  // os99-install --status is read-only and cheap by construction: it draws no
  // art, copies nothing and writes nothing, so it is safe to ask on every
  // start and after every run.
  Process {
    id: statuser
    running: true
    command: root.helperArgv(root.installer, ["--status"], 16384)
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var s
        try {
          s = JSON.parse(String(text).substring(0, 16384))
        } catch (e) {
          return   // not answering is not the same as answering "not ready"
        }
        var rows = Array.isArray(s.steps) ? s.steps : []
        var out = []
        for (var i = 0; i < rows.length && out.length < 16; i++) {
          out.push({ id: String(rows[i].id || ""),
                     label: root.clean(rows[i].label),
                     done: rows[i].done === true,
                     fixable: rows[i].fixable === true,
                     hint: root.clean(rows[i].hint || "") })
        }
        root.setupSteps = out
        root.setupReady = s.ready === true
        root.publish({ setupSteps: root.setupSteps, setupReady: root.setupReady })
      }
    }
  }

  function checkSetup() {
    if (statuser.running || setup.running)
      return
    statuser.running = true
  }

  function runSetup() {
    if (setup.running || root.setupReady)
      return
    root.setupRunning = true
    root.setupLine = "starting"
    root.publish({ setupRunning: true, setupLine: root.setupLine })
    setup.command = root.helperArgv(root.installer, ["--auto"], 65536, root.setupDeadline)
    setupWatchdog.restart()
    setup.running = true
  }

  Process {
    id: setup
    stdout: SplitParser {
      splitMarker: "\n"
      onRead: line => {
        var t = root.clean(line).trim()
        if (t.length === 0)
          return
        root.setupLine = t
        root.publish({ setupLine: t })
      }
    }
    stderr: SplitParser {
      splitMarker: "\n"
      onRead: line => {
        var t = root.clean(line).trim()
        if (t.length > 0) {
          root.setupLine = t
          root.publish({ setupLine: t })
        }
      }
    }
    onExited: (code, status) => {
      setupWatchdog.stop()
      root.setupRunning = false
      root.publish({ setupRunning: false })
      root.checkSetup()
    }
  }

  Timer {
    id: setupWatchdog
    interval: (root.setupDeadline + 5) * 1000
    onTriggered: {
      root.setupLine = "setup did not finish in time"
      root.abort(setup)
    }
  }

  // Coalesces bursts. Minimising one window emits several Hyprland events, and
  // each would otherwise start its own helper.
  Timer {
    id: refreshSoon
    interval: 120
    onTriggered: root.refresh()
  }

  // Preferred trigger: Hyprland's own event stream, so the count is right the
  // moment a window moves. ignoreUnknownSignals keeps this from being an error
  // on a Quickshell build that does not expose rawEvent -- the timer below is
  // the safety net, and one of the two always works.
  Connections {
    target: Hyprland
    ignoreUnknownSignals: true
    function onRawEvent(event) { refreshSoon.restart() }
  }

  Timer {
    interval: 4000
    running: true
    repeat: true
    onTriggered: root.refresh()
  }

  // Setup does not change on its own, so this asks rarely -- often enough to
  // notice a theme switched away from OS 99 or a plugin that stopped loading,
  // never often enough to be a poll.
  Timer {
    interval: 60000
    running: true
    repeat: true
    onTriggered: { if (root.isPoller()) root.checkSetup() }
  }

  // ------------------------------------------------------------------- face

  // One Grid rather than a Row and a Column behind a Loader: two cells either
  // way, so the only thing that changes with the bar's orientation is how many
  // columns they sit in.
  Grid {
    id: content
    anchors.centerIn: parent
    columns: root.vertical ? 1 : 2
    spacing: Style.space(6)
    horizontalItemAlignment: Grid.AlignHCenter
    verticalItemAlignment: Grid.AlignVCenter

    // A window rolled down to its title bar: the same idea the collapse box
    // draws, at bar scale. Drawn rather than iconified so it follows the theme
    // colours and needs no icon font.
    Item {
      visible: root.helperError === ""
      width: Style.space(14)
      height: Style.space(11)

      Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: root.foreground
        border.width: 1
      }

      Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 1
        height: Style.space(3)
        color: root.foreground
      }
    }

    Text {
      // An unfinished install says its own name: an item labelled OS 99 in the
      // bar is something to click, where a bare count is not.
      text: root.helperError !== "" ? "!"
            : (!root.setupReady ? (root.setupRunning ? "OS 99 …" : "OS 99")
                                : String(root.count))
      color: root.foreground
      font.family: root.fontFamily
      font.pixelSize: Style.font.body
      textFormat: Text.PlainText
    }
  }

  MouseArea {
    anchors.fill: parent
    enabled: root.count > 0 || root.helperError !== "" || !root.setupReady
    onClicked: {
      if (root.setupReady)
        root.refresh()
      else
        root.checkSetup()
      root.popupOpen = !root.popupOpen
    }
  }

  PopupCard {
    id: popup
    anchorItem: root
    owner: root
    bar: root.bar
    open: root.popupOpen
    contentWidth: popup.fittedContentWidth(Style.space(360))
    contentHeight: popup.fittedContentHeight(column.implicitHeight)

    Column {
      id: column
      anchors.fill: parent
      spacing: Style.space(6)

      Text {
        text: root.helperError !== "" ? root.helperError
              : (!root.setupReady ? "Finish setting up OS 99" : "Minimized")
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.body
        font.bold: true
        textFormat: Text.PlainText
        width: column.width
        elide: Text.ElideRight
      }

      // What to do about a missing helper, named exactly. The CLI keeps working
      // whatever this widget is doing, so the way out is always available.
      Text {
        visible: root.helperError !== ""
        width: column.width
        text: "Expected: " + root.pluginRoot + "bin/\n"
              + "Reinstall the plugin, or run bin/os99-install from a checkout.\n"
              + "Parked windows are still on the os99-minimized workspace."
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.caption
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
      }

      // ------------------------------------------------------ setup checklist

      Repeater {
        model: root.helperError === "" && !root.setupReady ? root.setupSteps : []

        Text {
          required property var modelData
          width: column.width
          text: (modelData.done ? "✓  " : "·  ") + modelData.label
                + (!modelData.done && !modelData.fixable && modelData.hint.length > 0
                   ? "  —  " + modelData.hint : "")
          color: root.foreground
          opacity: modelData.done ? 0.6 : 1.0
          font.family: root.fontFamily
          font.pixelSize: Style.font.caption
          textFormat: Text.PlainText
          wrapMode: Text.Wrap
        }
      }

      Text {
        visible: root.helperError === "" && !root.setupReady && !root.setupRunning
        width: column.width
        text: "Setting up copies the theme in, draws the window art at this "
              + "display's scale, builds the frame plugin against your Hyprland "
              + "(a few minutes), and switches to OS 99 Platinum."
        color: root.foreground
        opacity: 0.7
        font.family: root.fontFamily
        font.pixelSize: Style.font.caption
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
      }

      Rectangle {
        visible: root.helperError === "" && !root.setupReady && !root.setupRunning
        width: column.width
        height: setupLabel.implicitHeight + Style.space(10)
        radius: 0
        color: setupHover.hovered ? Color.popups.hover : "transparent"
        border.color: root.foreground
        border.width: 1

        HoverHandler { id: setupHover }

        Text {
          id: setupLabel
          anchors.centerIn: parent
          text: "Set up OS 99"
          color: root.foreground
          font.family: root.fontFamily
          font.pixelSize: Style.font.body
          textFormat: Text.PlainText
        }

        MouseArea {
          anchors.fill: parent
          cursorShape: Qt.PointingHandCursor
          onClicked: root.runSetup()
        }
      }

      Text {
        visible: root.setupRunning
        width: column.width
        text: root.setupLine
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.caption
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
      }

      // ------------------------------------------------------ minimized list

      Repeater {
        model: root.entries

        Rectangle {
          id: rowItem
          required property var modelData

          width: column.width
          height: label.implicitHeight + Style.space(8)
          radius: 0
          color: hover.hovered ? Color.popups.hover : "transparent"

          HoverHandler { id: hover }

          Text {
            id: label
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Style.space(4)
            anchors.rightMargin: Style.space(4)
            // A window title is written by the window, so it is shown as
            // literal text and nothing else. Text.PlainText is not the default:
            // without it Qt sniffs the string and may decide a title is rich
            // text, in which case markup in it is obeyed and any resource it
            // names is fetched.
            textFormat: Text.PlainText
            text: rowItem.modelData.title && rowItem.modelData.title.length > 0
                  ? rowItem.modelData.title
                  : (rowItem.modelData.appClass || "window")
            color: root.foreground
            font.family: root.fontFamily
            font.pixelSize: Style.font.body
            elide: Text.ElideRight
          }

          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.restore(rowItem.modelData.address)
          }
        }
      }

      Text {
        visible: root.entries.length > 1
        text: "Restore all"
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.caption
        textFormat: Text.PlainText
        opacity: allHover.hovered ? 1.0 : 0.7

        HoverHandler { id: allHover }

        MouseArea {
          anchors.fill: parent
          cursorShape: Qt.PointingHandCursor
          onClicked: root.act(["restore-all"])
        }
      }
    }
  }
}
