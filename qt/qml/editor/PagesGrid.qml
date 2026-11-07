// Every page at once, as a grid (pinch or Ctrl+scroll out past the smallest
// view, or ⋯ → All pages): tap one to go there; press and hold one, then drag
// it, to move it. Back or Esc closes it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    required property InkCanvas canvas
    /// Moving a page: which one, where it would land, and where the pointer is (in the grid).
    property int dragFrom: -1
    property int dragTo: -1
    property point dragAt: Qt.point(0, 0)
    function dropAt(x, y) {
        const i = grid.indexAt(x + grid.contentX, y + grid.contentY)
        return i >= 0 ? i : (y > grid.height / 2 ? grid.count - 1 : 0)
    }
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, Theme.dark ? 0.45 : 0.25) }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 1.04; to: 1; duration: 180; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }
    background: Rectangle { color: Theme.bg }
    onOpened: grid.positionViewAtIndex(Math.max(0, canvas.currentPage), GridView.Center)

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.leftMargin: 20
            spacing: 10
            Label { text: "All pages"; font.pixelSize: 18; font.weight: Font.Bold; color: Theme.text }
            Label { text: root.dragFrom >= 0 ? "Drop it where it goes" : root.canvas.pages.length + (root.canvas.pages.length === 1 ? " page" : " pages") + " · hold one to move it"; color: Theme.muted; Layout.fillWidth: true; elide: Text.ElideRight }
            IconButton { iconName: "close"; tip: "Close (Esc)"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            // As many as fit, at least two; each about 170 wide.
            readonly property int columns: Math.max(2, Math.floor((width - 16) / 170))
            cellWidth: Math.floor((width - 16) / columns)
            cellHeight: cellWidth * 1.414 + 34
            leftMargin: 8; rightMargin: 8; topMargin: 12; bottomMargin: 12
            model: root.canvas.pages
            // The page being moved, under the pointer.
            Image {
                id: ghost
                property real ratio: 1.414
                parent: grid
                z: 10
                visible: root.dragFrom >= 0
                width: 110; height: width * ratio
                x: root.dragAt.x - width / 2
                y: root.dragAt.y - height / 2
                opacity: 0.9
                fillMode: Image.PreserveAspectFit
                Rectangle { anchors.fill: parent; color: "transparent"; border.color: Theme.accent; border.width: 2; radius: 4 }
            }
            Timer {
                id: scroller
                property int dir: 0
                running: dir !== 0
                repeat: true
                interval: 16
                onTriggered: grid.contentY = Math.max(grid.originY - grid.topMargin, Math.min(grid.contentY + dir * 12, grid.contentHeight + grid.originY - grid.height + grid.bottomMargin))
            }
            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                readonly property bool current: index === root.canvas.currentPage
                width: grid.cellWidth
                height: grid.cellHeight
                // Where a dragged page would land: a bar before this one.
                Rectangle {
                    visible: root.dragFrom >= 0 && root.dragTo === cell.index && root.dragTo !== root.dragFrom
                    x: root.dragTo > root.dragFrom ? parent.width - 3 : 0
                    y: thumb.y - 6
                    width: 4; height: thumb.height + 12; radius: 2
                    color: Theme.accent
                }
                Item {
                    id: thumb
                    readonly property real aspect: (cell.modelData.height || 1123) / (cell.modelData.width || 794)
                    readonly property real room: cell.width - 24
                    readonly property bool hovered: mouse.containsMouse
                    // Fits the cell either way (a landscape page is wider than tall).
                    width: Math.min(room, (grid.cellHeight - 34 - 12) / aspect)
                    height: width * aspect
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 12 + (grid.cellHeight - 34 - 12 - height) / 2
                    opacity: root.dragFrom === cell.index ? 0.35 : 1
                    scale: mouse.pressed && root.dragFrom < 0 ? 0.97 : hovered && root.dragFrom < 0 ? 1.02 : 1
                    Behavior on scale { NumberAnimation { duration: 120 } }
                    Rectangle { antialiasing: true; anchors.fill: parent; anchors.margins: cell.current ? -3 : -1; radius: Theme.radius; color: cell.current ? Theme.accent : Theme.border }
                    Shadow { anchors.fill: parent; radius: Theme.radiusSm; level: thumb.hovered ? 2 : 1 }
                    RoundImage {
                        id: picture
                        anchors.fill: parent
                        color: cell.modelData.paperColor || "#fffdf7"
                        source: root.canvas.thumbRev >= 0 ? root.canvas.thumbOf(cell.index) : ""
                        radiusTopLeft: 5; radiusTopRight: 5; radiusBottomLeft: 5; radiusBottomRight: 5
                    }
                    MouseArea {
                        id: mouse
                        anchors.fill: parent
                        hoverEnabled: true
                        pressAndHoldInterval: 350
                        cursorShape: root.dragFrom >= 0 ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                        // Held: this page is picked up; the grid stops scrolling while it moves.
                        onPressAndHold: { root.dragFrom = cell.index; root.dragTo = cell.index; grid.interactive = false; ghost.source = picture.source; ghost.ratio = thumb.aspect; move(mouse.mouseX, mouse.mouseY) }
                        onPositionChanged: m => { if (root.dragFrom >= 0) move(m.x, m.y) }
                        function move(mx, my) {
                            const p = mouse.mapToItem(grid, mx, my)
                            root.dragAt = p
                            root.dragTo = root.dropAt(p.x, p.y)
                            // Near the top or bottom: the grid scrolls.
                            scroller.dir = p.y < 50 ? -1 : p.y > grid.height - 50 ? 1 : 0
                        }
                        onReleased: {
                            if (root.dragFrom < 0) return
                            const from = root.dragFrom, to = root.dragTo
                            root.dragFrom = -1
                            root.dragTo = -1
                            grid.interactive = true
                            scroller.dir = 0
                            if (to >= 0 && to !== from) root.canvas.movePage(from, to)
                        }
                        onCanceled: { root.dragFrom = -1; root.dragTo = -1; grid.interactive = true; scroller.dir = 0 }
                        onClicked: if (root.dragFrom < 0) { root.canvas.goToPage(cell.index); root.close() }
                    }
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: thumb.bottom
                    anchors.topMargin: 6
                    text: cell.index + 1
                    font.pixelSize: 12
                    font.weight: cell.current ? Font.Bold : Font.Normal
                    color: cell.current ? Theme.accent : Theme.muted
                }
            }
        }
    }
}
