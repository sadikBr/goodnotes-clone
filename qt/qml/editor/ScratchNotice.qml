// "Scratched out · Undo", for a moment after a pen scribble erased ink, so
// it's clear what happened and one tap brings it back.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Rectangle {
    id: root
    /// The page view it happened on (Undo acts there).
    property var view: null
    property bool shown: false
    function show(v) {
        view = v
        shown = true
        hide.restart()
    }
    parent: Overlay.overlay
    z: 900
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? parent.height - height - (shown ? 84 : 60) : 0
    opacity: shown ? 1 : 0
    visible: opacity > 0
    width: row.implicitWidth + 20
    height: 44
    radius: 22
    color: "#22231f"
    antialiasing: true
    Behavior on opacity { NumberAnimation { duration: 180 } }
    Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Timer { id: hide; interval: 4000; onTriggered: root.shown = false }
    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 10
        Icon { name: "eraser"; size: 17; color: "#f4f3ef"; Layout.leftMargin: 6 }
        Label { text: "Scratched out"; color: "#f4f3ef"; font.weight: Font.Medium }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; color: "#55564f" }
        AbstractButton {
            id: undoBtn
            implicitHeight: 32
            implicitWidth: undoLabel.implicitWidth + 24
            hoverEnabled: true
            onClicked: {
                if (root.view) root.view.undo()
                root.shown = false
            }
            background: Rectangle { antialiasing: true; radius: 16; color: undoBtn.hovered || undoBtn.pressed ? "#3a3b35" : "transparent" }
            contentItem: Label { id: undoLabel; text: "Undo"; color: "#8fe0b8"; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }
    }
}
