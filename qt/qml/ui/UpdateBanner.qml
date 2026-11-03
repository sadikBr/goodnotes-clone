// "Inkleaf x.y is available" at the bottom left (src/updates.ts showBanner):
// what's new, Later, and Download (opens the download in the browser).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Item {
    id: root
    required property Services services
    readonly property var update: services.update
    property bool notesOpen: false
    parent: Overlay.overlay
    z: 60
    visible: !!update
    x: 16
    y: parent ? parent.height - height - 16 : 0
    width: Math.min(460, (parent ? parent.width : 500) - 32)
    height: box.implicitHeight + 28
    onVisibleChanged: if (visible) pop.restart()
    NumberAnimation { id: pop; target: root; property: "opacity"; from: 0; to: 1; duration: 180 }

    Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
    Rectangle { anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    RowLayout {
        id: box
        x: 16; y: 14
        width: parent.width - 32
        spacing: 12
        Icon { name: "download"; size: 20; color: Theme.accent; Layout.alignment: Qt.AlignTop }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label { text: root.update ? "Inkleaf " + root.update.version + " is available" : ""; font.pixelSize: 14; font.weight: Font.Bold; color: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label {
                visible: !!root.update && root.update.notes.length > 0
                text: (root.notesOpen ? "▾ " : "▸ ") + "What's new"
                font.pixelSize: 12
                color: Theme.muted
                TapHandler { onTapped: root.notesOpen = !root.notesOpen }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
            ScrollView {
                visible: root.notesOpen && !!root.update
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(160, notes.implicitHeight)
                Label { id: notes; width: parent.width; text: root.update ? root.update.notes : ""; wrapMode: Text.Wrap; font.pixelSize: 12; color: Theme.text }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 6
            UiButton { text: "Later"; kind: "ghost"; onClicked: root.services.updateLater() }
            UiButton { text: "Download"; kind: "primary"; onClicked: root.services.downloadUpdate() }
        }
    }
}
