// The Settings screen: a section at a time (pen and touch, AI, sync and
// backups, appearance, updates), its settings in groups. Changes apply at once.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    // On a phone it takes the whole screen.
    width: Theme.phone ? parent.width : Math.min(parent.width - 32, 860)
    height: Theme.phone ? parent.height : Math.min(parent.height - 32, 680)
    /// Side list of sections, or tabs across the top on a narrow window.
    readonly property bool narrow: width < 640
    /// The section shown (kept between openings).
    property string page: "pen"
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    readonly property var s: AppSettings.store
    required property Services services
    readonly property var sync: services.syncStatus
    readonly property var backup: services.backup
    // Opened for a section (AppSettings.settingsPage, e.g. "ai" from the AI panel).
    onAboutToShow: if (AppSettings.settingsPage) { page = AppSettings.settingsPage; AppSettings.settingsPage = "" }
    onOpened: { services.refreshSync(); services.refreshBackup(); Ai.refresh(); Ai.refreshProgress(); aiTest = "" }
    readonly property var ai: Ai.status
    readonly property string aiProvider: ai ? ai.provider : ""
    property string aiTest: ""
    property bool aiTesting: false
    readonly property var aiProviders: [
        { value: "", label: "Off" },
        { value: "anthropic", label: "Claude (Anthropic API key)", group: "With an API key" },
        { value: "openai", label: "ChatGPT (OpenAI API key)", group: "With an API key" },
        { value: "gemini", label: "Gemini (Google API key)", group: "With an API key" },
        { value: "ollama", label: "Ollama (on your computer)", group: "Local or other" },
        { value: "custom", label: "Other OpenAI-compatible", group: "Local or other" }
    ]
    function aiSet(change) {
        const err = Ai.setup(change)
        aiTest = err ? "<font color='" + Theme.danger + "'>" + esc(err) + "</font>" : ""
    }
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
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.phone ? 0 : Theme.radiusXl; color: Theme.surface; border.color: Theme.phone ? "transparent" : Theme.border }
    }

    readonly property var pages: [
        { id: "pen", icon: "stylus", label: "Pen & touch" },
        { id: "ai", icon: "sparkle", label: "AI" },
        { id: "sync", icon: "docs", label: "Sync & backups" },
        { id: "look", icon: "sun", label: "Appearance" },
        { id: "about", icon: "download", label: "Updates" }
    ]

    /// A group of settings in a rounded card, with a small heading.
    component Group: ColumnLayout {
        id: grp
        property string title
        default property alias rows: card.data
        Layout.fillWidth: true
        Layout.bottomMargin: 18
        spacing: 6
        Label { visible: grp.title.length > 0; text: grp.title; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted; Layout.leftMargin: 4 }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: card.implicitHeight
            antialiasing: true
            radius: Theme.radiusLg
            color: Theme.dark ? Qt.lighter(Theme.surface, 1.12) : Theme.surface2
            border.color: Theme.border
            ColumnLayout {
                id: card
                x: 16
                width: parent.width - 32
                spacing: 0
            }
        }
    }
    /// A scroll speed: slower ↔ faster, its percentage, and back to normal.
    component SpeedSlider: RowLayout {
        id: sp
        property real value: 1
        signal moved(real value)
        spacing: 10
        Label { text: "Slower"; font.pixelSize: 12; color: Theme.muted }
        UiSlider {
            id: sl
            Layout.preferredWidth: 160
            from: 0.25; to: 2; stepSize: 0.05
            value: sp.value
            onMoved: sp.moved(Math.round(value * 100) / 100)
        }
        Label { text: "Faster"; font.pixelSize: 12; color: Theme.muted }
        Label {
            Layout.preferredWidth: 42
            horizontalAlignment: Text.AlignRight
            text: Math.round(sl.value * 100) + "%"
            font.pixelSize: 13; font.weight: Font.DemiBold
            color: Math.abs(sl.value - 1) < 0.001 ? Theme.muted : Theme.text
        }
        IconButton {
            iconName: "reset"; tip: "Back to 100%"
            implicitWidth: 28; implicitHeight: 28; iconSize: 14
            opacity: Math.abs(sp.value - 1) < 0.001 ? 0 : 1
            enabled: opacity > 0
            onClicked: sp.moved(1)
        }
    }
    component SettingRow: Item {
        id: row
        property string title
        property string help
        property bool first: false
        default property alias control: holder.data
        /// On a phone a wide control goes under its words (a switch stays beside them).
        readonly property bool stacked: Theme.phone && holder.childrenRect.width > 90
        Layout.fillWidth: true
        implicitHeight: stacked ? text.implicitHeight + holder.childrenRect.height + 32 : Math.max(text.implicitHeight, holder.childrenRect.height) + 22
        Rectangle { visible: !row.first; width: parent.width; height: 1; color: Theme.border; opacity: 0.7 }
        ColumnLayout {
            id: text
            anchors.left: parent.left
            anchors.right: row.stacked ? parent.right : holder.left
            anchors.rightMargin: row.stacked ? 0 : 16
            anchors.verticalCenter: row.stacked ? undefined : parent.verticalCenter
            anchors.top: row.stacked ? parent.top : undefined
            anchors.topMargin: 12
            spacing: 2
            Label { text: row.title; font.weight: Font.Medium; color: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { visible: row.help.length > 0; text: row.help; textFormat: Text.StyledText; font.pixelSize: 12; color: Theme.muted; linkColor: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.15 }
        }
        Item {
            id: holder
            anchors.right: row.stacked ? undefined : parent.right
            anchors.left: row.stacked ? parent.left : undefined
            anchors.verticalCenter: row.stacked ? undefined : parent.verticalCenter
            anchors.top: row.stacked ? text.bottom : undefined
            anchors.topMargin: 8
            width: childrenRect.width
            height: childrenRect.height
        }
    }
    /// One section: its title, a line about it, and its groups.
    component Page: Flickable {
        id: pg
        property string name
        property string title
        property string about
        default property alias groups: col.data
        visible: root.page === name
        contentHeight: col.implicitHeight + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: col
            x: root.narrow ? 16 : 28
            y: root.narrow ? 12 : 22
            width: pg.width - 2 * x
            spacing: 0
            Label { visible: !root.narrow; text: pg.title; font.pixelSize: 20; font.weight: Font.Bold; color: Theme.text }
            Label { visible: pg.about.length > 0; text: pg.about; color: Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.topMargin: 2; Layout.bottomMargin: 18 }
            Item { visible: pg.about.length === 0; Layout.preferredHeight: 14 }
        }
    }
    /// A section in the list (or a tab).
    component NavItem: AbstractButton {
        id: nav
        required property var modelData
        readonly property bool current: root.page === modelData.id
        hoverEnabled: true
        implicitHeight: root.narrow ? 34 : 40
        implicitWidth: navRow.implicitWidth + (root.narrow ? 24 : 0)
        onClicked: root.page = modelData.id
        background: Rectangle {
            antialiasing: true
            radius: Theme.radius
            color: nav.current ? Theme.alpha(Theme.accent, 0.14) : nav.hovered ? Theme.surface2 : "transparent"
        }
        contentItem: RowLayout {
            id: navRow
            spacing: 10
            Item { visible: !root.narrow; Layout.preferredWidth: 2 }
            Icon { name: nav.modelData.icon; size: 17; color: nav.current ? Theme.accent : Theme.muted }
            Label { text: nav.modelData.label; color: nav.current ? Theme.text : Theme.muted; font.weight: nav.current ? Font.DemiBold : Font.Normal; Layout.fillWidth: !root.narrow }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.leftMargin: 24
            Layout.rightMargin: 16
            Label { text: "Settings"; font.pixelSize: 18; font.weight: Font.Bold; color: Theme.text; Layout.fillWidth: true }
            UiButton { text: "Done"; kind: "primary"; onClicked: root.close() }
        }
        // Narrow: the sections as tabs.
        Flickable {
            visible: root.narrow
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            contentWidth: tabs.implicitWidth + 32
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Row {
                id: tabs
                x: 16
                spacing: 6
                Repeater { model: root.pages; delegate: NavItem {} }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            // Wide: the sections in a list on the left.
            ColumnLayout {
                visible: !root.narrow
                Layout.preferredWidth: 210
                Layout.maximumWidth: 210
                Layout.fillWidth: false
                Layout.fillHeight: true
                Layout.margins: 12
                spacing: 2
                Repeater { model: root.pages; delegate: NavItem { Layout.fillWidth: true } }
                Item { Layout.fillHeight: true }
                Label { text: "Inkleaf " + root.services.version; color: Theme.muted; font.pixelSize: 12; Layout.leftMargin: 12; Layout.bottomMargin: 4 }
            }
            Rectangle { visible: !root.narrow; Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Page {
                    anchors.fill: parent
                    name: "pen"; title: "Pen & touch"; about: "How writing feels, and what the pen and your fingers do."
                    Group {
                        title: "Writing"
                        SettingRow {
                            first: true
                            title: "Smoothing"; help: "Evens out shaky lines. Off draws exactly what you write."
                            Segmented { options: [{ value: "off", label: "Off" }, { value: "light", label: "Light" }, { value: "normal", label: "Normal" }, { value: "strong", label: "Strong" }]; value: root.s.smoothing; onPicked: v => root.s.smoothing = v }
                        }
                        SettingRow {
                            title: "Pressure"; help: "How hard you press for a full line."
                            Segmented { options: [{ value: "light", label: "Light" }, { value: "normal", label: "Normal" }, { value: "firm", label: "Firm" }]; value: root.s.pressure; onPicked: v => root.s.pressure = v }
                        }
                        SettingRow {
                            title: "Cursor under the pen"; help: "Shown where the pen will write."
                            Segmented { options: [{ value: "cross", label: "Cross" }, { value: "dot", label: "Dot" }, { value: "none", label: "Hidden" }]; value: root.s.penCursor; onPicked: v => root.s.penCursor = v }
                        }
                    }
                    Group {
                        title: "Pen"
                        SettingRow {
                            first: true
                            title: "Side button"; help: "Hold it while writing to use this tool instead."
                            Segmented { options: [{ value: "eraser", label: "Eraser" }, { value: "lasso", label: "Lasso" }, { value: "none", label: "Nothing" }]; value: root.s.sideButton; onPicked: v => root.s.sideButton = v }
                        }
                        SettingRow {
                            title: "Eraser end"; help: "Turn the pen over to erase."
                            UiSwitch { checked: root.s.eraserEnd; onClicked: root.s.eraserEnd = checked }
                        }
                        SettingRow {
                            title: "Palm rejection"; help: "Ignores your hand resting on the screen."
                            UiSwitch { checked: root.s.palmRejection; onClicked: root.s.palmRejection = checked }
                        }
                        SettingRow {
                            title: "Pen-only mode"; help: "Touch and the mouse scroll and zoom; only the pen writes. On by itself once you use a pen."
                            UiSwitch { checked: root.s.penOnly; onClicked: root.s.penOnly = checked }
                        }
                    }
                    Group {
                        title: "Gestures"
                        SettingRow {
                            first: true
                            title: "Scratch out to erase"; help: "Scribble over ink with the pen to erase it."
                            UiSwitch { checked: root.s.scratchOut; onClicked: root.s.scratchOut = checked }
                        }
                        SettingRow {
                            title: "Finger taps"; help: "Two fingers undo, three redo."
                            UiSwitch { checked: root.s.fingerTaps; onClicked: root.s.fingerTaps = checked }
                        }
                    }
                    Group {
                        title: "Scrolling"
                        SettingRow {
                            first: true
                            title: "Mouse wheel"; help: "How far a turn of the wheel scrolls. Each computer keeps its own."
                            SpeedSlider { value: root.s.wheelSpeed; onMoved: root.s.wheelSpeed = value }
                        }
                        SettingRow {
                            title: "Touchpad"; help: "How far a two-finger swipe scrolls."
                            SpeedSlider { value: root.s.touchpadSpeed; onMoved: root.s.touchpadSpeed = value }
                        }
                    }
                }
                Page {
                    anchors.fill: parent
                    name: "ai"; title: "AI"; about: "Your own AI assistant, on your computer or with your API key."
                    Group {
                        title: "Assistant"
                        SettingRow {
                            first: true
                            title: "AI assistant"
                            help: "Ask about your notes, read handwriting, explain or solve a selection. A model on your computer, or your own API key."
                            UiSelect {
                                width: 240
                                options: root.aiProviders
                                value: root.aiProvider
                                onPicked: v => root.aiSet({ provider: v })
                            }
                        }
                        SettingRow {
                            visible: ["anthropic", "openai", "gemini", "custom"].indexOf(root.aiProvider) >= 0
                            title: "API key"
                            help: root.ai && root.ai.hasKey ? "Saved on this device only (not in backups or sync): " + root.esc(root.ai.keyHint || "set") + ". Paste a new one to replace it."
                                  : root.aiProvider === "anthropic" ? "Create one at console.anthropic.com → API keys."
                                  : root.aiProvider === "openai" ? "Create one at platform.openai.com → API keys."
                                  : root.aiProvider === "gemini" ? "Create one at aistudio.google.com → Get API key."
                                  : "If your server needs one (OpenRouter, Mistral, Groq…; LM Studio doesn't)."
                            RowLayout {
                                spacing: 6
                                UiField {
                                    id: keyField
                                    Layout.preferredWidth: 180
                                    echoMode: TextInput.Password
                                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                                    placeholderText: root.ai && root.ai.hasKey ? "••••••••" : "Paste your key"
                                    onAccepted: { root.aiSet({ apiKey: text }); text = "" }
                                }
                                UiButton { text: "Save"; enabled: keyField.text.length > 0; onClicked: { root.aiSet({ apiKey: keyField.text }); keyField.text = "" } }
                                UiButton { visible: !!(root.ai && root.ai.hasKey); kind: "ghost"; text: "Remove"; onClicked: root.aiSet({ apiKey: "" }) }
                            }
                        }
                        SettingRow {
                            visible: root.aiProvider !== ""
                            title: "Model"
                            help: root.aiProvider === "anthropic" ? "Leave empty for " + "claude-opus-5-5" + ". For lower costs, try claude-haiku-5-5."
                                  : root.aiProvider === "ollama" ? "A model you've pulled, e.g. <b>qwen2.5vl</b> or <b>llama3.2-vision</b> (vision models can read handwriting)."
                                  : "The model's name, as your provider writes it. Pick one that can see pictures to read handwriting."
                            UiField {
                                width: 240
                                text: root.ai ? root.ai.model : ""
                                placeholderText: root.ai && root.ai.effectiveModel ? root.ai.effectiveModel : "model name"
                                onEditingFinished: if (root.ai && text !== root.ai.model) root.aiSet({ model: text })
                            }
                        }
                        SettingRow {
                            visible: root.aiProvider === "ollama" || root.aiProvider === "custom"
                            title: "Server address"
                            help: root.aiProvider === "ollama" ? "Leave empty for Ollama on this computer (http://localhost:11434/v1)." : "Its OpenAI-compatible address, e.g. http://localhost:1234/v1 for LM Studio."
                            UiField {
                                width: 240
                                text: root.ai ? root.ai.baseUrl : ""
                                placeholderText: root.ai && root.ai.effectiveUrl ? root.ai.effectiveUrl : "http://…/v1"
                                onEditingFinished: if (root.ai && text !== root.ai.baseUrl) root.aiSet({ baseUrl: text })
                            }
                        }
                        SettingRow {
                            visible: root.aiProvider !== ""
                            title: "Status"
                            help: root.aiTest ? root.aiTest
                                  : !Ai.ready ? "Not ready yet: " + (root.aiProvider === "ollama" || root.aiProvider === "custom" ? "enter a model." : "add your API key.")
                                  : (Ai.local ? "Runs on your computer or network: your notes stay with you." : "What you ask about (and pictures of what you select) is sent to " + (root.aiProvider === "anthropic" ? "Anthropic" : root.aiProvider === "openai" ? "OpenAI" : root.aiProvider === "gemini" ? "Google" : "that service") + ", and uses your API credit.")
                            UiButton {
                                text: root.aiTesting ? "Testing…" : "Test"
                                enabled: Ai.ready && !root.aiTesting
                                onClicked: {
                                    root.aiTesting = true
                                    root.aiTest = ""
                                    Ai.test((answer, err) => {
                                        root.aiTesting = false
                                        root.aiTest = err ? "<font color='" + Theme.danger + "'>" + root.esc(err) + "</font>" : "Working. The model answered: “" + root.esc(answer.slice(0, 60)) + "”"
                                    })
                                }
                            }
                        }
                        SettingRow {
                            visible: root.aiProvider !== ""
                            title: "Make handwriting searchable"
                            help: {
                                const p = Ai.progress
                                let t = "Reads your handwritten pages in the background, so search and questions find them. Locked notebooks are never read."
                                if (!Ai.local && root.aiProvider) t += " Each page is one request to your provider."
                                if (Ai.readingOn && p && p.total) t += "<br><b>" + p.done + " of " + p.total + "</b> handwritten pages read" + (Ai.reading ? " (reading…)" : "") + "."
                                if (Ai.readingOn && Ai.readError) t += "<br><font color='" + Theme.danger + "'>" + root.esc(Ai.readError) + "</font>"
                                return t
                            }
                            UiSwitch { enabled: Ai.ready; checked: !!(root.ai && root.ai.searchHandwriting); onClicked: root.aiSet({ searchHandwriting: checked }) }
                        }
                    }
                }
                Page {
                    anchors.fill: parent
                    name: "sync"; title: "Sync & backups"; about: "Keep your notebooks safe and on all your devices."
                    Group {
                        title: "Sync between devices"
                        SettingRow {
                            first: true
                            visible: !root.sync
                            title: "Google Drive"; help: "Loading…"
                        }
                        SettingRow {
                            first: true
                            visible: !!(!!root.sync && !root.sync.available)
                            title: "Google Drive"; help: "This build of Inkleaf can't sign in to Google (it was built without a Google key)."
                        }
                        SettingRow {
                            first: true
                            visible: !!(!!root.sync && root.sync.available && !root.sync.signedIn)
                            title: "Google Drive"; help: "The same notebooks on your computer and tablet, kept in sync by themselves."
                            UiButton { text: "Sign in…"; kind: "primary"; onClicked: root.services.signIn() }
                        }
                        SettingRow {
                            first: true
                            visible: !!(!!root.sync && root.sync.signedIn)
                            title: "Google Drive"
                            help: root.sync ? "Signed in as <b>" + root.esc(root.sync.email || "your Google account") + "</b>" + (root.sync.device ? " · this device: " + root.esc(root.sync.device) : "") : ""
                            UiButton { text: "Sign out"; kind: "ghost"; onClicked: root.services.signOut() }
                        }
                        SettingRow {
                            visible: !!(!!root.sync && root.sync.signedIn)
                            title: "Status"
                            help: !root.sync ? "" : root.sync.lastError ? "<font color='" + Theme.danger + "'>" + root.esc(root.sync.lastError) + "</font>" : (root.sync.lastSyncAt ? "Last synced " + root.services.relativeTime(root.sync.lastSyncAt) : "Not synced yet") + ". Notebooks are kept in Inkleaf's own private space in your Drive, which doesn't show among your files (Drive's Settings → Manage apps lists it)."
                            UiButton { text: root.services.syncing ? "Syncing…" : "Sync now"; enabled: !root.services.syncing; onClicked: root.services.syncNow(true) }
                        }
                    }
                    Group {
                        title: "Backups"
                        visible: Qt.platform.os !== "android"
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
                            title: "Back up every day"; help: "Once a day while Inkleaf is open."
                            UiSwitch {
                                enabled: !!(root.backup && root.backup.folder)
                                checked: !!(root.backup && root.backup.daily)
                                onClicked: root.services.setBackup(root.backup.folder, checked, root.backup.keep)
                            }
                        }
                        SettingRow {
                            visible: Qt.platform.os !== "android"
                            title: "Keep"; help: "Older backups are removed from the folder."
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
                    }
                    Group {
                        title: "Your library"
                        SettingRow {
                            first: true
                            title: "Export everything"; help: "Your whole library as one .inkleaf file."
                            UiButton {
                                text: "Export…"
                                onClicked: {
                                    exportDialog.selectedFile = InkleafCore.documentsFolder() + "/Inkleaf " + root.services.backupLabel() + ".inkleaf"
                                    exportDialog.open()
                                }
                            }
                        }
                        SettingRow {
                            title: "Restore from a backup"; help: "Replaces your library (a safety copy is kept)."
                            UiButton { text: "Restore…"; kind: "danger"; onClicked: restoreDialog.open() }
                        }
                    }
                }
                Page {
                    anchors.fill: parent
                    name: "look"; title: "Appearance"; about: ""
                    Group {
                        title: "Theme"
                        SettingRow {
                            first: true
                            title: "Theme"; help: "System follows your device's light or dark setting."
                            Segmented { options: [{ value: "light", label: "Light" }, { value: "dark", label: "Dark" }, { value: "system", label: "System" }]; value: root.s.theme; onPicked: v => root.s.theme = v }
                        }
                    }
                    Group {
                        title: "Library"
                        SettingRow {
                            first: true
                            title: "The first card on the shelf"; help: "What it makes with one tap. The New button has everything."
                            UiSelect {
                                width: 210
                                options: [{ value: "notebook", label: "Notebook" }, { value: "daftar", label: "Daftar annossous" }, { value: "pdf", label: "Import PDFs" }, { value: "folder", label: "Folder" }, { value: "sketchbook", label: "Sketchbook" }, { value: "whiteboard", label: "Whiteboard" }]
                                value: root.s.newCard
                                onPicked: v => root.s.newCard = v
                            }
                        }
                    }
                    Group {
                        title: "Tools"
                        SettingRow {
                            first: true
                            title: "Where the tools sit"; help: "A rail down the left side that leaves the top bar to the notebook, or the top bar."
                            Segmented { options: [{ value: "rail", label: "Side rail" }, { value: "top", label: "Top bar" }]; value: root.s.toolbar; onPicked: v => root.s.toolbar = v }
                        }
                    }
                }
                Page {
                    anchors.fill: parent
                    name: "about"; title: "Updates"; about: "You're on Inkleaf " + root.services.version + "."
                    Group {
                        title: "Updates"
                        SettingRow {
                            first: true
                            title: "Check for updates automatically"; help: "Tells you when a new version is out, with what's new."
                            UiSwitch { checked: root.services.autoCheck; onClicked: root.services.setAutoCheck(checked) }
                        }
                        SettingRow {
                            title: "Inkleaf"; help: "Version " + root.services.version
                            RowLayout {
                                spacing: 8
                                UiButton { text: "What's new"; kind: "ghost"; onClicked: { root.services.showWhatsNew(); root.close() } }
                                UiButton { text: "Check now"; onClicked: root.services.checkForUpdates(true) }
                            }
                        }
                    }
                }
            }
        }
    }
}
