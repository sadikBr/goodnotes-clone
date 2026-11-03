// The web app's on/off switch.
import QtQuick
import QtQuick.Controls
import Inkleaf

AbstractButton {
    id: root
    opacity: enabled ? 1 : 0.4
    checkable: true
    implicitWidth: 34
    implicitHeight: 20
    hoverEnabled: true
    background: Rectangle {
        radius: height / 2
        color: root.checked ? Theme.accent : Theme.surface3
        Behavior on color { ColorAnimation { duration: 180 } }
        Rectangle {
            width: 16; height: 16; radius: 8
            y: 2
            x: root.checked ? parent.width - width - 2 : 2
            color: "#ffffff"
            border.color: Qt.rgba(0, 0, 0, 0.12)
            Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item {}
}
