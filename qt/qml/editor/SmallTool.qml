// A small toggle in the drawer and selection bar (src/styles.css .tool-btn.small / .icon-btn.small).
import QtQuick
import QtQuick.Controls
import Inkleaf

AbstractButton {
    id: root
    property string iconName
    property string label: ""
    property bool active: false
    property string tip
    property bool serif: false
    property bool bold: false
    implicitWidth: label.length ? lbl.implicitWidth + 16 : 32
    implicitHeight: 30
    hoverEnabled: true
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    background: Rectangle { antialiasing: true; radius: 8; color: root.active ? Theme.accentSoft : root.hovered ? Theme.surface2 : "transparent" }
    contentItem: Item {
        Icon { visible: root.iconName.length > 0; anchors.centerIn: parent; name: root.iconName; size: 17; color: root.active ? Theme.accent : Theme.text }
        Label {
            id: lbl
            visible: root.label.length > 0
            anchors.centerIn: parent
            text: root.label
            color: root.active ? Theme.accent : Theme.text
            font.weight: root.bold ? Font.Bold : Font.DemiBold
            font.italic: root.serif
            font.family: root.serif ? "Lora" : Theme.font
            font.pixelSize: 15
        }
    }
}
