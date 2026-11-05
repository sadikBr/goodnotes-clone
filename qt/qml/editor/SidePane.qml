// A second page view beside the notebook: another notebook (a textbook PDF
// next to your notes) to write in as well, or this notebook itself (another
// page of the daftar) to look at while you write on the left, kept up to
// date. The tools are the editor's; they act on the side you last touched.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Rectangle {
    id: root
    required property var prefs
    /// The notebook open in the editor.
    required property string mainNotebookId
    /// The editor (its handlers for the selection bar: maths, AI, graphs, pieces).
    required property var editor
    property alias canvas: view
    /// What this side shows.
    property string notebookId: mainNotebookId
    readonly property bool same: notebookId === mainNotebookId
    signal closeRequested()
    signal notify(string message, bool error)

    color: Theme.bg
    // The notebooks it can show (locked ones once unlocked).
    property var notebooks: []
    function refresh() {
        try {
            const lib = InkleafCore.call("library")
            notebooks = lib.notebooks.filter(n => !n.locked || n.unlocked).sort((a, b) => b.updatedAt - a.updatedAt)
        } catch (e) { notebooks = [] }
    }
    Component.onCompleted: refresh()
    /// The same notebook changed on the left: shown again here.
    function mainChanged() { if (same) reloadSoon.restart() }
    Timer { id: reloadSoon; interval: 700; onTriggered: view.reloadPages(true) }

    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.border; z: 10 }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // Which notebook and page; close.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            Layout.leftMargin: 12
            spacing: 6
            UiSelect {
                Layout.fillWidth: true
                Layout.maximumWidth: 280
                options: [{ value: root.mainNotebookId, label: "This notebook" }].concat(root.notebooks.filter(n => n.id !== root.mainNotebookId).map(n => ({ value: n.id, label: n.title })))
                value: root.notebookId
                onPicked: v => {
                    view.flush()
                    root.notebookId = v
                }
            }
            Label {
                visible: root.same
                text: "View only"
                color: Theme.muted
                font.pixelSize: 12
                ToolTip.visible: sameHover.hovered
                ToolTip.text: "This notebook again, to look at another page: write on the left, it follows."
                HoverHandler { id: sameHover }
            }
            Item { Layout.fillWidth: true }
            IconButton { iconName: "up"; tip: "Previous page"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; enabled: view.currentPage > 0; onClicked: view.goToPage(view.currentPage - 1) }
            Label { text: (view.currentPage + 1) + " / " + view.pageCount; color: Theme.muted; font.pixelSize: 12 }
            IconButton { iconName: "down"; tip: "Next page"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; enabled: view.currentPage < view.pageCount - 1; onClicked: view.goToPage(view.currentPage + 1) }
            IconButton { iconName: "close"; tip: "Close the side view"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; onClicked: { view.finishEdit(); view.flush(); root.closeRequested() } }
        }
        Item {
            id: area
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            InkCanvas {
                id: view
                anchors.fill: parent
                layer.enabled: true
                layer.samples: 4
                notebookId: root.notebookId
                readOnly: root.same
                tool: root.prefs.store.tool
                penStyle: root.prefs.store.penStyle
                deskColor: Theme.desk
                penDash: root.prefs.store.penDash
                penGap: root.prefs.store.penGap
                colors: root.prefs.colors
                widths: root.prefs.widths
                shapeKind: root.prefs.store.shapeKind
                eraserMode: root.prefs.store.eraserMode
                laserMode: root.prefs.store.laserMode
                textFont: root.prefs.store.textFont
                smoothing: AppSettings.streamline
                penOnly: AppSettings.store.penOnly
                pressureCurve: AppSettings.store.pressure
                eraserEnd: AppSettings.store.eraserEnd
                sideButton: AppSettings.store.sideButton
                palmRejection: AppSettings.store.palmRejection
                scratchOut: AppSettings.store.scratchOut
                fingerTaps: AppSettings.store.fingerTaps
                onScratchedOut: root.editor.scratched(this)
                onFingerTapped: redo => Notice.post(redo ? "Redone" : "Undone", false, { icon: redo ? "redo" : "undo" })
                penCursor: AppSettings.store.penCursor
                onMessage: (text, error) => root.notify(text, error)
                onToolChanged: if (tool !== root.prefs.store.tool) root.prefs.store.tool = tool
                // Fits the page to its width once it has one (and when it changes).
                onWidthChanged: fitSoon.restart()
                onNotebookIdChanged: fitSoon.restart()
                Timer { id: fitSoon; interval: 120; onTriggered: view.fitWidth() }
            }
            EraserCursor { anchors.fill: view; canvas: view; z: 6; visible: !root.same }
            TextEditor { anchors.fill: parent; canvas: view; z: 5 }
            TableCellEditor { anchors.fill: parent; canvas: view; z: 5 }
            TableHandles { anchors.fill: parent; canvas: view; z: 5 }
            SelectionBar {
                canvas: view; z: 7
                onAskAi: what => root.editor.askAboutSelection(what)
                onMathsRequested: root.editor.openMaths()
                onGraphEditRequested: g => root.editor.editGraph(g)
                onSavePieceRequested: root.editor.savePiece()
            }
        }
    }
}
