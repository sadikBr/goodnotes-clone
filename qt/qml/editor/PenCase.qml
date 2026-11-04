// The pen case: every pen as a stroke drawn with it, on a little piece of
// paper, in three trays: pens for writing, pens for colour and light, and
// the special ones (construction lines, masking tape). A line under the
// trays says what the pen under the pointer (or the chosen one) does.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    /// The chosen pen's id, its ink (hex, no #) and width, and the construction dash.
    property string current: "fountain"
    property string hex: "111827"
    property real penWidth: 3
    property string dash: "dash"
    signal picked(string id)

    readonly property var trays: [
        { title: "Writing", pens: [
            { id: "fountain", label: "Fountain", about: "Fountain pen: thick and thin with pressure and speed." },
            { id: "ballpoint", label: "Ballpoint", about: "Ballpoint: the same line all the way." },
            { id: "pencil", label: "Pencil", about: "Pencil: soft grain, lighter when you press lightly." },
            { id: "calligraphy", label: "Calligraphy", about: "Calligraphy: a flat nib, broad one way and fine the other." },
            { id: "brush", label: "Brush", about: "Brush pen: pointed ends that swell with pressure." }] },
        { title: "Colour & light", pens: [
            { id: "marker", label: "Marker", about: "Marker: a see-through felt tip, darker where strokes cross." },
            { id: "watercolor", label: "Watercolour", about: "Watercolour: soft washes that deepen as you layer them." },
            { id: "neon", label: "Neon", about: "Neon: a bright line with a glow around it.", night: true }] },
        { title: "Special", pens: [
            { id: "construction", label: "Dashed", about: "Construction lines: dashes or dots, for guides and hidden edges." },
            { id: "tape", label: "Tape", about: "Masking tape: cover answers and tap the tape to peek under it." }] }
    ]
    function penFor(id) {
        for (const t of trays) for (const p of t.pens) if (p.id === id) return p
        return trays[0].pens[0]
    }
    property string hovered: ""
    readonly property var shown: penFor(hovered || current)

    function show(anchor) {
        hovered = ""
        const p = anchor.mapToItem(parent, 0, 0)
        x = Math.max(8, Math.min(parent.width - width - 8, p.x + anchor.width / 2 - width / 2))
        y = p.y + anchor.height + 10
        open()
    }

    parent: Overlay.overlay
    padding: 14
    width: 392
    modal: true
    dim: false
    Overlay.modal: Item {}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 140; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
    }

    component Tile: AbstractButton {
        id: tile
        required property var pen
        readonly property bool on: root.current === pen.id
        Layout.fillWidth: true
        implicitWidth: 66
        implicitHeight: 74
        hoverEnabled: true
        onHoveredChanged: root.hovered = hovered ? pen.id : (root.hovered === pen.id ? "" : root.hovered)
        onClicked: { root.picked(pen.id); root.close() }
        scale: pressed ? 0.95 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        background: Rectangle {
            antialiasing: true
            radius: 12
            color: tile.on ? Theme.accentSoft : tile.hovered ? Theme.surface2 : "transparent"
            border.width: tile.on ? 2 : 0
            border.color: Theme.accent
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        contentItem: ColumnLayout {
            spacing: 4
            // A scrap of paper with the pen's stroke (neon on a dark one).
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 4
                implicitWidth: 56; implicitHeight: 34
                radius: 8
                antialiasing: true
                color: tile.pen.night ? "#111827" : "#ffffff"
                border.color: Theme.alpha(Theme.text, Theme.dark ? 0.0 : 0.1)
                Image {
                    anchors.centerIn: parent
                    width: 54; height: 21
                    sourceSize: Qt.size(108, 42)
                    cache: false
                    source: "image://ink/nib/" + tile.pen.id + "/" + root.hex + "/" + root.penWidth + "/" + (tile.pen.night ? "1" : "0") + "/" + root.dash
                }
                // The chosen one.
                Rectangle {
                    visible: tile.on
                    anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: -5
                    width: 16; height: 16; radius: 8
                    color: Theme.accent
                    border.color: Theme.surface; border.width: 2
                    Icon { anchors.centerIn: parent; name: "check"; size: 10; color: "white" }
                }
            }
            Label {
                Layout.fillWidth: true
                text: tile.pen.label
                font.pixelSize: 11
                font.weight: tile.on ? Font.DemiBold : Font.Normal
                color: tile.on ? Theme.accent : Theme.text
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }
    component TrayTitle: Label {
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 0.4
        color: Theme.muted
        Layout.leftMargin: 4
    }

    contentItem: ColumnLayout {
        spacing: 8
        TrayTitle { text: root.trays[0].title.toUpperCase() }
        RowLayout {
            spacing: 4
            Repeater { model: root.trays[0].pens; delegate: Tile { required property var modelData; pen: modelData } }
        }
        Rectangle { Layout.fillWidth: true; Layout.topMargin: 2; Layout.bottomMargin: 2; implicitHeight: 1; color: Theme.border }
        RowLayout {
            spacing: 12
            ColumnLayout {
                spacing: 8
                Layout.preferredWidth: 3
                Layout.fillWidth: true
                TrayTitle { text: root.trays[1].title.toUpperCase() }
                RowLayout {
                    spacing: 4
                    Repeater { model: root.trays[1].pens; delegate: Tile { required property var modelData; pen: modelData } }
                }
            }
            Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Theme.border }
            ColumnLayout {
                spacing: 8
                Layout.preferredWidth: 2
                Layout.fillWidth: true
                TrayTitle { text: root.trays[2].title.toUpperCase() }
                RowLayout {
                    spacing: 4
                    Repeater { model: root.trays[2].pens; delegate: Tile { required property var modelData; pen: modelData } }
                }
            }
        }
        // What the pen does.
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 2
            implicitHeight: about.implicitHeight + 16
            radius: 10
            color: Theme.surface2
            Label {
                id: about
                anchors.fill: parent
                anchors.margins: 8
                anchors.leftMargin: 10
                text: root.shown.about
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: Theme.muted
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
