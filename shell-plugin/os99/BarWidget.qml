import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import qs.Commons
import qs.Ui

// The OS 99 bar widget: the way back from the collapse box, and the menu-bar
// face. One widget because the marketplace lists one plugin per repository,
// and these two only ever ship together.
//
// MINIMIZED WINDOWS. OS 99's title bar minimises a window by parking it on the
// os99-minimized workspace (see bin/os99-minimize). A parked window has no bar
// on screen to click, so something that can see windows the user cannot has to
// offer the way back. That is this: a count in the bar, and a list you pick
// from. It is a convenience, not the mechanism. Everything here shells out to
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
// cannot end up on a different face from the title bars. The probe runs once
// at construction; hooks/theme-set.d/os99-gtk.sh restarts the shell whenever a
// theme switch crosses the OS 99 boundary.
BarWidget {
  id: root
  moduleName: "io.github.caariiboouu.os-99"

  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family

  // Rows of { address, appClass, title }, most recently minimized first.
  property var entries: []
  readonly property int count: entries.length
  property bool popupOpen: false

  // ~/.local/bin is where os99-install puts the scripts, and the shell is not
  // started from a login shell, so PATH cannot be assumed to include it.
  readonly property string cli: "PATH=\"$HOME/.local/bin:$PATH\"; os99-minimize"

  // Nothing minimized, nothing to say. The bar stays quiet until it has news.
  // The font probe still runs: a Process does not care that its widget is
  // collapsed to zero width.
  visible: count > 0 && !vertical
  implicitWidth: visible ? row.implicitWidth + Style.spacing.controlPaddingX * 2 : 0
  implicitHeight: barSize

  Behavior on implicitWidth {
    NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
  }

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

  function refresh() {
    lister.running = true
  }

  function parseList(text) {
    var out = []
    var lines = String(text).split("\n")
    for (var i = 0; i < lines.length; i++) {
      if (lines[i].length === 0) continue
      var f = lines[i].split("\t")
      // N, address, class, title -- anything shorter is not a row we wrote.
      if (f.length < 4) continue
      out.push({ address: f[1], appClass: f[2], title: f[3] })
    }
    root.entries = out
    if (out.length === 0) root.popupOpen = false
  }

  function restore(address) {
    // The address comes from our own list, but it is about to be pasted into a
    // shell command line, so it gets checked anyway. A title never is: it is
    // display-only and never reaches a shell.
    if (!/^0x[0-9a-fA-F]+$/.test(address)) return
    restorer.command = ["sh", "-c", root.cli + " restore " + address]
    restorer.running = true
    root.popupOpen = false
  }

  Process {
    id: lister
    running: true
    command: ["sh", "-c", root.cli + " list"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: root.parseList(text)
    }
  }

  Process {
    id: restorer
    onExited: refreshSoon.restart()
  }

  // Coalesces bursts. Minimising one window emits several Hyprland events, and
  // each would otherwise start its own hyprctl.
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

  Row {
    id: row
    anchors.centerIn: parent
    spacing: Style.space(6)

    // A window rolled down to its title bar: the same idea the collapse box
    // draws, at bar scale. Drawn rather than iconified so it follows the theme
    // colours and needs no icon font.
    Item {
      anchors.verticalCenter: parent.verticalCenter
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
      anchors.verticalCenter: parent.verticalCenter
      text: root.count
      color: root.foreground
      font.family: root.fontFamily
      font.pixelSize: Style.font.body
    }
  }

  MouseArea {
    anchors.fill: parent
    enabled: root.count > 0
    onClicked: {
      root.refresh()
      root.popupOpen = !root.popupOpen
    }
  }

  PopupCard {
    id: popup
    anchorItem: root
    owner: root
    bar: root.bar
    open: root.popupOpen
    contentWidth: popup.fittedContentWidth(Style.space(320))
    contentHeight: popup.fittedContentHeight(column.implicitHeight)

    Column {
      id: column
      anchors.fill: parent
      spacing: Style.space(6)

      Text {
        text: "Minimized"
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.body
        font.bold: true
      }

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
        opacity: allHover.hovered ? 1.0 : 0.7

        HoverHandler { id: allHover }

        MouseArea {
          anchors.fill: parent
          cursorShape: Qt.PointingHandCursor
          onClicked: {
            restorer.command = ["sh", "-c", root.cli + " restore-all"]
            restorer.running = true
            root.popupOpen = false
          }
        }
      }
    }
  }
}
