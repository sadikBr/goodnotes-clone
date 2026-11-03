// A short message at the bottom of the window (the web app's .toast).
import QtQuick
import QtQuick.Controls
import Inkleaf

Rectangle { antialiasing: true;
    id: root
    property bool error: false
    function show(message, isError) {
        label.text = message
        error = !!isError
        shown = true
        timer.restart()
    }
    property bool shown: false
    // Above dialogs too (Settings reports there).
    parent: Overlay.overlay
    anchors.horizontalCenter: parent.horizontalCenter
    y: parent.height - height - (shown ? 28 : 12)
    opacity: shown ? 1 : 0
    visible: opacity > 0
    z: 1000
    width: Math.min(label.implicitWidth + 36, parent.width * 0.9)
    height: label.implicitHeight + 20
    radius: height / 2
    color: error ? "#9b2c1f" : "#22231f"
    Behavior on opacity { NumberAnimation { duration: 200 } }
    Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
    Label {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, root.parent.width * 0.9 - 36)
        wrapMode: Text.Wrap
        color: "#f4f3ef"
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
    }
    Timer { id: timer; interval: root.error ? 5000 : 2600; onTriggered: root.shown = false }
}
