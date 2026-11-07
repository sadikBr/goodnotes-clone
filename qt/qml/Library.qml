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
    /// The app's background work (sync), for the sync line in the side bar.
    property var services: null
    signal notify(string message, bool error)

    /// { kind: "all" | "favourites" | "trash" | "folder", id? }
    property var view: ({ kind: "all" })
    /// Everything not in the trash (the sidebar always shows this).
    property var library: ({ folders: [], notebooks: [] })
    /// What the shelf shows: the library, or the trash's contents.
    property var shown: ({ folders: [], notebooks: [] })
    property string query: ""
    property var hits: []

    // ------------------------------------------------------------ selecting several
    /// Picked shelf items by key ("notebook:<id>", "folder:<id>"), in picking order.
    property var picked: ({})
    readonly property int pickedCount: Object.keys(picked).length
    /// Selecting (the Select button), or something is picked.
    property bool selectMode: false
    /// Too narrow for the heading, search and buttons on one line.
    readonly property bool narrowHead: width < 1240
    readonly property bool selecting: selectMode || pickedCount > 0
    property string lastPicked: ""
    /// A bulk action is running: the library reloads once at its end.
    property bool bulk: false
    function keyOf(e) { return e.type + ":" + e.item.id }
    /// Back (a phone's back button): out of selecting, a search, then a folder or the trash; false on the shelf itself.
    function goBack() {
        if (selecting) { clearPicks(); return true }
        if (query.length > 0) { query = ""; hits = []; return true }
        if (view.kind !== "all") {
            const f = view.kind === "folder" ? (library.folders || []).find(x => x.id === view.id) : null
            show(f && f.parentId ? { kind: "folder", id: f.parentId } : { kind: "all" })
            return true
        }
        return false
    }
    function clearPicks() { picked = ({}); selectMode = false; lastPicked = "" }
    function pick(entry, range) {
        const key = keyOf(entry)
        const next = Object.assign({}, picked)
        const items = shelf.filter(e => e.type !== "new")
        const from = items.findIndex(e => keyOf(e) === lastPicked)
        const to = items.findIndex(e => keyOf(e) === key)
        if (range && from >= 0 && to >= 0) {
            for (let i = Math.min(from, to); i <= Math.max(from, to); ++i) next[keyOf(items[i])] = items[i]
        } else if (next[key]) {
            delete next[key]
        } else {
            next[key] = entry
        }
        picked = next
        lastPicked = key
    }
    function pickAll() {
        const next = {}
        for (const e of shelf) if (e.type !== "new") next[keyOf(e)] = e
        picked = next
        selectMode = true
    }
    function pickedEntries() { return Object.keys(picked).map(k => picked[k]) }
    /// Runs one command per picked item, then reloads once and says how it went.
    function bulkRun(steps, done) {
        bulk = true
        let failed = 0
        for (const s of steps) {
            try { InkleafCore.call(s[0], s[1]) } catch (e) { failed++ }
        }
        bulk = false
        clearPicks()
        reload()
        if (failed) root.notify(failed + " of " + steps.length + " couldn't be done", true)
        else if (done) root.notify(done, false)
    }
    function described(entries) {
        const f = entries.filter(e => e.type === "folder").length, n = entries.length - f
        return [n ? plural(n, "notebook", "notebooks") : "", f ? plural(f, "folder", "folders") : ""].filter(x => x).join(" and ")
    }
    function bulkFavourite() {
        const entries = pickedEntries()
        const on = !entries.every(e => e.item.favourite)
        bulkRun(entries.map(e => [e.type === "folder" ? "setFolderFavourite" : "setNotebookFavourite", { id: e.item.id, favourite: on }]), on ? "Added to favourites" : "Removed from favourites")
    }
    function bulkTrash() {
        const entries = pickedEntries()
        bulkRun(entries.map(e => [e.type === "folder" ? "setFolderTrashed" : "setNotebookTrashed", { id: e.item.id, trashed: true }]), described(entries) + " moved to trash")
    }
    function bulkRecover() {
        const entries = pickedEntries()
        bulkRun(entries.map(e => [e.type === "folder" ? "setFolderTrashed" : "setNotebookTrashed", { id: e.item.id, trashed: false }]), described(entries) + " recovered")
    }
    function bulkDeleteForever() {
        const entries = pickedEntries()
        dialog.ask("Delete " + described(entries) + " forever?", [], "Delete", () => bulkRun(entries.map(e => [e.type === "folder" ? "deleteFolder" : "deleteNotebook", { id: e.item.id }]), "Deleted"), {
            danger: true,
            message: "They will be permanently deleted" + (entries.some(e => e.type === "folder") ? ", with everything inside the folders." : ".")
        })
    }
    function bulkMove() {
        const entries = pickedEntries()
        const folders = entries.filter(e => e.type === "folder").map(e => e.item.id)
        dialog.ask("Move " + described(entries), [{ name: "folder", label: "Destination", type: "select", value: "", options: folderOptions(folders) }], "Move", r => {
            const to = r.folder || null
            intoFolder(to, () => bulkRun(entries.map(e => e.type === "folder" ? ["moveFolder", { id: e.item.id, parentId: to }] : ["moveNotebook", { id: e.item.id, folderId: to }]), described(entries) + " moved"))
        })
    }

    Prefs {
        id: prefs
        category: "library"
        property string sort: "recent"
    }
    readonly property var sorts: [{ id: "recent", label: "Recently edited", icon: "pen" }, { id: "created", label: "Recently created", icon: "clock" }, { id: "name", label: "Name", icon: "az" }]

    function show(v) {
        if (navDrawer.opened) navDrawer.close()
        view = v
        clearPicks()
        reload()
    }
    function reload() {
        try {
            library = InkleafCore.call("library")
            shown = view.kind === "trash" ? InkleafCore.call("trash") : library
            // A folder that's gone, or locked again, isn't shown any more.
            if (view.kind === "folder" && !library.folders.some(f => f.id === view.id && (!f.locked || f.unlocked))) view = { kind: "all" }
        } catch (e) {
            root.notify(String(e.message || e), true)
        }
        if (query.trim()) runSearch()
        // Picked items that are gone (or moved out of view) aren't picked any more.
        const keep = {}
        for (const e of shelf) if (e.type !== "new" && picked[keyOf(e)]) keep[keyOf(e)] = e
        if (Object.keys(keep).length !== pickedCount) picked = keep
    }
    Component.onCompleted: reload()
    Connections {
        target: InkleafCore
        function onLibraryChanged() { if (!root.bulk) root.reload() }
    }
    /// Runs a library command; shows a message when it's done (or failed).
    function run(cmd, args, message) {
        try {
            const r = InkleafCore.call(cmd, args)
            if (message) root.notify(message, false)
            return r
        } catch (e) {
            root.notify(errorText(e), true)
        }
    }

    /// Moves a notebook or folder to the trash, with Undo in the message.
    function trashWithUndo(cmd, id, message) {
        try { InkleafCore.call(cmd, { id: id, trashed: true }) } catch (e) { return root.notify(errorText(e), true) }
        Notice.post(message, false, { icon: "trash", actionLabel: "Undo", action: () => run(cmd, { id: id, trashed: false }) })
    }

    // ------------------------------------------------------------ passwords

    function errorText(e) {
        const m = String(e.message || e)
        return m.startsWith("locked:") ? "Unlock it first (it's password protected)." : m
    }
    /// Asks for the password of a locked item that isn't unlocked, then does `then`.
    function withUnlocked(kind, item, then) {
        if (!item || !item.locked || item.unlocked) { then(); return }
        dialog.ask("Unlock “" + (kind === "folder" ? item.name : item.title) + "”", [
            { name: "password", label: "Password", type: "password", value: "" }
        ], "Unlock", () => then(), {
            message: kind === "folder" ? "This folder is password protected." : "This notebook is password protected.",
            validate: v => {
                if (!v.password) return "Enter the password."
                try { InkleafCore.call("unlock", { kind: kind, id: item.id, password: v.password }) }
                catch (e) { return String(e.message || e) }
                return ""
            }
        })
    }
    /// Opens a notebook (asking for its password first when it's locked).
    function openNotebook(id, title) {
        const nb = library.notebooks.find(n => n.id === id)
        withUnlocked("notebook", nb, () => root.openRequested(id, title || (nb ? nb.title : "")))
    }
    function openFolder(f) {
        withUnlocked("folder", f, () => root.show({ kind: "folder", id: f.id }))
    }
    /// The lock an item shares with the folder it's in (rather than one of its own).
    function inheritedLock(kind, item) {
        if (!item.locked) return false
        const parent = library.folders.find(f => f.id === (kind === "folder" ? item.parentId : item.folderId))
        return !!(parent && parent.lockId && parent.lockId === item.lockId)
    }
    readonly property var newPasswordFields: [
        { name: "password", label: "Password", type: "password", value: "" },
        { name: "confirm", label: "Password again", type: "password", value: "" }
    ]
    function checkNewPassword(v) {
        if (String(v.password).length < 4) return "Use a password of at least 4 characters."
        if (v.password !== v.confirm) return "The passwords don't match."
        return ""
    }
    function lockItem(kind, item) {
        const name = kind === "folder" ? item.name : item.title
        dialog.ask("Protect “" + name + "” with a password", newPasswordFields, "Lock", r => {
            run(kind === "folder" ? "lockFolder" : "lockNotebook", { id: item.id, password: r.password },
                kind === "folder" ? "Folder locked: it stays open until you lock it or quit" : "Notebook locked: it stays open until you lock it or quit")
        }, {
            message: (kind === "folder" ? "Everything in this folder, and anything you add to it later, is encrypted" : "Its pages, writing, pictures and PDF are encrypted")
                     + " on this device, in backups and in Drive sync. If you forget the password, it can't be recovered: there's no way back in."
                     + (kind === "folder" ? " Notebooks that have a password of their own keep it." : ""),
            validate: checkNewPassword
        })
    }
    function changePassword(kind, item) {
        withUnlocked(kind, item, () => dialog.ask("New password for “" + (kind === "folder" ? item.name : item.title) + "”", newPasswordFields, "Change", r => {
            run("changePassword", { kind: kind, id: item.id, password: r.password }, "Password changed")
        }, {
            message: kind === "folder" ? "It changes for everything in the folder too. If you forget it, it can't be recovered." : "If you forget it, it can't be recovered.",
            validate: checkNewPassword
        }))
    }
    function removePassword(kind, item) {
        withUnlocked(kind, item, () => dialog.ask("Remove the password from “" + (kind === "folder" ? item.name : item.title) + "”?", [], "Remove password", () => {
            run("removeLock", { kind: kind, id: item.id }, "Password removed")
        }, {
            danger: true,
            message: kind === "folder" ? "The folder and what's inside it are decrypted and open without a password." : "The notebook is decrypted and opens without a password."
        }))
    }
    readonly property bool anyUnlocked: library.folders.some(f => f.unlocked) || library.notebooks.some(n => n.unlocked) || shown.notebooks.some(n => n.unlocked)
    function lockNow() { run("lockAll", {}, "Locked") }
    /// The menu entries about a password.
    function lockEntries(kind, item) {
        if (!item.locked) return [{ icon: "lock", label: "Lock with password…", action: () => lockItem(kind, item) }]
        if (inheritedLock(kind, item)) return []
        return [
            { icon: "lock", label: "Change password…", action: () => changePassword(kind, item) },
            { icon: "unlock", label: "Remove password…", action: () => removePassword(kind, item) }
        ]
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
        return plural(nbs.length, "notebook", "notebooks") + " · last edited “" + latest.title + "”\u200E " + relative(latest.updatedAt)
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
        // (A phone has the round + button instead.)
        if (!Theme.phone && (folders.length || notebooks.length) && (v.kind === "all" || v.kind === "folder")) out.push({ type: "new" })
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
            { id: "folder", icon: "folder", title: "Folder", sub: "Group your documents", tint: "#ca8a04", iconColor: "#b45309", action: () => newFolder(fid) },
            { id: "sketchbook", icon: "brush", title: "Sketchbook", sub: "Blank pages for drawing", tint: "#db2777", action: () => newNotebook(fid, { heading: "New sketchbook", title: "Untitled sketchbook", template: "blank", orientation: "landscape", paper: "#ffffff", cover: "#111827", design: "minimal" }) },
            { id: "whiteboard", icon: "sidebar", title: "Whiteboard", sub: "One page with room to grow", tint: "#475569", action: () => { const nb = DocKinds.whiteboard("Whiteboard", fid || ""); if (nb && nb.id) root.openRequested(nb.id, nb.title) } }
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
    /// A new notebook (or, with a preset, another kind: a sketchbook…) in `fid`.
    function newNotebook(fid, preset) {
        newNbDialog.folder = fid
        newNbDialog.show(preset)
    }
    NewNotebookDialog {
        id: newNbDialog
        property var folder: null
        templates: root.templates
        designs: root.coverDesigns
        onCreated: r => {
            const size = Theme.paperSizes.find(p => p.id === r.size) || Theme.paperSizes[0]
            const landscape = r.orientation === "landscape"
            const nb = run("createNotebook", { input: { title: r.title, folderId: folder, template: r.template, coverColor: r.cover, paperColor: r.paper, width: landscape ? size.h : size.w, height: landscape ? size.w : size.h, coverPage: true } })
            if (!nb) return
            // The chosen cover design, its title and date, and its picture for the library.
            if (r.design !== "classic") {
                const pages = run("pages", { notebookId: nb.id })
                if (pages && pages.length) run("setPageTemplate", { pageId: pages[0].id, template: "cover-" + r.design, paperColor: r.cover })
            }
            // One of your templates: the first page (after the cover) shows it; new pages follow it.
            if (r.mine) {
                const pages = run("pages", { notebookId: nb.id })
                if (pages && pages.length > 1) run("usePageTemplate", { pageId: pages[1].id, templateId: r.mine.id })
            }
            DaftarApi.setUpCover(nb)
            root.openRequested(nb.id, nb.title)
        }
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
    readonly property var coverDesigns: (DaftarApi.coverDesigns()).map(d => ({ value: d.id, label: d.label }))
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
            else DaftarApi.applyCover(n, r.design, r.cover)
        }, { aside: coverPreview })
    }
    /// Folders something can move into (not a folder itself or anything inside it).
    function folderOptions(exclude) {
        const blocked = new Set()
        // One folder or several (they and everything inside can't be destinations).
        const excluded = !exclude ? [] : Array.isArray(exclude) ? exclude : [exclude]
        if (excluded.length) {
            for (const id of excluded) blocked.add(id)
            let grew = true
            while (grew) {
                grew = false
                for (const f of library.folders)
                    if (f.parentId && blocked.has(f.parentId) && !blocked.has(f.id)) { blocked.add(f.id); grew = true }
            }
        }
        return [{ value: "", label: "Library (no folder)" }].concat(library.folders.filter(f => !blocked.has(f.id)).map(f => ({ value: f.id, label: f.name })))
    }
    /// Moving into a locked folder: unlocked first (what moves in is encrypted with it).
    function intoFolder(id, then) {
        withUnlocked("folder", id ? library.folders.find(f => f.id === id) : null, then)
    }
    function moveNotebook(n) {
        dialog.ask("Move notebook", [{ name: "folder", label: "Destination", type: "select", value: n.folderId || "", options: folderOptions() }], "Move",
                   r => intoFolder(r.folder, () => run("moveNotebook", { id: n.id, folderId: r.folder || null }, "Notebook moved")))
    }
    function moveFolder(f) {
        dialog.ask("Move folder", [{ name: "folder", label: "Destination", type: "select", value: f.parentId || "", options: folderOptions(f.id) }], "Move",
                   r => intoFolder(r.folder, () => run("moveFolder", { id: f.id, parentId: r.folder || null }, "Folder moved")))
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
            { icon: "undo", label: "Recover", action: () => run("setNotebookTrashed", { id: n.id, trashed: false }, "Notebook recovered") },
            { separator: true },
            { icon: "trash", label: "Delete forever", danger: true, action: () => deleteForever("notebook", n.id, n.title) }
        ] : [
            { icon: "pen", label: "Rename", action: () => renameNotebook(n) },
            { icon: "star", label: n.favourite ? "Remove from favourites" : "Add to favourites", action: () => run("setNotebookFavourite", { id: n.id, favourite: !n.favourite }) },
            { icon: "book", label: "Change cover", action: () => changeCover(n) },
            { icon: "copy", label: "Duplicate", action: () => run("duplicateNotebook", { id: n.id }, "Notebook duplicated") },
            { icon: "folder", label: "Move to…", action: () => moveNotebook(n) }
        ].concat(lockEntries("notebook", n), [
            { separator: true },
            { icon: "trash", label: "Move to trash", danger: true, action: () => trashWithUndo("setNotebookTrashed", n.id, "Notebook moved to trash") }
        ]))
    }
    function folderMenu(f, anchor) {
        menu.show(anchor, view.kind === "trash" ? [
            { icon: "undo", label: "Recover", action: () => run("setFolderTrashed", { id: f.id, trashed: false }) },
            { separator: true },
            { icon: "trash", label: "Delete forever", danger: true, action: () => deleteForever("folder", f.id, f.name) }
        ] : [
            { icon: "palette", label: "Edit name and colour…", action: () => editFolder(f) },
            { icon: "star", label: f.favourite ? "Remove from favourites" : "Add to favourites", action: () => run("setFolderFavourite", { id: f.id, favourite: !f.favourite }) },
            { icon: "folder", label: "Move to…", action: () => moveFolder(f) }
        ].concat(lockEntries("folder", f), [
            { separator: true },
            { icon: "trash", label: "Move to trash", danger: true, action: () => trashWithUndo("setFolderTrashed", f.id, "Folder moved to trash") }
        ]))
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
        background: Rectangle { antialiasing: true; radius: Theme.radius; color: nav.active ? Theme.accentSoft : nav.hovered ? Theme.surface2 : "transparent"; Behavior on color { ColorAnimation { duration: 150 } } }
        contentItem: RowLayout {
            spacing: 11
            Item { Layout.preferredWidth: nav.indent }
            Icon { name: nav.iconName; size: 19; color: nav.active ? Theme.accent : Theme.muted }
            Label { text: nav.text; color: nav.active ? Theme.accent : Theme.text; font.weight: nav.active ? Font.DemiBold : Font.Normal; elide: Text.ElideRight; Layout.fillWidth: true }
            Rectangle { antialiasing: true;
                visible: nav.dot.a > 0
                Layout.rightMargin: 6
                Layout.preferredWidth: 13; Layout.preferredHeight: 13; radius: Theme.radiusXs
                color: Theme.surface
                Rectangle { antialiasing: true; anchors.centerIn: parent; width: 9; height: 9; radius: 2.5; color: nav.dot }
            }
            // How many: a soft pill in the accent when selected, quiet grey text otherwise.
            Rectangle { antialiasing: true;
                visible: nav.count.length > 0
                Layout.rightMargin: 6
                Layout.preferredHeight: 22
                Layout.preferredWidth: Math.max(26, countText.implicitWidth + 14)
                radius: Theme.radius
                color: nav.active ? Theme.alpha(Theme.accent, Theme.dark ? 0.22 : 0.16) : "transparent"
                border.width: nav.active ? 1 : 0
                border.color: Theme.alpha(Theme.accent, 0.35)
                Behavior on color { ColorAnimation { duration: 150 } }
                Label { id: countText; anchors.centerIn: parent; text: nav.count; font.pixelSize: 12; font.weight: Font.DemiBold; color: nav.active ? Theme.accent : Theme.muted }
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
            root.openNotebook(id, "")
        }
    }
    PopupMenu { id: menu }
    CreateMenu { id: createMenu }
    AiPanel {
        id: aiPanel
        onSettingsRequested: root.settingsRequested()
    }
    AssistantPanel {
        id: assistant
        onSettingsRequested: root.settingsRequested()
    }
    function askLibrary() {
        aiPanel.start({
            heading: "Ask your library",
            suggestions: [
                { label: "What did I write about…", prompt: "What did I write about ", fill: true },
                { label: "Summarise my recent notes", prompt: "Summarise what I've been working on recently, notebook by notebook." },
                { label: "My open to-dos", prompt: "List the to-dos, deadlines and open questions in my notes, with where each one is." },
                { label: "Where are my notes on…", prompt: "Which notebooks and pages have my notes on ", fill: true }
            ]
        })
    }
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
                visible: !!(!!dialog.values.design && dialog.values.design !== "none")
                readonly property bool landscape: dialog.values.orientation === "landscape"
                anchors.horizontalCenter: parent.horizontalCenter
                width: landscape ? 250 : 200
                height: landscape ? 177 : 283
                Shadow { anchors.fill: parent; radius: Theme.radius; level: 3 }
                LiveImage {
                    anchors.fill: parent
                    sourceSize: Qt.size(parent.width * 2, parent.height * 2)
                    source: parent.visible ? dialog.coverUrl(dialog.values.design) : ""
                }
                transform: Rotation { origin.x: 100; origin.y: 141; axis { x: 0; y: 1; z: 0 } angle: -8 }
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                // .nb-preview-caption: the design's name.
                text: {
                    const id = dialog.values.design && dialog.values.design !== "none" ? dialog.values.design : "classic"
                    const d = root.coverDesigns.find(c => c.value === id)
                    return (d ? d.label : "Classic") + " cover"
                }
                color: Theme.muted; font.pixelSize: 13; font.weight: Font.DemiBold
            }
        }
    }

    Shortcut { sequence: "/"; onActivated: search.forceActiveFocus() }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: !!(root.visible && !search.activeFocus && !dialog.opened && root.shelf.length > 0); onActivated: root.pickAll() }
    Shortcut { sequence: "Esc"; enabled: !!(root.visible && root.selecting && !dialog.opened); onActivated: root.clearPicks() }
    Shortcut {
        sequences: ["Delete"]
        enabled: !!(root.visible && root.pickedCount > 0 && !search.activeFocus && !dialog.opened)
        onActivated: root.view.kind === "trash" ? root.bulkDeleteForever() : root.bulkTrash()
    }
    Shortcut { sequence: "N"; enabled: !!(root.view.kind !== "trash" && !dialog.opened); onActivated: root.newNotebook(root.folderId || null) }

    Drawer {
        id: navDrawer
        edge: Qt.LeftEdge
        width: Math.min(300, root.width * 0.84)
        height: root.height
        interactive: Theme.phone
        padding: 0
        background: Rectangle { color: Theme.surface }
    }
    // A phone's New: a round button in the corner.
    RoundButton {
        id: fab
        z: 20
        visible: Theme.phone && root.view.kind !== "trash"
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 18
        width: 58; height: 58
        onClicked: createMenu.show(fab, root.createOptions())
        background: Item {
            Shadow { anchors.fill: parent; radius: width / 2; level: 3 }
            Rectangle { anchors.fill: parent; radius: width / 2; antialiasing: true; color: fab.pressed ? Theme.accentHover : Theme.accent }
        }
        contentItem: Item { Icon { anchors.centerIn: parent; name: "plus"; size: 24; color: Theme.accentContrast } }
    }
    RowLayout {
        id: body
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------------------ sidebar
        // (On a phone it slides in from the left, from ☰: it lives in navDrawer then.)
        Item {
            id: sideSlot
            visible: !Theme.phone
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.phone ? 0 : 248
        }
        Rectangle {
            id: sideBar
            parent: Theme.phone ? navDrawer.contentItem : sideSlot
            anchors.fill: parent
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
                    ToolTip.visible: !!(!!(hovered && !createMenu.opened)) && !tipDismissed
                    // Tooltips go away once clicked (until the pointer leaves), as in the web app.
                    property bool tipDismissed: false
                    onPressedChanged: if (pressed) tipDismissed = true
                    onHoveredChanged: if (!hovered) tipDismissed = false
                    ToolTip.delay: 600
                    ToolTip.text: "Create a notebook, daftar, PDF or folder"
                    background: Item {
                        Shadow { anchors.fill: parent; radius: Theme.radius; level: newBtn.hovered || createMenu.opened ? 2 : 1 }
                        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radius; color: Theme.surface; border.color: newBtn.hovered || createMenu.opened ? Theme.mix(Theme.accent, Theme.border, 0.45) : Theme.border }
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        Item { Layout.preferredWidth: 0 }
                        Rectangle { antialiasing: true;
                            Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: Theme.radius; color: Theme.accent
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
                    text: "Group notebooks into folders with\u00A0+"
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
                                iconName: modelData.f.locked && !modelData.f.unlocked ? "lock" : "folder"
                                dot: modelData.f.color
                                indent: modelData.depth * 14
                                active: !!(root.view.kind === "folder" && root.view.id === modelData.f.id)
                                onClicked: root.openFolder(modelData.f)
                            }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border; Layout.bottomMargin: 8 }
                // Sync: when it last happened, what's waiting, or that it didn't work (tap: sync now).
                NavItem {
                    id: syncItem
                    readonly property var st: root.services ? root.services.syncStatus : null
                    readonly property bool busy: !!(root.services && root.services.syncing)
                    property int clock: 0
                    visible: !!(st && st.available)
                    iconName: st && st.signedIn && st.lastError && !busy ? "cloudOff" : "cloud"
                    text: {
                        clock
                        if (!st || !st.signedIn) return "Sync with Google Drive"
                        if (busy) return "Syncing…"
                        if (st.offline) return st.pending > 0 ? "Offline · " + st.pending + " to sync later" : "Offline · will sync later"
                        if (st.lastError) return "Sync didn't work · retry"
                        if (st.pending > 0) return st.pending + (st.pending === 1 ? " change" : " changes") + " to sync"
                        return st.lastSyncAt ? "Synced " + root.services.relativeTime(st.lastSyncAt) : "Not synced yet"
                    }
                    ToolTip.visible: hovered && !!(st && st.signedIn)
                    ToolTip.delay: 500
                    ToolTip.text: st ? (st.lastError ? st.lastError : "Google Drive" + (st.email ? " · " + st.email : "") + (st.device ? " · this device: " + st.device : "")) : ""
                    onClicked: {
                        if (!st || !st.signedIn) { AppSettings.settingsPage = "sync"; root.settingsRequested() }
                        else root.services.syncNow(true)
                    }
                    // Kept current: "2 min ago", and changes made since.
                    Timer { running: root.visible && syncItem.visible; repeat: true; interval: 30000; onTriggered: { syncItem.clock++; if (root.services) root.services.refreshSync() } }
                    Component.onCompleted: if (root.services) root.services.refreshSync()
                }
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

            // Heading, search and sort. Narrow: the heading on a line of its own,
            // the buttons as icons.
            ColumnLayout {
                id: headCol
                Layout.fillWidth: true
                Layout.leftMargin: Theme.phone ? 14 : 36
                Layout.rightMargin: Theme.phone ? 14 : 36
                Layout.topMargin: root.narrowHead ? 22 : 30
                visible: root.narrowHead
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    IconButton { visible: Theme.phone; iconName: "menu"; tip: "Folders and more"; implicitWidth: 38; implicitHeight: 38; iconSize: 20; onClicked: navDrawer.open() }
                    Label { text: root.heading(); font.pixelSize: Theme.phone ? 22 : 26; font.weight: Font.Bold; font.letterSpacing: -0.5; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                }
                Label { text: root.summary(); color: Theme.muted; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.phone ? 14 : 36
                Layout.rightMargin: Theme.phone ? 14 : 36
                Layout.topMargin: root.narrowHead ? 12 : 30
                Layout.bottomMargin: 18
                spacing: root.narrowHead ? 10 : 20
                ColumnLayout {
                    visible: !root.narrowHead
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignBottom
                    spacing: 6
                    Label { text: root.heading(); font.pixelSize: 28; font.weight: Font.Bold; font.letterSpacing: -0.5; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { text: root.summary(); color: Theme.muted; elide: Text.ElideRight; Layout.fillWidth: true }
                }
                // Search.
                Item {
                    Layout.fillWidth: root.narrowHead
                    Layout.maximumWidth: root.narrowHead ? 420 : -1
                    Layout.alignment: Qt.AlignBottom
                    implicitWidth: searchRow.implicitWidth + 22
                    implicitHeight: 38
                    Rectangle { antialiasing: true; anchors.fill: parent; anchors.margins: -3; radius: Theme.radius + 3; color: Theme.accentSoft; visible: search.activeFocus }
                    Shadow { anchors.fill: parent; radius: Theme.radius; level: 1 }
                    Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radius; color: Theme.surface; border.color: search.activeFocus ? Theme.accent : Theme.border }
                    RowLayout {
                        id: searchRow
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: 8
                        Icon { name: "search"; size: 18; color: Theme.muted }
                        TextField {
                            id: search
                            Layout.preferredWidth: Theme.phone ? 120 : root.narrowHead ? 200 : 240
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
                        Rectangle { antialiasing: true;
                            visible: !search.activeFocus
                            Layout.preferredWidth: 20; Layout.preferredHeight: 20; radius: 5
                            color: Theme.surface2; border.color: Theme.border
                            Label { anchors.centerIn: parent; text: "/"; font.pixelSize: 11; color: Theme.muted }
                        }
                    }
                }
                UiButton {
                    Layout.alignment: Qt.AlignBottom
                    kind: "ghost"
                    iconName: "sparkle"
                    text: root.narrowHead ? "" : "Ask"
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: "Ask AI about your notes"
                    onClicked: assistant.opened ? assistant.close() : assistant.show(null)
                }
                IconButton {
                    visible: root.anyUnlocked
                    Layout.alignment: Qt.AlignBottom
                    iconName: "lock"
                    tip: "Lock now (locks every unlocked notebook and folder again)"
                    onClicked: root.lockNow()
                }
                UiButton {
                    visible: root.shelf.some(e => e.type !== "new")
                    Layout.alignment: Qt.AlignBottom
                    kind: root.selecting ? "primary" : "ghost"
                    iconName: "check"
                    text: root.selecting ? "Done" : root.narrowHead ? "" : "Select"
                    onClicked: root.selecting ? root.clearPicks() : (root.selectMode = true)
                }
                UiButton {
                    id: sortBtn
                    visible: root.view.kind !== "trash"
                    Layout.alignment: Qt.AlignBottom
                    kind: "ghost"
                    iconName: "sort"
                    text: root.narrowHead ? "" : (root.sorts.find(s => s.id === prefs.sort) || root.sorts[0]).label
                    ToolTip.visible: root.narrowHead && hovered
                    ToolTip.text: "Sort: " + (root.sorts.find(s => s.id === prefs.sort) || root.sorts[0]).label
                    onClicked: menu.show(sortBtn, [{ header: "SORT BY" }].concat(root.sorts.map(s => ({ icon: s.icon, label: s.label, checked: prefs.sort === s.id, action: () => prefs.sort = s.id }))))
                }
                UiButton {
                    visible: root.view.kind === "trash"
                    Layout.alignment: Qt.AlignBottom
                    kind: "danger"
                    iconName: "trash"
                    text: Theme.phone ? "Empty" : "Empty trash"
                    onClicked: root.emptyTrash()
                }
            }

            // Search results.
            ListView {
                id: results
                visible: !!(root.query.trim().length > 0 && root.view.kind !== "trash")
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
                // Nothing found: what was looked for, and how to find more.
                ColumnLayout {
                    visible: root.hits.length === 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 50
                    width: Math.min(420, results.width - 40)
                    spacing: 6
                    Rectangle {
                        antialiasing: true
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 84; Layout.preferredHeight: 84; radius: Theme.radiusXl + 4
                        color: Theme.alpha(Theme.accent, 0.10)
                        Icon { anchors.centerIn: parent; name: "search"; size: 36; color: Theme.accent }
                    }
                    Label { Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 12; text: "Nothing found for “" + root.query + "”"; font.pixelSize: 17; font.weight: Font.Bold; color: Theme.text; elide: Text.ElideRight; Layout.maximumWidth: parent.width }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        lineHeight: 1.2
                        text: "Try fewer or other words. Search looks in titles, typed text and PDFs" + (Ai.ready ? (Ai.readingOn ? ", and your handwriting." : "; turn on “Make handwriting searchable” in Settings → AI to search handwriting too.") : ".")
                    }
                }
                delegate: AbstractButton {
                    id: hit
                    required property var modelData
                    width: Math.min(760, results.width)
                    implicitHeight: hitCol.implicitHeight + 28
                    hoverEnabled: true
                    onClicked: root.openNotebook(modelData.notebookId, modelData.notebookTitle)
                    transform: Translate { x: hit.hovered ? 2 : 0; Behavior on x { NumberAnimation { duration: 200 } } }
                    background: Item {
                        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 1 }
                        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: hit.hovered ? Theme.accent : Theme.border }
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
                visible: !!(!results.visible && root.shelf.length === 0)
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: empty.implicitHeight + 140
                clip: true
                ColumnLayout {
                    id: empty
                    y: 70
                    width: parent.width
                    spacing: 0
                    // Trash, favourites, an empty folder: their own picture.
                    Item {
                        visible: root.view.kind === "trash" || root.view.kind === "favourites" || root.view.kind === "folder"
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 120; Layout.preferredHeight: 120
                        Rectangle { antialiasing: true; anchors.centerIn: parent; width: 108; height: 108; radius: 30; color: Theme.alpha(Theme.accent, 0.10) }
                        Rectangle { antialiasing: true; anchors.centerIn: parent; width: 76; height: 76; radius: 22; color: Theme.alpha(Theme.accent, 0.12) }
                        Icon {
                            anchors.centerIn: parent
                            name: root.view.kind === "trash" ? "trash" : root.view.kind === "favourites" ? "star" : "folder"
                            size: 40
                            color: Theme.accent
                        }
                    }
                    Image {
                        visible: !(root.view.kind === "trash" || root.view.kind === "favourites" || root.view.kind === "folder")
                        Layout.alignment: Qt.AlignHCenter
                        sourceSize: Qt.size(320, 240)
                        Layout.preferredWidth: 160; Layout.preferredHeight: 120
                        source: "data:image/svg+xml;utf8," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 160 120" width="160" height="120"><ellipse cx="80" cy="108" rx="58" ry="7" fill="' + Theme.text + '" opacity="0.08"/><g transform="rotate(-8 60 70)"><rect x="34" y="28" width="52" height="70" rx="5" fill="#e07a5f"/><rect x="34" y="28" width="8" height="70" rx="3" fill="#000" opacity="0.15"/><rect x="48" y="44" width="30" height="12" rx="3" fill="#fff" opacity="0.9"/></g><g transform="rotate(6 98 66)"><rect x="70" y="22" width="54" height="74" rx="5" fill="' + Theme.accent + '"/><rect x="70" y="22" width="8" height="74" rx="3" fill="#000" opacity="0.15"/><rect x="84" y="38" width="32" height="13" rx="3" fill="#fff" opacity="0.92"/><path d="M84 66h30M84 74h24" stroke="#fff" stroke-opacity="0.5" stroke-width="2" stroke-linecap="round"/></g><g transform="rotate(38 128 40)"><rect x="122" y="6" width="9" height="58" rx="3" fill="#3d405b"/><path d="M122 64l4.5 10 4.5-10z" fill="#3d405b"/><rect x="122" y="6" width="9" height="10" rx="3" fill="#000" opacity="0.2"/></g></svg>')
                    }
                    readonly property var texts: root.view.kind === "trash" ? ["Trash is empty", "Notebooks and folders you delete wait here before they're gone for good."]
                        : root.view.kind === "favourites" ? ["No favourites yet", "Choose “Add to favourites” in a notebook's or folder's ⋯ menu to keep it here, close at hand."]
                        : root.view.kind === "folder" ? ["This folder is empty", "Make something new here, or move a notebook in with “Move to…” in its ⋯ menu."]
                        : ["Your shelf is empty", "Start a notebook, a daftar annossous, or write on a PDF."]
                    Label { Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 18; text: empty.texts[0]; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text }
                    Label { Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 6; Layout.maximumWidth: 380; text: empty.texts[1]; color: Theme.muted; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; lineHeight: 1.2 }
                    GridLayout {
                        visible: root.canCreate
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 32
                        // At most 560 wide (.create-cards), centred.
                        Layout.fillWidth: false
                        Layout.preferredWidth: Math.min(560, empty.width - 40)
                        Layout.maximumWidth: Math.min(560, empty.width - 40)
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 10
                        Repeater {
                            // (Kept while hidden: emptying it under the layout crashed Qt.)
                            model: root.createOptions()
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
                visible: !!(!results.visible && root.shelf.length > 0)
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.phone ? 14 : 36
                Layout.rightMargin: Theme.phone ? 14 - 14 : 36 - 28
                topMargin: 14
                bottomMargin: 80
                clip: true
                readonly property int columns: Theme.phone ? 2 : Math.max(1, Math.floor(width / (178 + 28)))
                cellWidth: Math.floor(width / columns)
                cellHeight: (cellWidth - 28) * 4 / 3 + 14 + 20 + 3 + 16 + 34
                model: root.shelf
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                cacheBuffer: 600
                // The wheel and the touchpad at the speeds chosen in Settings (Pen & touch → Scrolling).
                function scrollTo(y) { return Math.max(originY - topMargin, Math.min(y, originY + contentHeight + bottomMargin - height)) }
                NumberAnimation { id: glide; target: grid; property: "contentY"; duration: 180; easing.type: Easing.OutCubic }
                MouseArea {
                    parent: grid
                    anchors.fill: parent
                    z: 100
                    acceptedButtons: Qt.NoButton
                    onWheel: w => {
                        if (w.modifiers & Qt.ControlModifier) return w.accepted = false
                        const touchpad = w.pixelDelta.y !== 0
                        if (touchpad) {
                            glide.stop()
                            grid.contentY = grid.scrollTo(grid.contentY - w.pixelDelta.y * AppSettings.store.touchpadSpeed)
                        } else {
                            const from = glide.running ? glide.to : grid.contentY
                            glide.stop()
                            glide.to = grid.scrollTo(from - w.angleDelta.y / 120 * 140 * AppSettings.store.wheelSpeed)
                            glide.start()
                        }
                    }
                }
                delegate: Item {
                    id: cell
                    required property var modelData
                    width: grid.cellWidth
                    height: grid.cellHeight
                    NewTile {
                        visible: cell.modelData.type === "new"
                        width: parent.width - 28
                        options: root.createOptions()
                        choice: AppSettings.store.newCard
                        onChooseRequested: anchor => menu.show(anchor, [{ header: "This card makes" }].concat(root.createOptions().map(o => ({ icon: o.icon, label: o.title, checked: o.id === AppSettings.store.newCard, action: () => AppSettings.store.newCard = o.id }))))
                    }
                    Card {
                        visible: cell.modelData.type !== "new"
                        width: parent.width - 28
                        isFolder: cell.modelData.type === "folder"
                        item: cell.modelData.item || ({})
                        trash: root.view.kind === "trash"
                        selecting: root.selecting
                        picked: !!(cell.modelData.item && root.picked[root.keyOf(cell.modelData)])
                        onToggled: range => root.pick(cell.modelData, range)
                        meta: !cell.modelData.item ? "" : isFolder ? root.folderMeta(cell.modelData.item) : root.plural(cell.modelData.item.pageCount, "page", "pages") + " · " + root.relative(cell.modelData.item.updatedAt)
                        sheets: isFolder ? root.shown.notebooks.filter(n => n.folderId === cell.modelData.item.id).sort((a, b) => b.updatedAt - a.updatedAt).slice(0, 2) : []
                        count: isFolder ? root.shown.notebooks.filter(n => n.folderId === cell.modelData.item.id).length : 0
                        onActivated: {
                            if (trash) return
                            if (isFolder) root.openFolder(item)
                            else root.openNotebook(item.id, item.title)
                        }
                        onMoreRequested: anchor => isFolder ? root.folderMenu(item, anchor) : root.notebookMenu(item, anchor)
                    }
                }
            }

            // What to do with the picked items.
            Item {
                visible: root.selecting
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                Rectangle {
                    antialiasing: true
                    anchors.centerIn: parent
                    width: barRow.implicitWidth + 28
                    height: 50
                    radius: Theme.radiusLg
                    color: Theme.surface
                    border.color: Theme.border
                    Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 2; z: -1 }
                    RowLayout {
                        id: barRow
                        anchors.centerIn: parent
                        spacing: 6
                        Label {
                            text: root.pickedCount ? root.pickedCount + " selected" : "Tap items to select them"
                            font.weight: Font.DemiBold
                            color: Theme.text
                            Layout.rightMargin: 8
                            Layout.leftMargin: 4
                        }
                        UiButton { kind: "ghost"; text: "Select all"; onClicked: root.pickAll() }
                        UiButton { visible: root.view.kind !== "trash"; enabled: root.pickedCount > 0; kind: "ghost"; iconName: "star"; text: root.pickedCount && root.pickedEntries().every(e => e.item.favourite) ? "Unfavourite" : "Favourite"; onClicked: root.bulkFavourite() }
                        UiButton { visible: root.view.kind !== "trash"; enabled: root.pickedCount > 0; kind: "ghost"; iconName: "folder"; text: "Move to…"; onClicked: root.bulkMove() }
                        UiButton { visible: root.view.kind !== "trash"; enabled: root.pickedCount > 0; kind: "danger"; iconName: "trash"; text: "Move to trash"; onClicked: root.bulkTrash() }
                        UiButton { visible: root.view.kind === "trash"; enabled: root.pickedCount > 0; kind: "ghost"; iconName: "undo"; text: "Recover"; onClicked: root.bulkRecover() }
                        UiButton { visible: root.view.kind === "trash"; enabled: root.pickedCount > 0; kind: "danger"; iconName: "trash"; text: "Delete forever"; onClicked: root.bulkDeleteForever() }
                        IconButton { iconName: "close"; tip: "Done (Esc)"; implicitWidth: 34; implicitHeight: 34; onClicked: root.clearPicks() }
                    }
                }
            }
        }
    }
}
