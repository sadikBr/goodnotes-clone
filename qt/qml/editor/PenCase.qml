// The pen case (or the brush case): every pen or brush as a stroke drawn
// with it, on a little piece of paper, in trays. A line under the trays says
// what the one under the pointer (or the chosen one) does.
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
    /// Each pen's own ink and size (id → { hex, width }); else hex and penWidth for all.
    property var inks: ({})
    signal picked(string id)
    /// The settings at the bottom: the line (pens with an even line), its gaps, and smoothing.
    property bool lines: false
    property string lineStyle: "solid"
    property real gap: 1
    property string smoothing: "normal"
    signal linePicked(string v)
    signal gapPicked(real v)
    signal smoothingPicked(string v)

    /// "pen": the pens; "brush": the brushes (two tools, two cases).
    property string family: "pen"
    readonly property var penTrays: [
        { title: "Pens", pens: [
            { id: "fountain", label: "Fountain", about: "Fountain pen: thick and thin with pressure and speed." },
            { id: "ballpoint", label: "Ballpoint", about: "Ballpoint: the same line all the way. Can be dashed or dotted." },
            { id: "fineliner", label: "Fineliner", about: "Fineliner: thin and perfectly even, for diagrams and labels. Can be dashed or dotted." },
            { id: "pencil", label: "Pencil", about: "Pencil: soft grain, lighter when you press lightly." },
            { id: "calligraphy", label: "Calligraphy", about: "Calligraphy: a flat nib, broad one way and fine the other." }] }
    ]
    readonly property var brushTrays: [
        { title: "Brushes", pens: [
            { id: "brush", label: "Brush pen", about: "Brush pen: pointed ends that swell with pressure." },
            { id: "inkbrush", label: "Ink brush", about: "Ink brush: long tapers and deep ink, like a sumi brush." },
            { id: "flat", label: "Flat brush", about: "Flat brush: a wide, slanted tip with the streaks of its bristles." },
            { id: "airbrush", label: "Airbrush", about: "Airbrush: a soft spray, for shading and glows." }] },
        { title: "Texture", pens: [
            { id: "chalk", label: "Chalk", about: "Chalk: grainy and dusty, at its best on dark and whiteboard pages." },
            { id: "crayon", label: "Crayon", about: "Crayon: waxy, catching on the paper's grain." },
            { id: "watercolor", label: "Watercolour", about: "Watercolour: soft washes that deepen as you layer them." }] },
        { title: "Colour & light", pens: [
            { id: "marker", label: "Marker", about: "Marker: a see-through felt tip, darker where strokes cross. Can be dashed or dotted." },
            { id: "neon", label: "Neon", about: "Neon: a bright line with a glow around it. Can be dashed or dotted.", night: true },
            { id: "tape", label: "Tape", about: "Masking tape: cover answers and tap the tape to peek under it." }] }
    ]
    readonly property var trays: family === "brush" ? brushTrays : penTrays
    function penFor(id) {
        for (const t of penTrays.concat(brushTrays)) for (const p of t.pens) if (p.id === id) return p
        return trays[0].pens[0]
    }
    property string hovered: ""
    readonly property var shown: penFor(hovered || current)

    function show(anchor) {
        hovered = ""
        const p = anchor.mapToItem(parent, 0, 0)
        x = Math.max(8, Math.min(parent.width - width - 8, p.x + anchor.width / 2 - width / 2))
        // On a phone the tools are at the bottom: the case opens above them.
        y = Theme.phone ? Math.max(8, p.y - implicitHeight - 10) : p.y + anchor.height + 10
        open()
    }

    parent: Overlay.overlay
    padding: 14
    width: Theme.phone && parent ? Math.min(parent.width - 16, 412) : family === "brush" ? 452 : 412
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
            radius: Theme.radiusLg
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
                radius: Theme.radius
                antialiasing: true
                color: tile.pen.night ? "#111827" : "#ffffff"
                border.color: Theme.alpha(Theme.text, Theme.dark ? 0.0 : 0.1)
                Image {
                    anchors.centerIn: parent
                    width: 54; height: 21
                    sourceSize: Qt.size(108, 42)
                    cache: false
                    source: "image://ink/nib/" + tile.pen.id + "/" + ((root.inks[tile.pen.id] || {}).hex || root.hex) + "/" + ((root.inks[tile.pen.id] || {}).width || root.penWidth) + "/" + (tile.pen.night ? "1" : "0") + "/" + root.dash
                }
                // The chosen one.
                Rectangle {
                    visible: tile.on
                    anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: -5
                    width: 16; height: 16; radius: Theme.radiusXs
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
        Rectangle { visible: root.trays.length > 2; Layout.fillWidth: true; Layout.topMargin: 2; Layout.bottomMargin: 2; implicitHeight: 1; color: Theme.border }
        // Side by side; on a phone, one under the other.
        GridLayout {
            visible: root.trays.length > 2
            columns: Theme.phone ? 1 : 3
            columnSpacing: 12
            rowSpacing: 8
            ColumnLayout {
                spacing: 8
                Layout.preferredWidth: 3
                Layout.fillWidth: true
                TrayTitle { text: (root.trays[1] || { title: "" }).title.toUpperCase() }
                RowLayout {
                    spacing: 4
                    Repeater { model: (root.trays[1] || { pens: [] }).pens; delegate: Tile { required property var modelData; pen: modelData } }
                }
            }
            Rectangle { Layout.fillHeight: !Theme.phone; Layout.fillWidth: Theme.phone; implicitWidth: 1; implicitHeight: 1; color: Theme.border }
            ColumnLayout {
                spacing: 8
                Layout.preferredWidth: 3
                Layout.fillWidth: true
                TrayTitle { text: (root.trays[2] || { title: "" }).title.toUpperCase() }
                RowLayout {
                    spacing: 4
                    Repeater { model: (root.trays[2] || { pens: [] }).pens; delegate: Tile { required property var modelData; pen: modelData } }
                }
            }
        }
        // What the pen does.
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 2
            implicitHeight: about.implicitHeight + 16
            radius: Theme.radius
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
        // How it draws: the line, its gaps, smoothing (kept here, not in the strip).
        component SettingRow: RowLayout {
            property alias text: rowLabel.text
            Layout.fillWidth: true
            spacing: 10
            Label { id: rowLabel; Layout.preferredWidth: 70; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border; visible: root.current !== "tape" }
        SettingRow {
            text: "Line"
            visible: root.lines
            ModeGroup {
                corner: Theme.radiusLg
                options: [{ value: "solid", label: "Solid" }, { value: "dash", label: "– – Dashed" }, { value: "dot", label: "• • Dotted" }]
                value: root.lineStyle
                onPicked: v => root.linePicked(v)
            }
            Item { Layout.fillWidth: true }
        }
        SettingRow {
            text: "Gaps"
            visible: root.lines && root.lineStyle !== "solid"
            ModeGroup {
                corner: Theme.radiusLg
                options: [{ value: 0.6, label: "Close" }, { value: 1, label: "Normal" }, { value: 1.8, label: "Wide" }, { value: 3, label: "Very wide" }]
                value: [0.6, 1, 1.8, 3].reduce((a, b) => Math.abs(b - root.gap) < Math.abs(a - root.gap) ? b : a)
                onPicked: v => root.gapPicked(v)
            }
            Item { Layout.fillWidth: true }
        }
        SettingRow {
            text: "Smoothing"
            visible: root.current !== "tape"
            ModeGroup {
                corner: Theme.radiusLg
                options: [{ value: "off", label: "Off", tip: "Draws exactly what you write" }, { value: "light", label: "Light" }, { value: "normal", label: "Normal" }, { value: "strong", label: "Strong", tip: "Evens out shaky lines the most" }]
                value: root.smoothing
                onPicked: v => root.smoothingPicked(v)
            }
            Item { Layout.fillWidth: true }
        }
    }
}
