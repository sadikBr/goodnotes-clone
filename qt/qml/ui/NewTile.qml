// The "New" card at the start of the shelf: what can be made, each a tap
// away (a notebook, a daftar, PDFs, a folder); the rest of the card opens
// the same as a menu.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

AbstractButton {
    id: root
    /// What can be made (Library.createOptions).
    property var options: []
    hoverEnabled: true
    readonly property real artHeight: width * 4 / 3
    implicitHeight: artHeight
    transform: Translate { y: root.hovered ? -3 : 0; Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } } }

    background: Item {
        width: root.width; height: root.artHeight
        Shadow { anchors.fill: parent; radius: 12; level: root.hovered ? 2 : 1 }
        Rectangle {
            anchors.fill: parent
            antialiasing: true
            radius: 12
            color: Theme.surface
            border.color: root.hovered ? Theme.accent : Theme.border
            Behavior on border.color { ColorAnimation { duration: 150 } }
        }
    }
    contentItem: Item {
        ColumnLayout {
            x: 12
            y: 14
            width: root.width - 24
            height: root.artHeight - 26
            spacing: 4
            RowLayout {
                spacing: 10
                Layout.bottomMargin: 6
                Rectangle {
                    antialiasing: true
                    Layout.preferredWidth: 34; Layout.preferredHeight: 34; radius: 17
                    color: root.hovered ? Theme.accent : Theme.alpha(Theme.accent, 0.14)
                    Behavior on color { ColorAnimation { duration: 150 } }
                    Icon { anchors.centerIn: parent; name: "plus"; size: 18; color: root.hovered ? Theme.accentContrast : Theme.accent }
                }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Label { text: "New"; font.pixelSize: 15; font.weight: Font.Bold; color: Theme.text }
                    Label { text: "Pick one to start"; font.pixelSize: 11; color: Theme.muted; elide: Text.ElideRight; Layout.fillWidth: true }
                }
            }
            Repeater {
                model: root.options.slice(0, 4)
                delegate: AbstractButton {
                    id: quick
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 40
                    hoverEnabled: true
                    onClicked: modelData.action()
                    background: Rectangle { antialiasing: true; radius: 9; color: quick.pressed ? Theme.surface3 : quick.hovered ? Theme.surface2 : "transparent" }
                    contentItem: RowLayout {
                        spacing: 9
                        Rectangle {
                            antialiasing: true
                            Layout.preferredWidth: 28; Layout.preferredHeight: 28; Layout.leftMargin: 4; radius: 8
                            color: Theme.alpha(quick.modelData.tint || Theme.accent, 0.13)
                            Icon { anchors.centerIn: parent; name: quick.modelData.icon; size: 16; color: quick.modelData.iconColor || quick.modelData.tint || Theme.accent }
                        }
                        Label { text: quick.modelData.title; font.pixelSize: 13; font.weight: Font.Medium; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}
