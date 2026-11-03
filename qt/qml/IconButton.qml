// A round-cornered icon button with a tooltip (the web app's .icon-btn / tool buttons).
import QtQuick
import QtQuick.Controls

AbstractButton {
    id: root
    property string iconName
    property string tip
    property bool active: false
    property color iconColor: active ? Theme.accentContrast : (enabled ? Theme.text : Theme.muted)
    property color activeColor: Theme.text
    property int iconSize: 20
    implicitWidth: 40
    implicitHeight: 38
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4

    background: Rectangle { antialiasing: true;
        radius: 10
        color: root.active ? root.activeColor : (root.hovered ? Theme.surface3 : "transparent")
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: root.iconName
            color: root.iconColor
            size: root.iconSize
        }
    }
    ToolTip.visible: !!(!!(hovered && tip.length > 0)) && !tipDismissed
    // Tooltips go away once clicked (until the pointer leaves), as in the web app.
    property bool tipDismissed: false
    onPressedChanged: if (pressed) tipDismissed = true
    onHoveredChanged: if (!hovered) tipDismissed = false
    ToolTip.delay: 500
    ToolTip.text: tip
}
