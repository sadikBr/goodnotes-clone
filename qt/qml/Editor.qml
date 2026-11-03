// The editor: the notebook's pages on the canvas, with the tool trays on top
// (the same layout as the web app: writing tools in the middle, undo/redo
// on the right; tapping the active tool opens its options).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Rectangle {
    id: root
    color: Theme.desk
    property string notebookId
    property string title
    signal back()
    signal settingsRequested()

    Prefs {
        id: toolPrefs
        category: "tools"
        property string tool: "pen"
        property string penStyle: "fountain"
        property string penColor: "#111827"
        property string highlighterColor: "#facc15"
        property real penWidth: 3
        property real highlighterWidth: 18
        property real eraserWidth: 14
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
                        IconButton { iconName: "stylus"; tip: canvas.penOnly ? "Pen-only mode is on: touch and mouse scroll" : "Pen-only mode"; active: canvas.penOnly; onClicked: AppSettings.store.penOnly = !AppSettings.store.penOnly }
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
        smoothing: AppSettings.streamline
        penOnly: AppSettings.store.penOnly
        pressureCurve: AppSettings.store.pressure
        eraserEnd: AppSettings.store.eraserEnd
        sideButton: AppSettings.store.sideButton
        palmRejection: AppSettings.store.palmRejection
        penCursor: AppSettings.store.penCursor
        onPenUsed: if (!AppSettings.store.penOnly && !penSeen) { penSeen = true; AppSettings.store.penOnly = true }
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
    Item {
        id: drawer
        visible: root.drawerOpen
        z: 3
        anchors.top: header.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: drawerCol.implicitWidth + 32
        height: drawerCol.implicitHeight + 28
        readonly property bool hl: canvas.tool === "highlighter"
        readonly property bool eraser: canvas.tool === "eraser"
        Shadow { anchors.fill: parent; radius: 16; level: 3 }
        Rectangle { anchors.fill: parent; radius: 16; color: Theme.surface; border.color: Theme.border }
        // Taps inside don't fall through to the page.
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
        ColumnLayout {
            id: drawerCol
            anchors.centerIn: parent
            spacing: 14

            Segmented {
                visible: canvas.tool === "pen"
                Layout.fillWidth: true
                options: [{ value: "fountain", label: "Fountain" }, { value: "ballpoint", label: "Ballpoint" }, { value: "pencil", label: "Pencil" }, { value: "brush", label: "Brush" }]
                value: canvas.penStyle
                onPicked: v => toolPrefs.penStyle = v
            }
            Swatches {
                visible: !drawer.eraser
                Layout.preferredWidth: 9 * 26 + 8 * 8
                size: 26
                colors: drawer.hl ? Theme.highlighterColors : Theme.inkColors
                value: String(drawer.hl ? canvas.highlighterColor : canvas.penColor)
                onPicked: c => drawer.hl ? toolPrefs.highlighterColor = c : toolPrefs.penColor = c
            }
            RowLayout {
                spacing: 12
                Label { text: "Size"; color: Theme.muted; font.pixelSize: 13 }
                UiSlider {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 220
                    from: drawer.hl ? 6 : drawer.eraser ? 6 : 0.5
                    to: drawer.hl ? 40 : drawer.eraser ? 60 : 14
                    stepSize: 0.5
                    value: drawer.hl ? canvas.highlighterWidth : drawer.eraser ? canvas.eraserWidth : canvas.penWidth
                    onMoved: {
                        if (drawer.hl) toolPrefs.highlighterWidth = value
                        else if (drawer.eraser) toolPrefs.eraserWidth = value
                        else toolPrefs.penWidth = value
                    }
                }
                Label {
                    text: (drawer.hl ? canvas.highlighterWidth : drawer.eraser ? canvas.eraserWidth : canvas.penWidth).toFixed(1)
                    color: Theme.text; font.pixelSize: 13; Layout.preferredWidth: 32; horizontalAlignment: Text.AlignRight
                }
            }
            RowLayout {
                visible: canvas.tool === "pen"
                spacing: 12
                Label { text: "Smoothing"; color: Theme.muted; font.pixelSize: 13; Layout.fillWidth: true }
                Segmented {
                    options: [{ value: "off", label: "Off" }, { value: "light", label: "Light" }, { value: "normal", label: "Normal" }, { value: "strong", label: "Strong" }]
                    value: AppSettings.store.smoothing
                    onPicked: v => AppSettings.store.smoothing = v
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
