// Background work for the app window (src/syncSettings.ts, backupSettings.ts,
// updates.ts): Google Drive sync at start, every five minutes and when a
// notebook is closed; the daily backup check; looking for new versions.
// The Settings screen shows and changes their state.
import QtQuick
import Inkleaf

QtObject {
    id: root
    signal notify(string message, bool error)
    /// The library was replaced (restore) or changed by sync.
    signal libraryReplaced()
    /// Saves the open notebook; returns its id (sync leaves it alone) or "".
    property var prepare: function () { return "" }

    readonly property string repo: "sadikBr/goodnotes-clone"
    readonly property string version: Qt.application.version

    function pad(n) { return (n < 10 ? "0" : "") + n }
    /** Local date and time for backup file names, e.g. "2026-11-03 0915". */
    function backupLabel() {
        const d = new Date()
        return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) + " " + pad(d.getHours()) + pad(d.getMinutes())
    }
    function relativeTime(ms) {
        const min = Math.round((Date.now() - ms) / 60000)
        if (min < 1) return "just now"
        if (min < 60) return min + " min ago"
        const hours = Math.round(min / 60)
        if (hours < 24) return hours + " h ago"
        return new Date(ms).toLocaleDateString()
    }
    function fileName(p) { return String(p || "").split(/[\\/]/).pop() }

    // ------------------------------------------------------------- sync

    property var syncStatus: null
    property bool syncing: false
    /// While signing in: { userCode, verificationUrl }.
    property var signInCode: null

    function refreshSync() {
        InkleafCore.callAsync("syncStatus", {}, (s, err) => { if (!err) root.syncStatus = s })
    }
    /** One sync now (quiet unless `manual`); skipped when not signed in. */
    function syncNow(manual) {
        if (syncing) return
        if (!syncStatus || !syncStatus.signedIn) {
            if (!syncStatus) refreshSync()
            return
        }
        syncing = true
        let open = ""
        try { open = prepare() || "" } catch (e) {}
        InkleafCore.callAsync("syncNow", { open: open || null }, (r, err) => {
            root.syncing = false
            root.refreshSync()
            if (err) {
                if (manual) root.notify(err, true)
                return
            }
            if (r.downloaded || r.deleted || r.conflicts.length) root.libraryReplaced()
            if (r.conflicts.length) root.notify("Changed on both devices, both versions kept: " + r.conflicts.join(", "), false)
            else if (manual) root.notify(r.uploaded || r.downloaded || r.deleted ? "Synced: " + r.uploaded + " sent, " + r.downloaded + " received" : "Everything is in sync", false)
        })
    }
    function defaultDevice() { return Qt.platform.os === "android" ? "Tablet" : Qt.platform.os === "windows" ? "Windows PC" : "Computer" }
    function signIn() {
        InkleafCore.callAsync("syncSignInStart", {}, (code, err) => {
            if (err) return root.notify(err, true)
            root.signInCode = code
            InkleafCore.callAsync("syncSignInWait", { device: root.defaultDevice() }, (s, err2) => {
                root.signInCode = null
                if (err2) {
                    if (err2.indexOf("cancelled") < 0) root.notify(err2, true)
                    return
                }
                root.syncStatus = s
                root.notify("Signed in" + (s.email ? " as " + s.email : "") + ". Syncing…", false)
                root.syncNow(true)
            })
        })
    }
    function cancelSignIn() {
        InkleafCore.callAsync("syncSignInCancel", {}, () => {})
        signInCode = null
    }
    function signOut() {
        InkleafCore.callAsync("syncSignOut", {}, (s, err) => {
            if (err) return root.notify(err, true)
            root.syncStatus = s
            root.notify("Signed out. Your notebooks stay on this device and in Drive.", false)
        })
    }

    // ------------------------------------------------------------- backups

    property var backup: null
    property bool backingUp: false
    property string reportedBackupError: ""

    function refreshBackup() {
        InkleafCore.callAsync("backupStatus", {}, (c, err) => { if (!err) root.backup = c })
    }
    function setBackup(folder, daily, keep, then) {
        InkleafCore.callAsync("setBackupSettings", { folder: folder, daily: daily, keep: keep }, (c, err) => {
            if (err) root.notify(err, true)
            else root.backup = c
            if (then && !err) then()
        })
    }
    function chooseFolder(folder) {
        const first = !backup || !backup.folder
        setBackup(folder, backup ? backup.daily : true, backup ? backup.keep : 14, () => {
            if (!root.backup.daily) return
            InkleafCore.callAsync("backupIfDue", { label: root.backupLabel() }, (c, err) => {
                if (err) return
                root.backup = c
                if (!c.lastError && first) root.notify("First backup saved", false)
            })
        })
    }
    function backupNow() {
        backingUp = true
        InkleafCore.callAsync("backupNow", { label: backupLabel() }, (c, err) => {
            root.backingUp = false
            if (err) {
                root.notify(err, true)
                root.refreshBackup()
                return
            }
            root.backup = c
            root.notify("Backup saved", false)
        })
    }
    function checkBackup() {
        InkleafCore.callAsync("backupIfDue", { label: backupLabel() }, (c, err) => {
            if (err) return
            root.backup = c
            // Say once per session when backups can't be made (e.g. the drive is missing).
            if (c.lastError && c.lastError !== root.reportedBackupError) root.notify("Backup didn't run: " + c.lastError, true)
            root.reportedBackupError = c.lastError || ""
        })
    }
    function exportLibrary(path) {
        try { prepare() } catch (e) {}
        InkleafCore.callAsync("exportLibrary", { path: path }, (p, err) => {
            if (err) root.notify(err, true)
            else root.notify("Saved " + root.fileName(p), false)
        })
    }
    /** What a backup file holds, for the confirmation: calls back with a sentence (or an error). */
    function inspectBackup(path, done) {
        InkleafCore.callAsync("inspectBackup", { path: path }, (s, err) => {
            if (err) return root.notify(err, true)
            const n = (k, one) => s[k] + " " + one + (s[k] === 1 ? "" : "s")
            done(n("notebooks", "notebook") + ", " + n("folders", "folder") + ", " + n("pages", "page"))
        })
    }
    function restore(path) {
        try { prepare() } catch (e) {}
        InkleafCore.callAsync("restoreBackup", { path: path }, (r, err) => {
            if (err) return root.notify(err, true)
            root.notify("Backup restored", false)
            root.libraryReplaced()
        })
    }

    // ------------------------------------------------------------- updates

    property bool autoCheck: prefs.autoUpdate
    property var update: null
    property var later: []
    readonly property Prefs prefs: Prefs {
        category: "updates"
        property bool autoUpdate: true
    }
    function setAutoCheck(on) { prefs.autoUpdate = on }

    /** "0.10.1" > "0.9.3". */
    function newer(a, b) {
        const pa = String(a).replace(/^v/, "").split(".").map(x => parseInt(x, 10) || 0)
        const pb = String(b).replace(/^v/, "").split(".").map(x => parseInt(x, 10) || 0)
        for (let i = 0; i < Math.max(pa.length, pb.length); i++)
            if ((pa[i] || 0) !== (pb[i] || 0)) return (pa[i] || 0) > (pb[i] || 0)
        return false
    }
    /** The download for this kind of computer in a release (else its page). */
    function assetFor(rel) {
        const os = Qt.platform.os
        const ends = os === "windows" ? [".exe", ".msi"] : os === "android" ? [".apk"] : os === "osx" ? [".dmg"] : [".AppImage"]
        const assets = rel.assets || []
        for (const e of ends) {
            const qt = assets.find(a => a.name.endsWith(e) && /qt/i.test(a.name))
            if (qt) return qt.browser_download_url
        }
        for (const e of ends) {
            const any = assets.find(a => a.name.endsWith(e))
            if (any) return any.browser_download_url
        }
        return rel.html_url
    }
    /** Looks for an update; `manual` also reports "up to date" and errors. */
    function checkForUpdates(manual) {
        if (pending) pending.abort()
        const xhr = new XMLHttpRequest()
        pending = xhr
        pendingManual = manual
        updateTimeout.restart()
        xhr.onreadystatechange = () => {
            if (xhr.readyState !== XMLHttpRequest.DONE) return
            if (root.pending !== xhr) return
            root.pending = null
            updateTimeout.stop()
            if (xhr.status !== 200) {
                if (manual) root.notify("Couldn't reach GitHub (" + (xhr.status || "offline") + ")", true)
                return
            }
            let rel
            try { rel = JSON.parse(xhr.responseText) } catch (e) { return }
            if (!rel.tag_name || !root.newer(rel.tag_name, root.version)) {
                if (manual) root.notify("Inkleaf is up to date", false)
                return
            }
            const v = rel.tag_name.replace(/^v/, "")
            if (!manual && root.later.indexOf(v) >= 0) return
            root.update = { version: v, notes: String(rel.body || "").trim(), url: root.assetFor(rel) }
        }
        xhr.open("GET", "https://api.github.com/repos/" + repo + "/releases/latest")
        xhr.setRequestHeader("Accept", "application/vnd.github+json")
        xhr.setRequestHeader("User-Agent", "Inkleaf/" + version)
        xhr.send()
    }
    property var pending: null
    property bool pendingManual: false
    // No answer from GitHub in time: give up (and say so when asked).
    property Timer updateTimeout: Timer {
        interval: 20000
        onTriggered: {
            const x = root.pending
            root.pending = null
            if (x) x.abort()
            if (root.pendingManual) root.notify("Couldn't reach GitHub (no answer)", true)
        }
    }
    function downloadUpdate() {
        if (!update) return
        Qt.openUrlExternally(update.url)
        update = null
    }
    function updateLater() {
        if (!update) return
        later = later.concat([update.version])
        update = null
    }

    // ------------------------------------------------------------- timers

    property list<QtObject> timers: [
        Timer { running: true; interval: 4000; onTriggered: root.syncNow(false) },
        Timer { running: true; repeat: true; interval: 5 * 60000; onTriggered: root.syncNow(false) },
        Timer { running: true; interval: 20000; onTriggered: root.checkBackup() },
        Timer { running: true; repeat: true; interval: 30 * 60000; onTriggered: root.checkBackup() },
        Timer { running: true; interval: 8000; onTriggered: if (root.autoCheck) root.checkForUpdates(false) },
        Timer { running: true; repeat: true; interval: 6 * 3600000; onTriggered: if (root.autoCheck) root.checkForUpdates(false) }
    ]
    Component.onCompleted: { refreshSync(); refreshBackup() }
}
