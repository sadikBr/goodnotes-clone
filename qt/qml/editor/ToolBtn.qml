// A tool in the header's trays: the active one lifts out, dipped in its ink;
// the others show their ink as a small drop (src/styles.css .tool-trays).
import QtQuick
import QtQuick.Controls
import Inkleaf

AbstractButton {
    id: root
    property string iconName
    property string ink: ""
    property bool active: false
    property string tip
    implicitWidth: 40
    implicitHeight: 38
    hoverEnabled: true
    readonly property color dip: ink.length ? ink : Theme.accent
    readonly property bool lightDip: {
        const c = Qt.color(dip)
        return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b > 0.6
    }
    transform: Translate { y: root.active ? -1 : 0; Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } } }
    scale: pressed ? 0.95 : 1
    Behavior on scale { NumberAnimation { duration: 120 } }
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    background: Item {
        Shadow { anchors.fill: parent; radius: 11; level: root.active ? 2 : 0; opacity: root.active ? 1 : 0 }
        Rectangle { antialiasing: true;
            anchors.fill: parent
            radius: 11
            color: root.active ? root.dip : root.hovered ? Theme.alpha(Theme.surface, 0.55) : "transparent"
            Behavior on color { ColorAnimation { duration: 180 } }
        }
        // The ink drop.
        Rectangle { antialiasing: true;
            visible: root.ink.length > 0 && !root.active
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2
            width: 6; height: 6; radius: 3
            color: root.ink
            border.width: 0.5
            border.color: Qt.rgba(0.5, 0.5, 0.5, 0.45)
        }
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: root.iconName
            size: 20
            color: root.active ? (root.lightDip ? "#1c1917" : (root.ink.length === 0 && Theme.dark ? "#0d1f17" : "#ffffff")) : root.hovered ? Theme.text : Theme.muted
        }
    }
}
