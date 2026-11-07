// Background work for the app window (src/syncSettings.ts, backupSettings.ts,
// updates.ts): Google Drive sync at start, every two minutes, when a
// notebook is closed and when you leave the app (and come back to it); the
// daily backup check; looking for new versions.
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
    /// Open notebooks whose changes from elsewhere were waiting (told once each).
    property var toldDeferred: []
    /// When the app went to the background (to sync again on coming back after a while).
    property double awaySince: 0
    /// While signing in: { verificationUrl } (Google's page, opened in the browser).
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
            // The open notebook changed on another device: said once, it comes in when closed here.
            for (const t of (r.deferred || [])) {
                if (root.toldDeferred.indexOf(t) >= 0) continue
                root.toldDeferred = root.toldDeferred.concat([t])
                root.notify("“" + t + "” was changed on another device: those changes come in when you close it here", false)
            }
            if (r.conflicts.length) root.notify("Changed on both devices, both versions kept: " + r.conflicts.join(", "), false)
            else if (manual) root.notify(r.uploaded || r.downloaded || r.deleted ? "Synced: " + r.uploaded + " sent, " + r.downloaded + " received" : "Everything is in sync", false)
        })
    }
    function defaultDevice() {
        if (Qt.platform.os === "android") {
            const sc = Qt.application.screens[0]
            return sc && Math.min(sc.width, sc.height) < 600 ? "Phone" : "Tablet"
        }
        return Qt.platform.os === "windows" ? "Windows PC" : "Computer"
    }
    function signIn() {
        InkleafCore.callAsync("syncSignInStart", {}, (code, err) => {
            if (err) return root.notify(err, true)
            root.signInCode = code
            Qt.openUrlExternally(code.verificationUrl)
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
        /// The version that ran last (to say what's new after an update).
        property string lastVersion: ""
    }
    function setAutoCheck(on) { prefs.autoUpdate = on }

    /// What's new in this version, on the update card: { version, notes, updated }.
    property var whatsNew: null
    function showWhatsNew() {
        const n = Updater.notesFor(version)
        if (!n) return root.notify("No notes for this version", false)
        whatsNew = { version: version, notes: n, updated: false }
    }
    function closeWhatsNew() { whatsNew = null }
    /// After an update (a newer version than last time): what's new in it.
    function greetUpdate() {
        const last = prefs.lastVersion
        prefs.lastVersion = version
        if (last && !newer(version, last)) return
        // Versions before 0.12 didn't note theirs: an existing library means an update.
        if (!last) {
            try { if (InkleafCore.call("library").notebooks.length === 0) return } catch (e) { return }
        }
        const n = Updater.notesFor(version)
        if (n) whatsNew = { version: version, notes: n, updated: true }
    }

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
        const ends = os === "windows" ? ["_setup.exe", "_windows_x64.zip"] : os === "android" ? [".apk"] : os === "osx" ? [".dmg"] : [".AppImage"]
        const assets = rel.assets || []
        for (const e of ends) {
            const a = assets.find(x => x.name.endsWith(e))
            if (a) return a.browser_download_url
        }
        return rel.html_url
    }
    /** The release file this copy can install itself ({ url, sha256, size }), or null. */
    function installableFor(rel) {
        if (!Updater.canInstall) return null
        const end = { setup: "_setup.exe", appimage: ".AppImage", apk: ".apk" }[Updater.kind]
        const a = end ? (rel.assets || []).find(x => x.name.endsWith(end)) : null
        if (!a) return null
        // GitHub publishes each file's checksum ("sha256:…").
        const digest = String(a.digest || "")
        return { url: a.browser_download_url, sha256: digest.startsWith("sha256:") ? digest.slice(7) : "", size: a.size || 0 }
    }
    /** Downloads the update (the banner shows how far it got). */
    function startUpdate() {
        if (!update || !update.install) return downloadUpdate()
        Updater.download(update.install.url, update.install.sha256, update.install.size, update.version)
    }
    /** Installs the downloaded update: saves the open notebook, then restarts into the new version. */
    function installUpdate() {
        try { prepare() } catch (e) {}
        // Let the save finish, then hand over.
        Qt.callLater(() => Updater.install())
    }
    /** Looks for an update; `manual` also reports "up to date" and errors. */
    function checkForUpdates(manual) {
        // Android: Qt has no HTTPS there (no OpenSSL), so the core fetches it.
        if (Qt.platform.os === "android") {
            InkleafCore.callAsync("fetchJson", { url: Updater.releasesUrl(repo), userAgent: "Inkleaf/" + version }, (rel, err) => {
                if (err) {
                    if (manual) root.notify("Couldn't reach GitHub (" + (String(err).indexOf("offline") >= 0 ? "offline" : err) + ")", true)
                    return
                }
                root.gotRelease(rel, manual)
            })
            return
        }
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
            root.gotRelease(rel, manual)
        }
        xhr.open("GET", Updater.releasesUrl(repo))
        xhr.setRequestHeader("Accept", "application/vnd.github+json")
        xhr.setRequestHeader("User-Agent", "Inkleaf/" + version)
        xhr.send()
    }
    /** The latest release, found: an update when it's newer (else "up to date" when asked). */
    function gotRelease(rel, manual) {
        if (!rel || !rel.tag_name || !root.newer(rel.tag_name, root.version)) {
            if (manual) root.notify("Inkleaf is up to date", false)
            return
        }
        const v = rel.tag_name.replace(/^v/, "")
        if (!manual && root.later.indexOf(v) >= 0) return
        root.update = { version: v, notes: String(rel.body || "").trim(), url: root.assetFor(rel), install: root.installableFor(rel) }
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
        Updater.cancel()
        later = later.concat([update.version])
        update = null
    }

    // ------------------------------------------------------------- timers

    property list<QtObject> timers: [
        Timer { running: true; interval: 4000; onTriggered: root.syncNow(false) },
        Timer { running: true; repeat: true; interval: 2 * 60000; onTriggered: root.syncNow(false) },
        Timer { running: true; interval: 20000; onTriggered: root.checkBackup() },
        Timer { running: true; repeat: true; interval: 30 * 60000; onTriggered: root.checkBackup() },
        Timer { running: true; interval: 1500; onTriggered: root.greetUpdate() },
        Timer { running: true; interval: 8000; onTriggered: if (root.autoCheck) root.checkForUpdates(false) },
        Timer { running: true; repeat: true; interval: 6 * 3600000; onTriggered: if (root.autoCheck) root.checkForUpdates(false) }
    ]
    // Leaving the app (another app, the home screen, the phone locked; on a
    // computer, another window): what was written goes to Drive now, so the
    // other device has it. Coming back after a while: what changed there comes in.
    property Connections appState: Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive) {
                if (root.awaySince > 0 && Date.now() - root.awaySince > 30000) root.syncNow(false)
                root.awaySince = 0
            } else if (root.awaySince === 0) {
                root.awaySince = Date.now()
                leaveSync.restart()
            }
        }
    }
    // (A moment later: the notebook saves itself first, and a quick flick between windows doesn't count.)
    property Timer leaveSync: Timer { interval: 1500; onTriggered: if (root.awaySince > 0) root.syncNow(false) }
    Component.onCompleted: { refreshSync(); refreshBackup() }
}
