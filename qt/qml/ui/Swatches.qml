// Colour swatches: rounded-square chips, the chosen one ringed.
import QtQuick
import QtQuick.Controls
import Inkleaf

Flow {
    id: root
    /// Colour strings, or [{ color, label }].
    property var colors: []
    property string value
    property int size: 28
    signal picked(string color)
    spacing: 8
    Repeater {
        model: root.colors
        delegate: AbstractButton {
            id: sw
            required property var modelData
            readonly property string c: typeof modelData === "string" ? modelData : modelData.color
            readonly property bool on: String(root.value).toLowerCase() === c.toLowerCase()
            width: root.size; height: root.size
            hoverEnabled: true
            onClicked: root.picked(c)
            ToolTip.visible: !!(hovered && typeof modelData !== "string") && !tipDismissed
            // Tooltips go away once clicked (until the pointer leaves), as in the web app.
            property bool tipDismissed: false
            onPressedChanged: if (pressed) tipDismissed = true
            onHoveredChanged: if (!hovered) tipDismissed = false
            ToolTip.text: typeof modelData === "string" ? "" : modelData.label
            ToolTip.delay: 400
            background: Item {
                Rectangle { antialiasing: true; anchors.fill: parent; anchors.margins: -3; radius: Theme.swatchRadius(width); color: "transparent"; border.width: 2; border.color: Theme.accent; visible: sw.on }
                Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.swatchRadius(width); color: sw.c; border.width: 1; border.color: Qt.rgba(0, 0, 0, 0.14) }
            }
            scale: hovered ? 1.08 : 1
            Behavior on scale { NumberAnimation { duration: 100 } }
        }
    }
}
