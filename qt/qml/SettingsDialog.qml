// The Settings screen: sync, writing, stylus and touch, backups, updates and
// appearance in one place (src/settingsScreen.ts). Changes apply at once.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 640)
    height: Math.min(parent.height - 32, 760)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    readonly property var s: AppSettings.store
    required property Services services
    readonly property var sync: services.syncStatus
    readonly property var backup: services.backup
    onOpened: { services.refreshSync(); services.refreshBackup() }
    function esc(t) { return String(t).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }
    function urlPath(u) { const s = String(u); return s.startsWith("file://") ? decodeURIComponent(s.slice(7)) : s }

    FolderDialog {
        id: folderDialog
        title: "Backup folder"
        onAccepted: root.services.chooseFolder(root.urlPath(selectedFolder))
    }
    FileDialog {
        id: exportDialog
        title: "Export everything"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Inkleaf library (*.inkleaf)"]
        onAccepted: root.services.exportLibrary(root.urlPath(selectedFile))
    }
    FileDialog {
        id: restoreDialog
        title: "Restore from a backup"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Inkleaf backups (*.inkleaf *.db)", "All files (*)"]
        onAccepted: {
            const path = root.urlPath(selectedFile)
            root.services.inspectBackup(path, what => confirm.ask("Restore this backup?", [], "Restore", () => { root.close(); root.services.restore(path) }, {
                danger: true,
                message: root.services.fileName(path) + " holds " + what + ". Everything in Inkleaf now will be replaced by it (a safety copy of your current library is kept)."
            }))
        }
    }
    FormDialog { id: confirm }

    Overlay.modal: Rectangle { color: Qt.rgba(20 / 255, 18 / 255, 14 / 255, 0.45) }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 180; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 100 } }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
        Rectangle { anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }

    component SectionHead: RowLayout {
        property string iconName
        property string text
        Layout.fillWidth: true
        Layout.topMargin: 18
        Layout.bottomMargin: 6
        spacing: 8
        Icon { name: parent.iconName; size: 16; color: Theme.muted }
        Label { text: parent.text.toUpperCase(); font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 0.7; color: Theme.muted }
    }
    component SettingRow: Item {
        id: row
        property string title
        property string help
        property bool first: false
        default property alias control: holder.data
        Layout.fillWidth: true
        implicitHeight: Math.max(text.implicitHeight, holder.childrenRect.height) + 24
        Rectangle { visible: !row.first; width: parent.width; height: 1; color: Theme.border }
        ColumnLayout {
            id: text
            anchors.left: parent.left
            anchors.right: holder.left
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Label { text: row.title; font.weight: Font.Medium; color: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: row.help; textFormat: Text.StyledText; font.pixelSize: 12; color: Theme.muted; linkColor: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.15 }
        }
        Item {
            id: holder
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: childrenRect.width
            height: childrenRect.height
        }
    }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Label { text: "Settings"; font.pixelSize: 18; font.weight: Font.Bold; color: Theme.text; Layout.fillWidth: true }
            UiButton { text: "Done"; kind: "primary"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: body.implicitHeight + 32
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: body
                x: 24
                y: 8
                width: parent.width - 48
                spacing: 0

                SectionHead { iconName: "docs"; text: "Sync" }
                SettingRow {
                    first: true
                    visible: !root.sync
                    title: "Google Drive"; help: "Loading…"
                }
                SettingRow {
                    first: true
                    visible: !!root.sync && !root.sync.available
                    title: "Google Drive"; help: "This build of Inkleaf can't sign in to Google (it was built without a Google key)."
                }
                SettingRow {
                    first: true
                    visible: !!root.sync && root.sync.available && !root.sync.signedIn
                    title: "Google Drive"; help: "Keep your notebooks the same on your computer and tablet. Each device signs in once; everything syncs by itself."
                    UiButton { text: "Sign in…"; kind: "primary"; onClicked: root.services.signIn() }
                }
                SettingRow {
                    first: true
                    visible: !!root.sync && root.sync.signedIn
                    title: "Google Drive"
                    help: root.sync ? "Signed in as <b>" + root.esc(root.sync.email || "your Google account") + "</b>" + (root.sync.device ? " · this device: " + root.esc(root.sync.device) : "") : ""
                    UiButton { text: "Sign out"; kind: "ghost"; onClicked: root.services.signOut() }
                }
                SettingRow {
                    visible: !!root.sync && root.sync.signedIn
                    title: "Status"
                    help: !root.sync ? "" : root.sync.lastError ? "<font color='" + Theme.danger + "'>" + root.esc(root.sync.lastError) + "</font>" : root.sync.lastSyncAt ? "Last synced " + root.services.relativeTime(root.sync.lastSyncAt) : "Not synced yet"
                    UiButton { text: root.services.syncing ? "Syncing…" : "Sync now"; enabled: !root.services.syncing; onClicked: root.services.syncNow(true) }
                }

                SectionHead { iconName: "pen"; text: "Writing" }
                SettingRow {
                    first: true
                    title: "Smoothing"; help: "Evens out shaky lines as you write. Off draws exactly what you write."
                    Segmented { options: [{ value: "off", label: "Off" }, { value: "light", label: "Light" }, { value: "normal", label: "Normal" }, { value: "strong", label: "Strong" }]; value: root.s.smoothing; onPicked: v => root.s.smoothing = v }
                }
                SettingRow {
                    title: "Pressure"; help: "How hard you press for a full line. Light: a gentle touch is enough. Firm: press harder for thick lines."
                    Segmented { options: [{ value: "light", label: "Light" }, { value: "normal", label: "Normal" }, { value: "firm", label: "Firm" }]; value: root.s.pressure; onPicked: v => root.s.pressure = v }
                }
                SettingRow {
                    title: "Cursor under the pen"; help: "Shown for the pen, highlighter and shapes."
                    Segmented { options: [{ value: "cross", label: "Cross" }, { value: "dot", label: "Dot" }, { value: "none", label: "Hidden" }]; value: root.s.penCursor; onPicked: v => root.s.penCursor = v }
                }

                SectionHead { iconName: "stylus"; text: "Stylus and touch" }
                SettingRow {
                    first: true
                    title: "Side button"; help: "Hold the pen's side button while writing to use this tool instead."
                    Segmented { options: [{ value: "eraser", label: "Eraser" }, { value: "lasso", label: "Lasso" }, { value: "none", label: "Nothing" }]; value: root.s.sideButton; onPicked: v => root.s.sideButton = v }
                }
                SettingRow {
                    title: "Eraser end"; help: "Turning the pen over erases (on pens that have an eraser end)."
                    UiSwitch { checked: root.s.eraserEnd; onClicked: root.s.eraserEnd = checked }
                }
                SettingRow {
                    title: "Palm rejection"; help: "Ignores your hand resting on the screen while you use the pen."
                    UiSwitch { checked: root.s.palmRejection; onClicked: root.s.palmRejection = checked }
                }
                SettingRow {
                    title: "Pen-only mode"; help: "Only the pen writes; touch and the mouse scroll and zoom. Turns on by itself the first time you use a pen."
                    UiSwitch { checked: root.s.penOnly; onClicked: root.s.penOnly = checked }
                }

                SectionHead { iconName: "download"; text: "Backups" }
                SettingRow {
                    first: true
                    visible: Qt.platform.os !== "android"
                    title: "Backup folder"
                    help: {
                        const f = root.backup ? root.backup.folder : null
                        if (!f) return "A USB key, or a folder that Google Drive, Dropbox or OneDrive keeps in sync."
                        const parts = f.split(/[\\/]/).filter(x => x.length)
                        return "Saving to <b>" + root.esc(parts.length > 2 ? "…/" + parts.slice(-2).join("/") : f) + "</b>"
                    }
                    UiButton { text: root.backup && root.backup.folder ? "Change…" : "Choose…"; onClicked: folderDialog.open() }
                }
                SettingRow {
                    visible: Qt.platform.os !== "android"
                    title: "Back up every day"; help: "While Inkleaf is open, once a day. Only the newest backups are kept in the folder."
                    UiSwitch {
                        enabled: !!(root.backup && root.backup.folder)
                        checked: !!(root.backup && root.backup.daily)
                        onClicked: root.services.setBackup(root.backup.folder, checked, root.backup.keep)
                    }
                }
                SettingRow {
                    visible: Qt.platform.os !== "android"
                    title: "Keep"; help: "How many daily backups stay in the folder."
                    Segmented {
                        options: [{ value: 7, label: "7" }, { value: 14, label: "14" }, { value: 30, label: "30" }]
                        value: root.backup ? root.backup.keep : 14
                        onPicked: v => root.services.setBackup(root.backup ? root.backup.folder : null, root.backup ? root.backup.daily : true, v)
                    }
                }
                SettingRow {
                    visible: Qt.platform.os !== "android"
                    title: "Status"
                    help: {
                        const c = root.backup
                        if (!c) return "Loading…"
                        if (c.lastError) return "<font color='" + Theme.danger + "'>" + root.esc(c.lastError) + "</font>"
                        if (c.lastBackupAt && c.lastBackupFile) return "Last backup " + root.services.relativeTime(c.lastBackupAt) + ": " + root.esc(root.services.fileName(c.lastBackupFile))
                        return c.folder ? "No backup made yet." : "Choose a folder to turn backups on."
                    }
                    UiButton { text: "Back up now"; enabled: !!(root.backup && root.backup.folder) && !root.services.backingUp; onClicked: root.services.backupNow() }
                }
                SettingRow {
                    title: "Export everything"; help: "Your whole library (notebooks, PDFs, settings of each daftar) as one .inkleaf file."
                    UiButton {
                        text: "Export…"
                        onClicked: {
                            exportDialog.selectedFile = InkleafCore.documentsFolder() + "/Inkleaf " + root.services.backupLabel() + ".inkleaf"
                            exportDialog.open()
                        }
                    }
                }
                SettingRow {
                    title: "Restore from a backup"; help: "Replaces everything in Inkleaf with a backup or export. Your current library is kept as a safety copy."
                    UiButton { text: "Restore…"; kind: "danger"; onClicked: restoreDialog.open() }
                }

                SectionHead { iconName: "download"; text: "Updates" }
                SettingRow {
                    first: true
                    title: "Check for updates automatically"; help: "You're told when a new version is out, and it downloads in your browser."
                    UiSwitch { checked: root.services.autoCheck; onClicked: root.services.setAutoCheck(checked) }
                }
                SettingRow {
                    title: "Inkleaf"; help: "Version " + root.services.version
                    UiButton { text: "Check now"; onClicked: root.services.checkForUpdates(true) }
                }

                SectionHead { iconName: "sun"; text: "Appearance" }
                SettingRow {
                    first: true
                    title: "Theme"; help: "System follows your computer's light or dark setting."
                    Segmented { options: [{ value: "light", label: "Light" }, { value: "dark", label: "Dark" }, { value: "system", label: "System" }]; value: root.s.theme; onPicked: v => root.s.theme = v }
                }

            }
        }
    }
}
