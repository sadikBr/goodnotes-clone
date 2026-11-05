// A short message at the bottom of the window, with an icon (a check, or a
// warning for an error, unless it says otherwise) and maybe an action.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Rectangle { antialiasing: true;
    id: root
    property bool error: false
    function show(message, isError, options) {
        const o = options || {}
        label.text = message
        error = !!isError
        iconName = o.icon || (error ? "alert" : "check")
        actionLabel = o.actionLabel || ""
        action = o.action || null
        shown = true
        timer.restart()
    }
    property string iconName: "check"
    property string actionLabel: ""
    property var action: null
    Connections {
        target: Notice
        function onPosted(text, error, options) { root.show(text, error, options) }
    }
    property bool shown: false
    // Above dialogs too (Settings reports there).
    parent: Overlay.overlay
    anchors.horizontalCenter: parent.horizontalCenter
    y: parent.height - height - (shown ? 28 : 12)
    opacity: shown ? 1 : 0
    visible: opacity > 0
    z: 1000
    width: Math.min(row.implicitWidth + 28, parent.width * 0.9)
    height: Math.max(42, row.implicitHeight + 16)
    radius: Theme.radiusLg
    color: error ? "#9b2c1f" : "#22231f"
    Behavior on opacity { NumberAnimation { duration: 200 } }
    Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
    // Clicks on it stop here.
    MouseArea { anchors.fill: parent; hoverEnabled: true; onContainsMouseChanged: containsMouse ? timer.stop() : timer.restart() }
    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 9
        Rectangle {
            antialiasing: true
            Layout.preferredWidth: 24; Layout.preferredHeight: 24; radius: Theme.radiusLg
            color: root.error ? "#c2410c" : "#2f6b4f"
            Icon { anchors.centerIn: parent; name: root.iconName; size: 14; color: "#ffffff" }
        }
        Label {
            id: label
            Layout.maximumWidth: root.parent ? root.parent.width * 0.9 - 120 : 400
            wrapMode: Text.Wrap
            color: "#f4f3ef"
            font.weight: Font.Medium
        }
        Rectangle { visible: !!root.action; Layout.preferredWidth: 1; Layout.preferredHeight: 18; color: "#55564f" }
        AbstractButton {
            id: actionBtn
            visible: !!root.action
            implicitHeight: 30
            implicitWidth: actionText.implicitWidth + 20
            hoverEnabled: true
            onClicked: { const a = root.action; root.shown = false; if (a) a() }
            background: Rectangle { antialiasing: true; radius: Theme.radius; color: actionBtn.hovered || actionBtn.pressed ? "#3a3b35" : "transparent" }
            contentItem: Label { id: actionText; text: root.actionLabel; color: "#8fe0b8"; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }
    }
    Timer { id: timer; interval: root.error ? 5000 : root.action ? 5000 : 2600; onTriggered: root.shown = false }
}
