// The first card on the shelf: what can be made (a notebook, a daftar,
// PDFs, a folder), each a tap away, with a line about it. It is the menu
// itself (the sidebar's New button opens the same choices as a menu).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Item {
    id: root
    /// What can be made (Library.createOptions).
    property var options: []
    /// "More kinds": the rest of the options, as a menu by `anchor`.
    signal moreRequested(Item anchor, var options)
    readonly property real artHeight: width * 4 / 3
    implicitHeight: artHeight

    Item {
        width: root.width
        height: root.artHeight
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 1 }
        Rectangle {
            anchors.fill: parent
            antialiasing: true
            radius: Theme.radiusLg
            color: Theme.surface
            border.color: Theme.border
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            anchors.topMargin: 12
            spacing: 2
            Label {
                text: "Start something new"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: Theme.muted
                Layout.leftMargin: 8
                Layout.bottomMargin: 4
            }
            Repeater {
                model: root.options.slice(0, root.options.length > 5 ? 4 : 5)
                delegate: AbstractButton {
                    id: choice
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.maximumHeight: 64
                    hoverEnabled: true
                    onClicked: modelData.action()
                    scale: pressed ? 0.98 : 1
                    Behavior on scale { NumberAnimation { duration: 90 } }
                    ToolTip.visible: hovered && !!modelData.key
                    ToolTip.delay: 700
                    ToolTip.text: modelData.key ? modelData.title + " (" + modelData.key + ")" : ""
                    background: Rectangle {
                        antialiasing: true
                        radius: Theme.radius
                        color: choice.pressed ? Theme.surface3 : choice.hovered ? Theme.surface2 : "transparent"
                        Behavior on color { ColorAnimation { duration: 120 } }
                    }
                    contentItem: RowLayout {
                        spacing: 10
                        Rectangle {
                            antialiasing: true
                            Layout.preferredWidth: 32; Layout.preferredHeight: 32; Layout.leftMargin: 6; radius: Theme.radius
                            color: Theme.alpha(choice.modelData.tint || Theme.accent, choice.hovered ? 0.2 : 0.13)
                            Icon { anchors.centerIn: parent; name: choice.modelData.icon; size: 17; color: choice.modelData.iconColor || choice.modelData.tint || Theme.accent }
                        }
                        ColumnLayout {
                            spacing: 1
                            Layout.fillWidth: true
                            Label { text: choice.modelData.title; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: choice.modelData.sub || ""; font.pixelSize: 11; color: Theme.muted; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                    }
                }
            }
            // The other kinds (a sketchbook, a lesson plan, a test, a register…).
            AbstractButton {
                id: moreKinds
                visible: root.options.length > 5
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.maximumHeight: 64
                hoverEnabled: true
                onClicked: root.moreRequested(moreKinds, root.options.slice(4))
                background: Rectangle { antialiasing: true; radius: Theme.radius; color: moreKinds.pressed ? Theme.surface3 : moreKinds.hovered ? Theme.surface2 : "transparent" }
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle {
                        antialiasing: true
                        Layout.preferredWidth: 32; Layout.preferredHeight: 32; Layout.leftMargin: 6; radius: Theme.radius
                        color: Theme.surface2
                        border.color: Theme.border
                        Icon { anchors.centerIn: parent; name: "more"; size: 17; color: Theme.muted }
                    }
                    ColumnLayout {
                        spacing: 1
                        Layout.fillWidth: true
                        Label { text: "More kinds"; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { text: root.options.slice(4).map(o => o.title.split(" · ")[0].toLowerCase()).join(", "); font.pixelSize: 11; color: Theme.muted; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                }
            }
        }
    }
}
