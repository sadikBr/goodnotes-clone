// The web app's .btn: default, primary, ghost and danger, with an optional icon.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

AbstractButton {
    id: root
    /// "default" | "primary" | "ghost" | "danger"
    property string kind: "default"
    property string iconName: ""
    property int iconSize: 18
    implicitHeight: 36
    implicitWidth: row.implicitWidth + 28
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.5

    readonly property color fg: kind === "primary" ? Theme.accentContrast : kind === "danger" ? Theme.danger : Theme.text
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radius; level: root.kind === "ghost" ? 0 : 1; visible: root.kind !== "ghost" }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: root.kind === "primary" ? (root.hovered ? Theme.accentHover : Theme.accent)
                 : root.kind === "ghost" ? (root.hovered ? Theme.surface2 : "transparent")
                 : root.kind === "danger" ? (root.hovered ? Theme.alpha(Theme.danger, 0.08) : Theme.surface)
                 : (root.hovered ? Theme.surface2 : Theme.surface)
            border.width: root.kind === "primary" || root.kind === "ghost" ? 0 : 1
            border.color: root.kind === "danger" ? Theme.alpha(Theme.danger, 0.4) : (root.hovered ? Theme.borderStrong : Theme.border)
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        Rectangle {
            anchors.fill: parent; anchors.margins: -3; radius: Theme.radius + 3
            color: "transparent"; border.width: 2; border.color: Theme.alpha(Theme.accent, 0.5)
            visible: root.visualFocus
        }
    }
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 7
            Icon { visible: root.iconName.length > 0; name: root.iconName; size: root.iconSize; color: root.fg }
            Label { visible: root.text.length > 0; text: root.text; color: root.fg; font.weight: root.kind === "primary" ? Font.DemiBold : Font.Medium; font.pixelSize: 14 }
        }
    }
    scale: pressed ? 0.98 : 1
    Behavior on scale { NumberAnimation { duration: 90 } }
}
