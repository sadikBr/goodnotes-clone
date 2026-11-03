// The app window: the library, or a notebook open in the editor.
import QtQuick
import QtQuick.Controls
import Inkleaf

ApplicationWindow {
    id: win
    width: 1280
    height: 860
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: openNotebook ? openTitle + " — Inkleaf" : "Inkleaf"
    color: Theme.bg
    font.family: Theme.font

    property string openNotebook: ""
    property string openTitle: ""

    Component.onCompleted: AppSettings.dark  // applies the theme
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
        }
    }
}
