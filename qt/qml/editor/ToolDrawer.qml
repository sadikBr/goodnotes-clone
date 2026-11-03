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
    readonly property string tool: canvas.tool
    readonly property var fav: prefs.get(tool)
    readonly property var spec: prefs.specs[tool]
    readonly property bool hasOptions: content.implicitWidth > 0
    signal changed()

    implicitWidth: content.implicitWidth + 20 + 34
    implicitHeight: Math.max(48, content.implicitHeight + 12)

    readonly property var penStyles: [{ id: "fountain", label: "Fountain pen" }, { id: "ballpoint", label: "Ballpoint" }, { id: "pencil", label: "Pencil" }, { id: "calligraphy", label: "Calligraphy" }, { id: "brush", label: "Brush pen" }]
    readonly property var smoothings: [{ id: "off", label: "Off", v: 0 }, { id: "light", label: "Light", v: 0.2 }, { id: "normal", label: "Normal", v: 0.38 }, { id: "strong", label: "Strong", v: 0.62 }]
    readonly property var shapeKinds: [{ id: "auto", label: "Auto shape", icon: "sAuto" }, { id: "line", label: "Line", icon: "sLine" }, { id: "arrow", label: "Arrow", icon: "sArrow" }, { id: "rectangle", label: "Rectangle", icon: "sRectangle" }, { id: "ellipse", label: "Ellipse / circle", icon: "sEllipse" }, { id: "triangle", label: "Triangle", icon: "sTriangle" }, { id: "diamond", label: "Diamond", icon: "sDiamond" }, { id: "star", label: "Star", icon: "sStar" }]
    readonly property string hex: (canvas.colors.pen || "#111827").replace("#", "")
    readonly property string dark: Theme.dark ? "1" : "0"

    PopupMenu { id: menu }
    ColorPopover { id: colorPop }
    Popup {
        id: sizePop
        parent: Overlay.overlay
        padding: 14
        width: 260
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property int index: 0
        background: Item {
            Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
            Rectangle { anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
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
    Shadow { anchors.fill: panel; radius: 18; level: 3 }
    Rectangle {
        id: caret
        width: 12; height: 12; rotation: 45
        y: -6
        x: {
            if (!root.anchorItem) return root.width / 2 - 6
            const p = root.anchorItem.mapToItem(root, root.anchorItem.width / 2, 0)
            return Math.max(16, Math.min(root.width - 28, p.x - 6))
        }
        color: Theme.surface
        border.color: Theme.border
        Behavior on x { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
    }
    Rectangle {
        id: panel
        anchors.fill: parent
        radius: 18
        color: Theme.surface
        border.color: Theme.border
        // Hide the pointer's inner half.
        Rectangle { x: caret.x - 2; y: 0.5; width: 16; height: 8; color: Theme.surface }
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
                id: nibChip
                label: (root.penStyles.find(p => p.id === root.canvas.penStyle) || root.penStyles[0]).label
                tip: "Pen type"
                art: Component { Image { source: "image://ink/nib/" + root.canvas.penStyle + "/" + root.hex + "/" + (root.canvas.widths.pen || 3) + "/" + root.dark; sourceSize: Qt.size(72, 36); width: 36; height: 18; cache: false } }
                open: menu.opened && menu.items.length === 5
                onClicked: menu.show(nibChip, root.penStyles.map(p => ({ label: p.label, image: "image://ink/nib/" + p.id + "/" + root.hex + "/" + (root.canvas.widths.pen || 3) + "/" + root.dark, checked: p.id === root.canvas.penStyle, action: () => root.prefs.store.penStyle = p.id })))
            }
            Chip {
                id: smoothChip
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
            spacing: 6
            Chip {
                id: fontChip
                readonly property string current: root.canvas.editing.font || root.canvas.textFont
                label: Fonts.preset(current).label
                tip: "Handwriting font"
                onClicked: menu.show(fontChip, Fonts.presets.map(f => ({ label: f.label, font: f.face, fontSize: 16, checked: f.id === fontChip.current, action: () => { root.canvas.editFormat({ font: f.id }); root.prefs.store.textFont = f.id } })))
            }
            FormatButtons { canvas: root.canvas }
        }
        Segmented {
            visible: root.tool === "eraser"
            options: [{ value: "precise", label: "Precise" }, { value: "stroke", label: "Whole stroke" }]
            value: root.canvas.eraserMode
            onPicked: v => root.prefs.store.eraserMode = v
        }
        Segmented {
            visible: root.tool === "laser"
            options: [{ value: "trail", label: "Trail" }, { value: "dot", label: "Dot" }]
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

        // --- favourite colours ---
        RowLayout {
            visible: !!root.fav && root.fav.colors.length > 0
            spacing: 6
            Repeater {
                model: root.fav ? root.fav.colors : []
                delegate: AbstractButton {
                    id: sw
                    required property string modelData
                    required property int index
                    readonly property bool on: root.fav && root.fav.color === index
                    implicitWidth: 26; implicitHeight: 26
                    hoverEnabled: true
                    scale: hovered ? 1.1 : 1
                    Behavior on scale { NumberAnimation { duration: 120 } }
                    ToolTip.visible: hovered && on
                    ToolTip.text: "Tap again to change this colour"
                    onClicked: on ? colorPop.show(sw, root.prefs.paletteFor(root.tool), modelData, c => root.prefs.setColor(root.tool, index, c)) : root.prefs.selectColor(root.tool, index)
                    background: Item {
                        Rectangle { anchors.fill: parent; anchors.margins: -4; radius: width / 2; color: "transparent"; border.width: 2; border.color: Theme.mix(sw.modelData, Theme.text, 0.2); visible: sw.on }
                        Rectangle { anchors.fill: parent; radius: width / 2; color: sw.modelData; border.width: 1; border.color: Qt.rgba(0.5, 0.5, 0.5, 0.4) }
                    }
                }
            }
            AbstractButton {
                id: rainbow
                implicitWidth: 26; implicitHeight: 26
                hoverEnabled: true
                rotation: hovered ? 30 : 0
                Behavior on rotation { NumberAnimation { duration: 150 } }
                ToolTip.visible: hovered
                ToolTip.text: "More colours"
                onClicked: colorPop.show(rainbow, root.prefs.paletteFor(root.tool), root.prefs.color(root.tool), c => root.prefs.setColor(root.tool, root.fav.color, c))
                background: Image {
                    sourceSize: Qt.size(52, 52)
                    source: "data:image/svg+xml;utf8," + encodeURIComponent("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path d='M12 12L12 0A12 12 0 0 1 22.4 6Z' fill='#ef4444'/><path d='M12 12L22.4 6A12 12 0 0 1 22.4 18Z' fill='#eab308'/><path d='M12 12L22.4 18A12 12 0 0 1 12 24Z' fill='#22c55e'/><path d='M12 12L12 24A12 12 0 0 1 1.6 18Z' fill='#06b6d4'/><path d='M12 12L1.6 18A12 12 0 0 1 1.6 6Z' fill='#3b82f6'/><path d='M12 12L1.6 6A12 12 0 0 1 12 0Z' fill='#a855f7'/><circle cx='12' cy='12' r='5' fill='white'/></svg>")
                }
            }
        }

        // --- favourite sizes ---
        RowLayout {
            visible: !!root.fav && root.fav.sizes.length > 0 && !!root.spec
            spacing: 6
            Rectangle {
                implicitWidth: sizesRow.implicitWidth + 6
                implicitHeight: 36
                radius: 12
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
                            width: 32; height: 30
                            hoverEnabled: true
                            onClicked: on ? (sizePop.index = index, sizePop.x = Math.max(8, sz.mapToItem(sizePop.parent, 0, 0).x - 110), sizePop.y = sz.mapToItem(sizePop.parent, 0, 0).y + 40, sizePop.open()) : root.prefs.selectSize(root.tool, index)
                            background: Item {
                                Shadow { anchors.fill: parent; radius: 9; level: sz.on ? 1 : 0 }
                                Rectangle { anchors.fill: parent; radius: 9; color: sz.on ? Theme.surface : "transparent" }
                            }
                            contentItem: Item {
                                readonly property bool letter: root.tool === "text" || root.tool === "sticky"
                                Rectangle {
                                    visible: !parent.letter
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
            Chip {
                id: valueChip
                label: root.fav && root.fav.sizes.length ? Number(root.fav.sizes[root.fav.size]).toFixed(root.spec && root.spec.step < 1 ? 1 : 0) : ""
                unit: "px"
                tip: root.spec ? root.spec.label : ""
                onClicked: { sizePop.index = root.fav.size; const p = valueChip.mapToItem(sizePop.parent, 0, 0); sizePop.x = Math.max(8, p.x - 100); sizePop.y = p.y + 42; sizePop.open() }
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
        ToolTip.visible: hovered
        ToolTip.text: root.prefs.store.drawerPinned ? "Pinned open — unpin to tuck options away while you write" : "Keep options open"
        onClicked: root.prefs.store.drawerPinned = !root.prefs.store.drawerPinned
        background: Rectangle { radius: 8; color: root.prefs.store.drawerPinned ? Theme.accentSoft : parent.hovered ? Theme.surface2 : "transparent" }
        contentItem: Item { Icon { anchors.centerIn: parent; name: "pin"; size: 15; color: root.prefs.store.drawerPinned ? Theme.accent : Theme.muted } }
    }
}
