// Every page at once, as a grid (pinch or Ctrl+scroll out past the smallest
// view, or ⋯ → All pages): tap one to go there. Back or Esc closes it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    required property InkCanvas canvas
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
            Label { text: root.canvas.pages.length + (root.canvas.pages.length === 1 ? " page" : " pages"); color: Theme.muted; Layout.fillWidth: true }
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
            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                readonly property bool current: index === root.canvas.currentPage
                width: grid.cellWidth
                height: grid.cellHeight
                AbstractButton {
                    id: thumb
                    readonly property real aspect: (cell.modelData.height || 1123) / (cell.modelData.width || 794)
                    readonly property real room: cell.width - 24
                    // Fits the cell either way (a landscape page is wider than tall).
                    width: Math.min(room, (grid.cellHeight - 34 - 12) / aspect)
                    height: width * aspect
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 12 + (grid.cellHeight - 34 - 12 - height) / 2
                    hoverEnabled: true
                    onClicked: { root.canvas.goToPage(cell.index); root.close() }
                    scale: pressed ? 0.97 : hovered ? 1.02 : 1
                    Behavior on scale { NumberAnimation { duration: 120 } }
                    background: Item {
                        Rectangle { antialiasing: true; anchors.fill: parent; anchors.margins: cell.current ? -3 : -1; radius: Theme.radius; color: cell.current ? Theme.accent : Theme.border }
                        Shadow { anchors.fill: parent; radius: Theme.radiusSm; level: thumb.hovered ? 2 : 1 }
                        RoundImage {
                            anchors.fill: parent
                            color: cell.modelData.paperColor || "#fffdf7"
                            source: cell.modelData.thumbnail || ""
                            radiusTopLeft: 5; radiusTopRight: 5; radiusBottomLeft: 5; radiusBottomRight: 5
                        }
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
