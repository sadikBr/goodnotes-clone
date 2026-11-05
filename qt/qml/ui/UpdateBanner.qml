// "Inkleaf x.y is available" at the bottom left: what's new in it (the
// first few changes, the rest a tap away), Later, and Update now. Also
// "Updated to Inkleaf x.y" once after an update, and Settings → What's new. This copy updates itself where it can (the installed Windows
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
    /// After an update, or from Settings: what's new in this version.
    readonly property var news: !update ? services.whatsNew : null
    readonly property bool installable: !!(update && update.install)
    readonly property string st: Updater.state
    readonly property var changes: items(update ? update.notes : news ? news.notes : "")
    property bool allShown: false
    /// How many changes show before "Show all".
    readonly property int brief: news ? 99 : 3
    /// Buttons under the text when there's a list or more to say (after a failure).
    readonly property bool wide: st === "failed" || (changes.length > 0 && st !== "downloading")
    parent: Overlay.overlay
    z: 60
    visible: !!(update || news)
    x: 16
    y: parent ? parent.height - height - 16 : 0
    width: Math.min(480, (parent ? parent.width : 500) - 32)
    height: box.implicitHeight + 28
    onVisibleChanged: if (visible) { allShown = false; pop.restart() }
    NumberAnimation { id: pop; target: root; property: "opacity"; from: 0; to: 1; duration: 180 }

    /// Release notes (Markdown, as on GitHub) as a list: [{ text (styled), head }].
    function items(md) {
        const out = []
        const esc = t => t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        for (let line of String(md || "").split(/\r?\n/)) {
            line = line.trim()
            if (!line || /full changelog/i.test(line) || /^#+\s*what.s changed/i.test(line) || /^<!--/.test(line)) continue
            let head = false
            if (/^#+\s/.test(line)) { head = true; line = line.replace(/^#+\s*/, "") }
            else if (/^([-*•]|\d+\.)\s+/.test(line)) line = line.replace(/^([-*•]|\d+\.)\s+/, "")
            else if (out.length && !out[out.length - 1].head) {
                // A wrapped line: part of the change above.
                out[out.length - 1].text += " " + esc(line).replace(/\*\*(.+?)\*\*/g, "<b>$1</b>")
                continue
            }
            line = line.replace(/ by @\S+ in \S+$/, "")                 // GitHub's generated notes
                       .replace(/\[([^\]]+)\]\([^)]*\)/g, "$1")        // links: their text
                       .replace(/`([^`]+)`/g, "$1")
            out.push({ text: esc(line).replace(/\*\*(.+?)\*\*/g, "<b>$1</b>").replace(/\*(.+?)\*/g, "<i>$1</i>"), head: head })
        }
        return out
    }

    // Clicks and taps on it stop here (not on the buttons or page behind it).
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: w => w.accepted = true }
    Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
    Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    RowLayout {
        id: box
        x: 16; y: 14
        width: parent.width - 32
        spacing: 12
        Rectangle {
            antialiasing: true
            Layout.preferredWidth: 32; Layout.preferredHeight: 32; radius: 10
            Layout.alignment: Qt.AlignTop
            color: Theme.alpha(Theme.accent, 0.14)
            Icon { anchors.centerIn: parent; name: root.news ? "sparkle" : root.st === "ready" ? "check" : "download"; size: 18; color: Theme.accent }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                text: root.news ? (root.news.updated ? "Updated to Inkleaf " : "What's new in Inkleaf ") + root.news.version
                      : !root.update ? ""
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
            // What's new: the first few changes, the rest a tap away.
            Label {
                visible: changeList.visible && text.length > 0
                text: root.news ? "" : "What's new"
                font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted
                Layout.topMargin: 2
            }
            ScrollView {
                id: changeList
                visible: root.changes.length > 0 && root.st !== "downloading"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(root.allShown || root.news ? 360 : 9999, list.implicitHeight)
                contentWidth: availableWidth
                ColumnLayout {
                    id: list
                    width: changeList.availableWidth
                    spacing: 5
                    Repeater {
                        model: root.allShown ? root.changes : root.changes.slice(0, root.brief)
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle {
                                visible: !modelData.head
                                Layout.preferredWidth: 5; Layout.preferredHeight: 5; radius: 2.5
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 6
                                color: Theme.accent
                            }
                            Label {
                                text: modelData.text
                                textFormat: Text.StyledText
                                wrapMode: Text.Wrap
                                font.pixelSize: modelData.head ? 12 : 12.5
                                font.weight: modelData.head ? Font.Bold : Font.Normal
                                color: Theme.text
                                lineHeight: 1.1
                                Layout.fillWidth: true
                                Layout.topMargin: modelData.head ? 4 : 0
                            }
                        }
                    }
                }
            }
            Label {
                visible: changeList.visible && !root.allShown && root.changes.length > root.brief
                text: "Show all " + root.changes.length + " changes"
                font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.accent
                TapHandler { onTapped: root.allShown = true }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
            Loader { visible: root.wide; active: root.wide; sourceComponent: actions; Layout.topMargin: 6; Layout.alignment: Qt.AlignRight }
        }
        // The buttons: beside the text, or under it when there's more to say (after a failure).
        Loader { visible: !root.wide; active: !root.wide; sourceComponent: actions; Layout.alignment: Qt.AlignVCenter }
    }
    Component {
        id: actions
        RowLayout {
            spacing: 6
            UiButton {
                visible: !!root.news
                text: "Got it"
                kind: "primary"
                onClicked: root.services.closeWhatsNew()
            }
            UiButton {
                visible: !root.news && root.st !== "ready"
                text: root.st === "downloading" ? "Cancel" : "Later"
                kind: "ghost"
                onClicked: root.st === "downloading" ? Updater.cancel() : root.services.updateLater()
            }
            UiButton {
                visible: !root.news && (root.st === "" || root.st === "failed")
                text: !root.installable ? "Download" : root.st === "failed" ? "Try again" : "Update now"
                kind: "primary"
                onClicked: root.installable ? root.services.startUpdate() : root.services.downloadUpdate()
            }
            UiButton {
                visible: !root.news && root.st === "failed" && root.installable
                text: "Download page"
                kind: "ghost"
                onClicked: root.services.downloadUpdate()
            }
            UiButton {
                visible: !root.news && root.st === "ready"
                text: Qt.platform.os === "android" ? "Install" : "Restart now"
                kind: "primary"
                onClicked: root.services.installUpdate()
            }
        }
    }
}
