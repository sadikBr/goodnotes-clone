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
    /// In a tool group (.tool-group .tool-btn.small): active is raised on the surface.
    property bool grouped: false
    implicitWidth: label.length ? lbl.implicitWidth + 16 : 32
    implicitHeight: 30
    hoverEnabled: true
    ToolTip.visible: !!(!!(hovered && tip.length > 0)) && !tipDismissed
    // Tooltips go away once clicked (until the pointer leaves), as in the web app.
    property bool tipDismissed: false
    onPressedChanged: if (pressed) tipDismissed = true
    onHoveredChanged: if (!hovered) tipDismissed = false
    ToolTip.text: tip
    background: Item {
        Rectangle { visible: root.grouped && root.active; antialiasing: true; y: 1; width: parent.width; height: parent.height; radius: Theme.radius; color: Qt.rgba(0, 0, 0, Theme.dark ? 0.3 : 0.08) }
        Rectangle {
            antialiasing: true
            anchors.fill: parent
            radius: root.grouped ? 10 : 8
            color: root.grouped ? (root.active ? Theme.surface : root.hovered ? Theme.alpha(Theme.surface, 0.55) : "transparent")
                                : (root.active ? Theme.accentSoft : root.hovered ? Theme.surface2 : "transparent")
        }
    }
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
