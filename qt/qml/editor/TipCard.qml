// A tool's hidden gestures, told once: the first time you pick a tool, a
// small card at the bottom of the page says what it can do beyond the
// obvious. "Got it" puts it away for good (Shortcuts and gestures can show
// them all again).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Item {
    id: root
    required property var prefs
    /// Opens Shortcuts and gestures.
    signal moreRequested()

    readonly property var tips: ({
        pen: { icon: "pen", title: "Your pen does more", text: "Draw a shape and <b>hold still</b>: it becomes a clean line, circle or box. <b>Scribble</b> over ink to erase it. <b>Hold still on writing</b> to select it, then drag it. Tap the pen again for other pens, colours, sizes and dashed lines." },
        brush: { icon: "brush", title: "Brushes", text: "Ink brush, flat brush, airbrush, chalk, crayon, watercolour, marker and neon: <b>tap the brush again</b> to choose. Each one keeps its own colour and size." },
        highlighter: { icon: "highlighter", title: "Highlighting", text: "It goes behind your writing. <b>Hold still</b> at the end of a swipe to make it perfectly straight." },
        eraser: { icon: "eraser", title: "Erasing", text: "Tap the eraser again to erase <b>parts of strokes</b> or <b>whole strokes</b>. With a pen you can also just scribble over ink to erase it." },
        lasso: { icon: "lasso", title: "Selecting", text: "Draw around things to select them. You don't always need the lasso: with any pen, <b>tap</b> a text box, picture or sticky note, <b>hold still</b> on writing, or <b>right-click</b> anything." },
        shape: { icon: "shape", title: "Shapes", text: "Draw it roughly and it's tidied up. <b>Tap the shape tool again</b> for a kind of shape, fill, and dashed or dotted lines." },
        laser: { icon: "laser", title: "Laser pointer", text: "Point at things while you explain; the trail fades by itself. Press <b>F5</b> to present the page on another screen." }
    })
    property string showing: ""
    readonly property var tip: tips[showing] || null
    visible: opacity > 0
    opacity: tip ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 180 } }

    function seen() { try { const v = JSON.parse(prefs.store.tipsSeen); return Array.isArray(v) ? v : [] } catch (e) { return [] } }
    /// The tool was picked: its tip, if it has one you haven't seen.
    function offer(tool) {
        if (!tips[tool] || seen().indexOf(tool) >= 0) { if (showing && showing !== tool) dismiss(false); return }
        later.tool = tool
        later.restart()
    }
    Timer { id: later; property string tool: ""; interval: 450; onTriggered: root.showing = tool }
    function dismiss(remember) {
        if (remember && showing) prefs.store.tipsSeen = JSON.stringify(seen().concat([showing]))
        showing = ""
    }
    function resetAll() { prefs.store.tipsSeen = "[]" }

    width: Math.min(parent ? parent.width - 32 : 420, 440)
    height: card.implicitHeight
    Rectangle {
        id: card
        anchors.fill: parent
        implicitHeight: col.implicitHeight + 28
        radius: Theme.radiusLg
        antialiasing: true
        color: Theme.surface
        border.color: Theme.border
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3; z: -1 }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
        RowLayout {
            id: col
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 14
            spacing: 12
            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: 34; Layout.preferredHeight: 34
                radius: Theme.radius
                antialiasing: true
                color: Theme.accentSoft
                Icon { anchors.centerIn: parent; name: root.tip ? root.tip.icon : "pen"; size: 17; color: Theme.accent }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: root.tip ? root.tip.title : ""; font.pixelSize: 14; font.weight: Font.DemiBold; color: Theme.text }
                Label {
                    Layout.fillWidth: true
                    text: root.tip ? root.tip.text : ""
                    textFormat: Text.StyledText
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                    color: Theme.muted
                    lineHeight: 1.2
                }
                RowLayout {
                    Layout.topMargin: 6
                    spacing: 8
                    UiButton { text: "Got it"; kind: "primary"; implicitHeight: 30; onClicked: root.dismiss(true) }
                    UiButton { text: "All gestures"; kind: "ghost"; implicitHeight: 30; onClicked: { root.dismiss(true); root.moreRequested() } }
                }
            }
            IconButton { Layout.alignment: Qt.AlignTop; iconName: "close"; tip: "Not now"; implicitWidth: 28; implicitHeight: 28; iconSize: 14; onClicked: root.dismiss(false) }
        }
    }
}
