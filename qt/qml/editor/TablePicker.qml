// Insert → Table: point at (or tap) the size you want, rows × columns.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    signal picked(int rows, int cols)
    readonly property int maxRows: 8
    readonly property int maxCols: 8
    property int rows: 3
    property int cols: 3
    readonly property int cellSize: 26

    function show(anchor) {
        rows = 3
        cols = 3
        const p = anchor.mapToItem(parent, 0, anchor.height + 8)
        x = Math.max(8, Math.min(parent.width - width - 8, p.x + anchor.width - width))
        y = p.y
        open()
    }

    parent: Overlay.overlay
    padding: 16
    focus: true
    // A tap outside only closes it (it doesn't also draw on the page), as for menus.
    modal: true
    dim: false
    Overlay.modal: Item {}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 150; easing.type: Easing.OutCubic }
    }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            spacing: 10
            Rectangle {
                antialiasing: true
                Layout.preferredWidth: 32; Layout.preferredHeight: 32; radius: 10
                color: Theme.alpha(Theme.accent, 0.14)
                Icon { anchors.centerIn: parent; name: "table"; size: 18; color: Theme.accent }
            }
            ColumnLayout {
                spacing: 0
                Label { text: "Table"; font.pixelSize: 15; font.weight: Font.Bold; color: Theme.text }
                Label { text: root.rows + " rows × " + root.cols + " columns"; font.pixelSize: 12; color: Theme.muted }
            }
        }
        // The sizes: everything up to the cell under the pointer lights up.
        Item {
            implicitWidth: grid.implicitWidth
            implicitHeight: grid.implicitHeight
            Grid {
                id: grid
                columns: root.maxCols
                spacing: 4
                Repeater {
                    model: root.maxRows * root.maxCols
                    delegate: Rectangle {
                        required property int index
                        readonly property int r: Math.floor(index / root.maxCols) + 1
                        readonly property int c: index % root.maxCols + 1
                        readonly property bool lit: r <= root.rows && c <= root.cols
                        antialiasing: true
                        width: root.cellSize; height: root.cellSize * 0.75
                        radius: 4
                        color: lit ? Theme.alpha(Theme.accent, r === 1 ? 0.45 : 0.25) : Theme.surface2
                        border.color: lit ? Theme.accent : Theme.border
                        Behavior on color { ColorAnimation { duration: 80 } }
                    }
                }
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                function at(m) {
                    root.cols = Math.max(1, Math.min(root.maxCols, Math.floor(m.x / (root.cellSize + 4)) + 1))
                    root.rows = Math.max(1, Math.min(root.maxRows, Math.floor(m.y / (root.cellSize * 0.75 + 4)) + 1))
                }
                onPositionChanged: m => at(m)
                onPressed: m => at(m)
                onClicked: { root.close(); root.picked(root.rows, root.cols) }
            }
        }
        Label {
            text: "Or draw a grid with the pen, select it, and choose Make table."
            color: Theme.muted; font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.preferredWidth: grid.width
        }
    }
}
