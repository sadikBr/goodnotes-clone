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

    Prefs {
        id: prefs
        category: "app"
        property bool dark: false
    }
    Component.onCompleted: Theme.dark = prefs.dark

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
            onToggleDark: {
                Theme.dark = !Theme.dark
                prefs.dark = Theme.dark
            }
            onNotify: (message, error) => toast.show(message, error)
            onSettingsRequested: toast.show("Settings are coming to the native app next.", false)
        }
    }
    Component {
        id: editorPage
        Editor {
            notebookId: win.openNotebook
            title: win.openTitle
            onBack: win.closeNotebook()
        }
    }
}
