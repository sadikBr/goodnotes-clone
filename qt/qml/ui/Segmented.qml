// A segmented choice (the web app's .seg): one of a few options.
import QtQuick
import QtQuick.Controls
import Inkleaf

Rectangle { antialiasing: true;
    id: root
    /// [{ value, label }]
    property var options: []
    property var value
    signal picked(var value)
    // The natural width (from the labels, not the stretched segments: no layout loop).
    implicitWidth: {
        let w = 6 + 2 * Math.max(0, options.length - 1)
        for (let i = 0; i < row.children.length; i++) w += row.children[i].implicitWidth || 0
        return w
    }
    implicitHeight: 34
    radius: 10
    color: Theme.surface3
    // Stretched wider than its content: the segments share the width.
    readonly property real segmentWidth: options.length ? (width - 6 - 2 * (options.length - 1)) / options.length : 0
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: root.options
            delegate: AbstractButton {
                id: b
                required property var modelData
                readonly property bool on: root.value === modelData.value
                implicitWidth: lbl.implicitWidth + 22
                width: Math.max(implicitWidth, root.segmentWidth)
                implicitHeight: 28
                hoverEnabled: true
                onClicked: root.picked(modelData.value)
                background: Item {
                    Shadow { anchors.fill: parent; radius: 7; level: b.on ? 1 : 0 }
                    Rectangle { antialiasing: true; anchors.fill: parent; radius: 7; color: b.on ? Theme.surface : (b.hovered ? Theme.alpha(Theme.surface, 0.5) : "transparent") }
                }
                contentItem: Label { id: lbl; text: b.modelData.label; color: Theme.text; font.pixelSize: 13; font.weight: b.on ? Font.DemiBold : Font.Normal; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
        }
    }
}
