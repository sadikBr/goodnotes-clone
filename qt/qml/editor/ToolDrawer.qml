// The active tool's options, hanging under the trays (src/editor/editor.ts
// renderToolbar / toolOptions): nib and smoothing, shape kind and fill, font
// and format, eraser and laser modes; favourite colours and sizes; the pin.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf
import "Fonts.js" as Fonts

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
    property real pointerY: height / 2
    /// Squarer corners, as the tool strip has.
    readonly property bool squared: true
    readonly property string tool: canvas.tool
    readonly property var fav: prefs.get(tool)
    readonly property var spec: prefs.specs[tool]
    readonly property bool hasOptions: content.implicitWidth > 0
    signal changed()
    signal penPicked()

    implicitWidth: content.implicitWidth + 20 + 34
    implicitHeight: Math.max(48, content.implicitHeight + 12)

    readonly property var smoothings: [{ id: "off", label: "Off", v: 0 }, { id: "light", label: "Light", v: 0.2 }, { id: "normal", label: "Normal", v: 0.38 }, { id: "strong", label: "Strong", v: 0.62 }]
    readonly property var shapeKinds: [{ id: "auto", label: "Auto shape", icon: "sAuto" }, { id: "line", label: "Line", icon: "sLine" }, { id: "arrow", label: "Arrow", icon: "sArrow" }, { id: "rectangle", label: "Rectangle", icon: "sRectangle" }, { id: "ellipse", label: "Ellipse / circle", icon: "sEllipse" }, { id: "triangle", label: "Triangle", icon: "sTriangle" }, { id: "diamond", label: "Diamond", icon: "sDiamond" }, { id: "star", label: "Star", icon: "sStar" }]
    readonly property string hex: (canvas.colors.pen || "#111827").replace("#", "")
    readonly property string dark: Theme.dark ? "1" : "0"

    PopupMenu { id: menu }
    PenCase { id: penCase; onPicked: id => { root.prefs.store.penStyle = id; root.penPicked() } }
    ColorPopover { id: colorPop }
    Popup {
        id: sizePop
        parent: Overlay.overlay
        padding: 14
        width: 260
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
            Label { text: root.spec ? root.spec.label : ""; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted }
            RowLayout {
                spacing: 10
                UiSlider {
                    id: sizeSlider
                    Layout.fillWidth: true
                    from: root.spec ? root.spec.min : 1
                    to: root.spec ? root.spec.max : 10
                    stepSize: root.spec ? root.spec.step : 1
                    value: root.fav && root.fav.sizes.length ? root.fav.sizes[root.fav.size] : 1
                    onMoved: root.prefs.setSize(root.tool, sizePop.index, value)
                }
                Label { text: sizeSlider.value.toFixed(root.spec && root.spec.step < 1 ? 1 : 0) + " px"; color: Theme.text; font.weight: Font.DemiBold; Layout.preferredWidth: 52; horizontalAlignment: Text.AlignRight }
            }
        }
    }

    // The panel and its pointer.
    Shadow { anchors.fill: panel; radius: panel.radius; level: 3 }
    Rectangle {
        id: caret
        width: 12; height: 12; rotation: 45
        y: root.fromLeft ? Math.round(Math.max(12, Math.min(root.height - 24, root.pointerY - 6))) : -6
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
        Rectangle { x: root.fromLeft ? 0.5 : caret.x - 2; y: root.fromLeft ? caret.y - 2 : 0.5; width: root.fromLeft ? 8 : 16; height: root.fromLeft ? 16 : 8; color: Theme.surface }
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    RowLayout {
        id: content
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14

        // --- the tool's own choices ---
        RowLayout {
            visible: root.tool === "pen"
            spacing: 6
            Chip {
                corner: Theme.radius
                id: nibChip
                label: penCase.penFor(root.canvas.penStyle).label
                tip: "Pen case"
                art: Component { Image { source: "image://ink/nib/" + root.canvas.penStyle + "/" + root.hex + "/" + (root.canvas.widths.pen || 3) + "/" + root.dark + "/" + root.canvas.penDash; sourceSize: Qt.size(72, 36); width: 36; height: 18; cache: false } }
                open: penCase.opened
                onClicked: { penCase.current = root.canvas.penStyle; penCase.hex = root.hex; penCase.penWidth = root.canvas.widths.pen || 3; penCase.dash = root.canvas.penDash; penCase.inks = root.prefs.penInks(); penCase.show(nibChip); root.prefs.store.penCaseSeen = true }
                // A dot until the pen case has been opened once (there are new pens in it).
                Rectangle {
                    visible: !root.prefs.store.penCaseSeen
                    anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: -3
                    width: 10; height: 10; radius: 3
                    color: Theme.accent
                    border.color: Theme.surface; border.width: 2
                }
            }
            // Construction lines: dashes or dots, and how far apart.
            ModeGroup {
                corner: Theme.radiusLg
                visible: root.canvas.penStyle === "construction"
                options: [{ value: "dash", label: "– – –", tip: "Dashes" }, { value: "dot", label: "• • •", tip: "Dots" }]
                value: root.canvas.penDash
                onPicked: v => root.prefs.store.penDash = v
            }
            Chip {
                corner: Theme.radius
                id: gapChip
                visible: root.canvas.penStyle === "construction"
                readonly property var gaps: [{ v: 0.6, label: "Close" }, { v: 1, label: "Normal" }, { v: 1.8, label: "Wide" }, { v: 3, label: "Very wide" }]
                label: (gaps.find(g => Math.abs(g.v - root.canvas.penGap) < 0.01) || gaps[1]).label
                tip: "Spacing"
                art: Component { Image { source: "image://ink/nib/construction/" + root.hex + "/" + (root.canvas.widths.pen || 3) + "/" + root.dark + "/" + root.canvas.penDash + "/" + root.canvas.penGap; sourceSize: Qt.size(72, 36); width: 36; height: 18; cache: false } }
                onClicked: menu.show(gapChip, gaps.map(g => ({ label: g.label, image: "image://ink/nib/construction/" + root.hex + "/" + (root.canvas.widths.pen || 3) + "/" + root.dark + "/" + root.canvas.penDash + "/" + g.v, checked: Math.abs(g.v - root.canvas.penGap) < 0.01, action: () => root.prefs.store.penGap = g.v })))
            }
            Label {
                visible: root.canvas.penStyle === "tape"
                text: "Tap tape to peek under it"
                color: Theme.muted
                font.pixelSize: 12
                leftPadding: 4; rightPadding: 4
            }
            Chip {
                corner: Theme.radius
                id: smoothChip
                visible: root.canvas.penStyle !== "tape"
                label: (root.smoothings.find(s => s.id === AppSettings.store.smoothing) || root.smoothings[2]).label
                tip: "Smoothing: evens out shaky lines (Off draws exactly what you write)"
                art: Component { Image { source: "image://ink/smooth/" + AppSettings.streamline + "/" + root.hex + "/" + root.dark; sourceSize: Qt.size(72, 36); width: 36; height: 18; cache: false } }
                onClicked: menu.show(smoothChip, root.smoothings.map(s => ({ label: s.label, image: "image://ink/smooth/" + s.v + "/" + root.hex + "/" + root.dark, checked: s.id === AppSettings.store.smoothing, action: () => AppSettings.store.smoothing = s.id })))
            }
        }
        RowLayout {
            visible: root.tool === "shape"
            spacing: 6
            Chip {
                corner: Theme.radius
                id: shapeChip
                label: (root.shapeKinds.find(k => k.id === root.canvas.shapeKind) || root.shapeKinds[0]).label
                tip: "Shape to draw"
                art: Component { Icon { name: (root.shapeKinds.find(k => k.id === root.canvas.shapeKind) || root.shapeKinds[0]).icon; size: 18; color: Theme.text } }
                onClicked: menu.show(shapeChip, root.shapeKinds.map(k => ({ label: k.label, icon: k.icon, checked: k.id === root.canvas.shapeKind, action: () => root.prefs.store.shapeKind = k.id })))
            }
            SmallTool { iconName: "fill"; active: root.canvas.shapeFill; tip: root.canvas.shapeFill ? "Fill on" : "Fill off"; onClicked: root.canvas.shapeFill = !root.canvas.shapeFill }
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

        // --- this tool's sizes: for a pen, its own stroke at each size ---
        Rectangle {
            visible: !!(!!root.fav && root.fav.sizes.length > 0 && !!root.spec)
            antialiasing: true
            implicitWidth: sizesRow.implicitWidth + 12
            implicitHeight: 40
            radius: Theme.radiusLg
            color: Theme.surface2
            readonly property bool strokes: root.tool === "pen"
            Row {
                id: sizesRow
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: root.fav ? root.fav.sizes : []
                    delegate: AbstractButton {
                        id: sz
                        required property real modelData
                        required property int index
                        readonly property bool on: root.fav && root.fav.size === index
                        readonly property bool strokes: root.tool === "pen"
                        width: strokes ? 62 : 34; height: 30
                        hoverEnabled: true
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: on ? "Tap again for any size" : Number(modelData).toFixed(root.spec && root.spec.step < 1 ? 1 : 0) + " px"
                        onClicked: on ? (sizePop.index = index, sizePop.x = Math.max(8, sz.mapToItem(sizePop.parent, 0, 0).x - 100), sizePop.y = sz.mapToItem(sizePop.parent, 0, 0).y + 40, sizePop.open()) : root.prefs.selectSize(root.tool, index)
                        background: Item {
                            Shadow { anchors.fill: parent; radius: Theme.radiusSm; level: sz.on ? 1 : 0 }
                            Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusSm; color: sz.on ? Theme.surface : sz.hovered ? Theme.alpha(Theme.surface, 0.55) : "transparent" }
                        }
                        contentItem: Item {
                            readonly property bool letter: root.tool === "text" || root.tool === "sticky"
                            // The pen's own line at this size, with the size beside it.
                            Row {
                                visible: sz.strokes
                                anchors.centerIn: parent
                                spacing: 3
                                Image {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 34; height: 16
                                    sourceSize: Qt.size(68, 32)
                                    cache: false
                                    source: sz.strokes ? "image://ink/nib/" + root.canvas.penStyle + "/" + root.hex + "/" + sz.modelData + "/" + root.dark + "/" + root.canvas.penDash + "/" + root.canvas.penGap + "/" + (sz.modelData / Math.max.apply(null, root.fav.sizes)) : ""
                                }
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: Number(sz.modelData).toFixed(sz.modelData % 1 ? 1 : 0)
                                    font.pixelSize: 11
                                    font.weight: sz.on ? Font.DemiBold : Font.Normal
                                    color: sz.on ? Theme.text : Theme.muted
                                }
                            }
                            Rectangle { antialiasing: true;
                                visible: !parent.letter && !sz.strokes
                                anchors.centerIn: parent
                                readonly property real d: Math.max(4, Math.min(16, sz.index === 0 ? 5 : sz.index === 1 ? 9 : 14))
                                width: d; height: d; radius: d / 2
                                color: sz.on ? Theme.accent : sz.hovered ? Theme.text : Theme.muted
                            }
                            Label {
                                visible: parent.letter
                                anchors.centerIn: parent
                                text: "A"
                                font.pixelSize: sz.index === 0 ? 11 : sz.index === 1 ? 14 : 18
                                font.weight: Font.DemiBold
                                color: sz.on ? Theme.accent : sz.hovered ? Theme.text : Theme.muted
                            }
                        }
                    }
                }
            }
        }
        // The exact size (not for a pen: its sizes show their numbers).
        Chip {
            visible: !!(!!root.fav && root.fav.sizes.length > 0 && !!root.spec) && root.tool !== "pen"
            corner: Theme.radius
            id: valueChip
            label: root.fav && root.fav.sizes.length ? Number(root.fav.sizes[root.fav.size]).toFixed(root.spec && root.spec.step < 1 ? 1 : 0) : ""
            unit: "px"
            tip: root.spec ? root.spec.label : ""
            onClicked: { sizePop.index = root.fav.size; const p = valueChip.mapToItem(sizePop.parent, 0, 0); sizePop.x = Math.max(8, p.x - 100); sizePop.y = p.y + 42; sizePop.open() }
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
