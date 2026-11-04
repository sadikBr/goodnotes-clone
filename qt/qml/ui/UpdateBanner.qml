// "Inkleaf x.y is available" at the bottom left: what's new, Later, and
// Update now. This copy updates itself where it can (the installed Windows
// app, the AppImage, Android): it downloads with a progress bar, checks the
// file, then restarts into the new version. Otherwise Download opens the
// download in the browser.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Item {
    id: root
    required property Services services
    readonly property var update: services.update
    readonly property bool installable: !!(update && update.install)
    readonly property string st: Updater.state
    property bool notesOpen: false
    /// Buttons under the text (a failure has a longer message and more choices).
    readonly property bool wide: st === "failed"
    parent: Overlay.overlay
    z: 60
    visible: !!update
    x: 16
    y: parent ? parent.height - height - 16 : 0
    width: Math.min(480, (parent ? parent.width : 500) - 32)
    height: box.implicitHeight + 28
    onVisibleChanged: if (visible) pop.restart()
    NumberAnimation { id: pop; target: root; property: "opacity"; from: 0; to: 1; duration: 180 }

    Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
    Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    RowLayout {
        id: box
        x: 16; y: 14
        width: parent.width - 32
        spacing: 12
        Icon { name: root.st === "ready" ? "check" : "download"; size: 20; color: Theme.accent; Layout.alignment: Qt.AlignTop }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                text: !root.update ? ""
                      : root.st === "downloading" ? "Downloading Inkleaf " + root.update.version + "…"
                      : root.st === "ready" ? "Inkleaf " + root.update.version + " is ready"
                      : "Inkleaf " + root.update.version + " is available"
                font.pixelSize: 14; font.weight: Font.Bold; color: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap
            }
            // How far the download got.
            Item {
                visible: root.st === "downloading"
                Layout.fillWidth: true
                Layout.preferredHeight: 6
                Layout.topMargin: 4
                Rectangle { anchors.fill: parent; radius: 3; color: Theme.surface2; border.color: Theme.border }
                Rectangle {
                    id: bar
                    radius: 3
                    height: parent.height
                    color: Theme.accent
                    // Unknown size: a short piece sliding back and forth.
                    width: Updater.progress >= 0 ? parent.width * Updater.progress : parent.width * 0.25
                    Behavior on width { enabled: Updater.progress >= 0; NumberAnimation { duration: 200 } }
                    SequentialAnimation on x {
                        running: root.st === "downloading" && Updater.progress < 0
                        loops: Animation.Infinite
                        NumberAnimation { from: 0; to: bar.parent.width * 0.75; duration: 900; easing.type: Easing.InOutQuad }
                        NumberAnimation { from: bar.parent.width * 0.75; to: 0; duration: 900; easing.type: Easing.InOutQuad }
                    }
                }
            }
            Label {
                visible: root.st === "downloading" || root.st === "ready" || root.st === "failed"
                text: root.st === "downloading" ? (Updater.progress >= 0 ? Math.round(Updater.progress * 100) + "%" : "Starting…")
                      : root.st === "ready" ? (Qt.platform.os === "android" ? "Downloaded and checked. Android will ask you to confirm the install."
                                                                              : "Downloaded and checked. Your notebook is saved, and Inkleaf restarts in the new version.")
                      : Updater.error
                font.pixelSize: 12
                color: root.st === "failed" ? Theme.danger : Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                visible: !!(!!root.update && root.update.notes.length > 0 && root.st !== "downloading")
                text: (root.notesOpen ? "▾ " : "▸ ") + "What's new"
                font.pixelSize: 12
                color: Theme.muted
                TapHandler { onTapped: root.notesOpen = !root.notesOpen }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
            Loader { visible: root.wide; active: root.wide; sourceComponent: actions; Layout.topMargin: 6 }
            ScrollView {
                visible: !!(root.notesOpen && !!root.update && root.st !== "downloading")
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(160, notes.implicitHeight)
                Label { id: notes; width: parent.width; text: root.update ? root.update.notes : ""; wrapMode: Text.Wrap; font.pixelSize: 12; color: Theme.text }
            }
        }
        // The buttons: beside the text, or under it when there's more to say (after a failure).
        Loader { visible: !root.wide; active: !root.wide; sourceComponent: actions; Layout.alignment: Qt.AlignVCenter }
    }
    Component {
        id: actions
        RowLayout {
            spacing: 6
            UiButton {
                visible: root.st !== "ready"
                text: root.st === "downloading" ? "Cancel" : "Later"
                kind: "ghost"
                onClicked: root.st === "downloading" ? Updater.cancel() : root.services.updateLater()
            }
            UiButton {
                visible: root.st === "" || root.st === "failed"
                text: !root.installable ? "Download" : root.st === "failed" ? "Try again" : "Update now"
                kind: "primary"
                onClicked: root.installable ? root.services.startUpdate() : root.services.downloadUpdate()
            }
            UiButton {
                visible: root.st === "failed" && root.installable
                text: "Download page"
                kind: "ghost"
                onClicked: root.services.downloadUpdate()
            }
            UiButton {
                visible: root.st === "ready"
                text: Qt.platform.os === "android" ? "Install" : "Restart now"
                kind: "primary"
                onClicked: root.services.installUpdate()
            }
        }
    }
}
