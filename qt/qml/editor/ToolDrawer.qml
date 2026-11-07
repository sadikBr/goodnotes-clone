// The active tool's options, hanging under the trays (src/editor/editor.ts
// renderToolbar / toolOptions): nib and smoothing, shape kind and fill, font
// and format, eraser and laser modes; favourite colours and sizes; the pin.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf
import "Fonts.js" as Fonts
import "Shapes.js" as Shapes

Item {
    id: root
    required property InkCanvas canvas
    required property var prefs
    /// The active tool's button (the drawer's pointer aims at it).
    property Item anchorItem: null
    /// Where the pointer aims, in the drawer's coordinates (set by the editor).
    property real pointerX: width / 2
    /// The drawer sits beside the tool (the side rail): the pointer is on its left, at `pointerY`.
    property bool fromLeft: false
    /// The drawer sits above the tool (a phone's bottom bar): the pointer is under it.
    property bool fromBelow: false
    property real pointerY: height / 2
    /// Squarer corners, as the tool strip has.
    readonly property bool squared: true
    /// The room it has: in a narrow window it drops the pen's name and the sizes' numbers.
    property real maxWidth: 100000
    readonly property bool compact: maxWidth < 760
    readonly property string tool: canvas.tool
    readonly property var fav: prefs.get(tool)
    readonly property var spec: prefs.specs[tool]
    readonly property bool hasOptions: content.implicitWidth > 0
    signal changed()
    signal penPicked()

    implicitWidth: content.implicitWidth + 20 + 34
    implicitHeight: Math.max(48, content.implicitHeight + 12)

    readonly property var smoothings: [{ id: "off", label: "Off", v: 0 }, { id: "light", label: "Light", v: 0.2 }, { id: "normal", label: "Normal", v: 0.38 }, { id: "strong", label: "Strong", v: 0.62 }]
    /// The pen and the brush: their kind (each its own), and its ink.
    readonly property bool inks: tool === "pen" || tool === "brush"
    readonly property string styleNow: tool === "brush" ? canvas.brushStyle : canvas.penStyle
    readonly property string hex: (canvas.colors[inks ? tool : "pen"] || canvas.colors.pen || "#111827").replace("#", "")
    readonly property real inkWidth: canvas.widths[inks ? tool : "pen"] || 3
    readonly property var lineStyles: [{ value: "solid", label: "——", tip: "Solid line" }, { value: "dash", label: "– – –", tip: "Dashed line" }, { value: "dot", label: "• • •", tip: "Dotted line" }]
    readonly property var gaps: [{ v: 0.6, label: "Close gaps" }, { v: 1, label: "Normal gaps" }, { v: 1.8, label: "Wide gaps" }, { v: 3, label: "Very wide gaps" }]
    readonly property string dark: Theme.dark ? "1" : "0"
    /// This tool's colour, for its size previews.
    readonly property string toolHex: (canvas.colors[tool] || canvas.colors.pen || "#111827").replace("#", "")

    PopupMenu { id: menu }
    ShapePicker { id: shapePicker; onPicked: id => root.prefs.store.shapeKind = id }
    // Two cases: the pens, and the brushes.
    component InkCase: PenCase {
        lines: root.prefs.dashable(root.styleNow)
        lineStyle: root.canvas.penDash
        gap: root.canvas.penGap
        smoothing: AppSettings.store.smoothing
        onLinePicked: v => root.prefs.setDash(root.tool, v)
        onGapPicked: v => root.prefs.setGap(root.tool, v)
        onSmoothingPicked: v => AppSettings.store.smoothing = v
    }
    InkCase { id: penCase; family: "pen"; onPicked: id => { root.prefs.store.penStyle = id; root.penPicked() } }
    InkCase { id: brushCase; family: "brush"; onPicked: id => { root.prefs.store.brushStyle = id; root.penPicked() } }
    readonly property var caseNow: tool === "brush" ? brushCase : penCase
    ColorPopover { id: colorPop }
    Popup {
        id: sizePop
        parent: Overlay.overlay
        padding: 14
        width: 300
        modal: true
        dim: false
        Overlay.modal: Item {}
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property int index: 0
        background: Item {
            Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
            Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
        }
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Label { text: root.spec ? "Your " + root.spec.label.toLowerCase() : ""; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.text; Layout.fillWidth: true }
                Label { text: root.spec ? Number(root.spec.min) + "–" + Number(root.spec.max) + " px" : ""; font.pixelSize: 11; color: Theme.muted }
            }
            // How it looks, live.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                radius: Theme.radius
                antialiasing: true
                color: Theme.surface2
                SizePreview {
                    anchors.centerIn: parent
                    big: true
                    active: true
                    tool: root.tool; penStyle: root.styleNow; hex: root.toolHex; dark: root.dark
                    dash: root.canvas.penDash; gap: root.canvas.penGap
                    value: sizeSlider.value
                    relative: root.spec ? (sizeSlider.value - root.spec.min) / (root.spec.max - root.spec.min) : 0.5
                }
            }
            RowLayout {
                spacing: 6
                component Step: AbstractButton {
                    id: stp
                    property string glyph: ""
                    implicitWidth: 28; implicitHeight: 28
                    hoverEnabled: true
                    autoRepeat: true
                    background: Rectangle { antialiasing: true; radius: Theme.radiusSm; color: stp.pressed ? Theme.border : stp.hovered ? Theme.surface2 : "transparent"; border.color: Theme.border }
                    contentItem: Label { text: stp.glyph; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text }
                }
                Step { glyph: "−"; onClicked: sizeField.nudge(-1) }
                UiSlider {
                    id: sizeSlider
                    Layout.fillWidth: true
                    from: root.spec ? root.spec.min : 1
                    to: root.spec ? root.spec.max : 10
                    stepSize: root.spec ? root.spec.step : 1
                    value: root.fav && root.fav.sizes.length ? root.fav.sizes[root.fav.size] : 1
                    onMoved: { root.prefs.setSize(root.tool, sizePop.index, value); sizeField.show(value) }
                }
                Step { glyph: "+"; onClicked: sizeField.nudge(1) }
                // Or type it ("12").
                UiField {
                    id: sizeField
                    Layout.preferredWidth: 58
                    implicitHeight: 30
                    horizontalAlignment: Text.AlignRight
                    rightPadding: 26
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    validator: DoubleValidator { bottom: 0; top: 999; decimals: 1; notation: DoubleValidator.StandardNotation }
                    function show(v) { text = Number(v).toFixed(v % 1 ? 1 : 0) }
                    function put(v) {
                        if (!root.spec) return
                        const st = root.spec.step
                        const c = Math.max(root.spec.min, Math.min(root.spec.max, Math.round(v / st) * st))
                        root.prefs.setSize(root.tool, sizePop.index, c)
                        show(c)
                    }
                    function nudge(d) { put(sizeSlider.value + d * (root.spec ? root.spec.step : 1)) }
                    function apply() {
                        const v = parseFloat(String(text).replace(",", "."))
                        if (!isNaN(v)) put(v)
                    }
                    onAccepted: { apply(); sizePop.close() }
                    onEditingFinished: apply()
                    Label { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: "px"; font.pixelSize: 12; color: Theme.muted }
                }
            }
        }
        onOpened: { sizeField.show(sizeSlider.value); sizeField.forceActiveFocus(); sizeField.selectAll() }
    }
    function openSize(item, index) {
        sizePop.index = index
        const p = item.mapToItem(sizePop.parent, 0, 0)
        sizePop.x = Math.max(8, Math.min(sizePop.parent.width - sizePop.width - 8, p.x + item.width / 2 - sizePop.width / 2))
        sizePop.y = Theme.phone ? Math.max(8, p.y - sizePop.implicitHeight - 8) : p.y + item.height + 8
        sizePop.open()
    }

    // The panel and its pointer.
    Shadow { anchors.fill: panel; radius: panel.radius; level: 3 }
    Rectangle {
        id: caret
        width: 12; height: 12; rotation: 45
        y: root.fromLeft ? Math.round(Math.max(12, Math.min(root.height - 24, root.pointerY - 6))) : root.fromBelow ? root.height - 6 : -6
        x: root.fromLeft ? -6 : Math.round(Math.max(16, Math.min(root.width - 28, root.pointerX - 6)))
        antialiasing: true
        color: Theme.surface
        border.color: Theme.border
    }
    Rectangle { antialiasing: true;
        id: panel
        anchors.fill: parent
        radius: Theme.radiusLg
        color: Theme.surface
        border.color: Theme.border
        // Hide the pointer's inner half.
        Rectangle { x: root.fromLeft ? 0.5 : caret.x - 2; y: root.fromLeft ? caret.y - 2 : root.fromBelow ? parent.height - 8.5 : 0.5; width: root.fromLeft ? 8 : 16; height: root.fromLeft ? 16 : 8; color: Theme.surface }
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    // In a row; on a phone, one above the other (the pen, its colours, its sizes).
    GridLayout {
        id: content
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        columns: Theme.phone ? 1 : -1
        columnSpacing: 14
        rowSpacing: 8

        // --- the tool's own choices ---
        RowLayout {
            visible: root.inks
            spacing: 6
            Chip {
                corner: Theme.radius
                id: nibChip
                readonly property bool seen: root.tool === "brush" ? root.prefs.store.brushCaseSeen : root.prefs.store.penCaseSeen
                label: root.compact ? "" : root.caseNow.penFor(root.styleNow).label
                tip: (root.tool === "brush" ? "Brushes" : "Pens") + ", line and smoothing"
                art: Component { Image { source: "image://ink/nib/" + root.styleNow + "/" + root.hex + "/" + root.inkWidth + "/" + root.dark + "/" + root.canvas.penDash + "/" + root.canvas.penGap; sourceSize: Qt.size(72, 36); width: 36; height: 18; cache: false } }
                open: root.caseNow.opened
                onClicked: {
                    const pc = root.caseNow; pc.current = root.styleNow; pc.hex = root.hex; pc.penWidth = root.inkWidth; pc.dash = "solid"; pc.inks = root.prefs.penInks(); pc.show(nibChip)
                    if (root.tool === "brush") root.prefs.store.brushCaseSeen = true; else root.prefs.store.penCaseSeen = true
                }
                // A dot until the case has been opened once (there are new ones in it).
                Rectangle {
                    visible: !nibChip.seen
                    anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: -3
                    width: 10; height: 10; radius: 3
                    color: Theme.accent
                    border.color: Theme.surface; border.width: 2
                }
            }
        }
        RowLayout {
            visible: root.tool === "shape"
            spacing: 6
            Chip {
                corner: Theme.radius
                id: shapeChip
                label: root.compact ? "" : Shapes.label(root.canvas.shapeKind)
                tip: "Shape to draw"
                art: Component {
                    Item {
                        implicitWidth: 18; implicitHeight: 18
                        Icon { visible: root.canvas.shapeKind === "auto"; anchors.centerIn: parent; name: "sAuto"; size: 18; color: Theme.text }
                        Image { visible: root.canvas.shapeKind !== "auto"; anchors.fill: parent; sourceSize: Qt.size(36, 36); source: "image://ink/shape/" + root.canvas.shapeKind + "/" + String(Theme.text).replace("#", "") }
                    }
                }
                onClicked: { shapePicker.current = root.canvas.shapeKind; shapePicker.withAuto = true; shapePicker.only = "all"; shapePicker.show(shapeChip) }
            }
            SmallTool { iconName: "fill"; active: root.canvas.shapeFill; tip: root.canvas.shapeFill ? "Fill on" : "Fill off"; onClicked: root.canvas.shapeFill = !root.canvas.shapeFill }
            ModeGroup {
                corner: Theme.radiusLg
                options: root.lineStyles
                value: root.canvas.penDash
                onPicked: v => root.prefs.setDash("shape", v)
            }
        }
        RowLayout {
            visible: root.tool === "text" || root.tool === "sticky"
            spacing: 12
            // .font-picker: the font's name in its own face.
            AbstractButton {
                id: fontChip
                readonly property string current: root.canvas.editing.font || root.canvas.textFont
                implicitHeight: 34
                implicitWidth: Math.max(170, fontRow.implicitWidth + 22)
                hoverEnabled: true
                ToolTip.visible: hovered && !menu.opened && !tipDismissed
                ToolTip.text: "Handwriting font"
                property bool tipDismissed: false
                onPressedChanged: if (pressed) tipDismissed = true
                onHoveredChanged: if (!hovered) tipDismissed = false
                onClicked: menu.show(fontChip, Fonts.presets.map(f => ({ label: f.label, font: f.face, fontSize: 18, checked: f.id === fontChip.current, action: () => { root.canvas.editFormat({ font: f.id }); root.prefs.store.textFont = f.id } })), 220)
                background: Rectangle { antialiasing: true; radius: Theme.radius; color: Theme.surface; border.color: fontChip.hovered ? Theme.accent : Theme.border }
                contentItem: Item {
                    RowLayout {
                        id: fontRow
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: 8
                        Label { text: Fonts.preset(fontChip.current).label; font.family: Fonts.preset(fontChip.current).face; font.pixelSize: 18; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                        Icon { name: "down"; size: 16; color: Theme.muted }
                    }
                }
            }
            FormatButtons { canvas: root.canvas }
        }
        ModeGroup {
            corner: Theme.radiusLg
            visible: root.tool === "eraser"
            options: [{ value: "precise", label: "Precise", tip: "Erase only the parts the eraser touches" }, { value: "stroke", label: "Whole stroke", tip: "Erase every stroke the eraser touches" }]
            value: root.canvas.eraserMode
            onPicked: v => root.prefs.store.eraserMode = v
        }
        ModeGroup {
            corner: Theme.radiusLg
            visible: root.tool === "laser"
            options: [{ value: "trail", label: "Trail", tip: "Draw a glowing line that fades away" }, { value: "dot", label: "Dot", tip: "Just point with a glowing dot" }]
            value: root.canvas.laserMode
            onPicked: v => root.prefs.store.laserMode = v
        }
        Label {
            visible: root.tool === "lasso" || root.tool === "hand"
            text: root.tool === "lasso" ? "Draw around items to select them, then drag to move" : "Drag to scroll · Ctrl + scroll or pinch to zoom"
            color: Theme.muted
            font.pixelSize: 13
            leftPadding: 8; rightPadding: 8
        }

        // --- this tool's colours (each pen type has its own), in a well ---
        Rectangle {
            visible: !!(!!root.fav && root.fav.colors.length > 0)
            antialiasing: true
            implicitWidth: colorsRow.implicitWidth + 12
            implicitHeight: 40
            radius: Theme.radiusLg
            color: Theme.surface2
            Row {
                id: colorsRow
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: root.fav ? root.fav.colors : []
                    delegate: AbstractButton {
                        id: sw
                        required property string modelData
                        required property int index
                        readonly property bool on: root.fav && root.fav.color === index
                        width: 30; height: 30
                        hoverEnabled: true
                        ToolTip.visible: !!(hovered && on) && !tipDismissed
                        // Tooltips go away once clicked (until the pointer leaves), as in the web app.
                        property bool tipDismissed: false
                        onPressedChanged: if (pressed) tipDismissed = true
                        onHoveredChanged: if (!hovered) tipDismissed = false
                        ToolTip.text: "Tap again to change this colour"
                        onClicked: on ? colorPop.show(sw, root.prefs.paletteFor(root.tool), modelData, c => root.prefs.setColor(root.tool, index, c)) : root.prefs.selectColor(root.tool, index)
                        background: Rectangle {
                            antialiasing: true
                            radius: Theme.radiusSm
                            color: sw.on ? Theme.surface : "transparent"
                            border.width: sw.on ? 1 : 0
                            border.color: Theme.border
                        }
                        contentItem: Item {
                            Rectangle {
                                antialiasing: true
                                anchors.centerIn: parent
                                width: sw.on ? 20 : 18; height: width
                                radius: Theme.swatchRadius(width)
                                color: sw.modelData
                                border.width: 1
                                border.color: Theme.dark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0, 0, 0, 0.14)
                                scale: sw.hovered && !sw.on ? 1.1 : 1
                                Behavior on scale { NumberAnimation { duration: 120 } }
                            }
                        }
                    }
                }
                Rectangle { width: 1; height: 20; anchors.verticalCenter: parent.verticalCenter; color: Theme.border }
                AbstractButton {
                    id: rainbow
                    width: 30; height: 30
                    hoverEnabled: true
                    ToolTip.visible: !!(hovered) && !tipDismissed
                    // Tooltips go away once clicked (until the pointer leaves), as in the web app.
                    property bool tipDismissed: false
                    onPressedChanged: if (pressed) tipDismissed = true
                    onHoveredChanged: if (!hovered) tipDismissed = false
                    ToolTip.text: "More colours"
                    onClicked: colorPop.show(rainbow, root.prefs.paletteFor(root.tool), root.prefs.color(root.tool), c => root.prefs.setColor(root.tool, root.fav.color, c))
                    background: Rectangle { antialiasing: true; radius: Theme.radiusSm; color: rainbow.hovered ? Theme.surface : "transparent" }
                    contentItem: Item {
                        Image { anchors.centerIn: parent; width: 20; height: 20; sourceSize: Qt.size(40, 40); source: Theme.rainbow(true) }
                    }
                }
            }
        }

        // --- this tool's sizes, each drawn as it looks; the last is yours to set ---
        Rectangle {
            visible: !!(!!root.fav && root.fav.sizes.length > 0 && !!root.spec)
            antialiasing: true
            implicitWidth: sizesRow.implicitWidth + 8
            implicitHeight: 40
            radius: Theme.radiusLg
            color: Theme.surface2
            Row {
                id: sizesRow
                anchors.centerIn: parent
                spacing: 2
                Repeater {
                    model: root.fav ? root.fav.sizes : []
                    delegate: AbstractButton {
                        id: sz
                        required property real modelData
                        required property int index
                        readonly property bool on: root.fav && root.fav.size === index
                        /// Your own size (the last): tap again to set it.
                        readonly property bool mine: index === root.prefs.customSize
                        readonly property string num: Number(modelData).toFixed(modelData % 1 ? 1 : 0)
                        width: preview.implicitWidth + label.implicitWidth + (mine ? 26 : 16)
                        height: 32
                        hoverEnabled: true
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: mine ? (on ? "Your size · tap to change it" : "Your size (" + num + " px) · tap again to change it") : num + " px"
                        onClicked: on && mine ? root.openSize(sz, index) : root.prefs.selectSize(root.tool, index)
                        background: Item {
                            Shadow { anchors.fill: parent; radius: Theme.radiusSm; level: sz.on ? 1 : 0 }
                            Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusSm; color: sz.on ? Theme.surface : sz.hovered ? Theme.alpha(Theme.surface, 0.55) : "transparent" }
                            DashedBox { visible: sz.mine && !sz.on; anchors.fill: parent; anchors.margins: 1; radius: Theme.radiusSm; lineWidth: 1; color: sz.hovered ? Theme.text : Theme.muted }
                        }
                        contentItem: Item {
                            Row {
                                anchors.centerIn: parent
                                spacing: 4
                                SizePreview {
                                    id: preview
                                    anchors.verticalCenter: parent.verticalCenter
                                    tool: root.tool; penStyle: root.styleNow; hex: root.toolHex; dark: root.dark
                                    dash: root.canvas.penDash; gap: root.canvas.penGap
                                    value: sz.modelData
                                    relative: sz.modelData / root.fav.sizes[2]
                                    active: sz.on
                                }
                                Label {
                                    id: label
                                    visible: !root.compact
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: sz.num
                                    font.pixelSize: 11
                                    font.weight: sz.on ? Font.DemiBold : Font.Normal
                                    color: sz.on ? Theme.text : Theme.muted
                                }
                                // Yours opens to set it.
                                Icon { visible: sz.mine; anchors.verticalCenter: parent.verticalCenter; name: "down"; size: 10; color: sz.on ? Theme.text : Theme.muted }
                            }
                        }
                        // A line before your own size.
                        Rectangle { visible: sz.mine; x: -2; z: 2; anchors.verticalCenter: parent.verticalCenter; width: 1; height: 18; color: Theme.alpha(Theme.muted, 0.5) }
                    }
                }
            }
        }
    }

    // Keep the options open (or let them tuck away while you write).
    AbstractButton {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 26; height: 26
        hoverEnabled: true
        ToolTip.visible: !!(hovered) && !tipDismissed
        // Tooltips go away once clicked (until the pointer leaves), as in the web app.
        property bool tipDismissed: false
        onPressedChanged: if (pressed) tipDismissed = true
        onHoveredChanged: if (!hovered) tipDismissed = false
        ToolTip.text: root.prefs.store.drawerPinned ? "Pinned open — unpin to tuck options away while you write" : "Keep options open"
        onClicked: root.prefs.store.drawerPinned = !root.prefs.store.drawerPinned
        background: Rectangle { antialiasing: true; radius: Theme.radius; color: root.prefs.store.drawerPinned ? Theme.accentSoft : parent.hovered ? Theme.surface2 : "transparent" }
        contentItem: Item { Icon { anchors.centerIn: parent; name: "pin"; size: 15; color: root.prefs.store.drawerPinned ? Theme.accent : Theme.muted } }
    }
}
