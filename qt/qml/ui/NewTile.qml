// The dashed "New…" tile at the start of the shelf.
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import Inkleaf

AbstractButton {
    id: root
    hoverEnabled: true
    readonly property real artHeight: width * 4 / 3
    implicitHeight: artHeight
    readonly property color line: hovered ? Theme.accent : Theme.borderStrong
    Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
    transform: Translate { y: root.hovered ? -3 : 0; Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } } }

    background: Item {
        width: root.width; height: root.artHeight
        Shape {
            anchors.fill: parent
            readonly property real w: width - 2
            readonly property real h: height - 2
            ShapePath {
                strokeColor: root.line
                strokeWidth: 2
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 2.5]
                fillColor: root.hovered ? Theme.accentSoft : "transparent"
                // A box with radii 6/12/12/6, inset by half the line.
                PathSvg {
                    readonly property real w: root.width - 2
                    readonly property real h: root.artHeight - 2
                    path: "M 7 1 L " + (w - 11) + " 1 A 12 12 0 0 1 " + (w + 1) + " 13 L " + (w + 1) + " " + (h - 11) + " A 12 12 0 0 1 " + (w - 11) + " " + (h + 1) + " L 7 " + (h + 1) + " A 6 6 0 0 1 1 " + (h - 5) + " L 1 7 A 6 6 0 0 1 7 1 Z"
                }
            }
        }
    }
    contentItem: Item {
        Column {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: (root.artHeight - root.height) / 2
            spacing: 12
            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 48; height: 48
                Shadow { anchors.fill: parent; radius: 24; level: 1 }
                Rectangle { antialiasing: true; anchors.fill: parent; radius: 24; color: Theme.surface }
                Icon { anchors.centerIn: parent; name: "plus"; color: Theme.accent; size: 22 }
            }
            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "New…"; font.weight: Font.DemiBold; color: root.hovered ? Theme.accent : Theme.muted }
        }
    }
}
