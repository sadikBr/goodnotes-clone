// Write from your phone, on the computer: the window with the code to scan,
// the frame on the page showing what the phone shows (and where it writes),
// and a pill while a phone is connected.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Item {
    id: root
    required property InkCanvas canvas
    /// The phone picked a tool.
    signal toolPicked(string tool)
    readonly property bool active: pad.running
    readonly property bool connected: pad.connected

    function open() { pad.start(); dialog.open() }
    function stop() { dialog.close(); pad.stop() }

    PhonePad {
        id: pad
        canvas: root.canvas
        onToolRequested: tool => root.toolPicked(tool)
        onConnectedChanged: if (connected) hideSoon.restart()
    }
    // Once the phone is in, the window steps aside (the pill stays).
    Timer { id: hideSoon; interval: 1600; onTriggered: if (pad.connected) dialog.close() }

    // ------------------------------------------------------------ the frame on the page
    property int viewRev: 0
    Connections { target: root.canvas; function onViewChanged() { root.viewRev++ } }
    Rectangle {
        id: frame
        readonly property rect r: { void root.viewRev; return pad.hasRegion ? root.canvas.pageRectToItem(pad.page, pad.region) : Qt.rect(0, 0, 0, 0) }
        visible: pad.connected && pad.hasRegion
        x: r.x - 2; y: r.y - 2
        width: r.width + 4; height: r.height + 4
        radius: 6
        antialiasing: true
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        Rectangle {
            anchors.left: parent.left; anchors.bottom: parent.top; anchors.bottomMargin: 4
            width: tagRow.implicitWidth + 14; height: 22; radius: 11
            color: Theme.accent
            Row {
                id: tagRow
                anchors.centerIn: parent
                spacing: 5
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "phone"; size: 12; color: Theme.accentContrast }
                Label { anchors.verticalCenter: parent.verticalCenter; text: "Your phone"; font.pixelSize: 11; font.weight: Font.DemiBold; color: Theme.accentContrast }
            }
        }
    }

    // ------------------------------------------------------------ the pill
    Rectangle {
        z: 9
        visible: pad.running && !dialog.visible
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        height: 38
        width: pillRow.implicitWidth + 16
        radius: Theme.radiusLg
        antialiasing: true
        color: Theme.surface
        border.color: Theme.border
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3; z: -1 }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
        RowLayout {
            id: pillRow
            anchors.centerIn: parent
            spacing: 4
            Rectangle { Layout.leftMargin: 6; width: 8; height: 8; radius: 4; color: pad.connected ? Theme.accent : "#f59e0b" }
            Label {
                Layout.leftMargin: 4; Layout.rightMargin: 6
                text: pad.connected ? "Writing from your phone · page " + (pad.page + 1) : "Waiting for your phone…"
                font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text
            }
            IconButton { iconName: "qr"; tip: "Show the code"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; onClicked: dialog.open() }
            IconButton { iconName: "close"; tip: "Stop"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; onClicked: root.stop() }
        }
    }

    // ------------------------------------------------------------ the window
    Popup {
        id: dialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 460)
        padding: 0
        modal: true
        focus: true
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, Theme.dark ? 0.5 : 0.28) }
        background: Item {
            Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 4 }
            Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
        }
        contentItem: ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.preferredWidth: dialog.width - 36
                Layout.leftMargin: 18; Layout.rightMargin: 18; Layout.topMargin: 16; Layout.bottomMargin: 14
                spacing: 12
                Rectangle {
                    Layout.preferredWidth: 34; Layout.preferredHeight: 34; radius: Theme.radius; antialiasing: true
                    color: Theme.accentSoft
                    Icon { anchors.centerIn: parent; name: "phone"; size: 17; color: Theme.accent }
                }
                ColumnLayout {
                    spacing: 1
                    Label { text: "Write from your phone"; font.pixelSize: 16; font.weight: Font.DemiBold; color: Theme.text }
                    Label { text: "Your phone becomes a writing pad for this notebook."; font.pixelSize: 12; color: Theme.muted }
                }
                Item { Layout.fillWidth: true }
                IconButton { iconName: "close"; tip: "Close (keeps it on)"; onClicked: dialog.close() }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

            // Couldn't start.
            Label {
                visible: pad.error.length > 0
                Layout.fillWidth: true
                Layout.margins: 20
                wrapMode: Text.Wrap
                text: pad.error
                color: Theme.danger
            }

            ColumnLayout {
                visible: pad.running
                Layout.fillWidth: true
                Layout.margins: 20
                spacing: 14
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 18
                    // The code.
                    Rectangle {
                        Layout.preferredWidth: 172; Layout.preferredHeight: 172
                        radius: Theme.radius
                        color: "#ffffff"
                        border.color: Theme.border
                        Image {
                            anchors.fill: parent
                            anchors.margins: 4
                            source: pad.qrImage
                            smooth: false
                            fillMode: Image.PreserveAspectFit
                        }
                        // Connected: a tick over it.
                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radius
                            color: Qt.rgba(1, 1, 1, 0.9)
                            visible: opacity > 0
                            opacity: pad.connected ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 200 } }
                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 44; height: 44; radius: 22; color: Theme.accent; Icon { anchors.centerIn: parent; name: "check"; size: 22; color: Theme.accentContrast } }
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "Connected"; font.weight: Font.DemiBold; color: "#1d1f23" }
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 9
                        Repeater {
                            model: ["Connect your phone to the same Wi-Fi as this computer.", "Scan the code with its camera, and open the link.", "Write with one finger or a stylus. Two fingers move and zoom."]
                            delegate: RowLayout {
                                required property string modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: 9
                                Rectangle {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.preferredWidth: 20; Layout.preferredHeight: 20; radius: 10
                                    color: Theme.surface2
                                    Label { anchors.centerIn: parent; text: index + 1; font.pixelSize: 11; font.weight: Font.DemiBold; color: Theme.muted }
                                }
                                Label { Layout.fillWidth: true; text: modelData; wrapMode: Text.Wrap; font.pixelSize: 13; color: Theme.text; lineHeight: 1.15 }
                            }
                        }
                    }
                }
                // The address, to type in instead.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    radius: Theme.radius
                    color: Theme.surface2
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12; anchors.rightMargin: 4
                        Label { Layout.fillWidth: true; text: pad.url; elide: Text.ElideMiddle; font.family: "monospace"; font.pixelSize: 12; color: Theme.muted }
                        IconButton { iconName: "copy"; tip: "Copy the address"; implicitWidth: 30; implicitHeight: 30; iconSize: 15; onClicked: { clip.text = pad.url; clip.selectAll(); clip.copy() } }
                    }
                }
                // A firewall (Omarchy's is on) keeps the phone out until the port is allowed.
                Rectangle {
                    visible: Qt.platform.os === "linux" && !pad.connected
                    Layout.fillWidth: true
                    implicitHeight: fw.implicitHeight + 20
                    radius: Theme.radius
                    color: "transparent"
                    border.color: Theme.border
                    ColumnLayout {
                        id: fw
                        anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12; anchors.rightMargin: 6
                        spacing: 6
                        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Phone can't open the link? Your firewall may be keeping it out. Allow Inkleaf's port once:"; font.pixelSize: 12; color: Theme.muted }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: "sudo ufw allow " + pad.port + "/tcp"; font.family: "monospace"; font.pixelSize: 12; color: Theme.text }
                            IconButton { iconName: "copy"; tip: "Copy"; implicitWidth: 28; implicitHeight: 28; iconSize: 14; onClicked: { clip.text = "sudo ufw allow " + pad.port + "/tcp"; clip.selectAll(); clip.copy() } }
                        }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            RowLayout {
                Layout.preferredWidth: dialog.width - 32
                Layout.margins: 14; Layout.leftMargin: 18; Layout.rightMargin: 14
                spacing: 8
                Label { text: pad.connected ? "Your phone is connected." : pad.running ? "Waiting for your phone…" : ""; font.pixelSize: 12; color: pad.connected ? Theme.accent : Theme.muted }
                Item { Layout.fillWidth: true }
                UiButton { text: "Stop"; kind: "ghost"; visible: pad.running; onClicked: root.stop() }
                UiButton { text: "Done"; kind: "primary"; onClicked: { dialog.close(); if (!pad.running) pad.stop() } }
            }
        }
    }
    TextEdit { id: clip; visible: false }
}
