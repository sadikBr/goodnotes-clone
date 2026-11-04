// Your reusable pieces: selections saved from any page (a diagram you draw
// often, a signature, a stamp, a table) to put on any other. Tap one to put
// it in the middle of the page; ⋯ to rename or delete it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    required property InkCanvas canvas
    signal notify(string message, bool error)
    property var pieces: []
    property string filter: ""
    readonly property var shown: filter.trim() ? pieces.filter(p => p.name.toLowerCase().includes(filter.trim().toLowerCase())) : pieces

    function reload() {
        try { pieces = InkleafCore.call("snippets") } catch (e) { pieces = [] }
    }
    function show(anchor) {
        filter = ""
        reload()
        const p = anchor.mapToItem(parent, 0, anchor.height + 8)
        x = Math.max(8, Math.min(parent.width - width - 8, p.x + anchor.width - width))
        y = p.y
        open()
    }
    function put(p) {
        canvas.insertPiece(p.data, p.width, p.height)
        try { InkleafCore.call("useSnippet", { id: p.id }) } catch (e) {}
        close()
    }

    parent: Overlay.overlay
    width: Math.min(parent.width - 16, 420)
    // As tall as its pieces need (rows of three), up to the room below.
    readonly property real cellSide: Math.floor((width - 20) / 3)
    height: Math.min(parent.height - y - 12, pieces.length ? 56 + Math.ceil(shown.length / 3) * (cellSide + 26) + 14 : 250)
    padding: 0
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
    }
    PopupMenu { id: menu }
    FormDialog { id: ask }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.bottomMargin: 8
            spacing: 8
            Icon { name: "stack"; size: 18; color: Theme.accent }
            Label { text: "My pieces"; font.pixelSize: 16; font.weight: Font.Bold; color: Theme.text; Layout.fillWidth: true }
            UiField {
                visible: root.pieces.length > 6
                Layout.preferredWidth: 150
                implicitHeight: 34
                placeholderText: "Search"
                text: root.filter
                onTextEdited: root.filter = text
            }
        }
        // Nothing saved yet: how to.
        ColumnLayout {
            visible: root.pieces.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 22
            spacing: 8
            Icon { name: "stack"; size: 34; color: Theme.muted; Layout.alignment: Qt.AlignHCenter }
            Label { text: "No pieces yet"; font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text; Layout.alignment: Qt.AlignHCenter }
            Label {
                text: "Select something with the lasso (a diagram, your signature, a stamp, a table) and choose “Save to my pieces” to use it again on any page."
                color: Theme.muted; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
        }
        GridView {
            id: grid
            visible: root.pieces.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 10
            Layout.rightMargin: 4
            Layout.bottomMargin: 10
            clip: true
            cellWidth: Math.floor((width - 6) / 3)
            cellHeight: cellWidth + 26
            model: root.shown
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: cell
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight
                AbstractButton {
                    id: tile
                    x: 4; y: 4
                    width: parent.width - 8
                    height: width
                    hoverEnabled: true
                    onClicked: root.put(cell.modelData)
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: "Put “" + cell.modelData.name + "” on the page"
                    background: Rectangle {
                        antialiasing: true
                        radius: Theme.radius
                        color: "#fffdf7"
                        border.color: tile.hovered ? Theme.accent : Theme.border
                        border.width: tile.hovered ? 2 : 1
                    }
                    contentItem: Item {
                        Image {
                            anchors.fill: parent
                            anchors.margins: 8
                            source: cell.modelData.preview || ""
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                        }
                    }
                    IconButton {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 2
                        width: 26; height: 26
                        iconSize: 15
                        iconName: "more"
                        tip: "Options"
                        opacity: tile.hovered || menu.opened ? 1 : 0
                        Rectangle { antialiasing: true; anchors.fill: parent; z: -1; radius: 9; color: Theme.surface }
                        onClicked: menu.show(this, [
                            { label: "Rename", action: () => ask.ask("Rename piece", [{ name: "name", label: "Name", value: cell.modelData.name }], "Save", r => {
                                try { InkleafCore.call("renameSnippet", { id: cell.modelData.id, name: r.name.trim() }); root.reload() } catch (e) { root.notify(String(e.message || e), true) }
                            }) },
                            { label: "Delete", danger: true, action: () => {
                                try { InkleafCore.call("deleteSnippet", { id: cell.modelData.id }); root.reload() } catch (e) { root.notify(String(e.message || e), true) }
                            } }
                        ])
                    }
                }
                Label {
                    y: tile.y + tile.height + 4
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: cell.modelData.name
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    color: Theme.text
                }
            }
        }
    }
}
