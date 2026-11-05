// The first card on the shelf: one thing to start, the one you chose (a
// notebook at first). Tap it to make one; its ⇄ button (or Settings →
// Appearance) changes what it makes. The sidebar's New button has them all.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Item {
    id: root
    /// What can be made (Library.createOptions).
    property var options: []
    /// The id of the one this card makes.
    property string choice: "notebook"
    /// Change it: the choices, as a menu by `anchor`.
    signal chooseRequested(Item anchor)
    readonly property var option: options.find(o => o.id === choice) || options[0] || ({ title: "", icon: "plus" })
    readonly property color tint: option.tint || Theme.accent
    readonly property real artHeight: width * 4 / 3
    implicitHeight: artHeight

    AbstractButton {
        id: tile
        width: root.width
        height: root.artHeight
        hoverEnabled: true
        onClicked: if (root.option.action) root.option.action()
        scale: pressed ? 0.98 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        ToolTip.visible: hovered && !!root.option.key && !swap.hovered
        ToolTip.delay: 700
        ToolTip.text: root.option.key ? root.option.title + " (" + root.option.key + ")" : ""
        background: Item {
            DashedBox {
                anchors.fill: parent
                radius: Theme.radiusLg
                lineWidth: 1.5
                color: tile.hovered ? root.tint : Theme.borderStrong
                fill: tile.hovered ? Theme.alpha(root.tint, 0.07) : Theme.alpha(Theme.surface, 0.5)
                Behavior on color { ColorAnimation { duration: 140 } }
            }
        }
        contentItem: Item {
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 32
                spacing: 12
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 64; Layout.preferredHeight: 64
                    radius: Theme.radiusXl
                    antialiasing: true
                    color: Theme.alpha(root.tint, tile.hovered ? 0.22 : 0.14)
                    scale: tile.hovered ? 1.06 : 1
                    Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                    Icon { anchors.centerIn: parent; name: root.option.icon; size: 30; color: root.option.iconColor || root.tint }
                    // A little + on its corner: "a new one".
                    Rectangle {
                        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: -4
                        width: 24; height: 24; radius: Theme.radius
                        antialiasing: true
                        color: root.tint
                        border.width: 2
                        border.color: Theme.surface
                        Icon { anchors.centerIn: parent; name: "plus"; size: 14; color: "#ffffff" }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: root.option.id === "pdf" || root.option.id === "folder" ? root.option.title : "New " + root.option.title.toLowerCase()
                    elide: Text.ElideRight
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: root.option.sub || ""
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    color: Theme.muted
                }
            }
        }
    }
    // What this card makes.
    AbstractButton {
        id: swap
        anchors.right: tile.right; anchors.top: tile.top; anchors.margins: 8
        width: 30; height: 30
        hoverEnabled: true
        opacity: tile.hovered || hovered || pressed ? 1 : 0.55
        Behavior on opacity { NumberAnimation { duration: 140 } }
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.text: "Choose what this card makes"
        onClicked: root.chooseRequested(swap)
        background: Rectangle { antialiasing: true; radius: Theme.radius; color: swap.hovered ? Theme.surface2 : "transparent"; border.color: swap.hovered ? Theme.border : "transparent" }
        contentItem: Item { Icon { anchors.centerIn: parent; name: "swap"; size: 15; color: swap.hovered ? Theme.text : Theme.muted } }
    }
}
