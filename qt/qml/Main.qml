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
    SettingsDialog { id: settings }

    function open(id, title) {
        openTitle = title
        openNotebook = id
    }
    function closeNotebook() {
        openNotebook = ""
    }

    Loader {
        id: loader
        anchors.fill: parent
        sourceComponent: win.openNotebook ? editorPage : libraryPage
    }
    Toast { id: toast }

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
        }
    }
}
