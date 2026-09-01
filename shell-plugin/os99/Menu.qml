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

  readonly property int itemH: Style.space(26)
  readonly property int checkW: Style.space(20)
  readonly property int padX: Style.space(12)
  readonly property int menuW: Style.space(170)
  readonly property int shadowOff: Style.space(4)

  function openAt(payloadJson) {
    var p
    try { p = JSON.parse(payloadJson || "{}") } catch (e) { p = {} }
    root.windowAddr = String(p.window || "")
    root.menuX = Number(p.x) || 0
    root.menuY = Number(p.y) || 0
    lister.running = true
  }

  function close() { root.opened = false }

  function toggle(id) {
    // The address is our own list's id, but it reaches a shell command line,
    // so hold it to the names the art actually has.
    if (!/^[a-z]+$/.test(id)) return
    toggler.command = ["sh", "-c", "PATH=\"$HOME/.local/bin:$PATH\"; os99-buttons toggle " + id]
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

  Process {
    id: lister
    command: ["sh", "-c", "PATH=\"$HOME/.local/bin:$PATH\"; os99-buttons list --json"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        try {
          root.entries = JSON.parse(String(text)).buttons || []
          root.opened = root.entries.length > 0
        } catch (e) {
          root.entries = []
        }
      }
    }
  }

  Process { id: toggler }

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
              }

              Text {
                x: root.checkW
                width: parent.width - root.checkW - root.padX
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.label
                color: rowHover.hovered ? Color.popups.background : Color.popups.text
                font.family: Style.fontFamily
                font.pixelSize: Style.font.body
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
