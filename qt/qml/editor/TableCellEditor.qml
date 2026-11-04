// Typing in a table's cells, over the cell on the page: Tab and Shift+Tab
// move along (Tab in the last cell adds a row), Enter goes down (adding a
// row at the bottom), Shift+Enter starts a new line in the cell, Esc or a
// tap elsewhere finishes. Each cell is saved as you leave it.
import QtQuick
import QtQuick.Controls
import Inkleaf
import "Fonts.js" as Fonts

Item {
    id: root
    required property InkCanvas canvas
    /// The cell being typed in (canvas.tableCell), or {}.
    property var cell: ({})
    readonly property bool active: !!cell.id
    visible: active
    property rect box: Qt.rect(0, 0, 0, 0)

    /// Starts typing in a cell ({ id, row, col }).
    function open(c) {
        const next = canvas.tableCell(c.id, c.row, c.col)
        if (!next.id) return
        cell = next
        area.text = next.text || ""
        area.cursorPosition = area.text.length
        place()
        area.forceActiveFocus()
        // Again once a menu that opened it has closed (it hands focus back).
        refocus.restart()
    }
    Timer { id: refocus; interval: 120; onTriggered: if (root.active) area.forceActiveFocus() }
    /// Focus went elsewhere (a tap on the page, another field) and stayed there: done.
    Timer { id: focusLost; interval: 250; onTriggered: if (root.active && !area.activeFocus && !refocus.running) root.finish() }
    function place() {
        if (!active) return
        const fresh = canvas.tableCell(cell.id, cell.row, cell.col)
        if (!fresh.id) { cell = ({}); return }
        box = fresh.rect
    }
    /// Saves what was typed in this cell.
    function commit() {
        if (!active) return
        const c = cell
        if ((c.text || "") !== area.text) canvas.setTableCell(c.id, c.row, c.col, area.text)
    }
    function finish() {
        commit()
        cell = ({})
    }
    /// Saves, then types in the cell `dr` rows and `dc` columns away (adding a row past the end).
    function step(dr, dc) {
        commit()
        let r = cell.row + dr, c = cell.col + dc
        if (c >= cell.cols) { c = 0; r++ }
        if (c < 0) { c = cell.cols - 1; r-- }
        if (r < 0) return
        if (r >= cell.rows) canvas.changeTable(cell.id, "addRow", -1)
        open({ id: cell.id, row: r, col: c })
    }

    Connections {
        target: root.canvas
        function onViewChanged() { root.place() }
        function onCellEditRequested(c) { root.commit(); root.open(c) }
        function onNotebookIdChanged() { root.cell = ({}) }
    }

    Item {
        x: root.box.x
        y: root.box.y
        width: root.box.width
        height: Math.max(root.box.height, area.contentHeight + area.topPadding + area.bottomPadding)
        Rectangle { anchors.fill: parent; color: Qt.rgba(1, 1, 1, 0.94) }
        Rectangle { anchors.fill: parent; color: "transparent"; border.color: Theme.accent; border.width: 2 }
        TextArea {
            id: area
            anchors.fill: parent
            readonly property real pad: (root.cell.fontSize || 18) * 0.45 * root.canvas.zoom
            leftPadding: pad; rightPadding: pad
            // Where the page puts it: lines 1.35 × the size apart, centred in the cell.
            topPadding: Math.max(pad * 0.5, (root.box.height - Math.max(1, lineCount) * (root.cell.fontSize || 18) * 1.35 * root.canvas.zoom) / 2)
            bottomPadding: pad * 0.5
            background: null
            wrapMode: TextEdit.Wrap
            color: root.cell.color || "#111827"
            font.family: Fonts.faceFor(root.cell.font, text)
            font.pixelSize: Math.max(1, (root.cell.fontSize || 18) * root.canvas.zoom)
            font.bold: !!root.cell.header
            selectionColor: Theme.alpha(Theme.accent, 0.3)
            selectedTextColor: color
            onActiveFocusChanged: if (!activeFocus && root.active) focusLost.restart()
            Keys.onEscapePressed: root.finish()
            // Escape and Tab are this field's (not the editor's shortcuts).
            Keys.onShortcutOverride: ev => { if (ev.key === Qt.Key_Escape || ev.key === Qt.Key_Tab || ev.key === Qt.Key_Backtab) ev.accepted = true }
            Keys.onTabPressed: ev => { root.step(0, 1); ev.accepted = true }
            Keys.onBacktabPressed: ev => { root.step(0, -1); ev.accepted = true }
            Keys.onPressed: ev => {
                if ((ev.key === Qt.Key_Return || ev.key === Qt.Key_Enter) && !(ev.modifiers & Qt.ShiftModifier)) {
                    root.step(1, 0)
                    ev.accepted = true
                }
            }
        }
    }
}
