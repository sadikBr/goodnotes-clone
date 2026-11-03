// Keyboard shortcuts (src/editor/shortcuts.ts).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 560)
    height: Math.min(parent.height - 32, implicitHeight)
    modal: true
    padding: 24
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(20 / 255, 18 / 255, 14 / 255, 0.45) }
    readonly property var groups: [
        { title: "Tools", items: [["P", "Pen"], ["H", "Highlighter"], ["E", "Eraser"], ["L", "Lasso select"], ["S", "Shape"], ["T", "Text box"], ["N", "Sticky note"], ["K", "Laser pointer"], ["R", "Ruler"]] },
        { title: "Editing", items: [["Ctrl+Z", "Undo"], ["Ctrl+Shift+Z", "Redo"], ["Ctrl+C / Ctrl+V", "Copy / paste"], ["Ctrl+D", "Duplicate"], ["Ctrl+A", "Select all"], ["Delete", "Delete selection"], ["Esc", "Deselect"]] },
        { title: "View", items: [["Ctrl+= / Ctrl+−", "Zoom in / out"], ["Ctrl+0", "Fit width"], ["Page Up / Down", "Previous / next page"], ["[", "Show or hide pages"], ["F", "Focus mode"], ["?", "This list"]] }
    ]
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
        Rectangle { anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }
    contentItem: ColumnLayout {
        spacing: 14
        Label { text: "Keyboard shortcuts"; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text }
        GridLayout {
            columns: 2
            columnSpacing: 28
            rowSpacing: 18
            Repeater {
                model: root.groups
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.alignment: Qt.AlignTop
                    spacing: 6
                    Label { text: modelData.title.toUpperCase(); font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.7; color: Theme.muted }
                    Repeater {
                        model: modelData.items
                        delegate: RowLayout {
                            required property var modelData
                            spacing: 10
                            Rectangle {
                                Layout.preferredHeight: 22
                                Layout.preferredWidth: keyLabel.implicitWidth + 12
                                radius: 6
                                color: Theme.surface2
                                border.color: Theme.border
                                Label { id: keyLabel; anchors.centerIn: parent; text: modelData[0]; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.text }
                            }
                            Label { text: modelData[1]; color: Theme.text }
                        }
                    }
                }
            }
        }
        UiButton { Layout.alignment: Qt.AlignRight; text: "Done"; kind: "primary"; onClicked: root.close() }
    }
}
