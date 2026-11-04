// A notebook or folder on the shelf: its picture, title and a line about it,
// with a ⋯ button that shows on hover. While the shelf is selecting, a tap
// picks it (a check shows); Ctrl+click and a long press pick it any time,
// Shift+click picks a range.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Item {
    id: root
    property bool isFolder: false
    property var item: ({})
    property string meta
    property var sheets: []
    property int count: 0
    property bool trash: false
    /// The shelf is selecting: a tap picks instead of opening.
    property bool selecting: false
    property bool picked: false
    signal activated()
    signal moreRequested(Item anchor)
    /// Picked or unpicked; `range`: Shift was held (pick up to here).
    signal toggled(bool range)

    readonly property real artHeight: width * 4 / 3
    implicitHeight: artHeight + 14 + 20 + 3 + 16
    readonly property bool hot: hover.hovered || more.down || moreMenuOpen
    property bool moreMenuOpen: false

    HoverHandler { id: hover; cursorShape: root.trash ? Qt.ArrowCursor : Qt.PointingHandCursor }
    TapHandler {
        acceptedModifiers: Qt.NoModifier
        onTapped: root.selecting ? root.toggled(false) : root.activated()
        onLongPressed: root.toggled(false)
    }
    TapHandler { acceptedModifiers: Qt.ControlModifier; onTapped: root.toggled(false) }
    TapHandler { acceptedModifiers: Qt.ShiftModifier; onTapped: root.toggled(true) }
    TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.moreRequested(more) }

    Item {
        id: art
        width: root.width
        height: root.artHeight
        Book {
            visible: !root.isFolder
            anchors.fill: parent
            coverColor: root.item.coverColor || "#2563eb"
            thumbnail: root.item.thumbnail || ""
            hasCover: !!root.item.hasCover
            title: root.item.title || ""
            favourite: !!root.item.favourite
            locked: !!root.item.locked
            unlocked: !!root.item.unlocked
            raised: root.hot && !root.trash
            // Lifted on hover (no tilt: a tilted stack of layers shimmers at the edges).
            transform: Translate { y: root.hot && !root.trash ? -5 : 0; Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } } }
        }
        Loader {
            active: root.isFolder
            anchors.fill: parent
            sourceComponent: FolderArt {
                folderColor: root.item.color || "#64748b"
                sheets: root.sheets
                count: root.count
                open: root.hot && !root.trash
                locked: !!root.item.locked
                unlocked: !!root.item.unlocked
            }
        }
    }
    // Picked: an accent ring around the picture, and a check in its corner.
    Rectangle {
        visible: root.picked
        antialiasing: true
        anchors.fill: art
        anchors.margins: -5
        radius: 10
        color: Theme.alpha(Theme.accent, 0.10)
        border.color: Theme.accent
        border.width: 2.5
    }
    Rectangle {
        visible: root.selecting || root.picked
        antialiasing: true
        x: 8; y: 8
        width: 26; height: 26; radius: 13
        color: root.picked ? Theme.accent : Theme.alpha("#000000", 0.35)
        border.color: "#ffffff"
        border.width: 2
        Icon { anchors.centerIn: parent; name: "check"; size: 15; color: "#ffffff"; visible: root.picked }
    }
    Label {
        id: title
        y: root.artHeight + 14
        width: root.width - 32
        text: (root.isFolder && root.item.favourite ? "★ " : "") + (root.isFolder ? root.item.name : root.item.title)
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignLeft
        font.weight: Font.DemiBold
        color: Theme.text
    }
    Label {
        y: title.y + title.height + 3
        width: root.width - 32
        text: root.meta
        elide: Text.ElideRight
        font.pixelSize: 12
        color: Theme.muted
    }
    IconButton {
        id: more
        x: root.width - width + 6
        y: root.height - height - 2
        width: 34; height: 34
        iconName: "more"
        tip: "Options"
        opacity: root.hot && !root.selecting ? 1 : 0
        enabled: !root.selecting
        Behavior on opacity { NumberAnimation { duration: 150 } }
        onClicked: root.moreRequested(more)
    }
}
