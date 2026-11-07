// The app window: the library, or a notebook open in the editor.
import QtQuick
import QtQuick.Controls
import Inkleaf

ApplicationWindow {
    id: win
    width: 1280
    height: 860
    // (On Android the window is the screen, a phone's included.)
    minimumWidth: Qt.platform.os === "android" ? 0 : 760
    minimumHeight: Qt.platform.os === "android" ? 0 : 520
    visible: true
    title: openNotebook ? openTitle + " — Inkleaf" : "Inkleaf"
    color: Theme.bg
    font.family: Theme.font

    property string openNotebook: ""
    // Phone-sized: the screens lay themselves out for it.
    Binding { target: Theme; property: "phone"; value: win.width < 600 }
    property string openTitle: ""

    // Applies the theme; starts the AI (reading handwriting in the background); takes the keyboard.
    Component.onCompleted: { AppSettings.dark; Ai.refresh(); requestActivate() }
    SettingsDialog { id: settings; services: services }
    Services {
        id: services
        onNotify: (message, error) => toast.show(message, error)
        // Saves the open notebook first; sync leaves it alone.
        prepare: () => win.openNotebook && loader.item && loader.item.saveForSync ? loader.item.saveForSync() : ""
        onLibraryReplaced: {
            // Show what changed: the library again (a restored library has other notebooks).
            let still = false
            try { still = !!win.openNotebook && !!InkleafCore.call("notebook", { id: win.openNotebook }) } catch (e) {}
            if (!still) win.openNotebook = ""
            loader.active = false
            loader.active = true
        }
    }

    function open(id, title) {
        openTitle = title
        openNotebook = id
    }
    /// From one notebook to another (a link): this one closes (and saves), then that one opens.
    property var pendingOpen: null
    function switchTo(id, title) {
        pendingOpen = { id: id, title: title }
        closeNotebook()
        switchTimer.restart()
    }
    Timer { id: switchTimer; interval: 1; onTriggered: { const p = win.pendingOpen; win.pendingOpen = null; if (p) win.open(p.id, p.title) } }
    // Android's back gesture closes the window: from a notebook it goes back to the library.
    onClosing: close => {
        if (Qt.platform.os === "android" && openNotebook) {
            close.accepted = false
            if (loader.item && loader.item.saveForSync) loader.item.saveForSync()
            closeNotebook()
        }
    }
    function closeNotebook() {
        openNotebook = ""
        // Send what was written in it.
        Qt.callLater(() => services.syncNow(false))
    }

    Loader {
        id: loader
        anchors.fill: parent
        sourceComponent: win.openNotebook ? editorPage : libraryPage
    }
    Toast { id: toast }
    SignInDialog { services: services }
    UpdateBanner { services: services }

    Component {
        id: libraryPage
        Library {
            onOpenRequested: (id, title) => win.open(id, title)
            onToggleDark: AppSettings.store.theme = Theme.dark ? "light" : "dark"
            onNotify: (message, error) => toast.show(message, error)
            onSettingsRequested: settings.open()
        }
    }
    Component {
        id: editorPage
        Editor {
            notebookId: win.openNotebook
            title: win.openTitle
            onBack: win.closeNotebook()
            onSettingsRequested: settings.open()
            onNotify: (message, error) => toast.show(message, error)
            // A link to another notebook: this one closes (and saves), that one opens.
            onOpenOther: (id, title) => win.switchTo(id, title)
        }
    }
}
