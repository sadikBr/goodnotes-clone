// The editor: the notebook's pages on the canvas, with the tool trays on top
// (the same layout as the web app: writing tools in the middle, undo/redo
// on the right; tapping the active tool opens its options).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.settings
import Inkleaf

Rectangle {
    id: root
    color: Theme.desk
    property string notebookId
    property string title
    signal back()

    Settings {
        id: toolPrefs
        category: "tools"
        property string tool: "pen"
        property string penStyle: "fountain"
        property string penColor: "#111827"
        property string highlighterColor: "#facc15"
        property real penWidth: 3
        property real highlighterWidth: 18
        property real eraserWidth: 14
        property real smoothing: 0.38
        property bool penOnly: false
    }

    property bool drawerOpen: false
    function pick(tool) {
        if (canvas.tool === tool) drawerOpen = !drawerOpen
        else {
            canvas.tool = tool
            drawerOpen = false
        }
        toolPrefs.tool = tool
    }

    // --- header -----------------------------------------------------------------
    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        height: 58
        color: Theme.surface
        z: 2
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 4
                IconButton { iconName: "back"; tip: "Library"; onClicked: { canvas.flush(); root.back() } }
                Label {
                    text: root.title
                    font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text
                    elide: Text.ElideRight; Layout.fillWidth: true; Layout.leftMargin: 6
                }
            }

            // Tool trays
            Row {
                spacing: 10
                Rectangle {
                    radius: 14; color: Theme.surface2
                    width: tray1.width + 8; height: 46
                    Row {
                        id: tray1
                        anchors.centerIn: parent
                        spacing: 2
                        IconButton {
                            iconName: "pen"; tip: "Pen (P) — tap again for colours and sizes"
                            active: canvas.tool === "pen"
                            onClicked: root.pick("pen")
                            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 3; width: 6; height: 6; radius: 3; color: canvas.penColor; visible: !parent.active }
                        }
                        IconButton {
                            iconName: "highlighter"; tip: "Highlighter (H)"
                            active: canvas.tool === "highlighter"
                            onClicked: root.pick("highlighter")
                            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 3; width: 6; height: 6; radius: 3; color: canvas.highlighterColor; visible: !parent.active }
                        }
                        IconButton { iconName: "eraser"; tip: "Eraser (E)"; active: canvas.tool === "eraser"; onClicked: root.pick("eraser") }
                    }
                }
                Rectangle {
                    radius: 14; color: Theme.surface2
                    width: tray2.width + 8; height: 46
                    Row {
                        id: tray2
                        anchors.centerIn: parent
                        spacing: 2
                        IconButton { iconName: "stylus"; tip: canvas.penOnly ? "Pen-only mode is on: touch and mouse scroll" : "Pen-only mode"; active: canvas.penOnly; onClicked: { canvas.penOnly = !canvas.penOnly; toolPrefs.penOnly = canvas.penOnly } }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 2
                Item { Layout.fillWidth: true }
                Rectangle {
                    radius: 14; color: Theme.surface2
                    implicitWidth: undoRow.width + 8; implicitHeight: 46
                    Row {
                        id: undoRow
                        anchors.centerIn: parent
                        IconButton { iconName: "undo"; tip: "Undo (Ctrl+Z)"; enabled: canvas.canUndo; onClicked: canvas.undo() }
                        IconButton { iconName: "redo"; tip: "Redo (Ctrl+Shift+Z)"; enabled: canvas.canRedo; onClicked: canvas.redo() }
                    }
                }
                IconButton { iconName: "plus"; tip: "Add a page at the end"; onClicked: canvas.addPage() }
            }
        }
    }

    // --- the pages ------------------------------------------------------------------
    InkCanvas {
        id: canvas
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        focus: true
        notebookId: root.notebookId
        tool: toolPrefs.tool
        penStyle: toolPrefs.penStyle
        penColor: toolPrefs.penColor
        highlighterColor: toolPrefs.highlighterColor
        penWidth: toolPrefs.penWidth
        highlighterWidth: toolPrefs.highlighterWidth
        eraserWidth: toolPrefs.eraserWidth
        smoothing: toolPrefs.smoothing
        penOnly: toolPrefs.penOnly
        onPenUsed: if (!toolPrefs.penOnly && !penSeen) { penSeen = true; toolPrefs.penOnly = true }
        property bool penSeen: false
        onError: (message) => console.warn(message)

        TapHandler { onTapped: root.drawerOpen = false; acceptedDevices: PointerDevice.AllDevices; gesturePolicy: TapHandler.DragThreshold; enabled: root.drawerOpen }
    }

    // Keyboard shortcuts (wherever the focus is).
    Shortcut { sequences: [StandardKey.Undo]; onActivated: canvas.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; onActivated: canvas.redo() }
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; onActivated: canvas.zoomBy(1.2) }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: canvas.zoomBy(1 / 1.2) }
    Shortcut { sequence: "Ctrl+0"; onActivated: canvas.fitWidth() }
    Shortcut { sequence: "P"; onActivated: root.pick("pen") }
    Shortcut { sequence: "H"; onActivated: root.pick("highlighter") }
    Shortcut { sequence: "E"; onActivated: root.pick("eraser") }
    Shortcut { sequence: "PgDown"; onActivated: canvas.goToPage(canvas.currentPage + 1) }
    Shortcut { sequence: "PgUp"; onActivated: canvas.goToPage(canvas.currentPage - 1) }
    Shortcut { sequence: "Esc"; onActivated: root.drawerOpen = false }

    // --- the active tool's options (hangs under the trays) --------------------------
    Rectangle {
        id: drawer
        visible: root.drawerOpen && canvas.tool !== "eraser" || (root.drawerOpen && canvas.tool === "eraser")
        z: 3
        anchors.top: header.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: drawerCol.implicitWidth + 28
        height: drawerCol.implicitHeight + 24
        radius: 16
        color: Theme.surface
        border.color: Theme.border
        ColumnLayout {
            id: drawerCol
            anchors.centerIn: parent
            spacing: 12

            // Pen styles
            Row {
                visible: canvas.tool === "pen"
                spacing: 6
                Repeater {
                    model: [{ id: "fountain", label: "Fountain" }, { id: "ballpoint", label: "Ballpoint" }, { id: "pencil", label: "Pencil" }, { id: "brush", label: "Brush" }]
                    delegate: Button {
                        required property var modelData
                        text: modelData.label
                        checked: canvas.penStyle === modelData.id
                        onClicked: toolPrefs.penStyle = modelData.id
                        hoverEnabled: true
                        background: Rectangle { radius: 9; color: parent.checked ? Theme.accentSoft : (parent.hovered ? Theme.surface2 : "transparent"); border.color: parent.checked ? Theme.accent : Theme.border }
                        contentItem: Label { text: parent.text; color: parent.checked ? Theme.accent : Theme.text; font.weight: parent.checked ? Font.DemiBold : Font.Normal; horizontalAlignment: Text.AlignHCenter; leftPadding: 6; rightPadding: 6 }
                    }
                }
            }
            // Colours
            Grid {
                visible: canvas.tool !== "eraser"
                columns: 9
                spacing: 8
                Repeater {
                    model: canvas.tool === "highlighter" ? Theme.highlighterColors : Theme.inkColors
                    delegate: Rectangle {
                        required property string modelData
                        readonly property bool on: (canvas.tool === "highlighter" ? canvas.highlighterColor : canvas.penColor).toString() === Qt.color(modelData).toString()
                        width: 26; height: 26; radius: 13
                        color: modelData
                        border.width: on ? 3 : 1
                        border.color: on ? Theme.accent : Qt.rgba(0, 0, 0, 0.15)
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: canvas.tool === "highlighter" ? toolPrefs.highlighterColor = parent.modelData : toolPrefs.penColor = parent.modelData
                        }
                    }
                }
            }
            // Sizes
            RowLayout {
                spacing: 10
                Label { text: "Size"; color: Theme.muted }
                Slider {
                    Layout.preferredWidth: 220
                    from: canvas.tool === "highlighter" ? 6 : canvas.tool === "eraser" ? 6 : 0.5
                    to: canvas.tool === "highlighter" ? 40 : canvas.tool === "eraser" ? 60 : 14
                    stepSize: 0.5
                    value: canvas.tool === "highlighter" ? canvas.highlighterWidth : canvas.tool === "eraser" ? canvas.eraserWidth : canvas.penWidth
                    onMoved: {
                        if (canvas.tool === "highlighter") toolPrefs.highlighterWidth = value
                        else if (canvas.tool === "eraser") toolPrefs.eraserWidth = value
                        else toolPrefs.penWidth = value
                    }
                }
                Label {
                    text: (canvas.tool === "highlighter" ? canvas.highlighterWidth : canvas.tool === "eraser" ? canvas.eraserWidth : canvas.penWidth).toFixed(1)
                    color: Theme.text; Layout.preferredWidth: 32
                }
            }
            // Smoothing
            RowLayout {
                visible: canvas.tool === "pen"
                spacing: 6
                Label { text: "Smoothing"; color: Theme.muted; Layout.rightMargin: 4 }
                Repeater {
                    model: [{ v: 0, l: "Off" }, { v: 0.2, l: "Light" }, { v: 0.38, l: "Normal" }, { v: 0.62, l: "Strong" }]
                    delegate: Button {
                        required property var modelData
                        text: modelData.l
                        checked: Math.abs(canvas.smoothing - modelData.v) < 0.01
                        onClicked: toolPrefs.smoothing = modelData.v
                        hoverEnabled: true
                        background: Rectangle { radius: 8; color: parent.checked ? Theme.surface3 : (parent.hovered ? Theme.surface2 : "transparent") }
                        contentItem: Label { text: parent.text; color: Theme.text; font.weight: parent.checked ? Font.DemiBold : Font.Normal; leftPadding: 4; rightPadding: 4 }
                    }
                }
            }
        }
    }

    // --- page and zoom controls (bottom right) -------------------------------------
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 18
        z: 2
        radius: 14
        color: Theme.surface
        border.color: Theme.border
        width: pager.implicitWidth + 12
        height: 44
        RowLayout {
            id: pager
            anchors.centerIn: parent
            spacing: 2
            IconButton { iconName: "up"; tip: "Previous page"; implicitWidth: 34; enabled: canvas.currentPage > 0; onClicked: canvas.goToPage(canvas.currentPage - 1) }
            Label { text: (canvas.currentPage + 1) + " / " + canvas.pageCount; color: Theme.text; font.weight: Font.DemiBold; Layout.leftMargin: 4; Layout.rightMargin: 4 }
            IconButton { iconName: "down"; tip: "Next page"; implicitWidth: 34; enabled: canvas.currentPage < canvas.pageCount - 1; onClicked: canvas.goToPage(canvas.currentPage + 1) }
            Rectangle { width: 1; height: 22; color: Theme.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }
            Label {
                text: Math.round(canvas.zoom * 100) + "%"
                color: Theme.text; Layout.preferredWidth: 44; horizontalAlignment: Text.AlignHCenter
            }
            IconButton { iconName: "fit"; tip: "Fit width (Ctrl+0)"; implicitWidth: 34; onClicked: canvas.fitWidth() }
        }
    }

    // What this preview doesn't show yet.
    Rectangle {
        visible: canvas.hiddenElements > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        z: 2
        radius: 12
        color: Theme.surface
        border.color: Theme.border
        width: notice.implicitWidth + 28
        height: 36
        Label {
            id: notice
            anchors.centerIn: parent
            text: "Text, shapes and pictures on these pages aren't shown in the native app yet; they're kept as they are."
            color: Theme.muted; font.pixelSize: 13
        }
    }
}
