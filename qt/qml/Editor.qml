// The editor (src/editor/editor.ts): the header with the tool trays, the page
// sidebar, the pages, and the floating tool drawer, selection bar, text
// editor and page controls.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Inkleaf
import "editor"

Rectangle {
    id: root
    color: Theme.desk
    property string notebookId
    property string title
    signal back()
    signal settingsRequested()
    signal notify(string message, bool error)

    ToolPrefs { id: prefs }

    readonly property var tools: [
        { id: "pen", label: "Pen", key: "P", tray: 0, icon: "pen" },
        { id: "highlighter", label: "Highlighter", key: "H", tray: 0, icon: "highlighter" },
        { id: "eraser", label: "Eraser", key: "E", tray: 0, icon: "eraser" },
        { id: "lasso", label: "Lasso select", key: "L", tray: 1, icon: "lasso" },
        { id: "shape", label: "Shape", key: "S", tray: 1, icon: "shape" },
        { id: "text", label: "Text box", key: "T", tray: 1, icon: "text" },
        { id: "sticky", label: "Sticky note", key: "N", tray: 1, icon: "sticky" },
        { id: "laser", label: "Laser pointer", key: "K", tray: 2, icon: "laser" },
        { id: "hand", label: "Pan", key: "", tray: 2, icon: "hand" }
    ]
    property bool drawerOpen: prefs.store.drawerPinned
    property bool sidebarShown: prefs.store.sidebar
    property bool focusMode: false
    property bool tipShown: false

    function pick(tool) {
        if (canvas.tool === tool) {
            drawerOpen = !drawerOpen
            return
        }
        canvas.tool = tool
        prefs.store.tool = tool
        if (!prefs.store.drawerPinned) drawerOpen = false
        if (!tipShown && !prefs.store.drawerPinned && tool !== "hand") {
            tipShown = true
            root.notify("Tip: tap the selected tool again for its colours and sizes", false)
        }
    }
    function toggleSidebar() {
        prefs.store.sidebar = !prefs.store.sidebar
    }

    FileDialog {
        id: imageDialog
        title: "Insert image"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp)"]
        onAccepted: canvas.insertImage(selectedFile.toString())
    }
    PopupMenu { id: menu }
    PaperPicker { id: paperPicker; canvas: canvas }
    CoverPicker { id: coverPicker; canvas: canvas; notebookId: root.notebookId; title: root.title }
    FormDialog { id: dialog }
    Daftar { id: daftarUi; dialog: dialog; onNotify: (m, e) => root.notify(m, e) }

    /** Saves everything (before a sync or a backup); returns this notebook's id. */
    function saveForSync() {
        canvas.finishEdit()
        canvas.flush()
        return notebookId
    }

    // --- daftar annossous --------------------------------------------------------
    /// The daftar's details when this notebook is one (else null).
    property var daftar: null
    function loadDaftar() { daftar = notebookId ? Js.run("daftarQt.info", [notebookId]) : null }
    onNotebookIdChanged: loadDaftar()
    Component.onCompleted: loadDaftar()
    readonly property string daftarClass: {
        if (!daftar) return ""
        const s = (canvas.pages[canvas.currentPage] || {}).section
        return s && daftar.classes.indexOf(s) >= 0 ? s : (daftar.classes[0] || "")
    }
    function customOutlines() {
        try {
            const v = JSON.parse(prefs.store.daftarOutlines || "[]")
            return Array.isArray(v) ? v.filter(o => o && typeof o.name === "string" && Array.isArray(o.lines)) : []
        } catch (e) { return [] }
    }
    function newLesson() {
        const holiday = Js.run("daftarQt.holidayToday", [daftar])
        const go = () => canvas.daftar("newLesson", [root.notebookId, root.daftarClass])
        if (holiday) dialog.ask("Today is a holiday · عطلة", [], "Write the lesson", go, { message: holiday + ". Write a lesson for " + daftarClass + " anyway?" })
        else go()
    }
    function insertOutline(lines) { canvas.daftar("insertOutline", [daftar, lines]) }
    function outlineMenu(anchor) {
        const mine = customOutlines()
        const builtIn = Js.run("daftarQt.OUTLINES.slice", [0]) || []
        menu.show(anchor, builtIn.concat(mine).map(o => ({ label: o.name, action: () => root.insertOutline(o.lines) })).concat([
            { label: "Write my own outline…", action: () => daftarUi.askOutline(o => {
                prefs.store.daftarOutlines = JSON.stringify(root.customOutlines().concat([o]).slice(-12))
                root.insertOutline(o.lines)
            }) }
        ]).concat(mine.length ? [{ label: "Remove my outlines", danger: true, action: () => { prefs.store.daftarOutlines = "[]"; root.notify("Your outlines were removed", false) } }] : []))
    }
    function daftarMenu(anchor) {
        menu.show(anchor, [
            { label: "لائحة التلاميذ · Paste the student list…", action: () => daftarUi.askStudents(daftar, daftarClass, (cls, text) => canvas.daftar("pasteStudents", [root.notebookId, cls, text])) },
            { label: "تصدير القسم · Export this class as PDF…", action: () => daftarUi.askExport(daftar, daftarClass, (cls, term) => {
                canvas.finishEdit()
                canvas.flush()
                const r = canvas.daftarQuery("exportPages", [daftar, cls, term])
                if (!r) return root.notify("No lessons in that period for this class", true)
                exporter.runPages(r.pages, r.name)
            }) },
            { label: "إعدادات الدفتر · Daftar settings…", action: () => {
                canvas.finishEdit()
                canvas.flush()
                daftarUi.askSettings({ id: root.notebookId, title: root.title }, daftar, () => {
                    root.loadDaftar()
                    canvas.reloadPages(true)
                    root.notify("Daftar updated", false)
                })
            } }
        ], 300)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- header ---------------------------------------------------------------
        Rectangle {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: root.focusMode ? 0 : 58
            visible: !root.focusMode
            z: 5
            color: Theme.surface
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 16

                // Left: back, pages, title, page chip.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 2
                    IconButton { iconName: "back"; tip: "Library"; implicitWidth: 36; implicitHeight: 36; onClicked: { canvas.finishEdit(); canvas.flush(); root.back() } }
                    IconButton { iconName: "sidebar"; tip: "Pages sidebar"; implicitWidth: 36; implicitHeight: 36; iconColor: Theme.muted; onClicked: root.toggleSidebar() }
                    AbstractButton {
                        id: titleBtn
                        // In a daftar the bar beside it needs room: the title gives way (to 5.5em).
                        Layout.maximumWidth: root.daftar ? Math.min(implicitWidth, 260) : 260
                        Layout.fillWidth: !!root.daftar
                        Layout.minimumWidth: root.daftar ? 82 : 0
                        implicitHeight: 32
                        implicitWidth: titleLabel.implicitWidth + 16
                        hoverEnabled: true
                        onClicked: dialog.ask("Rename notebook", [{ name: "title", label: "Title", value: root.title }], "Save", r => {
                            try {
                                InkleafCore.call("renameNotebook", { id: root.notebookId, title: r.title.trim() })
                                root.title = r.title.trim()
                            } catch (e) { root.notify(String(e.message || e), true) }
                        })
                        background: Rectangle { radius: 8; color: titleBtn.hovered ? Theme.surface2 : "transparent" }
                        contentItem: Label { id: titleLabel; text: root.title; font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight; horizontalAlignment: Text.AlignLeft; leftPadding: 8; rightPadding: 8; verticalAlignment: Text.AlignVCenter }
                    }
                    // A daftar's class (switches section), New lesson, outline and its menu.
                    RowLayout {
                        visible: !!root.daftar
                        Layout.leftMargin: 6
                        spacing: 6
                        AbstractButton {
                            id: classChip
                            implicitHeight: 32
                            implicitWidth: classRow.implicitWidth + 18
                            hoverEnabled: true
                            ToolTip.visible: hovered
                            ToolTip.text: "Switch class · القسم"
                            ToolTip.delay: 500
                            onClicked: menu.show(classChip, (root.daftar ? root.daftar.classes : []).map(c => ({ label: c, checked: c === root.daftarClass, action: () => canvas.daftar("goToClass", [c]) })))
                            background: Rectangle { radius: 10; color: classChip.hovered ? Theme.surface2 : Theme.surface; border.color: Theme.border }
                            contentItem: Item {
                                RowLayout {
                                    id: classRow
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 10
                                    spacing: 6
                                    Icon { name: "book"; size: 15; color: Theme.accent }
                                    Label { text: root.daftarClass || "—"; font.pixelSize: 13; font.weight: 650; color: Theme.text }
                                    Icon { name: "down"; size: 13; color: Theme.muted }
                                }
                            }
                        }
                        AbstractButton {
                            id: lessonBtn
                            implicitHeight: 32
                            implicitWidth: lessonRow.implicitWidth + 24
                            hoverEnabled: true
                            ToolTip.visible: hovered
                            ToolTip.text: "Write today's date, the class and the lesson time on the next free line"
                            ToolTip.delay: 500
                            onClicked: root.newLesson()
                            background: Rectangle { radius: 10; color: Theme.accentSoft; Rectangle { anchors.fill: parent; radius: 10; color: "black"; opacity: lessonBtn.hovered ? 0.03 : 0 } }
                            contentItem: Item {
                                RowLayout {
                                    id: lessonRow
                                    anchors.centerIn: parent
                                    spacing: 4
                                    Icon { name: "plus"; size: 16; color: Theme.accent }
                                    Label { text: "حصة جديدة"; font.pixelSize: 13; font.weight: 650; color: Theme.accent }
                                    Label { visible: root.width > 1500; text: " · New lesson"; font.pixelSize: 13; font.weight: 650; color: Theme.accent }
                                }
                            }
                        }
                        IconButton { id: outlineBtn; iconName: "page"; tip: "Write a lesson outline (النشاط 1، النشاط 2…) on the lesson's lines"; implicitWidth: 34; implicitHeight: 34; onClicked: root.outlineMenu(outlineBtn) }
                        IconButton { id: daftarMore; iconName: "more"; tip: "Student list, class export, daftar settings"; implicitWidth: 34; implicitHeight: 34; onClicked: root.daftarMenu(daftarMore) }
                    }
                    Rectangle {
                        visible: root.width > 1100 && !root.daftar
                        implicitHeight: 20
                        implicitWidth: chipLabel.implicitWidth + 18
                        radius: 10
                        color: Theme.surface2
                        Label { id: chipLabel; anchors.centerIn: parent; text: "Page " + (canvas.currentPage + 1) + " of " + canvas.pageCount; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted }
                    }
                    Item { Layout.fillWidth: true }
                }

                // Middle: the tool trays.
                RowLayout {
                    id: trays
                    spacing: 10
                    Repeater {
                        model: 3
                        delegate: Rectangle {
                            id: tray
                            required property int index
                            implicitWidth: trayRow.implicitWidth + 8
                            implicitHeight: 46
                            radius: 14
                            color: Theme.surface2
                            RowLayout {
                                id: trayRow
                                anchors.centerIn: parent
                                spacing: 2
                                Repeater {
                                    model: root.tools.filter(t => t.tray === tray.index)
                                    delegate: ToolBtn {
                                        required property var modelData
                                        objectName: "tool-" + modelData.id
                                        iconName: modelData.icon
                                        active: canvas.tool === modelData.id
                                        ink: modelData.id === "sticky" || modelData.id === "lasso" || modelData.id === "hand" || modelData.id === "eraser" ? "" : (canvas.colors[modelData.id] || "")
                                        tip: active ? modelData.label + " — tap again to " + (root.drawerOpen ? "hide" : "show") + " its options" : modelData.label + (modelData.key ? " (" + modelData.key + ")" : "")
                                        onClicked: root.pick(modelData.id)
                                        onActiveChanged: if (active) drawer.anchorItem = this
                                        Component.onCompleted: if (active) drawer.anchorItem = this
                                    }
                                }
                            }
                        }
                    }
                }

                // Right: history, page, export, more.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    Pill {
                        IconButton { iconName: "undo"; tip: "Undo (Ctrl+Z)"; implicitWidth: 34; implicitHeight: 34; enabled: canvas.canUndo; onClicked: canvas.undo() }
                        IconButton { iconName: "redo"; tip: "Redo (Ctrl+Shift+Z)"; implicitWidth: 34; implicitHeight: 34; enabled: canvas.canRedo; onClicked: canvas.redo() }
                    }
                    Pill {
                        AbstractButton {
                            id: paperBtn
                            readonly property var meta: canvas.pages[canvas.currentPage] || ({})
                            implicitHeight: 34
                            implicitWidth: paperRow.implicitWidth + 10
                            hoverEnabled: true
                            ToolTip.visible: hovered && !paperPicker.opened
                            ToolTip.text: cover ? "Cover design and colour" : "Page paper"
                            readonly property bool cover: String(meta.template || "").startsWith("cover")
                            onClicked: {
                                if (paperPicker.opened || coverPicker.opened) { paperPicker.close(); coverPicker.close(); return }
                                if (cover) coverPicker.show(paperBtn)
                                else paperPicker.show(paperBtn, canvas.currentPage)
                            }
                            background: Rectangle { radius: 10; color: paperBtn.hovered || paperPicker.opened ? Theme.surface : "transparent" }
                            contentItem: RowLayout {
                                id: paperRow
                                spacing: 6
                                Rectangle {
                                    Layout.preferredWidth: 22; Layout.preferredHeight: 28; Layout.leftMargin: 4
                                    radius: 3
                                    color: Theme.border
                                    Image {
                                        anchors.fill: parent; anchors.margins: 1
                                        sourceSize: Qt.size(44, 56)
                                        source: "image://paper/" + (paperBtn.meta.template || "blank") + "/" + (paperBtn.meta.paperColor || "#fffdf7").replace("#", "") + "/whole"
                                    }
                                }
                                Label {
                                    visible: root.width > 1320
                                    text: {
                                        const id = paperBtn.meta.template || "blank"
                                        return String(id).startsWith("cover") ? "Cover" : (paperPicker.all.find(x => x.id === id) || { label: id }).label
                                    }
                                    color: Theme.text; font.weight: Font.Medium
                                }
                                Icon { name: "down"; size: 14; color: Theme.muted; Layout.rightMargin: 4 }
                            }
                        }
                        IconButton { iconName: "image"; tip: "Insert image"; implicitWidth: 34; implicitHeight: 34; onClicked: imageDialog.open() }
                        IconButton {
                            id: rulerBtn
                            iconName: "ruler"; tip: "Ruler, protractor, set squares (R)"; implicitWidth: 34; implicitHeight: 34
                            readonly property bool on: !!canvas.instrument.visible
                            active: on; activeColor: Theme.accentSoft; iconColor: on ? Theme.accent : Theme.text
                            onClicked: menu.show(rulerBtn, [
                                { label: "Ruler", checked: on && canvas.instrument.kind === "ruler", action: () => canvas.showInstrument("ruler") },
                                { label: "Protractor", checked: on && canvas.instrument.kind === "protractor", action: () => canvas.showInstrument("protractor") },
                                { label: "Set square 45°", checked: on && canvas.instrument.kind === "square45", action: () => canvas.showInstrument("square45") },
                                { label: "Set square 30°/60°", checked: on && canvas.instrument.kind === "square60", action: () => canvas.showInstrument("square60") }
                            ].concat(on ? [{ label: "Hide", action: () => canvas.showInstrument("") }] : []))
                        }
                        IconButton {
                            iconName: "stylus"
                            tip: AppSettings.store.penOnly ? "Pen-only mode is on: touch and mouse scroll" : "Pen-only mode"
                            implicitWidth: 34; implicitHeight: 34
                            active: AppSettings.store.penOnly
                            activeColor: Theme.accentSoft
                            iconColor: AppSettings.store.penOnly ? Theme.accent : Theme.muted
                            onClicked: AppSettings.store.penOnly = !AppSettings.store.penOnly
                        }
                    }
                    AbstractButton {
                        id: exportBtn
                        implicitHeight: 36
                        implicitWidth: exportRow.implicitWidth + 24
                        hoverEnabled: true
                        ToolTip.visible: hovered
                        ToolTip.text: "Export as PDF, PowerPoint or image"
                        scale: pressed ? 0.96 : 1
                        onClicked: menu.show(exportBtn, [
                            { label: "PDF — whole notebook", action: () => exporter.run("pdf", "all") },
                            { label: "PDF — this page", action: () => exporter.run("pdf", "current") },
                            { label: "PowerPoint (.pptx) — whole notebook", action: () => exporter.run("pptx", "all") },
                            { label: "PowerPoint (.pptx) — this page", action: () => exporter.run("pptx", "current") },
                            { label: "PNG image — this page", action: () => exporter.run("png", "current") }
                        ])
                        background: Rectangle { radius: 12; color: exportBtn.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent }
                        contentItem: Item {
                            RowLayout {
                                id: exportRow
                                anchors.centerIn: parent
                                spacing: 6
                                Icon { name: "download"; size: 16; color: Theme.dark ? "#0d1f17" : "#ffffff" }
                                Label { visible: root.width > 1320; text: "Export"; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.dark ? "#0d1f17" : "#ffffff" }
                            }
                        }
                    }
                    IconButton {
                        id: moreBtn
                        iconName: "more"; tip: "More"; implicitWidth: 36; implicitHeight: 36
                        onClicked: menu.show(moreBtn, [
                            { label: "Focus mode", key: "F", action: () => root.focusMode = !root.focusMode },
                            { label: root.sidebarShown ? "Hide pages" : "Show pages", key: "[", action: () => root.toggleSidebar() },
                            { label: "Settings…", action: () => root.settingsRequested() },
                            { label: "Keyboard shortcuts", key: "?", action: () => shortcuts.open() },
                            { label: Theme.dark ? "Light mode" : "Dark mode", action: () => AppSettings.store.theme = Theme.dark ? "light" : "dark" }
                        ])
                    }
                }
            }
        }

        // --- body: pages sidebar and the pages -------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            PageSidebar {
                canvas: canvas
                daftar: !!root.daftar
                onAddDaftarPage: { if (canvas.daftar("addClassPage", [root.notebookId]).plain) canvas.addPage(-1, "") }
                Layout.fillHeight: true
                Layout.preferredWidth: root.sidebarShown && !root.focusMode ? 168 : 0
                visible: Layout.preferredWidth > 0
                Behavior on Layout.preferredWidth { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
            }
            Item {
                id: area
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                InkCanvas {
                    id: canvas
                    anchors.fill: parent
                    notebookId: root.notebookId
                    tool: prefs.store.tool
                    penStyle: prefs.store.penStyle
                    colors: prefs.colors
                    widths: prefs.widths
                    shapeKind: prefs.store.shapeKind
                    eraserMode: prefs.store.eraserMode
                    laserMode: prefs.store.laserMode
                    textFont: prefs.store.textFont
                    smoothing: AppSettings.streamline
                    penOnly: AppSettings.store.penOnly
                    pressureCurve: AppSettings.store.pressure
                    eraserEnd: AppSettings.store.eraserEnd
                    sideButton: AppSettings.store.sideButton
                    palmRejection: AppSettings.store.palmRejection
                    penCursor: AppSettings.store.penCursor
                    property bool penSeen: false
                    onPenUsed: if (!AppSettings.store.penOnly && !penSeen) { penSeen = true; AppSettings.store.penOnly = true }
                    onMessage: (text, error) => root.notify(text, error)
                    onToolChanged: if (tool !== prefs.store.tool) prefs.store.tool = tool
                }

                Instrument { anchors.fill: parent; canvas: canvas; z: 4 }
                TextEditor { anchors.fill: parent; canvas: canvas; z: 5 }
                SelectionBar { id: selectionBar; canvas: canvas; z: 6 }

                // The active tool's options.
                ToolDrawer {
                    id: drawer
                    canvas: canvas
                    prefs: prefs
                    visible: root.drawerOpen && !root.focusMode
                    z: 9
                    y: 14
                    x: {
                        if (!anchorItem) return (area.width - width) / 2
                        const c = anchorItem.mapToItem(area, anchorItem.width / 2, 0).x
                        return Math.max(12, Math.min(c - width / 2, area.width - width - 12))
                    }
                    Behavior on x { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                }

                // Page and zoom controls.
                Item {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 16
                    z: 8
                    width: status.implicitWidth + 10
                    height: 40
                    Shadow { anchors.fill: parent; radius: 20; level: 2 }
                    Rectangle { anchors.fill: parent; radius: 20; color: Theme.surface; border.color: Theme.border }
                    RowLayout {
                        id: status
                        anchors.centerIn: parent
                        spacing: 2
                        IconButton { iconName: "up"; tip: "Previous page"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; enabled: canvas.currentPage > 0; onClicked: canvas.goToPage(canvas.currentPage - 1) }
                        Label { text: (canvas.currentPage + 1) + " / " + canvas.pageCount; color: Theme.muted; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.preferredWidth: 46; horizontalAlignment: Text.AlignHCenter }
                        IconButton { iconName: "down"; tip: "Next page"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; enabled: canvas.currentPage < canvas.pageCount - 1; onClicked: canvas.goToPage(canvas.currentPage + 1) }
                        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 16; color: Theme.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                        IconButton { iconName: "zoomOut"; tip: "Zoom out (Ctrl+−)"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; onClicked: canvas.zoomBy(1 / 1.2) }
                        Label { text: Math.round(canvas.zoom * 100) + "%"; color: Theme.muted; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.preferredWidth: 46; horizontalAlignment: Text.AlignHCenter }
                        IconButton { iconName: "zoomIn"; tip: "Zoom in (Ctrl+=)"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; onClicked: canvas.zoomBy(1.2) }
                        IconButton { iconName: "fit"; tip: "Fit width (Ctrl+0)"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; onClicked: canvas.fitWidth() }
                    }
                }

                // Leaving focus mode.
                AbstractButton {
                    visible: root.focusMode
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 14
                    z: 8
                    implicitHeight: 34
                    implicitWidth: focusRow.implicitWidth + 24
                    hoverEnabled: true
                    onClicked: root.focusMode = false
                    background: Item {
                        Shadow { anchors.fill: parent; radius: 17; level: 2 }
                        Rectangle { anchors.fill: parent; radius: 17; color: Theme.surface; border.color: Theme.border }
                    }
                    contentItem: Item {
                        RowLayout {
                            id: focusRow
                            anchors.centerIn: parent
                            spacing: 6
                            Icon { name: "focus"; size: 16; color: Theme.muted }
                            Label { text: "Exit focus"; font.weight: Font.DemiBold; color: Theme.muted }
                        }
                    }
                }
            }
        }
    }

    // Export (PDF, PowerPoint, PNG).
    Exporter { id: exporter; canvas: canvas; title: root.title; onNotify: (m, e) => root.notify(m, e) }
    ShortcutsSheet { id: shortcuts }

    // Keyboard shortcuts (src/editor/editor.ts handleKey).
    readonly property bool typing: !!canvas.editing.id
    Shortcut { sequences: [StandardKey.Undo]; enabled: !root.typing; onActivated: canvas.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z", "Ctrl+Y"]; enabled: !root.typing; onActivated: canvas.redo() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: !root.typing; onActivated: { const n = canvas.copySelection(); if (n) root.notify("Copied " + n + " item" + (n > 1 ? "s" : ""), false) } }
    Shortcut { sequences: [StandardKey.Cut]; enabled: !root.typing; onActivated: canvas.cutSelection() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: !root.typing; onActivated: canvas.paste() }
    Shortcut { sequence: "Ctrl+D"; enabled: !root.typing; onActivated: canvas.duplicateSelection() }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: !root.typing; onActivated: canvas.selectAll() }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; onActivated: canvas.zoomBy(1.2) }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: canvas.zoomBy(1 / 1.2) }
    Shortcut { sequence: "Ctrl+0"; onActivated: canvas.fitWidth() }
    Shortcut { sequences: ["Delete", "Backspace"]; enabled: !root.typing && (canvas.selection.count || 0) > 0; onActivated: canvas.deleteSelection() }
    Shortcut { sequence: "Esc"; enabled: !root.typing; onActivated: { if ((canvas.selection.count || 0) > 0) canvas.clearSelection(); else if (root.focusMode) root.focusMode = false; else root.drawerOpen = prefs.store.drawerPinned } }
    Shortcut { sequences: ["PgDown", "Right"]; enabled: !root.typing; onActivated: canvas.goToPage(canvas.currentPage + 1) }
    Shortcut { sequences: ["PgUp", "Left"]; enabled: !root.typing; onActivated: canvas.goToPage(canvas.currentPage - 1) }
    Shortcut { sequence: "R"; enabled: !root.typing; onActivated: canvas.instrument.visible ? canvas.showInstrument("") : canvas.showInstrument(canvas.instrument.kind || "ruler") }
    Shortcut { sequence: "F"; enabled: !root.typing; onActivated: root.focusMode = !root.focusMode }
    Shortcut { sequence: "["; enabled: !root.typing; onActivated: root.toggleSidebar() }
    Shortcut { sequence: "?"; enabled: !root.typing; onActivated: shortcuts.open() }
    Repeater {
        model: root.tools.filter(t => t.key.length > 0)
        delegate: Item {
            required property var modelData
            Shortcut { sequence: modelData.key; enabled: !root.typing; onActivated: root.pick(modelData.id) }
        }
    }
}
