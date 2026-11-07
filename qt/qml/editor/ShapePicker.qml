// The shapes, as tiles in groups (lines, basic, polygons, symbols), seven to
// a row so it fits a phone too; the line at the bottom names the one under
// the pointer (or the chosen one). The shape tool shows "Auto shape" on top;
// changing a selected shape shows only the shapes it can become.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf
import "Shapes.js" as Shapes

Popup {
    id: root
    property string current: ""
    /// "auto" first (the shape tool), or not (changing a shape).
    property bool withAuto: true
    /// "all", "closed" (no lines) or "segments" (only lines).
    property string only: "all"
    signal picked(string id)

    readonly property int columns: 7
    readonly property int tile: 44
    readonly property var groups: Shapes.groups.filter(g => (only === "all" || (only === "segments") === !!g.segments) && (withAuto || !g.marks))
    property string hovered: ""
    readonly property string inkHex: String(Theme.text).replace("#", "")

    function show(anchor) {
        hovered = ""
        const p = anchor.mapToItem(parent, 0, 0)
        x = Math.max(8, Math.min(parent.width - width - 8, p.x + anchor.width / 2 - width / 2))
        // Under it, or above it when there's no room (a phone's bottom tools).
        const h = implicitHeight
        y = p.y + anchor.height + 10 + h <= parent.height - 8 ? p.y + anchor.height + 10 : Math.max(8, p.y - h - 10)
        open()
    }

    parent: Overlay.overlay
    padding: 12
    width: columns * tile + (columns - 1) * 4 + 2 * padding
    modal: true
    dim: false
    Overlay.modal: Item {}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 140; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
    }

    component Tile: AbstractButton {
        id: t
        required property var shape
        readonly property bool on: root.current === shape.id
        implicitWidth: root.tile
        implicitHeight: root.tile
        hoverEnabled: true
        onHoveredChanged: root.hovered = hovered ? shape.id : (root.hovered === shape.id ? "" : root.hovered)
        onClicked: { root.picked(shape.id); root.close() }
        scale: pressed ? 0.92 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        background: Rectangle {
            antialiasing: true
            radius: Theme.radius
            color: t.on ? Theme.accentSoft : t.hovered ? Theme.surface2 : "transparent"
            border.width: t.on ? 1.5 : 0
            border.color: Theme.accent
        }
        contentItem: Item {
            Image {
                anchors.centerIn: parent
                width: 28; height: 28
                sourceSize: Qt.size(56, 56)
                source: "image://ink/shape/" + t.shape.id + "/" + (t.on ? String(Theme.accent).replace("#", "") : root.inkHex)
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 6
        // Auto: draw it roughly, it's tidied up.
        AbstractButton {
            id: autoBtn
            visible: root.withAuto
            readonly property bool on: root.current === "auto"
            Layout.fillWidth: true
            implicitHeight: 40
            hoverEnabled: true
            onClicked: { root.picked("auto"); root.close() }
            background: Rectangle {
                antialiasing: true
                radius: Theme.radius
                color: autoBtn.on ? Theme.accentSoft : autoBtn.hovered ? Theme.surface2 : Theme.alpha(Theme.surface2, 0.5)
                border.width: autoBtn.on ? 1.5 : 0
                border.color: Theme.accent
            }
            contentItem: RowLayout {
                spacing: 10
                Item { Layout.preferredWidth: 2 }
                Icon { name: "sAuto"; size: 18; color: autoBtn.on ? Theme.accent : Theme.text }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Label { text: "Auto shape"; font.pixelSize: 13; font.weight: Font.DemiBold; color: autoBtn.on ? Theme.accent : Theme.text }
                    Label { text: "Draw it roughly: it's tidied up"; font.pixelSize: 11; color: Theme.muted; elide: Text.ElideRight; Layout.fillWidth: true }
                }
            }
        }
        Repeater {
            model: root.groups
            delegate: ColumnLayout {
                required property var modelData
                spacing: 2
                Layout.topMargin: 4
                Label { text: modelData.title.toUpperCase(); font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 0.5; color: Theme.muted; Layout.leftMargin: 4 }
                GridLayout {
                    columns: root.columns
                    columnSpacing: 4
                    rowSpacing: 4
                    Repeater { model: modelData.shapes; delegate: Tile { required property var modelData; shape: modelData } }
                }
            }
        }
        // Which one: under the pointer, else the chosen one.
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 2
            horizontalAlignment: Text.AlignHCenter
            text: root.hovered ? Shapes.label(root.hovered) : root.current ? Shapes.label(root.current) : ""
            font.pixelSize: 12
            color: Theme.muted
        }
    }
}
