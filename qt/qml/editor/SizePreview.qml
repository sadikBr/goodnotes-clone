// What a size looks like for a tool: the pen's own stroke, a highlighter
// swipe, a line for shapes, the eraser's circle, or letters for text.
import QtQuick
import QtQuick.Controls
import Inkleaf

Item {
    id: root
    property string tool: "pen"
    property string penStyle: "fountain"
    property string hex: "111827"
    property string dark: "0"
    property string dash: "dash"
    property real gap: 1
    property real value: 3
    /// 0..1: where the size sits among the tool's sizes (or in its range).
    property real relative: 0.5
    property bool active: false
    /// Larger (the panel that sets your own size).
    property bool big: false

    readonly property bool line: tool === "pen" || tool === "brush" || tool === "highlighter" || tool === "shape"
    /// (For the pen and the brush, `penStyle` is the kind in use.)
    readonly property string style: tool === "pen" || tool === "brush" ? penStyle : tool === "highlighter" ? "marker" : "ballpoint"
    readonly property color ink: "#" + hex
    /// The ink, unless it would vanish on the panel (dark ink in dark mode, light in light).
    readonly property color shownInk: {
        const c = Qt.color(ink), l = 0.299 * c.r + 0.587 * c.g + 0.114 * c.b
        return (Theme.dark ? l < 0.35 : l > 0.85) ? Theme.text : ink
    }
    readonly property real k: Math.max(0.05, Math.min(1, relative))

    implicitWidth: big ? 112 : 34
    implicitHeight: big ? 44 : 16

    Image {
        visible: root.line
        anchors.centerIn: parent
        width: root.big ? 112 : 34
        height: root.big ? 44 : 16
        sourceSize: Qt.size(width * 2, height * 2)
        cache: false
        source: root.line ? "image://ink/nib/" + root.style + "/" + root.hex + "/" + root.value + "/" + root.dark + "/" + root.dash + "/" + root.gap + "/" + root.k : ""
    }
    // The eraser: its circle, with a soft fill.
    Rectangle {
        visible: root.tool === "eraser"
        anchors.centerIn: parent
        readonly property real d: root.big ? 8 + 32 * root.k : 5 + 11 * root.k
        width: d; height: d; radius: d / 2
        antialiasing: true
        color: Theme.alpha(root.active ? Theme.accent : Theme.text, root.active ? 0.18 : 0.08)
        border.width: 1.5
        border.color: root.active ? Theme.accent : Theme.muted
    }
    // Text and sticky notes: letters at the size.
    Label {
        visible: root.tool === "text" || root.tool === "sticky"
        anchors.centerIn: parent
        text: "Aa"
        font.pixelSize: Math.round(root.big ? 12 + 26 * root.k : 9 + 8 * root.k)
        font.weight: Font.DemiBold
        color: root.tool === "text" ? root.shownInk : (root.active ? Theme.text : Theme.muted)
    }
}
