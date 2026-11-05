// One of a few modes as text chips in a tool group (the eraser's and laser's
// modes: .tool-group.options with .tool-btn.small.text-chip buttons).
import QtQuick
import QtQuick.Controls
import Inkleaf

ToolGroup {
    id: root
    /// [{ value, label, tip }]
    property var options: []
    property var value
    signal picked(var value)
    Repeater {
        model: root.options
        delegate: AbstractButton {
            id: b
            required property var modelData
            readonly property bool on: root.value === modelData.value
            implicitWidth: Math.ceil(lbl.implicitWidth) + 24
            implicitHeight: 30
            hoverEnabled: true
            scale: pressed ? 0.94 : 1
            Behavior on scale { NumberAnimation { duration: 120 } }
            onClicked: root.picked(modelData.value)
            ToolTip.visible: !!(hovered && modelData.tip) && !tipDismissed
            ToolTip.text: modelData.tip || ""
            property bool tipDismissed: false
            onPressedChanged: if (pressed) tipDismissed = true
            onHoveredChanged: if (!hovered) tipDismissed = false
            background: Item {
                // .tool-btn.active: the surface colour, a light shadow.
                Rectangle { visible: b.on; antialiasing: true; y: 1; width: parent.width; height: parent.height; radius: root.corner - 4; color: Qt.rgba(0, 0, 0, Theme.dark ? 0.3 : 0.08) }
                Rectangle { antialiasing: true; anchors.fill: parent; radius: root.corner - 4; color: b.on ? Theme.surface : b.hovered ? Theme.alpha(Theme.surface, 0.55) : "transparent" }
            }
            contentItem: Label {
                id: lbl
                text: b.modelData.label
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: b.on ? Theme.accent : b.hovered ? Theme.text : Theme.muted
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
