// The page thumbnails (src/editor/editor.ts renderPageList): go to a page,
// its ⋯ menu (add after, duplicate, move, bookmark, delete), Add page.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Rectangle {
    id: root
    required property InkCanvas canvas
    /// Set for a daftar: its pages are grouped by class (page.section).
    property bool daftar: false
    signal addDaftarPage()
    color: Theme.surface
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
    clip: true

    PopupMenu { id: menu }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 14
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 6
            Label { text: "PAGES"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.7; color: Theme.muted; Layout.fillWidth: true }
            Rectangle {
                Layout.preferredHeight: 18
                Layout.preferredWidth: countLabel.implicitWidth + 14
                radius: 9
                color: Theme.surface2
                Label { id: countLabel; anchors.centerIn: parent; text: root.canvas.pageCount; font.pixelSize: 11; font.weight: Font.Bold; color: Theme.muted }
            }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            topMargin: 8
            bottomMargin: 8
            spacing: 16
            model: root.canvas.pages
            ScrollBar.vertical: ScrollBar {}
            currentIndex: root.canvas.currentPage
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                readonly property bool active: index === root.canvas.currentPage
                // A daftar's list has a heading where each class's section starts.
                readonly property string section: root.daftar && modelData.section && modelData.section !== ((root.canvas.pages[index - 1] || {}).section) ? modelData.section : ""
                width: list.width
                height: thumb.height + 24 + thumb.y
                HoverHandler { id: hover }
                Rectangle {
                    visible: cell.section !== ""
                    x: 22; y: 2
                    width: parent.width - 44
                    height: 22
                    radius: 6
                    color: Theme.accentSoft
                    Label { anchors.centerIn: parent; width: parent.width - 12; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; text: cell.section; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.2; color: Theme.accent }
                }
                AbstractButton {
                    id: thumb
                    x: 18
                    y: cell.section !== "" ? 34 : 0
                    width: parent.width - 36
                    height: width * (cell.modelData.height || 1123) / (cell.modelData.width || 794)
                    hoverEnabled: true
                    onClicked: root.canvas.goToPage(cell.index)
                    transform: Translate { y: thumb.hovered && !cell.active ? -2 : 0; Behavior on y { NumberAnimation { duration: 200 } } }
                    background: Item {
                        Rectangle { anchors.fill: parent; anchors.margins: cell.active ? -2.5 : -1; radius: 8; color: cell.active ? Theme.accent : Theme.border }
                        Shadow { anchors.fill: parent; radius: 6; level: thumb.hovered ? 2 : 1 }
                        RoundImage {
                            anchors.fill: parent
                            color: cell.modelData.paperColor || "#fffdf7"
                            source: cell.modelData.thumbnail || ""
                            radiusTopLeft: 6; radiusTopRight: 6; radiusBottomLeft: 6; radiusBottomRight: 6
                        }
                    }
                }
                IconButton {
                    id: more
                    anchors.top: thumb.top
                    anchors.right: thumb.right
                    anchors.margins: 6
                    width: 28; height: 28
                    iconSize: 16
                    iconName: "more"
                    tip: "Page " + (cell.index + 1) + " options"
                    opacity: hover.hovered || menu.opened ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                    Rectangle { anchors.fill: parent; z: -1; radius: 10; color: Theme.surface }
                    onClicked: menu.show(more, [
                        { label: "Add page after", action: () => root.daftar && cell.modelData.section ? root.addDaftarPage() : root.canvas.addPage(cell.index + 1, "") },
                        { label: "Duplicate page", action: () => root.canvas.duplicatePage(cell.index) },
                        { label: "Move up", action: () => root.canvas.movePage(cell.index, cell.index - 1) },
                        { label: "Move down", action: () => root.canvas.movePage(cell.index, cell.index + 1) },
                        { label: cell.modelData.favourite ? "Remove bookmark" : "Bookmark page", action: () => root.canvas.togglePageFavourite(cell.index) },
                        { label: "Delete page", danger: true, action: () => root.canvas.deletePage(cell.index) }
                    ])
                }
                Label {
                    anchors.top: thumb.bottom
                    anchors.topMargin: 6
                    anchors.horizontalCenter: thumb.horizontalCenter
                    text: (cell.modelData.favourite ? "★ " : "") + (cell.index + 1)
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: cell.active ? Theme.accent : Theme.muted
                }
            }
        }
        AbstractButton {
            id: add
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 6
            Layout.bottomMargin: 16
            implicitHeight: 40
            hoverEnabled: true
            onClicked: root.daftar ? root.addDaftarPage() : root.canvas.addPage(-1, "")
            background: Item {
                DashedBox { anchors.fill: parent; radius: Theme.radius; color: add.hovered ? Theme.accent : Theme.borderStrong; fill: add.hovered ? Theme.accentSoft : "transparent"; lineWidth: 1.5 }
            }
            contentItem: Item {
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 6
                    Icon { name: "plus"; size: 17; color: add.hovered ? Theme.accent : Theme.muted }
                    Label { text: "Add page"; font.weight: Font.DemiBold; color: add.hovered ? Theme.accent : Theme.muted }
                }
            }
        }
    }
}
