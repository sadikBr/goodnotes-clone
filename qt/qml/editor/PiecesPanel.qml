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
    /// Save the current selection (the editor asks for its name).
    signal saveRequested()
    property var pieces: []
    property string filter: ""
    readonly property var shown: filter.trim() ? pieces.filter(p => p.name.toLowerCase().includes(filter.trim().toLowerCase())) : pieces
    readonly property bool canSave: !!(canvas && canvas.selection && canvas.selection.count > 0 && !canvas.readOnly)

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
    /// "Used today", "Saved 3 days ago"…
    function when(p) {
        const t = p.usedAt || p.createdAt
        if (!t) return ""
        const days = Math.floor((Date.now() - t) / 86400000)
        const ago = days <= 0 ? "today" : days === 1 ? "yesterday" : days < 7 ? days + " days ago"
                  : days < 30 ? Math.floor(days / 7) + (days < 14 ? " week ago" : " weeks ago")
                  : new Date(t).toLocaleDateString(Qt.locale(), "d MMM yyyy")
        return (p.usedAt ? "Used " : "Saved ") + ago
    }

    parent: Overlay.overlay
    width: Math.min(parent.width - 16, 460)
    readonly property int columns: 3
    readonly property real cellSide: Math.floor((width - 28) / columns)
    readonly property real headerHeight: 70 + (pieces.length > 6 ? 46 : 0)
    height: Math.min(parent.height - y - 12,
                     pieces.length ? headerHeight + Math.max(1, Math.ceil(shown.length / columns)) * (cellSide + 38) + 16 : 330)
    padding: 0
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 150; easing.type: Easing.OutCubic }
    }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }
    PopupMenu { id: menu }
    FormDialog { id: ask }

    contentItem: ColumnLayout {
        spacing: 0
        // Title, how many, save the selection, close.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 10
            spacing: 10
            Rectangle {
                antialiasing: true
                Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: 11
                color: Theme.alpha(Theme.accent, 0.14)
                Icon { anchors.centerIn: parent; name: "stack"; size: 19; color: Theme.accent }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label { text: "My pieces"; font.pixelSize: 16; font.weight: Font.Bold; color: Theme.text }
                Label {
                    text: root.pieces.length === 0 ? "Things you use again and again"
                          : root.pieces.length + (root.pieces.length === 1 ? " piece" : " pieces") + " · tap one to use it"
                    color: Theme.muted; font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            UiButton {
                visible: root.canSave
                kind: "ghost"
                iconName: "plus"
                text: "Save selection"
                onClicked: { root.close(); root.saveRequested() }
            }
            IconButton { iconName: "close"; tip: "Close"; implicitWidth: 32; implicitHeight: 32; iconSize: 16; onClicked: root.close() }
        }
        UiField {
            visible: root.pieces.length > 6
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 10
            implicitHeight: 36
            placeholderText: "Search your pieces"
            text: root.filter
            onTextEdited: root.filter = text
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border; visible: root.pieces.length > 0 }

        // Nothing saved yet: how to.
        ColumnLayout {
            visible: root.pieces.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            Layout.bottomMargin: 22
            spacing: 10
            Rectangle {
                antialiasing: true
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 6
                Layout.preferredWidth: 150; Layout.preferredHeight: 92; radius: Theme.radius
                color: Theme.surface2
                border.color: Theme.border
                // Three cards fanned out: a stack of pieces.
                Repeater {
                    model: 3
                    delegate: Rectangle {
                        required property int index
                        antialiasing: true
                        width: 52; height: 62; radius: 8
                        x: 49 + (index - 1) * 30; y: 15
                        rotation: (index - 1) * 9
                        color: "#fffdf7"
                        border.color: index === 1 ? Theme.accent : Theme.border
                        border.width: index === 1 ? 2 : 1
                        z: index === 1 ? 1 : 0
                        Icon { anchors.centerIn: parent; visible: index === 1; name: "plus"; size: 20; color: Theme.accent }
                    }
                }
            }
            Label { text: "No pieces yet"; font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text; Layout.alignment: Qt.AlignHCenter }
            Label {
                text: "Keep a diagram you draw often, your signature, a stamp or a table, and put it on any page in one tap."
                color: Theme.muted; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 4
                spacing: 6
                component Step: Rectangle {
                    property string label
                    antialiasing: true
                    implicitWidth: stepLabel.implicitWidth + 20; implicitHeight: 28; radius: 14
                    color: Theme.surface2; border.color: Theme.border
                    Label { id: stepLabel; anchors.centerIn: parent; text: parent.label; font.pixelSize: 12; color: Theme.text }
                }
                Step { label: "Select with the lasso" }
                Label { text: "→"; color: Theme.muted }
                Step { label: "Save to my pieces" }
            }
            UiButton {
                visible: root.canSave
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 6
                kind: "primary"
                iconName: "plus"
                text: "Save the selection"
                onClicked: { root.close(); root.saveRequested() }
            }
        }
        Label {
            visible: root.pieces.length > 0 && root.shown.length === 0
            text: "Nothing called “" + root.filter.trim() + "”"
            color: Theme.muted
            Layout.alignment: Qt.AlignHCenter
            Layout.margins: 24
        }
        GridView {
            id: grid
            visible: root.shown.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 12
            Layout.rightMargin: 6
            Layout.topMargin: 10
            Layout.bottomMargin: 8
            clip: true
            cellWidth: Math.floor((width - 6) / root.columns)
            cellHeight: cellWidth + 38
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
                    readonly property bool lit: hovered || (menu.opened && menu.anchorItem === more)
                    x: 4; y: lit ? 2 : 4
                    width: parent.width - 8
                    height: parent.height - 8
                    hoverEnabled: true
                    Behavior on y { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    scale: pressed ? 0.97 : 1
                    Behavior on scale { NumberAnimation { duration: 90 } }
                    onClicked: root.put(cell.modelData)
                    ToolTip.visible: hovered && !menu.opened
                    ToolTip.delay: 700
                    ToolTip.text: "Put “" + cell.modelData.name + "” on the page"
                    background: Item {
                        Shadow { anchors.fill: parent; radius: Theme.radius; level: 2; visible: tile.lit }
                        Rectangle {
                            antialiasing: true
                            anchors.fill: parent
                            radius: Theme.radius
                            color: Theme.surface2
                            border.color: tile.lit ? Theme.accent : Theme.border
                            border.width: tile.lit ? 2 : 1
                            Behavior on border.color { ColorAnimation { duration: 120 } }
                        }
                    }
                    contentItem: ColumnLayout {
                        spacing: 0
                        // The piece on a bit of paper.
                        Rectangle {
                            antialiasing: true
                            Layout.fillWidth: true
                            Layout.preferredHeight: tile.width - 12
                            Layout.margins: 6
                            Layout.bottomMargin: 0
                            radius: Theme.radius - 4
                            color: "#fffdf7"
                            border.color: Theme.alpha("#000000", 0.06)
                            Image {
                                anchors.fill: parent
                                anchors.margins: 10
                                source: cell.modelData.preview || ""
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                mipmap: true
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 9
                            Layout.rightMargin: 4
                            Layout.topMargin: 5
                            Layout.bottomMargin: 6
                            spacing: 0
                            Label {
                                text: cell.modelData.name
                                elide: Text.ElideRight
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                color: Theme.text
                                Layout.fillWidth: true
                            }
                            Label {
                                text: root.when(cell.modelData)
                                elide: Text.ElideRight
                                font.pixelSize: 10
                                color: Theme.muted
                                Layout.fillWidth: true
                            }
                        }
                    }
                    IconButton {
                        id: more
                        z: 2
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 10
                        width: 26; height: 26
                        iconSize: 15
                        iconName: "more"
                        tip: "Rename or delete"
                        opacity: tile.lit ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                        Rectangle { antialiasing: true; anchors.fill: parent; z: -1; radius: 9; color: Theme.surface; border.color: Theme.border }
                        onClicked: menu.show(more, [
                            { icon: "pen", label: "Rename", action: () => ask.ask("Rename piece", [{ name: "name", label: "Name", value: cell.modelData.name }], "Save", r => {
                                try { InkleafCore.call("renameSnippet", { id: cell.modelData.id, name: r.name.trim() }); root.reload() } catch (e) { root.notify(String(e.message || e), true) }
                            }) },
                            { icon: "trash", label: "Delete", danger: true, action: () => {
                                try { InkleafCore.call("deleteSnippet", { id: cell.modelData.id }); root.reload() } catch (e) { root.notify(String(e.message || e), true) }
                            } }
                        ])
                    }
                }
            }
        }
    }
}
