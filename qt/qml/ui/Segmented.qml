// A segmented choice (the web app's .seg): one of a few options, in a
// rounded well with 3px padding; the chosen one raised on the surface colour.
import QtQuick
import QtQuick.Controls
import Inkleaf

Rectangle {
    id: root
    /// [{ value, label }]
    property var options: []
    property var value
    signal picked(var value)
    antialiasing: true
    // The natural width (from the labels, not the stretched segments: no layout loop).
    implicitWidth: {
        let w = 6 + 2 * Math.max(0, options.length - 1)
        for (let i = 0; i < row.children.length; i++) w += row.children[i].implicitWidth || 0
        return Math.ceil(w)
    }
    implicitHeight: 34
    radius: Theme.radius
    color: Theme.surface3
    // Stretched wider than its content: the extra is shared out (whole pixels).
    readonly property int extra: options.length ? Math.max(0, Math.floor((width - implicitWidth) / options.length)) : 0
    Row {
        id: row
        x: 3
        y: 3
        spacing: 2
        Repeater {
            model: root.options
            delegate: AbstractButton {
                id: b
                required property var modelData
                readonly property bool on: root.value === modelData.value
                implicitWidth: Math.ceil(lbl.implicitWidth) + 22
                width: implicitWidth + root.extra
                height: root.height - 6
                hoverEnabled: true
                onClicked: root.picked(modelData.value)
                background: Item {
                    // box-shadow: 0 1px 2px rgba(0, 0, 0, 0.15)
                    Rectangle { visible: b.on; antialiasing: true; x: 0; y: 1; width: parent.width; height: parent.height; radius: Theme.radiusSm; color: Qt.rgba(0, 0, 0, Theme.dark ? 0.35 : 0.12) }
                    Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusSm; color: b.on ? Theme.surface : (b.hovered ? Theme.alpha(Theme.surface, 0.5) : "transparent") }
                }
                contentItem: Label { id: lbl; text: b.modelData.label; color: Theme.text; font.pixelSize: 13; font.weight: b.on ? Font.DemiBold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
        }
    }
}
