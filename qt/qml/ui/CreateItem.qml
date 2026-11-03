// One thing to create: a tinted icon tile, a title and a line under it.
// `card` is the bordered version shown in an empty library.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

AbstractButton {
    id: root
    property var option: ({})
    property bool card: false
    readonly property color tint: option.tint || Theme.accent
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: card ? 72 : 58
    Keys.onReturnPressed: clicked()
    transform: Translate { y: root.card && root.hovered ? -2 : 0; Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } } }
    background: Item {
        Shadow { anchors.fill: parent; radius: 12; level: root.card && root.hovered ? 2 : 0 }
        Rectangle { antialiasing: true;
            anchors.fill: parent
            radius: 12
            color: root.card ? Theme.surface : (root.hovered || root.visualFocus ? Theme.surface2 : "transparent")
            border.width: root.card ? 1 : 0
            border.color: root.hovered ? Theme.accent : Theme.border
        }
    }
    contentItem: RowLayout {
        spacing: 12
        Item { Layout.preferredWidth: root.card ? 4 : 0 }
        Rectangle { antialiasing: true;
            Layout.preferredWidth: 38; Layout.preferredHeight: 38; radius: 11
            color: root.option.tint ? Theme.alpha(root.tint, 0.14) : Theme.accentSoft
            Icon { anchors.centerIn: parent; name: root.option.icon || "plus"; size: 19; color: root.option.iconColor || root.tint }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label { text: root.option.title || ""; font.pixelSize: 14; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: root.option.sub || ""; font.pixelSize: 12; color: Theme.muted; elide: Text.ElideRight; Layout.fillWidth: true }
        }
        Kbd { visible: !!(!root.card && !!root.option.key); text: root.option.key || ""; Layout.rightMargin: 6 }
    }
}
