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
    /// Open another notebook (a link to it was followed).
    signal openOther(string id, string title)
    /// A page link was followed from here: Back returns to it (-1: none).
    property int backPage: -1
    function followLink(link, from) {
        if (link.kind === "url") {
            if (!Qt.openUrlExternally(link.url)) root.notify("Couldn't open " + link.url, true)
        } else if (link.kind === "page") {
            const i = canvas.pageIndexOf(link.page)
            if (i < 0) return root.notify("The linked page was deleted", true)
            backPage = from
            backTimer.restart()
            canvas.goToPage(i)
        } else if (link.kind === "notebook") {
            let nb = null
            try { nb = InkleafCore.call("notebook", { id: link.notebook }) } catch (e) {}
            if (!nb || nb.trashedAt) return root.notify("The linked notebook isn't in your library any more", true)
            saveForSync()
            root.openOther(nb.id, nb.title)
        }
    }
    Timer { id: backTimer; interval: 20000; onTriggered: root.backPage = -1 }

    ToolPrefs { id: prefs }
    /// The tool preferences, for children with a `prefs` property of their own
    /// (`prefs: prefs` there would read their own, still empty).
    readonly property alias toolPrefs: prefs
    readonly property alias mainCanvas: canvas
    /// What kind of document this is ("whiteboard"; "" for a notebook).
    readonly property string docKind: DocKinds.kindOf(notebookId)

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
    /// The tools sit in a rail down the left side (Settings → Appearance), not in the top bar.
    readonly property bool rail: AppSettings.store.toolbar === "rail"

    function pick(tool) {
        if (canvas.tool === tool) {
            drawerOpen = !drawerOpen
            return
        }
        canvas.tool = tool
        prefs.store.tool = tool
        if (rail) railTag.flash()
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
        onAccepted: root.activeCanvas.insertImage(selectedFile.toString())
    }
    PopupMenu { id: menu }
    PresentWindow { id: presenter; canvas: root.mainCanvas; onVisibleChanged: if (!visible) blank = false }
    function togglePresenting() {
        if (presenter.visible) presenter.close()
        else { presenter.start(); root.notify(presenter.second ? "Presenting on the other screen · F5 to stop" : "Presenting in its own window: drag it to the projector · double-click for full screen", false) }
    }
    readonly property PopupMenu mainMenu: menu
    // Picks a colour to add to the side rail (added once it closes).
    ColorPopover {
        id: railColorPop
        property string tool: ""
        property string picked: ""
        onClosed: if (picked) root.toolPrefs.addRailColor(picked)
        /// By the strip's + (beside the rail, under the top bar), once its size is known.
        property Item beside: null
        function place() {
            if (!beside) return
            const p = beside.mapToItem(parent, 0, 0)
            if (root.rail) {
                x = p.x + beside.width + 14
                y = Math.max(8, Math.min(parent.height - height - 8, p.y + beside.height / 2 - height / 2))
            } else {
                x = Math.max(8, Math.min(parent.width - width - 8, p.x + beside.width / 2 - width / 2))
                y = p.y + beside.height + 10
            }
        }
        onHeightChanged: place()
        onOpened: place()
    }
    /// + in the tool strip: pick a colour; it's used now and added for every tool when the picker closes.
    function addRailColor(anchor) {
        const t = canvas.tool
        railColorPop.tool = t
        railColorPop.picked = ""
        railColorPop.show(anchor, toolPrefs.paletteFor(t), toolPrefs.color(t), c => { railColorPop.picked = c; toolPrefs.useColor(railColorPop.tool, c) })
        railColorPop.beside = anchor
        railColorPop.place()
    }
    PaperPicker { id: paperPicker; canvas: root.mainCanvas }
    CoverPicker { id: coverPicker; canvas: root.mainCanvas; notebookId: root.notebookId; title: root.title }
    FormDialog { id: dialog }
    Daftar { id: daftarUi; dialog: dialog; onNotify: (m, e) => root.notify(m, e) }

    /// The active tool button's centre in the page area (the drawer points at it).
    /// The voids are what moves the button: mapToItem alone isn't re-evaluated.
    readonly property real toolCenter: {
        const a = drawer.anchorItem
        if (!a) return area.width / 2
        void root.anchorMoved
        return a.mapToItem(area, a.width / 2, 0).x
    }
    /// The same button's middle, down the page area (the rail's drawer points at it).
    readonly property real toolMiddle: {
        const a = drawer.anchorItem
        if (!a) return area.height / 2
        void root.anchorMoved
        return a.mapToItem(area, 0, a.height / 2).y
    }
    /// Changes whenever the active tool's button, or anything holding it, moves or
    /// resizes (the strip re-centres when its colours come and go), so the drawer
    /// keeps pointing at it.
    property int anchorMoved: 0
    property var anchorChain: []
    function watchAnchor() {
        for (const it of anchorChain) {
            try {
                it.xChanged.disconnect(bumpAnchor); it.yChanged.disconnect(bumpAnchor)
                it.widthChanged.disconnect(bumpAnchor); it.heightChanged.disconnect(bumpAnchor)
            } catch (e) {}  // (an item that has gone)
        }
        const chain = []
        for (let it = drawer.anchorItem; it && it !== root; it = it.parent) chain.push(it)
        for (const it of chain) {
            it.xChanged.connect(bumpAnchor); it.yChanged.connect(bumpAnchor)
            it.widthChanged.connect(bumpAnchor); it.heightChanged.connect(bumpAnchor)
        }
        anchorChain = chain
        bumpAnchor()
    }
    function bumpAnchor() { anchorMoved++ }
    // (Again whenever it opens: a button picked while the notebook was still being
    // laid out may not have had its final place, or all its parents, yet.)
    Connections { target: drawer; function onAnchorItemChanged() { root.watchAnchor() } function onVisibleChanged() { if (drawer.visible) root.watchAnchor() } }
    Connections { target: area; function onWidthChanged() { root.bumpAnchor() } function onHeightChanged() { root.bumpAnchor() } }

    /** Saves everything (before a sync or a backup); returns this notebook's id. */
    function saveForSync() {
        canvas.finishEdit()
        canvas.flush()
        if (sidePane.item) { sidePane.item.canvas.finishEdit(); sidePane.item.canvas.flush() }
        return notebookId
    }

    // --- daftar annossous --------------------------------------------------------
    /// The daftar's details when this notebook is one (else null).
    property var daftar: null
    function loadDaftar() { daftar = notebookId ? DaftarApi.info(notebookId) : null }
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
        const holiday = DaftarApi.holidayToday(daftar)
        const go = () => canvas.daftar("newLesson", [root.notebookId, root.daftarClass])
        if (holiday) dialog.ask("Today is a holiday · عطلة", [], "Write the lesson", go, { message: holiday + ". Write a lesson for " + daftarClass + " anyway?" })
        else go()
    }
    function insertOutline(lines) { canvas.daftar("insertOutline", [daftar, lines]) }
    function outlineMenu(anchor) {
        const mine = customOutlines()
        const builtIn = DaftarApi.outlines()
        menu.show(anchor, [{ header: "LESSON OUTLINE" }].concat(builtIn.concat(mine).map(o => ({ icon: "listNumber", label: o.name, action: () => root.insertOutline(o.lines) }))).concat([
            { separator: true },
            { icon: "pen", label: "Write my own outline…", action: () => daftarUi.askOutline(o => {
                prefs.store.daftarOutlines = JSON.stringify(root.customOutlines().concat([o]).slice(-12))
                root.insertOutline(o.lines)
            }) }
        ]).concat(mine.length ? [{ icon: "trash", label: "Remove my outlines", danger: true, action: () => { prefs.store.daftarOutlines = "[]"; root.notify("Your outlines were removed", false) } }] : []))
    }
    function daftarMenu(anchor) {
        menu.show(anchor, [
            { icon: "listBullet", label: "لائحة التلاميذ · Paste the student list…", action: () => daftarUi.askStudents(daftar, daftarClass, (cls, text) => canvas.daftar("pasteStudents", [root.notebookId, cls, text])) },
            { icon: "pdf", label: "تصدير القسم · Export this class as PDF…", action: () => daftarUi.askExport(daftar, daftarClass, (cls, term) => {
                canvas.finishEdit()
                canvas.flush()
                const r = canvas.daftarQuery("exportPages", [daftar, cls, term])
                if (!r) return root.notify("No lessons in that period for this class", true)
                exporter.runPages(r.pages, r.name)
            }) },
            { separator: true },
            { icon: "gear", label: "إعدادات الدفتر · Daftar settings…", action: () => {
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
                    id: leftBar
                    Layout.fillWidth: true
                    // Both sides ask for the same width (the tools stay centred) and
                    // give way down to their minimums on narrow windows.
                    Layout.preferredWidth: Math.max(Layout.minimumWidth, rightBar.Layout.minimumWidth)
                    // Never narrower than its buttons (with the title at its smallest):
                    // on narrow windows the tools move right instead of covering them.
                    Layout.minimumWidth: implicitWidth - (titleBtn.visible ? titleBtn.implicitWidth - titleBtn.Layout.minimumWidth : 0)
                    spacing: 2
                    IconButton { iconName: "back"; tip: "Library"; implicitWidth: 36; implicitHeight: 36; onClicked: { canvas.finishEdit(); canvas.flush(); if (sidePane.item) { sidePane.item.canvas.finishEdit(); sidePane.item.canvas.flush() } root.back() } }
                    IconButton { iconName: "sidebar"; tip: "Pages sidebar"; implicitWidth: 36; implicitHeight: 36; iconColor: Theme.muted; onClicked: root.toggleSidebar() }
                    IconButton { iconName: "split"; tip: root.split ? "Close the side view" : "Side by side: another page or notebook beside this one"; active: root.split; implicitWidth: 36; implicitHeight: 36; iconColor: root.split ? Theme.accentContrast : Theme.muted; onClicked: root.toggleSplit() }
                    AbstractButton {
                        id: titleBtn
                        // In a daftar the bar beside it needs room: the title gives way (to 5.5em),
                        // and on narrow windows goes (the cover and the library show it).
                        visible: !root.daftar || root.width >= 1360
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
                        background: Rectangle { antialiasing: true; radius: Theme.radius; color: titleBtn.hovered ? Theme.surface2 : "transparent" }
                        contentItem: Label { id: titleLabel; text: root.title; font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight; horizontalAlignment: Text.AlignLeft; leftPadding: 8; rightPadding: 8; verticalAlignment: Text.AlignVCenter }
                    }
                    // A daftar's class (switches section), New lesson, outline and its menu.
                    Pill {
                        visible: !!root.daftar
                        Layout.leftMargin: 6
                        AbstractButton {
                            id: classChip
                            implicitHeight: 34
                            implicitWidth: classRow.implicitWidth + 18
                            hoverEnabled: true
                            ToolTip.visible: !!(hovered) && !tipDismissed
                            // Tooltips go away once clicked (until the pointer leaves), as in the web app.
                            property bool tipDismissed: false
                            onPressedChanged: if (pressed) tipDismissed = true
                            onHoveredChanged: if (!hovered) tipDismissed = false
                            ToolTip.text: "Switch class · القسم"
                            ToolTip.delay: 500
                            onClicked: menu.show(classChip, (root.daftar ? root.daftar.classes : []).map(c => ({ label: c, checked: c === root.daftarClass, action: () => canvas.daftar("goToClass", [c]) })))
                            background: Rectangle { antialiasing: true; radius: Theme.radius; color: classChip.hovered ? Theme.surface3 : "transparent" }
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
                            implicitHeight: 34
                            implicitWidth: lessonRow.implicitWidth + 24
                            hoverEnabled: true
                            ToolTip.visible: !!(hovered) && !tipDismissed
                            // Tooltips go away once clicked (until the pointer leaves), as in the web app.
                            property bool tipDismissed: false
                            onPressedChanged: if (pressed) tipDismissed = true
                            onHoveredChanged: if (!hovered) tipDismissed = false
                            ToolTip.text: "Write today's date, the class and the lesson time on the next free line"
                            ToolTip.delay: 500
                            onClicked: root.newLesson()
                            background: Rectangle { antialiasing: true; radius: Theme.radius; color: Theme.accentSoft; Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radius; color: "black"; opacity: lessonBtn.hovered ? 0.03 : 0 } }
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
                        IconButton { id: outlineBtn; iconName: "listNumber"; tip: "Lesson outline (النشاط 1، النشاط 2…) on the lesson's lines"; implicitWidth: 34; implicitHeight: 34; iconSize: 18; onClicked: root.outlineMenu(outlineBtn) }
                        IconButton { id: daftarMore; iconName: "more"; tip: "Student list, class export, daftar settings"; implicitWidth: 34; implicitHeight: 34; iconSize: 18; onClicked: root.daftarMenu(daftarMore) }
                    }
                    Rectangle { antialiasing: true;
                        visible: !!(root.width > (root.rail ? 1100 : 1440) && !root.daftar)
                        implicitHeight: 20
                        implicitWidth: chipLabel.implicitWidth + 18
                        radius: Theme.radius
                        color: Theme.surface2
                        Label { id: chipLabel; anchors.centerIn: parent; text: "Page " + (canvas.currentPage + 1) + " of " + canvas.pageCount; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted }
                    }
                    Item { Layout.fillWidth: true }
                }

                // Middle: the tools (or nothing, with them in the side rail).
                ToolStrip {
                    id: trays
                    visible: !root.rail
                    vertical: false
                    canvas: root.mainCanvas
                    prefs: root.toolPrefs
                    tools: root.tools
                    menu: root.mainMenu
                    drawerOpen: root.drawerOpen
                    onPicked: id => root.pick(id)
                    onActiveButton: item => { if (!root.rail) drawer.anchorItem = item }
                    onAddColor: anchor => root.addRailColor(anchor)
                }

                // Right: history, page, export, more.
                RowLayout {
                    id: rightBar
                    Layout.fillWidth: true
                    Layout.preferredWidth: leftBar.Layout.preferredWidth
                    Layout.minimumWidth: implicitWidth
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    Pill {
                        IconButton { iconName: "search"; tip: "Find in this notebook (Ctrl+F)"; implicitWidth: 34; implicitHeight: 34; active: finder.active; onClicked: finder.active ? finder.close() : finder.open() }
                        IconButton { iconName: "sparkle"; tip: "Assistant: ask, or have it make things"; implicitWidth: 34; implicitHeight: 34; iconColor: Theme.accent; onClicked: assistant.opened ? assistant.close() : assistant.show({ id: root.notebookId, title: root.title, page: canvas.currentPage, daftar: !!root.daftar }) }
                    }
                    Pill {
                        IconButton { iconName: "undo"; tip: "Undo (Ctrl+Z)"; implicitWidth: 34; implicitHeight: 34; enabled: root.activeCanvas.canUndo; onClicked: root.activeCanvas.undo() }
                        IconButton { iconName: "redo"; tip: "Redo (Ctrl+Shift+Z)"; implicitWidth: 34; implicitHeight: 34; enabled: root.activeCanvas.canRedo; onClicked: root.activeCanvas.redo() }
                    }
                    Pill {
                        AbstractButton {
                            id: paperBtn
                            readonly property var meta: canvas.pages[canvas.currentPage] || ({})
                            implicitHeight: 34
                            implicitWidth: paperRow.implicitWidth + 10
                            hoverEnabled: true
                            ToolTip.visible: !!(!!(hovered && !paperPicker.opened)) && !tipDismissed
                            // Tooltips go away once clicked (until the pointer leaves), as in the web app.
                            property bool tipDismissed: false
                            onPressedChanged: if (pressed) tipDismissed = true
                            onHoveredChanged: if (!hovered) tipDismissed = false
                            ToolTip.text: cover ? "Cover design and colour" : "Page paper: " + (paperPicker.all.find(x => x.id === (meta.template || "blank")) || { label: meta.template || "blank" }).label
                            readonly property bool cover: String(meta.template || "").startsWith("cover")
                            onClicked: {
                                if (paperPicker.opened || coverPicker.opened) { paperPicker.close(); coverPicker.close(); return }
                                if (cover) coverPicker.show(paperBtn)
                                else paperPicker.show(paperBtn, canvas.currentPage)
                            }
                            background: Rectangle { antialiasing: true; radius: Theme.radius; color: paperBtn.hovered || paperPicker.opened || coverPicker.opened ? Theme.surface3 : "transparent"; Behavior on color { ColorAnimation { duration: 120 } } }
                            contentItem: RowLayout {
                                id: paperRow
                                spacing: 6
                                Rectangle { antialiasing: true;
                                    Layout.preferredWidth: 22; Layout.preferredHeight: 28; Layout.leftMargin: 4
                                    radius: 3
                                    color: Theme.border
                                    Image {
                                        anchors.fill: parent; anchors.margins: 1
                                        sourceSize: Qt.size(44, 56)
                                        source: "image://paper/" + (paperBtn.meta.template || "blank") + "/" + (paperBtn.meta.paperColor || "#fffdf7").replace("#", "") + "/whole"
                                    }
                                }
                                Icon { name: "down"; size: 14; color: Theme.muted; Layout.rightMargin: 4 }
                            }
                        }
                    }
                    // Things to put on the page, in one menu.
                    Pill {
                        AbstractButton {
                            id: insertBtn
                            readonly property bool open: menu.opened && menu.anchorItem === insertBtn
                            implicitHeight: 34
                            implicitWidth: insertRow.implicitWidth + 16
                            hoverEnabled: true
                            ToolTip.visible: !!(hovered && !open) && !tipDismissed
                            ToolTip.delay: 500
                            property bool tipDismissed: false
                            onPressedChanged: if (pressed) tipDismissed = true
                            onHoveredChanged: if (!hovered) tipDismissed = false
                            ToolTip.text: "Insert a picture, a table, a graph, one of your pieces or a page"
                            onClicked: menu.show(insertBtn, [
                                { icon: "image", label: "Image", sub: "A picture from your files", action: () => imageDialog.open() },
                                { icon: "table", label: "Table", sub: "Rows and columns to type or write in", action: () => tablePicker.show(insertBtn) },
                            { icon: "graph", label: "Graph", sub: "Plot functions, as you'd write them", action: () => graphDialog.show(null, false) },
                                { icon: "stack", label: "My pieces", sub: "Something you saved to use again", action: () => piecesPanel.show(insertBtn) },
                                { icon: "page", label: "Page", sub: "A new page after this one", action: () => root.activeCanvas.addPage(root.activeCanvas.currentPage + 1, "") },
                                root.activeCanvas.contentsPageIndex() >= 0
                                    ? { icon: "contents", label: "Update the contents page", sub: "Its headings and page numbers, as they are now", action: () => { root.activeCanvas.insertContentsPage(); root.notify("Contents page updated", false) } }
                                    : { icon: "contents", label: "Contents page", sub: "Your headings and chapters, each a link", action: () => root.activeCanvas.insertContentsPage() }
                            ])
                            background: Rectangle {
                                antialiasing: true
                                radius: Theme.radius
                                color: insertBtn.hovered || insertBtn.open ? Theme.surface3 : "transparent"
                                Behavior on color { ColorAnimation { duration: 120 } }
                            }
                            contentItem: Item {
                                RowLayout {
                                    id: insertRow
                                    anchors.centerIn: parent
                                    spacing: 5
                                    Icon { name: "plus"; size: 18; color: Theme.text }
                                    Label { visible: root.width > 1320 || !root.daftar; text: "Insert"; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text }
                                    Icon { name: "down"; size: 14; color: Theme.muted }
                                }
                            }
                        }
                    }
                    AbstractButton {
                        id: exportBtn
                        implicitHeight: 40
                        implicitWidth: exportRow.implicitWidth + 24
                        hoverEnabled: true
                        ToolTip.visible: !!(hovered) && !tipDismissed
                        // Tooltips go away once clicked (until the pointer leaves), as in the web app.
                        property bool tipDismissed: false
                        onPressedChanged: if (pressed) tipDismissed = true
                        onHoveredChanged: if (!hovered) tipDismissed = false
                        ToolTip.text: "Export as PDF, PowerPoint or image"
                        scale: pressed ? 0.96 : 1
                        onClicked: menu.show(exportBtn, [
                            { header: "WHOLE NOTEBOOK" },
                            { icon: "pdf", label: "PDF", action: () => exporter.run("pdf", "all") },
                            { icon: "slides", label: "PowerPoint (.pptx)", action: () => exporter.run("pptx", "all") },
                            { separator: true },
                            { header: "THIS PAGE" },
                            { icon: "pdf", label: "PDF", action: () => exporter.run("pdf", "current") },
                            { icon: "slides", label: "PowerPoint (.pptx)", action: () => exporter.run("pptx", "current") },
                            { icon: "image", label: "PNG image", action: () => exporter.run("png", "current") }
                        ])
                        background: Rectangle { antialiasing: true; radius: Theme.radius; color: exportBtn.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent }
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
                    Pill {
                        IconButton {
                            id: moreBtn
                            iconName: "more"; tip: "More"; implicitWidth: 34; implicitHeight: 34
                            onClicked: menu.show(moreBtn, [
                                { icon: "stylus", label: "Pen-only mode", toggle: AppSettings.store.penOnly, action: () => AppSettings.store.penOnly = !AppSettings.store.penOnly },
                                { icon: "focus", label: "Focus mode", key: "F", toggle: root.focusMode, action: () => root.focusMode = !root.focusMode },
                                { icon: "zoombox", label: "Zoom box", key: "Z", toggle: zoomBox.active, action: () => zoomBox.active ? zoomBox.stop() : zoomBox.start() },
                                { icon: "present", label: "Present", key: "F5", toggle: presenter.visible, action: () => root.togglePresenting() },
                                { icon: "spell", label: "Check spelling on this page", action: () => spell.check(root.mainCanvas.currentPage, false) },
                                { icon: "sidebar", label: "Tools on the side", toggle: root.rail, action: () => AppSettings.store.toolbar = root.rail ? "top" : "rail" },
                                { separator: true },
                                { icon: "sidebar", label: root.sidebarShown ? "Hide pages" : "Show pages", key: "[", action: () => root.toggleSidebar() },
                                { icon: Theme.dark ? "sun" : "moon", label: Theme.dark ? "Light mode" : "Dark mode", action: () => AppSettings.store.theme = Theme.dark ? "light" : "dark" },
                                { separator: true },
                                { icon: "keyboard", label: "Shortcuts and gestures", key: "?", action: () => shortcuts.open() },
                                { icon: "gear", label: "Settings…", action: () => root.settingsRequested() }
                                ])
                        }
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
                id: pageSidebar
                canvas: root.mainCanvas
                daftar: !!root.daftar
                onAddDaftarPage: { if (canvas.daftar("addClassPage", [root.notebookId]).plain) canvas.addPage(-1, "") }
                Layout.fillHeight: true
                Layout.preferredWidth: root.sidebarShown && !root.focusMode ? pageSidebar.wantWidth : 0
                visible: Layout.preferredWidth > 0
                Behavior on Layout.preferredWidth { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
            }
            // The side rail's own column, between the pages and the page area.
            Item {
                id: railSlot
                Layout.fillHeight: true
                Layout.preferredWidth: root.rail && !root.focusMode ? 74 : 0
                visible: Layout.preferredWidth > 0
                z: 6
                    // The tools down the left side (Settings → Appearance → Tools: Side rail).
                    ToolStrip {
                        id: toolRail
                        visible: root.rail && !root.focusMode
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        vertical: true
                        canvas: root.mainCanvas
                        prefs: root.toolPrefs
                        tools: root.tools
                        menu: root.mainMenu
                        drawerOpen: root.drawerOpen
                        onPicked: id => root.pick(id)
                        onActiveButton: item => { if (root.rail) drawer.anchorItem = item }
                        onAddColor: anchor => root.addRailColor(anchor)
                    }
            }
            Item {
                id: area
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                InkCanvas {
                    id: canvas
                    anchors.fill: parent
                    // Smooth ink edges: the canvas is drawn 4× multisampled.
                    layer.enabled: true
                    layer.samples: 4
                    notebookId: root.notebookId
                    tool: prefs.store.tool
                    penStyle: prefs.store.penStyle
                    penDash: prefs.store.penDash
                    penGap: prefs.store.penGap
                    colors: prefs.colors
                    widths: prefs.widths
                    shapeKind: prefs.store.shapeKind
                    eraserMode: prefs.store.eraserMode
                    laserMode: prefs.store.laserMode
                    textFont: prefs.store.textFont
                    smoothing: AppSettings.streamline
                    penOnly: AppSettings.store.penOnly
                    dismissOnPress: drawer.visible && drawer.hasOptions && !prefs.store.drawerPinned
                    onDismissed: root.drawerOpen = false
                    pressureCurve: AppSettings.store.pressure
                    eraserEnd: AppSettings.store.eraserEnd
                    sideButton: AppSettings.store.sideButton
                    palmRejection: AppSettings.store.palmRejection
                    scratchOut: AppSettings.store.scratchOut
                    growable: root.docKind === "whiteboard"
                    deskColor: Theme.desk
                    fingerTaps: AppSettings.store.fingerTaps
                    onScratchedOut: scratchNotice.show(this)
                    onFingerTapped: redo => Notice.post(redo ? "Redone" : "Undone", false, { icon: redo ? "redo" : "undo" })
                    penCursor: AppSettings.store.penCursor
                    property bool penSeen: false
                    onPenUsed: if (!AppSettings.store.penOnly && !penSeen) { penSeen = true; AppSettings.store.penOnly = true }
                    onMessage: (text, error) => root.notify(text, error)
                    onToolChanged: if (tool !== prefs.store.tool) prefs.store.tool = tool
                    onActiveFocusChanged: if (activeFocus) root.activeCanvas = canvas
                    onPagesChanged: if (sidePane.item) sidePane.item.mainChanged()
                }

                Instrument { anchors.fill: parent; canvas: root.mainCanvas; z: 4 }
                EraserCursor { anchors.fill: canvas; canvas: root.mainCanvas; z: 6 }
                TextEditor { anchors.fill: parent; canvas: root.mainCanvas; z: 5 }
                TableCellEditor { anchors.fill: parent; canvas: root.mainCanvas; z: 5 }
                TableHandles { anchors.fill: parent; canvas: root.mainCanvas; z: 5 }
                // The laser pointer (src/editor/laser.ts): a soft glow, a saturated
                // tube and a thin white core, each layer drawn once with its own
                // opacity so crossings never darken; and the pointer dot.
                Item {
                    anchors.fill: canvas
                    z: 5
                    enabled: false
                    visible: canvas.tool === "laser" || canvas.laserTrail
                    Item {
                        anchors.fill: parent
                        visible: canvas.laserTrail
                        layer.enabled: visible
                        opacity: 0.28 * canvas.laserColorAlpha
                        LaserTrail { anchors.fill: parent; canvas: root.mainCanvas; radius: Theme.radiusSm; color: canvas.laserColor }
                    }
                    Item {
                        anchors.fill: parent
                        visible: canvas.laserTrail
                        layer.enabled: visible
                        opacity: canvas.laserColorAlpha
                        LaserTrail { anchors.fill: parent; canvas: root.mainCanvas; radius: 3; color: canvas.laserColor }
                    }
                    Item {
                        anchors.fill: parent
                        visible: canvas.laserTrail
                        layer.enabled: visible
                        opacity: canvas.laserCoreAlpha
                        LaserTrail { anchors.fill: parent; canvas: root.mainCanvas; radius: 0.9; color: "white" }
                    }
                    // .laser-dot: 6px, with a grey and a white ring.
                    Item {
                        visible: !!(canvas.tool === "laser" && canvas.laserDot.x >= 0)
                        x: canvas.laserDot.x
                        y: canvas.laserDot.y
                        Rectangle { antialiasing: true; x: -6.5; y: -6.5; width: 13; height: 13; radius: Theme.radiusSm; color: Qt.rgba(1, 1, 1, 0.5) }
                        Rectangle { antialiasing: true; x: -5.5; y: -5.5; width: 11; height: 11; radius: Theme.radiusSm; color: Qt.rgba(120 / 255, 120 / 255, 120 / 255, 0.45) }
                        Rectangle { antialiasing: true; x: -3; y: -3; width: 6; height: 6; radius: 3; color: canvas.laserColor }
                    }
                }

                SelectionBar {
                    id: selectionBar; canvas: root.mainCanvas; z: 6
                    held: cropper.active
                    onAskAi: what => root.askAboutSelection(what)
                    onMathsRequested: mathCard.start()
                    onGraphEditRequested: g => graphDialog.show(g, true)
                    onSavePieceRequested: root.savePiece()
                    onCropRequested: cropper.start()
                    onLinkRequested: linkDialog.show()
                    onSpellRequested: spell.check(root.mainCanvas.activePage, true)
                }
                SpellCheck { id: spell; anchors.fill: parent; z: 5; canvas: root.mainCanvas; visible: !cropper.active; onNotify: (m, e) => root.notify(m, e) }
                LinkDialog { id: linkDialog; canvas: root.mainCanvas; notebookId: root.notebookId }
                LinkLayer {
                    anchors.fill: parent
                    z: 5
                    canvas: root.mainCanvas
                    visible: !cropper.active
                    onFollow: (link, page) => root.followLink(link, page)
                }
                // Back where a page link was followed from.
                AbstractButton {
                    id: backPill
                    z: 8
                    visible: opacity > 0
                    opacity: root.backPage >= 0 ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 180 } }
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: zoomBox.active ? zoomBox.panelHeight + 34 : 22
                    height: 36
                    width: backRow.implicitWidth + 28
                    hoverEnabled: true
                    onClicked: { const p = root.backPage; root.backPage = -1; if (p >= 0) root.mainCanvas.goToPage(p) }
                    background: Item {
                        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
                        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: backPill.hovered ? Theme.accent : Theme.surface; border.color: backPill.hovered ? Theme.accent : Theme.border }
                    }
                    contentItem: Item {
                        Row {
                            id: backRow
                            anchors.centerIn: parent
                            spacing: 6
                            Icon { anchors.verticalCenter: parent.verticalCenter; name: "back"; size: 15; color: backPill.hovered ? Theme.accentContrast : Theme.accent }
                            Label { anchors.verticalCenter: parent.verticalCenter; text: "Back to page " + (root.backPage + 1); font.pixelSize: 13; font.weight: Font.DemiBold; color: backPill.hovered ? Theme.accentContrast : Theme.text }
                        }
                    }
                }
                CropOverlay { id: cropper; canvas: root.mainCanvas; anchors.fill: parent; z: 20 }
                ZoomBox { id: zoomBox; canvas: root.mainCanvas; anchors.fill: parent; z: 7 }
                FindBar { id: finder; canvas: root.mainCanvas; anchors.fill: parent; z: 9 }

                // Presenting: what's on the projector, and Blank / Stop.
                Rectangle {
                    id: presentPill
                    z: 9
                    visible: presenter.visible
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: finder.active ? 70 : 12
                    height: 38
                    width: presentRow.implicitWidth + 16
                    radius: Theme.radiusLg
                    antialiasing: true
                    color: Theme.surface
                    border.color: Theme.border
                    Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3; z: -1 }
                    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
                    RowLayout {
                        id: presentRow
                        anchors.centerIn: parent
                        spacing: 4
                        Rectangle {
                            Layout.leftMargin: 6
                            width: 8; height: 8; radius: 4
                            color: presenter.blank ? Theme.muted : "#ef4444"
                            SequentialAnimation on opacity { running: presenter.visible && !presenter.blank; loops: Animation.Infinite; NumberAnimation { to: 0.35; duration: 900 } NumberAnimation { to: 1; duration: 900 } }
                        }
                        Label {
                            Layout.leftMargin: 4
                            Layout.rightMargin: 6
                            text: presenter.blank ? "Presenting · screen blank" : "Presenting page " + (root.mainCanvas.currentPage + 1)
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                        IconButton { iconName: "blank"; tip: presenter.blank ? "Show the page again (B)" : "Blank the screen (B)"; active: presenter.blank; implicitWidth: 30; implicitHeight: 30; iconSize: 16; onClicked: presenter.blank = !presenter.blank }
                        IconButton { iconName: "close"; tip: "Stop presenting (F5)"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; onClicked: presenter.close() }
                    }
                }

                // What you just picked, beside the rail for a moment ("Fountain pen · 3 px").
                Rectangle {
                    id: railTag
                    z: 8
                    visible: opacity > 0 && toolRail.visible && !drawer.visible
                    opacity: 0
                    Behavior on opacity { NumberAnimation { duration: 180 } }
                    x: 6
                    y: Math.round(Math.max(8, root.toolMiddle - height / 2))
                    height: 32
                    width: tagRow.implicitWidth + 22
                    radius: Theme.radiusSm
                    antialiasing: true
                    color: Theme.dark ? "#f5f5f4" : "#1c1917"
                    function flash() { opacity = 1; tagTimer.restart() }
                    Timer { id: tagTimer; interval: 1400; onTriggered: railTag.opacity = 0 }
                    readonly property string tool: canvas.tool
                    readonly property var nibs: ({ fountain: "Fountain pen", ballpoint: "Ballpoint", pencil: "Pencil", calligraphy: "Calligraphy", brush: "Brush pen", marker: "Marker", watercolor: "Watercolour", neon: "Neon", construction: "Dashed", tape: "Tape" })
                    readonly property string name: tool === "pen" ? (nibs[canvas.penStyle] || "Pen") : ((root.tools.find(t => t.id === tool) || {}).label || "")
                    readonly property var size: canvas.widths[tool]
                    Row {
                        id: tagRow
                        anchors.centerIn: parent
                        spacing: 8
                        Rectangle { visible: !!canvas.colors[railTag.tool] && railTag.tool !== "sticky"; anchors.verticalCenter: parent.verticalCenter; width: 12; height: 12; radius: 3; color: canvas.colors[railTag.tool] || "transparent"; border.width: 1.5; border.color: Theme.dark ? "#1c1917" : "#fafaf9" }
                        Label { text: railTag.name + (railTag.size && railTag.tool !== "text" && railTag.tool !== "sticky" ? " · " + Number(railTag.size).toFixed(railTag.size < 10 && railTag.size % 1 ? 1 : 0) + " px" : ""); font.pixelSize: 13; font.weight: Font.Medium; color: Theme.dark ? "#1c1917" : "#fafaf9" }
                    }
                }

                // The active tool's options.
                ToolDrawer {
                    id: drawer
                    canvas: root.mainCanvas
                    prefs: root.toolPrefs
                    visible: !!(root.drawerOpen && !root.focusMode)
                    z: 9
                    // Under the top bar's tool, or beside the rail's.
                    fromLeft: root.rail
                    y: root.rail ? Math.round(Math.max(12, Math.min(root.toolMiddle - height / 2, area.height - height - 12))) : 14
                    x: root.rail ? 6 : Math.round(Math.max(12, Math.min(root.toolCenter - width / 2, area.width - width - 12)))
                    // The pointer stays on the tool while the drawer slides.
                    pointerX: root.toolCenter - x
                    pointerY: root.toolMiddle - y
                    Behavior on x { enabled: !root.rail; NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                    Behavior on y { enabled: root.rail; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                    // A pen picked from the pen case: out of the way, so the next stroke writes.
                    onPenPicked: if (!root.toolPrefs.store.drawerPinned) root.drawerOpen = false
                }

                // Page and zoom controls (faded out while you write, back a
                // moment after).
                Item {
                    id: zoomBar
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 16
                    // Above the zoom box while it's open.
                    anchors.bottomMargin: zoomBox.active ? zoomBox.panelHeight + 28 : 16
                    z: 8
                    property bool quiet: false
                    opacity: quiet ? 0.12 : 1
                    Behavior on opacity { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                    Timer { id: zoomBack; interval: 900; onTriggered: zoomBar.quiet = false }
                    Connections {
                        target: canvas
                        function onWritingChanged() {
                            if (canvas.writing) { zoomBack.stop(); zoomBar.quiet = true }
                            else zoomBack.restart()
                        }
                    }
                    HoverHandler { onHoveredChanged: if (hovered) { zoomBack.stop(); zoomBar.quiet = false } }
                    width: status.implicitWidth + 10
                    height: 40
                    Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 2 }
                    Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
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
                        IconButton { iconName: "zoombox"; tip: zoomBox.active ? "Close the zoom box" : "Zoom box: write large, it lands small (Z)"; active: zoomBox.active; implicitWidth: 30; implicitHeight: 30; iconSize: 16; onClicked: zoomBox.active ? zoomBox.stop() : zoomBox.start() }
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
                        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 2 }
                        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
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
            // The side view: another page or notebook beside this one.
            Loader {
                id: sidePane
                active: root.split
                visible: active
                Layout.fillHeight: true
                // Half of what's beside the page list; the page takes the rest.
                readonly property real half: Math.max(280, (parent.width - (pageSidebar.visible ? pageSidebar.width : 0)) / 2)
                Layout.preferredWidth: half
                Layout.maximumWidth: half
                sourceComponent: SidePane {
                    prefs: root.toolPrefs
                    mainNotebookId: root.notebookId
                    editor: root
                    onCloseRequested: root.toggleSplit()
                    onNotify: (m, e) => root.notify(m, e)
                }
                onLoaded: item.canvas.activeFocusChanged.connect(() => { if (item && item.canvas.activeFocus) root.activeCanvas = item.canvas })
                onActiveChanged: if (!active) root.activeCanvas = canvas
            }
        }
    }

    // Export (PDF, PowerPoint, PNG).
    Exporter { id: exporter; canvas: root.mainCanvas; title: root.title; onNotify: (m, e) => root.notify(m, e) }
    GraphDialog { id: graphDialog; canvas: root.activeCanvas }
    ScratchNotice { id: scratchNotice }
    /// The side pane's canvas scratched something out.
    function scratched(view) { scratchNotice.show(view) }
    TablePicker { id: tablePicker; onPicked: (rows, cols) => root.activeCanvas.insertTable(rows, cols) }
    PiecesPanel { id: piecesPanel; canvas: root.activeCanvas; onNotify: (m, e) => root.notify(m, e); onSaveRequested: root.savePiece() }
    /// The page view last used: the notebook, or the side view. Tools, the
    /// maths card, graphs, pieces, undo and the clipboard act on it.
    property InkCanvas activeCanvas: canvas
    property bool split: false
    function toggleSplit() {
        if (split && sidePane.item) { sidePane.item.canvas.finishEdit(); sidePane.item.canvas.flush() }
        split = !split
        if (!split) activeCanvas = canvas
        fitMain.restart()
    }
    Timer { id: fitMain; interval: 150; onTriggered: canvas.fitWidth() }
    function openMaths() { mathCard.start() }
    function editGraph(g) { graphDialog.show(g, true) }
    /// The selection into my pieces, under a name.
    function savePiece() {
        const view = root.activeCanvas
        const p = view.selectionPiece(240)
        if (!p.elements) return
        dialog.ask("Save to my pieces", [{ name: "name", label: "Name", value: "" , placeholder: "Triangle, signature, stamp…" }], "Save", r => {
            try {
                InkleafCore.call("saveSnippet", { name: r.name.trim(), data: p.elements, preview: p.preview, width: p.width, height: p.height, notebookId: view.notebookId })
                Notice.post("Saved to your pieces", false, { icon: "stack", actionLabel: "Show", action: () => piecesPanel.show(insertBtn) })
            } catch (e) {
                root.notify(String(e.message || e), true)
            }
        }, { message: "Put it on any page later from Insert → My pieces." })
    }
    MathCard {
        id: mathCard
        canvas: root.activeCanvas
        onNotify: (m, e) => root.notify(m, e)
        onGraphRequested: f => {
            graphDialog.show({ functions: f, xmin: -5, xmax: 5, ymin: -5, ymax: 5, grid: true }, false)
            graphDialog.fitY()
        }
        onStepsRequested: expr => {
            if (!Ai.ready) { root.notify("Set up an AI in Settings → AI to see the steps", true); return }
            aiPanel.start({
                heading: "Steps",
                notebookId: root.notebookId,
                useNotes: false,
                canInsert: true,
                extra: "The user is a maths teacher or student working in their notebook; answer in the language they use, at school level, with clear numbered steps and the final answer at the end.",
                prompt: "Solve step by step: " + expr
            })
        }
    }
    AssistantPanel {
        id: assistant
        onSettingsRequested: root.settingsRequested()
    }
    // A plan the assistant carries out: what's on screen is saved first, and
    // this notebook shows its changes after.
    Connections {
        target: Assistant
        function onAboutToChange() { root.saveForSync() }
        function onNotebooksChanged(ids) {
            if (ids.indexOf(root.notebookId) >= 0 || ids.indexOf("*") >= 0) { canvas.reloadPages(true); root.loadDaftar() }
        }
    }
    AiPanel {
        id: aiPanel
        onSettingsRequested: root.settingsRequested()
        onInsertRequested: text => { root.activeCanvas.insertText(text); root.notify("Added to the page as text", false) }
    }
    /// The page being looked at, as a picture for the AI.
    function pagePicture() {
        const r = canvas.exportPage(canvas.currentPage, 1400 / Math.max(1, (canvas.pages[canvas.currentPage] || { width: 800 }).width || 800), true)
        return r && r.dataUrl ? r.dataUrl : ""
    }
    /// Ask about this notebook (and the page being looked at).
    function askAboutNotebook() {
        const page = canvas.currentPage + 1
        const daftarHelp = root.daftar ? [
            { label: "Plan my next lesson", prompt: "From my daftar (lesson log) so far, plan the next lesson: its objectives, the steps with timings, a class activity, and homework." },
            { label: "Activities for a lesson…", prompt: "Suggest three engaging class activities (with timings and materials) for a lesson on: ", fill: true },
            { label: "Write up this page's lesson", prompt: "Write a short, well-organised lesson summary for the daftar from what's on this page.", attach: () => root.pagePicture() },
            { label: "What did I cover so far?", prompt: "List the lessons I've covered so far in my daftar, by class and date, and point out gaps or what comes next." }
        ] : []
        aiPanel.start({
            heading: "Ask about “" + root.title + "”",
            notebookId: root.notebookId,
            canInsert: true,
            extra: "The user has the notebook “" + root.title + "” open, at page " + page + " of " + canvas.pageCount + "." + (root.daftar ? " It is their daftar annossous (lesson log)." : ""),
            suggestions: daftarHelp.concat([
                { label: "Summarise this page", prompt: "Summarise this page (page " + page + ").", attach: () => root.pagePicture() },
                { label: "Summarise the notebook", prompt: "Summarise this notebook: the main topics and key points." },
                { label: "Quiz me", prompt: "Make a short quiz (5 questions, answers at the end) to revise this notebook." },
                { label: "Key points and to-dos", prompt: "List the key points and any to-dos or open questions in this notebook." }
            ])
        })
    }
    /// Ask about the selection (its picture, and its typed text).
    function askAboutSelection(what) {
        const image = root.activeCanvas.selectionImage(1400)
        const typed = root.activeCanvas.selectionText()
        const prompts = {
            explain: "Explain what's in the picture clearly, as a good teacher would.",
            solve: "Solve this step by step, explaining each step briefly. Check the answer at the end.",
            summarise: "Summarise this.",
            translate: "Translate this into ",
            ask: ""
        }
        aiPanel.start({
            heading: "Ask about the selection",
            notebookId: root.notebookId,
            image: image,
            useNotes: false,
            canInsert: true,
            extra: "The user selected part of a page in their notebook “" + root.title + "”; the picture shows it (handwriting, text, drawings, or part of a PDF)." + (typed ? "\nTyped text in the selection:\n" + typed : ""),
            suggestions: what === "translate" ? [
                { label: "English", prompt: "Translate this into English." },
                { label: "Français", prompt: "Traduis ceci en français." },
                { label: "العربية", prompt: "ترجم هذا إلى العربية." },
                { label: "Another language…", prompt: "Translate this into ", fill: true }
            ] : [
                { label: "Explain", prompt: prompts.explain },
                { label: "Solve step by step", prompt: prompts.solve },
                { label: "Summarise", prompt: prompts.summarise },
                { label: "Write it as text", prompt: "Write out everything in the picture as text (keep its language), with no comments." }
            ],
            prompt: what === "translate" || what === "ask" ? "" : prompts[what]
        })
    }
    ShortcutsSheet { id: shortcuts }

    // Keyboard shortcuts (src/editor/editor.ts handleKey).
    readonly property bool typing: !!canvas.editing.id
    // Done typing: the keyboard goes back to the page (tool shortcuts work again).
    onTypingChanged: if (!typing) canvas.forceActiveFocus()
    Shortcut { sequences: [StandardKey.Undo]; enabled: !root.typing; onActivated: root.activeCanvas.undo() }
    // Each key once: on Linux the standard Redo key is Ctrl+Shift+Z, and a key
    // listed twice is ambiguous to Qt (it ignores the press).
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; enabled: !root.typing; onActivated: root.activeCanvas.redo(); onActivatedAmbiguously: root.activeCanvas.redo() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: !root.typing; onActivated: { const n = root.activeCanvas.copySelection(); if (n) root.notify("Copied " + n + " item" + (n > 1 ? "s" : ""), false) } }
    Shortcut { sequences: [StandardKey.Cut]; enabled: !root.typing; onActivated: root.activeCanvas.cutSelection() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: !root.typing; onActivated: root.activeCanvas.paste() }
    Shortcut { sequence: "Ctrl+D"; enabled: !root.typing; onActivated: root.activeCanvas.duplicateSelection() }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: !root.typing; onActivated: root.activeCanvas.selectAll() }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; onActivated: canvas.zoomBy(1.2); onActivatedAmbiguously: canvas.zoomBy(1.2) }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: canvas.zoomBy(1 / 1.2) }
    Shortcut { sequence: "Ctrl+0"; onActivated: canvas.fitWidth() }
    Shortcut { sequences: ["Delete", "Backspace"]; enabled: !!(!root.typing && (root.activeCanvas.selection.count || 0) > 0); onActivated: root.activeCanvas.deleteSelection() }
    Shortcut { sequence: "Esc"; enabled: !root.typing; onActivated: { if ((root.activeCanvas.selection.count || 0) > 0) root.activeCanvas.clearSelection(); else if (root.focusMode) root.focusMode = false; else root.drawerOpen = prefs.store.drawerPinned } }
    Shortcut { sequences: ["PgDown", "Right"]; enabled: !root.typing; onActivated: canvas.goToPage(canvas.currentPage + 1) }
    Shortcut { sequences: ["PgUp", "Left"]; enabled: !root.typing; onActivated: canvas.goToPage(canvas.currentPage - 1) }
    Shortcut { sequence: "R"; enabled: !root.typing; onActivated: canvas.instrument.visible ? canvas.showInstrument("") : canvas.showInstrument(canvas.instrument.kind || "ruler") }
    Shortcut { sequence: "F"; enabled: !root.typing; onActivated: root.focusMode = !root.focusMode }
    Shortcut { sequences: [StandardKey.Find]; onActivated: finder.open() }
    Shortcut { sequence: "F5"; onActivated: root.togglePresenting() }
    Shortcut { sequence: "B"; enabled: !root.typing && presenter.visible; onActivated: presenter.blank = !presenter.blank }
    Shortcut { sequence: "Z"; enabled: !root.typing; onActivated: zoomBox.active ? zoomBox.stop() : zoomBox.start() }
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
