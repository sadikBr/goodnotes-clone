// The library: Documents, Favourites, folders and the trash in the sidebar;
// the shelf of folders and notebooks, search and sorting on the right (the
// same screen as the web app's src/library.ts).
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Inkleaf

Rectangle {
    id: root
    color: Theme.bg
    signal openRequested(string id, string title)
    signal toggleDark()
    signal settingsRequested()
    signal notify(string message, bool error)

    /// { kind: "all" | "favourites" | "trash" | "folder", id? }
    property var view: ({ kind: "all" })
    /// Everything not in the trash (the sidebar always shows this).
    property var library: ({ folders: [], notebooks: [] })
    /// What the shelf shows: the library, or the trash's contents.
    property var shown: ({ folders: [], notebooks: [] })
    property string query: ""
    property var hits: []

    Prefs {
        id: prefs
        category: "library"
        property string sort: "recent"
    }
    readonly property var sorts: [{ id: "recent", label: "Recently edited" }, { id: "created", label: "Recently created" }, { id: "name", label: "Name" }]

    function show(v) {
        view = v
        reload()
    }
    function reload() {
        try {
            library = InkleafCore.call("library")
            shown = view.kind === "trash" ? InkleafCore.call("trash") : library
            if (view.kind === "folder" && !library.folders.some(f => f.id === view.id)) view = { kind: "all" }
        } catch (e) {
            root.notify(String(e.message || e), true)
        }
        if (query.trim()) runSearch()
    }
    Component.onCompleted: reload()
    Connections {
        target: InkleafCore
        function onLibraryChanged() { root.reload() }
    }
    /// Runs a library command; shows a message when it's done (or failed).
    function run(cmd, args, message) {
        try {
            const r = InkleafCore.call(cmd, args)
            if (message) root.notify(message, false)
            return r
        } catch (e) {
            root.notify(String(e.message || e), true)
        }
    }

    // ------------------------------------------------------------ helpers

    function greeting() {
        const h = new Date().getHours()
        return h < 5 ? "Good night" : h < 12 ? "Good morning" : h < 18 ? "Good afternoon" : "Good evening"
    }
    function relative(ms) {
        const min = Math.round((Date.now() - ms) / 60000)
        if (min < 1) return "just now"
        if (min < 60) return min + " min ago"
        const h = Math.round(min / 60)
        if (h < 24) return h + " h ago"
        return new Date(ms).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }
    function plural(n, one, many) { return n + " " + (n === 1 ? one : many) }
    readonly property string folderId: view.kind === "folder" ? view.id : ""
    function heading() {
        switch (view.kind) {
        case "all": return greeting()
        case "favourites": return "Favourites"
        case "trash": return "Trash"
        default: {
            const f = library.folders.find(x => x.id === view.id)
            return f ? f.name : "Folder"
        }
        }
    }
    function viewNotebooks() {
        const nbs = shown.notebooks
        if (view.kind === "folder") return nbs.filter(n => n.folderId === view.id)
        if (view.kind === "favourites") return nbs.filter(n => n.favourite)
        return nbs
    }
    function summary() {
        const nbs = viewNotebooks()
        if (view.kind === "trash") return nbs.length ? plural(nbs.length, "notebook", "notebooks") + " in the trash" : "Nothing in the trash"
        if (!nbs.length) return view.kind === "all" ? "Let's start something new" : "Nothing here yet"
        const latest = nbs.slice().sort((a, b) => b.updatedAt - a.updatedAt)[0]
        return plural(nbs.length, "notebook", "notebooks") + " · last edited “" + latest.title + "” " + relative(latest.updatedAt)
    }
    function sorted(list) {
        const out = list.slice()
        if (prefs.sort === "name") out.sort((a, b) => a.title.localeCompare(b.title))
        else if (prefs.sort === "created") out.sort((a, b) => b.createdAt - a.createdAt)
        else out.sort((a, b) => b.updatedAt - a.updatedAt)
        return out
    }
    function folderMeta(f) {
        const n = shown.notebooks.filter(x => x.folderId === f.id).length
        const sub = shown.folders.filter(x => x.parentId === f.id).length
        return plural(n, "notebook", "notebooks") + (sub ? " · " + plural(sub, "folder", "folders") : "")
    }

    /// The shelf: the New tile, folders, then notebooks.
    readonly property var shelf: {
        const v = view
        let folders = [], notebooks = shown.notebooks
        if (v.kind === "all") {
            // Anything whose folder isn't shown appears at the top level rather than nowhere.
            const visible = new Set(shown.folders.map(f => f.id))
            folders = shown.folders.filter(f => f.parentId === null || !visible.has(f.parentId))
            notebooks = notebooks.filter(n => n.folderId === null || !visible.has(n.folderId))
        } else if (v.kind === "favourites") {
            folders = shown.folders.filter(f => f.favourite)
            notebooks = notebooks.filter(n => n.favourite)
        } else if (v.kind === "folder") {
            folders = shown.folders.filter(f => f.parentId === v.id)
            notebooks = notebooks.filter(n => n.folderId === v.id)
        } else {
            folders = shown.folders
        }
        prefs.sort // re-sort when it changes
        const out = []
        if ((folders.length || notebooks.length) && (v.kind === "all" || v.kind === "folder")) out.push({ type: "new" })
        for (const f of folders.slice().sort((a, b) => a.name.localeCompare(b.name))) out.push({ type: "folder", item: f })
        for (const n of sorted(notebooks)) out.push({ type: "notebook", item: n })
        return out
    }
    readonly property bool canCreate: view.kind === "all" || view.kind === "folder"

    // ------------------------------------------------------------ actions

    function createOptions() {
        const fid = folderId || null
        return [
            { id: "notebook", icon: "book", title: "Notebook", sub: "Cover, paper and page size", key: "N", action: () => newNotebook(fid) },
            { id: "daftar", icon: "docs", title: "Daftar annossous", sub: "Lesson log for your classes", tint: "#7c3aed", action: () => daftarUi.askNew(fid, nb => root.openRequested(nb.id, nb.title)) },
            { id: "pdf", icon: "page", title: "Import PDFs", sub: "One or several, to write on", tint: "#dc2626", action: () => { pdfDialog.folder = fid; pdfDialog.open() } },
            { id: "folder", icon: "folder", title: "Folder", sub: "Group your documents", tint: "#ca8a04", iconColor: "#b45309", action: () => newFolder(fid) }
        ]
    }
    readonly property var templates: [
        { value: "blank", label: "Blank", group: "Basic" }, { value: "ruled", label: "Ruled", group: "Basic" }, { value: "narrow-ruled", label: "Narrow ruled", group: "Basic" },
        { value: "wide-ruled", label: "Wide ruled", group: "Basic" }, { value: "grid", label: "Grid", group: "Basic" }, { value: "dotted", label: "Dotted", group: "Basic" },
        { value: "cornell", label: "Cornell", group: "Notes" }, { value: "legal", label: "Legal pad", group: "Notes" }, { value: "two-column", label: "Two columns", group: "Notes" },
        { value: "checklist", label: "To-do list", group: "Notes" },
        { value: "graph", label: "Graph", group: "Maths & science" }, { value: "coordinate", label: "Coordinates", group: "Maths & science" },
        { value: "isometric", label: "Isometric", group: "Maths & science" }, { value: "hexagon", label: "Hexagon", group: "Maths & science" },
        { value: "handwriting", label: "Handwriting", group: "Writing & art" }, { value: "calligraphy", label: "Calligraphy", group: "Writing & art" },
        { value: "storyboard", label: "Storyboard", group: "Writing & art" },
        { value: "planner-day", label: "Daily plan", group: "Planner" }, { value: "planner-week", label: "Weekly plan", group: "Planner" }
    ]
    function newNotebook(fid) {
        dialog.ask("New notebook", [
            { name: "title", label: "Title", value: "Untitled notebook" },
            { name: "template", label: "Paper", type: "select", value: "ruled", options: templates },
            { name: "size", label: "Page size", type: "select", value: "a4", options: Theme.paperSizes.map(p => ({ value: p.id, label: p.label })) },
            { name: "orientation", label: "Orientation", type: "segmented", value: "portrait", options: [{ value: "portrait", label: "Portrait" }, { value: "landscape", label: "Landscape" }] },
            { name: "paper", label: "Paper colour", type: "swatches", value: "#fffdf7", colors: Theme.paperColors },
            { name: "cover", label: "Cover colour", type: "swatches", colors: Theme.coverColors },
            { name: "design", label: "Cover design", type: "covers", value: "classic", options: coverDesigns }
        ], "Create", r => {
            const size = Theme.paperSizes.find(p => p.id === r.size) || Theme.paperSizes[0]
            const landscape = r.orientation === "landscape"
            const nb = run("createNotebook", { input: { title: r.title.trim(), folderId: fid, template: r.template, coverColor: r.cover, paperColor: r.paper, width: landscape ? size.h : size.w, height: landscape ? size.w : size.h, coverPage: true } })
            if (!nb) return
            // The chosen cover design, its title and date, and its picture for the library.
            if (r.design !== "classic") {
                const pages = run("pages", { notebookId: nb.id })
                if (pages && pages.length) run("setPageTemplate", { pageId: pages[0].id, template: "cover-" + r.design, paperColor: r.cover })
            }
            Js.run("setUpCover", [nb])
            root.openRequested(nb.id, nb.title)
        }, { aside: coverPreview })
    }
    function newFolder(parentId) {
        dialog.ask("New folder", [
            { name: "name", label: "Name", value: "New folder" },
            { name: "color", label: "Colour", type: "swatches", colors: Theme.folderColors }
        ], "Create", r => run("createFolder", { name: r.name.trim(), parentId: parentId, color: r.color }))
    }
    function editFolder(f) {
        dialog.ask("Edit folder", [
            { name: "name", label: "Name", value: f.name },
            { name: "color", label: "Colour", type: "swatches", value: f.color, colors: Theme.folderColors }
        ], "Save", r => {
            if (r.name.trim() !== f.name) run("renameFolder", { id: f.id, name: r.name.trim() })
            if (r.color !== f.color) run("setFolderColor", { id: f.id, color: r.color })
        })
    }
    function renameNotebook(n) {
        dialog.ask("Rename notebook", [{ name: "title", label: "Title", value: n.title }], "Save", r => run("renameNotebook", { id: n.id, title: r.title.trim() }))
    }
    readonly property var coverDesigns: (Js.run("COVER_DESIGNS.slice", [0]) || []).map(d => ({ value: d.id, label: d.label }))
    function changeCover(n) {
        // The current design (or none, for a notebook without a cover page).
        let current = "none"
        if (n.hasCover) {
            const pages = run("pages", { notebookId: n.id })
            if (pages && pages.length && String(pages[0].template).startsWith("cover")) current = pages[0].template === "cover" ? "classic" : String(pages[0].template).slice(6)
        }
        dialog.ask("Change cover", [
            { name: "title", label: "", type: "hidden", value: n.title },
            { name: "cover", label: "Cover colour", type: "swatches", value: n.coverColor, colors: Theme.coverColors },
            { name: "design", label: "Cover design", type: "covers", value: current, options: (n.hasCover ? [] : [{ value: "none", label: "No cover" }]).concat(coverDesigns) }
        ], "Save", r => {
            if (r.design === "none") run("setNotebookCover", { id: n.id, color: r.cover })
            else Js.run("applyCover", [n, r.design, r.cover])
        }, { aside: coverPreview })
    }
    /// Folders something can move into (not a folder itself or anything inside it).
    function folderOptions(exclude) {
        const blocked = new Set()
        if (exclude) {
            blocked.add(exclude)
            let grew = true
            while (grew) {
                grew = false
                for (const f of library.folders)
                    if (f.parentId && blocked.has(f.parentId) && !blocked.has(f.id)) { blocked.add(f.id); grew = true }
            }
        }
        return [{ value: "", label: "Library (no folder)" }].concat(library.folders.filter(f => !blocked.has(f.id)).map(f => ({ value: f.id, label: f.name })))
    }
    function moveNotebook(n) {
        dialog.ask("Move notebook", [{ name: "folder", label: "Destination", type: "select", value: n.folderId || "", options: folderOptions() }], "Move",
                   r => run("moveNotebook", { id: n.id, folderId: r.folder || null }, "Notebook moved"))
    }
    function moveFolder(f) {
        dialog.ask("Move folder", [{ name: "folder", label: "Destination", type: "select", value: f.parentId || "", options: folderOptions(f.id) }], "Move",
                   r => run("moveFolder", { id: f.id, parentId: r.folder || null }, "Folder moved"))
    }
    function deleteForever(kind, id, name) {
        dialog.ask("Delete “" + name + "” forever?", [], "Delete", () => run(kind === "folder" ? "deleteFolder" : "deleteNotebook", { id: id }, "Deleted"), {
            danger: true,
            message: kind === "folder" ? "The folder, its subfolders and every notebook inside them will be permanently deleted." : "This notebook and all of its pages will be permanently deleted."
        })
    }
    function emptyTrash() {
        dialog.ask("Empty trash?", [], "Empty trash", () => run("emptyTrash", {}, "Trash emptied"), {
            danger: true,
            message: "Everything in the trash will be permanently deleted, including all the subfolders and notebooks inside trashed folders."
        })
    }
    function notebookMenu(n, anchor) {
        menu.show(anchor, view.kind === "trash" ? [
            { label: "Recover", action: () => run("setNotebookTrashed", { id: n.id, trashed: false }, "Notebook recovered") },
            { label: "Delete forever", danger: true, action: () => deleteForever("notebook", n.id, n.title) }
        ] : [
            { label: "Rename", action: () => renameNotebook(n) },
            { label: n.favourite ? "Remove from favourites" : "Add to favourites", action: () => run("setNotebookFavourite", { id: n.id, favourite: !n.favourite }) },
            { label: "Change cover", action: () => changeCover(n) },
            { label: "Duplicate", action: () => run("duplicateNotebook", { id: n.id }, "Notebook duplicated") },
            { label: "Move to…", action: () => moveNotebook(n) },
            { label: "Move to trash", danger: true, action: () => run("setNotebookTrashed", { id: n.id, trashed: true }, "Notebook moved to trash") }
        ])
    }
    function folderMenu(f, anchor) {
        menu.show(anchor, view.kind === "trash" ? [
            { label: "Recover", action: () => run("setFolderTrashed", { id: f.id, trashed: false }) },
            { label: "Delete forever", danger: true, action: () => deleteForever("folder", f.id, f.name) }
        ] : [
            { label: "Edit name and colour…", action: () => editFolder(f) },
            { label: f.favourite ? "Remove from favourites" : "Add to favourites", action: () => run("setFolderFavourite", { id: f.id, favourite: !f.favourite }) },
            { label: "Move to…", action: () => moveFolder(f) },
            { label: "Move to trash", danger: true, action: () => run("setFolderTrashed", { id: f.id, trashed: true }, "Folder moved to trash") }
        ])
    }
    function runSearch() {
        const q = query.trim()
        if (!q || view.kind === "trash") { hits = []; return }
        try { hits = InkleafCore.call("search", { query: q }) } catch (e) { hits = [] }
    }

    component NavItem: AbstractButton {
        id: nav
        property string iconName
        property bool active: false
        property string count: ""
        property color dot: "transparent"
        property int indent: 0
        Layout.fillWidth: true
        implicitHeight: 38
        hoverEnabled: true
        background: Rectangle { radius: Theme.radius; color: nav.active ? Theme.accentSoft : nav.hovered ? Theme.surface2 : "transparent"; Behavior on color { ColorAnimation { duration: 150 } } }
        contentItem: RowLayout {
            spacing: 11
            Item { Layout.preferredWidth: nav.indent }
            Icon { name: nav.iconName; size: 19; color: nav.active ? Theme.accent : Theme.muted }
            Label { text: nav.text; color: nav.active ? Theme.accent : Theme.text; font.weight: nav.active ? Font.DemiBold : Font.Normal; elide: Text.ElideRight; Layout.fillWidth: true }
            Rectangle {
                visible: nav.dot.a > 0
                Layout.preferredWidth: 13; Layout.preferredHeight: 13; radius: 6.5
                color: Theme.surface
                Rectangle { anchors.centerIn: parent; width: 9; height: 9; radius: 4.5; color: nav.dot }
            }
            Rectangle {
                visible: nav.count.length > 0
                Layout.preferredHeight: 20
                Layout.preferredWidth: Math.max(22, countText.implicitWidth + 12)
                radius: 10
                color: nav.active ? Theme.surface : Theme.surface2
                Label { id: countText; anchors.centerIn: parent; text: nav.count; font.pixelSize: 11; font.weight: Font.DemiBold; color: nav.active ? Theme.accent : Theme.muted }
            }
        }
    }

    FileDialog {
        id: pdfDialog
        property var folder: null
        title: "Import PDFs"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["PDF documents (*.pdf)"]
        onAccepted: PdfImporter.add(selectedFiles.map(u => u.toString()), folder || "")
    }
    Connections {
        target: PdfImporter
        function onAdded() { root.reload() }
    }
    ImportPanel {
        z: 50
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        onOpenRequested: id => {
            const nb = root.library.notebooks.find(n => n.id === id)
            root.openRequested(id, nb ? nb.title : "")
        }
    }
    PopupMenu { id: menu }
    CreateMenu { id: createMenu }
    FormDialog { id: dialog }
    Daftar { id: daftarUi; dialog: dialog; onNotify: (m, e) => root.notify(m, e) }
    Timer { id: searchDelay; interval: 200; onTriggered: root.runSearch() }

    // The live cover in the new-notebook and change-cover dialogs.
    Component {
        id: coverPreview
        Column {
            spacing: 16
            Book {
                visible: !dialog.values.design || dialog.values.design === "none"
                anchors.horizontalCenter: parent.horizontalCenter
                width: 200; height: 266
                coverColor: dialog.values.cover || Theme.coverColors[0]
                title: dialog.values.title || ""
                raised: true
                transform: Rotation { origin.x: 100; origin.y: 133; axis { x: 0; y: 1; z: 0 } angle: -8 }
            }
            Item {
                visible: !!dialog.values.design && dialog.values.design !== "none"
                readonly property bool landscape: dialog.values.orientation === "landscape"
                anchors.horizontalCenter: parent.horizontalCenter
                width: landscape ? 250 : 200
                height: landscape ? 177 : 283
                Shadow { anchors.fill: parent; radius: 8; level: 3 }
                Image {
                    anchors.fill: parent
                    sourceSize: Qt.size(parent.width * 2, parent.height * 2)
                    source: parent.visible ? dialog.coverUrl(dialog.values.design) : ""
                    asynchronous: false
                }
                transform: Rotation { origin.x: 100; origin.y: 141; axis { x: 0; y: 1; z: 0 } angle: -8 }
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: {
                    const s = Theme.paperSizes.find(p => p.id === dialog.values.size)
                    return s ? s.label + " · " + (dialog.values.orientation === "landscape" ? "Landscape" : "Portrait") : ""
                }
                color: Theme.muted; font.pixelSize: 12; font.weight: Font.DemiBold
            }
        }
    }

    Shortcut { sequence: "/"; onActivated: search.forceActiveFocus() }
    Shortcut { sequence: "N"; enabled: root.view.kind !== "trash" && !dialog.opened; onActivated: root.newNotebook(root.folderId || null) }

    RowLayout {
        id: body
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------------------ sidebar
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 248
            color: Theme.surface
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 18
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.bottomMargin: 12
                spacing: 2
                RowLayout {
                    spacing: 10
                    Layout.leftMargin: 8
                    Layout.topMargin: 2
                    Layout.bottomMargin: 18
                    Image { source: "qrc:/qt/qml/Inkleaf/assets/icon.svg"; sourceSize: Qt.size(60, 60); Layout.preferredWidth: 30; Layout.preferredHeight: 30 }
                    Label { text: "Inkleaf"; font.pixelSize: 18; font.weight: Font.Bold; color: Theme.text }
                }
                AbstractButton {
                    id: newBtn
                    Layout.fillWidth: true
                    Layout.bottomMargin: 14
                    implicitHeight: 48
                    hoverEnabled: true
                    onClicked: createMenu.show(newBtn, root.createOptions())
                    scale: pressed ? 0.98 : 1
                    Behavior on scale { NumberAnimation { duration: 90 } }
                    ToolTip.visible: hovered && !createMenu.opened
                    ToolTip.delay: 600
                    ToolTip.text: "Create a notebook, daftar, PDF or folder"
                    background: Item {
                        Shadow { anchors.fill: parent; radius: 14; level: newBtn.hovered || createMenu.opened ? 2 : 1 }
                        Rectangle { anchors.fill: parent; radius: 14; color: Theme.surface; border.color: newBtn.hovered || createMenu.opened ? Theme.mix(Theme.accent, Theme.border, 0.45) : Theme.border }
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        Item { Layout.preferredWidth: 0 }
                        Rectangle {
                            Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: 10; color: Theme.accent
                            Icon { anchors.centerIn: parent; name: "plus"; size: 18; color: Theme.accentContrast }
                        }
                        Label { text: "New"; font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text; Layout.fillWidth: true }
                        Icon { name: "down"; size: 15; color: Theme.muted; rotation: createMenu.opened ? 180 : 0; Layout.rightMargin: 12; Behavior on rotation { NumberAnimation { duration: 200 } } }
                    }
                }
                NavItem {
                    text: "Documents"; iconName: "docs"
                    active: root.view.kind === "all"
                    count: String(root.library.notebooks.length + root.library.folders.length || "")
                    onClicked: root.show({ kind: "all" })
                }
                NavItem {
                    text: "Favourites"; iconName: "star"
                    active: root.view.kind === "favourites"
                    count: String(root.library.notebooks.filter(n => n.favourite).length + root.library.folders.filter(f => f.favourite).length || "")
                    onClicked: root.show({ kind: "favourites" })
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 18
                    Layout.leftMargin: 12
                    Layout.rightMargin: 4
                    Layout.bottomMargin: 6
                    Label { text: "FOLDERS"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.7; color: Theme.muted; Layout.fillWidth: true }
                    IconButton { iconName: "plus"; tip: "New folder"; implicitWidth: 28; implicitHeight: 28; iconSize: 16; iconColor: Theme.muted; onClicked: root.newFolder(null) }
                }
                Label {
                    visible: root.library.folders.length === 0
                    text: "Group notebooks into folders with +"
                    color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap
                    Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.topMargin: 2
                }
                // The folder tree.
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: tree.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollIndicator.vertical: ScrollIndicator {}
                    ColumnLayout {
                        id: tree
                        width: parent.width
                        spacing: 2
                        Repeater {
                            model: {
                                const out = []
                                const add = (parent, depth) => {
                                    for (const f of root.library.folders.filter(x => x.parentId === parent).sort((a, b) => a.name.localeCompare(b.name))) {
                                        out.push({ f: f, depth: depth })
                                        add(f.id, depth + 1)
                                    }
                                }
                                add(null, 0)
                                return out
                            }
                            delegate: NavItem {
                                required property var modelData
                                text: modelData.f.name
                                iconName: "folder"
                                dot: modelData.f.color
                                indent: modelData.depth * 14
                                active: root.view.kind === "folder" && root.view.id === modelData.f.id
                                onClicked: root.show({ kind: "folder", id: modelData.f.id })
                            }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border; Layout.bottomMargin: 8 }
                NavItem { text: "Trash"; iconName: "trash"; active: root.view.kind === "trash"; onClicked: root.show({ kind: "trash" }) }
                NavItem {
                    id: darkItem
                    text: "Dark mode"; iconName: "moon"
                    onClicked: root.toggleDark()
                    UiSwitch { anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; checked: Theme.dark; onClicked: root.toggleDark() }
                }
                NavItem { text: "Settings"; iconName: "gear"; onClicked: root.settingsRequested() }
            }
        }

        // ------------------------------------------------------------ main
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Heading, search and sort.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 36
                Layout.rightMargin: 36
                Layout.topMargin: 30
                Layout.bottomMargin: 18
                spacing: 20
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignBottom
                    spacing: 6
                    Label { text: root.heading(); font.pixelSize: 28; font.weight: Font.Bold; font.letterSpacing: -0.5; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { text: root.summary(); color: Theme.muted; elide: Text.ElideRight; Layout.fillWidth: true }
                }
                // Search.
                Item {
                    Layout.alignment: Qt.AlignBottom
                    implicitWidth: searchRow.implicitWidth + 22
                    implicitHeight: 38
                    Rectangle { anchors.fill: parent; anchors.margins: -3; radius: height / 2; color: Theme.accentSoft; visible: search.activeFocus }
                    Shadow { anchors.fill: parent; radius: 19; level: 1 }
                    Rectangle { anchors.fill: parent; radius: 19; color: Theme.surface; border.color: search.activeFocus ? Theme.accent : Theme.border }
                    RowLayout {
                        id: searchRow
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: 8
                        Icon { name: "search"; size: 18; color: Theme.muted }
                        TextField {
                            id: search
                            Layout.preferredWidth: 240
                            placeholderText: "Search documents and text…"
                            placeholderTextColor: Theme.muted
                            color: Theme.text
                            selectionColor: Theme.alpha(Theme.accent, 0.3)
                            font.pixelSize: 14
                            background: null
                            padding: 0
                            text: root.query
                            onTextEdited: { root.query = text; searchDelay.restart() }
                            Keys.onEscapePressed: { text = ""; root.query = ""; root.hits = []; focus = false }
                        }
                        Rectangle {
                            visible: !search.activeFocus
                            Layout.preferredWidth: 20; Layout.preferredHeight: 20; radius: 5
                            color: Theme.surface2; border.color: Theme.border
                            Label { anchors.centerIn: parent; text: "/"; font.pixelSize: 11; color: Theme.muted }
                        }
                    }
                }
                UiButton {
                    id: sortBtn
                    visible: root.view.kind !== "trash"
                    Layout.alignment: Qt.AlignBottom
                    kind: "ghost"
                    iconName: "sort"
                    text: (root.sorts.find(s => s.id === prefs.sort) || root.sorts[0]).label
                    onClicked: menu.show(sortBtn, root.sorts.map(s => ({ label: s.label, checked: prefs.sort === s.id, action: () => prefs.sort = s.id })))
                }
                UiButton {
                    visible: root.view.kind === "trash"
                    Layout.alignment: Qt.AlignBottom
                    kind: "danger"
                    iconName: "trash"
                    text: "Empty trash"
                    onClicked: root.emptyTrash()
                }
            }

            // Search results.
            ListView {
                id: results
                visible: root.query.trim().length > 0 && root.view.kind !== "trash"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 36
                Layout.rightMargin: 36
                topMargin: 10
                bottomMargin: 80
                clip: true
                spacing: 10
                model: root.hits
                ScrollBar.vertical: ScrollBar {}
                Label {
                    visible: root.hits.length === 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 40
                    text: "No results for “" + root.query + "”."
                    color: Theme.muted
                }
                delegate: AbstractButton {
                    id: hit
                    required property var modelData
                    width: Math.min(760, results.width)
                    implicitHeight: hitCol.implicitHeight + 28
                    hoverEnabled: true
                    onClicked: root.openRequested(modelData.notebookId, modelData.notebookTitle)
                    transform: Translate { x: hit.hovered ? 2 : 0; Behavior on x { NumberAnimation { duration: 200 } } }
                    background: Item {
                        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 1 }
                        Rectangle { anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: hit.hovered ? Theme.accent : Theme.border }
                    }
                    contentItem: ColumnLayout {
                        id: hitCol
                        spacing: 4
                        Label {
                            textFormat: Text.StyledText
                            text: "<b>" + hit.modelData.notebookTitle.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</b>" + (hit.modelData.pageNumber ? "<font color='" + Theme.muted + "'> · page " + hit.modelData.pageNumber + "</font>" : "")
                            color: Theme.text
                            Layout.leftMargin: 16
                        }
                        Label { text: hit.modelData.snippet; color: Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; lineHeight: 1.2 }
                    }
                }
            }

            // Nothing here: a picture, a line and what can be made.
            Flickable {
                visible: !results.visible && root.shelf.length === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: empty.implicitHeight + 140
                clip: true
                ColumnLayout {
                    id: empty
                    y: 70
                    width: parent.width
                    spacing: 0
                    Image {
                        Layout.alignment: Qt.AlignHCenter
                        sourceSize: Qt.size(320, 240)
                        Layout.preferredWidth: 160; Layout.preferredHeight: 120
                        source: "data:image/svg+xml;utf8," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 160 120" width="160" height="120"><ellipse cx="80" cy="108" rx="58" ry="7" fill="' + Theme.text + '" opacity="0.08"/><g transform="rotate(-8 60 70)"><rect x="34" y="28" width="52" height="70" rx="5" fill="#e07a5f"/><rect x="34" y="28" width="8" height="70" rx="3" fill="#000" opacity="0.15"/><rect x="48" y="44" width="30" height="12" rx="3" fill="#fff" opacity="0.9"/></g><g transform="rotate(6 98 66)"><rect x="70" y="22" width="54" height="74" rx="5" fill="' + Theme.accent + '"/><rect x="70" y="22" width="8" height="74" rx="3" fill="#000" opacity="0.15"/><rect x="84" y="38" width="32" height="13" rx="3" fill="#fff" opacity="0.92"/><path d="M84 66h30M84 74h24" stroke="#fff" stroke-opacity="0.5" stroke-width="2" stroke-linecap="round"/></g><g transform="rotate(38 128 40)"><rect x="122" y="6" width="9" height="58" rx="3" fill="#3d405b"/><path d="M122 64l4.5 10 4.5-10z" fill="#3d405b"/><rect x="122" y="6" width="9" height="10" rx="3" fill="#000" opacity="0.2"/></g></svg>')
                    }
                    readonly property var texts: root.view.kind === "trash" ? ["Trash is empty", "Notebooks and folders you delete wait here before they're gone for good."]
                        : root.view.kind === "favourites" ? ["No favourites yet", "Star a notebook or folder from its ⋯ menu to keep it close at hand."]
                        : root.view.kind === "folder" ? ["This folder is empty", "Create a notebook here, or move one in from its ⋯ menu."]
                        : ["Your shelf is empty", "Start a notebook, a daftar annossous, or write on a PDF."]
                    Label { Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 18; text: empty.texts[0]; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text }
                    Label { Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 6; Layout.maximumWidth: 380; text: empty.texts[1]; color: Theme.muted; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; lineHeight: 1.2 }
                    GridLayout {
                        visible: root.canCreate
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 32
                        Layout.preferredWidth: Math.min(560, empty.width - 40)
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 10
                        Repeater {
                            model: root.canCreate ? root.createOptions() : []
                            delegate: CreateItem {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                card: true
                                option: modelData
                                onClicked: modelData.action()
                            }
                        }
                    }
                }
            }

            // The shelf.
            GridView {
                id: grid
                visible: !results.visible && root.shelf.length > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 36
                Layout.rightMargin: 36 - 28
                topMargin: 14
                bottomMargin: 80
                clip: true
                readonly property int columns: Math.max(1, Math.floor(width / (178 + 28)))
                cellWidth: Math.floor(width / columns)
                cellHeight: (cellWidth - 28) * 4 / 3 + 14 + 20 + 3 + 16 + 34
                model: root.shelf
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                cacheBuffer: 600
                delegate: Item {
                    id: cell
                    required property var modelData
                    width: grid.cellWidth
                    height: grid.cellHeight
                    NewTile {
                        visible: cell.modelData.type === "new"
                        width: parent.width - 28
                        onClicked: createMenu.show(this, root.createOptions())
                    }
                    Card {
                        visible: cell.modelData.type !== "new"
                        width: parent.width - 28
                        isFolder: cell.modelData.type === "folder"
                        item: cell.modelData.item || ({})
                        trash: root.view.kind === "trash"
                        meta: !cell.modelData.item ? "" : isFolder ? root.folderMeta(cell.modelData.item) : root.plural(cell.modelData.item.pageCount, "page", "pages") + " · " + root.relative(cell.modelData.item.updatedAt)
                        sheets: isFolder ? root.shown.notebooks.filter(n => n.folderId === cell.modelData.item.id).sort((a, b) => b.updatedAt - a.updatedAt).slice(0, 2) : []
                        count: isFolder ? root.shown.notebooks.filter(n => n.folderId === cell.modelData.item.id).length : 0
                        onActivated: {
                            if (trash) return
                            if (isFolder) root.show({ kind: "folder", id: item.id })
                            else root.openRequested(item.id, item.title)
                        }
                        onMoreRequested: anchor => isFolder ? root.folderMenu(item, anchor) : root.notebookMenu(item, anchor)
                    }
                }
            }
        }
    }
}
