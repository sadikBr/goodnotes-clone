// A selected table's inner lines can be dragged: columns wider or narrower,
// rows taller or shorter (the table keeps its size; rows still grow to fit
// their text).
import QtQuick
import Inkleaf

Item {
    id: root
    required property InkCanvas canvas
    readonly property var table: canvas.selection.table || null
    property var edges: ({})
    visible: !!table && canvas.tool === "lasso" && !!edges.xs

    function refresh() { edges = table ? canvas.tableEdges(table.id) : ({}) }
    onTableChanged: refresh()
    Connections {
        target: root.canvas
        function onViewChanged() { root.refresh() }
        function onSelectionChanged() { root.refresh() }
    }

    /// New shares after moving edge `i` (1 … n − 1) of `list` to `pos`.
    function shares(list, i, pos) {
        const lo = list[i - 1] + 14, hi = list[i + 1] - 14
        const moved = list.slice()
        moved[i] = Math.max(lo, Math.min(hi, pos))
        const total = moved[moved.length - 1] - moved[0]
        const out = []
        for (let k = 0; k + 1 < moved.length; k++) out.push((moved[k + 1] - moved[k]) / total)
        return out
    }

    // Column lines.
    Repeater {
        model: root.edges.xs ? root.edges.xs.length - 2 : 0
        delegate: Item {
            id: colEdge
            required property int index
            readonly property int edge: index + 1
            x: root.edges.xs[edge] - width / 2
            y: root.edges.ys[0]
            width: 12
            height: root.edges.ys[root.edges.ys.length - 1] - root.edges.ys[0]
            property real dragX: NaN
            Rectangle {
                visible: area.containsMouse || area.pressed
                x: (isNaN(colEdge.dragX) ? colEdge.width / 2 : colEdge.dragX) - 1.5
                width: 3; height: parent.height
                radius: 1.5
                color: Theme.accent
            }
            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SplitHCursor
                preventStealing: true
                onPositionChanged: m => { if (pressed) colEdge.dragX = m.x }
                onReleased: m => {
                    const pos = colEdge.x + m.x
                    colEdge.dragX = NaN
                    root.canvas.setTableSizes(root.table.id, root.shares(root.edges.xs, colEdge.edge, pos), [])
                }
            }
        }
    }
    // Row lines.
    Repeater {
        model: root.edges.ys ? root.edges.ys.length - 2 : 0
        delegate: Item {
            id: rowEdge
            required property int index
            readonly property int edge: index + 1
            x: root.edges.xs[0]
            y: root.edges.ys[edge] - height / 2
            width: root.edges.xs[root.edges.xs.length - 1] - root.edges.xs[0]
            height: 12
            property real dragY: NaN
            Rectangle {
                visible: rowArea.containsMouse || rowArea.pressed
                y: (isNaN(rowEdge.dragY) ? rowEdge.height / 2 : rowEdge.dragY) - 1.5
                height: 3; width: parent.width
                radius: 1.5
                color: Theme.accent
            }
            MouseArea {
                id: rowArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SplitVCursor
                preventStealing: true
                onPositionChanged: m => { if (pressed) rowEdge.dragY = m.y }
                onReleased: m => {
                    const pos = rowEdge.y + m.y
                    rowEdge.dragY = NaN
                    root.canvas.setTableSizes(root.table.id, [], root.shares(root.edges.ys, rowEdge.edge, pos))
                }
            }
        }
    }
}
