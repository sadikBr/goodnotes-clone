// The drawing tools in one strip, down the side rail or across the top bar
// (the same look either way): Select; the pens; what's placed on the page;
// presenting and moving around; then your colours (one list for every
// tool, none at first) and + to add one.
import QtQuick
import QtQuick.Controls
import Inkleaf

Rectangle {
    id: root
    required property InkCanvas canvas
    required property var prefs
    /// [{ id, label, key, icon }]
    required property var tools
    property PopupMenu menu: null
    property bool vertical: true
    property bool drawerOpen: false
    signal picked(string id)
    /// The active tool's button (the options drawer points at it).
    signal activeButton(Item item)
    /// + was pressed: pick a colour to add, by `anchor`.
    signal addColor(Item anchor)

    readonly property var fav: prefs.get(canvas.tool)
    readonly property bool hasColor: !!fav && fav.colors.length > 0
    readonly property var presets: prefs.railColors()
    readonly property int btn: vertical ? 42 : 38
    readonly property var groups: [["lasso"], ["pen", "highlighter", "eraser"], ["shape", "text", "sticky", "ruler"], ["laser", "hand"]]

    implicitWidth: vertical ? btn + 12 : strip.implicitWidth + 12
    implicitHeight: vertical ? strip.implicitHeight + 12 : btn + 10
    radius: Theme.radiusLg
    antialiasing: true
    color: Theme.surface
    border.color: Theme.border
    Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 2; z: -1 }
    // Presses on the strip stay off the page.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    /// A thin line between groups, across the strip.
    component Sep: Item {
        width: root.vertical ? root.btn : 9
        height: root.vertical ? 9 : root.btn
        Rectangle { anchors.centerIn: parent; width: root.vertical ? 26 : 1; height: root.vertical ? 1 : 24; color: Theme.border }
    }

    Grid {
        id: strip
        anchors.centerIn: parent
        columns: root.vertical ? 1 : 1000
        rowSpacing: 2
        columnSpacing: 2
        verticalItemAlignment: Grid.AlignVCenter
        horizontalItemAlignment: Grid.AlignHCenter
        Repeater {
            model: root.groups
            delegate: Grid {
                id: group
                required property var modelData
                required property int index
                columns: root.vertical ? 1 : 1000
                rowSpacing: 2
                columnSpacing: 2
                verticalItemAlignment: Grid.AlignVCenter
                Sep { visible: group.index > 0 }
                Repeater {
                    model: group.modelData.filter(id => id !== "ruler").map(id => root.tools.find(t => t.id === id)).filter(t => !!t)
                    delegate: ToolBtn {
                        required property var modelData
                        objectName: "tool-" + modelData.id
                        implicitWidth: root.btn; implicitHeight: root.btn
                        corner: 8
                        iconName: modelData.icon
                        active: root.canvas.tool === modelData.id
                        ink: modelData.id === "sticky" || modelData.id === "lasso" || modelData.id === "hand" || modelData.id === "eraser" ? "" : (root.canvas.colors[modelData.id] || "")
                        tip: active ? modelData.label + " — tap again to " + (root.drawerOpen ? "hide" : "show") + " its options" : modelData.label + (modelData.key ? " (" + modelData.key + ")" : "")
                        ToolTip.delay: 600
                        onClicked: root.picked(modelData.id)
                        onActiveChanged: if (active) root.activeButton(this)
                        Component.onCompleted: if (active) root.activeButton(this)
                    }
                }
                // The instruments, with what's placed on the page.
                ToolBtn {
                    id: ruler
                    visible: group.modelData.indexOf("ruler") >= 0
                    implicitWidth: root.btn; implicitHeight: root.btn
                    corner: 8
                    iconName: "ruler"
                    readonly property bool on: !!root.canvas.instrument.visible
                    active: on
                    tip: "Ruler, protractor, set squares (R)"
                    onClicked: {
                        const c = root.canvas
                        const items = [
                            { label: "Ruler", checked: on && c.instrument.kind === "ruler", action: () => c.showInstrument("ruler") },
                            { label: "Protractor", checked: on && c.instrument.kind === "protractor", action: () => c.showInstrument("protractor") },
                            { label: "Set square 45°", checked: on && c.instrument.kind === "square45", action: () => c.showInstrument("square45") },
                            { label: "Set square 30°/60°", checked: on && c.instrument.kind === "square60", action: () => c.showInstrument("square60") }
                        ].concat(on ? [{ label: "Hide", action: () => c.showInstrument("") }] : [])
                        if (root.vertical) root.menu.showBeside(ruler, items)
                        else root.menu.show(ruler, items)
                    }
                }
            }
        }
        // Your colours (the same for every tool that has one), and +.
        Sep { visible: root.hasColor }
        Repeater {
            model: root.hasColor ? root.presets : []
            delegate: AbstractButton {
                id: sw
                required property string modelData
                readonly property bool on: String(root.canvas.colors[root.canvas.tool] || "").toLowerCase() === modelData.toLowerCase()
                width: root.vertical ? root.btn : 32
                height: root.vertical ? 32 : root.btn
                hoverEnabled: true
                onClicked: root.prefs.useColor(root.canvas.tool, modelData)
                onPressAndHold: askRemove()
                function askRemove() {
                    const items = [{ icon: "trash", label: "Remove this colour", danger: true, action: () => root.prefs.removeRailColor(sw.modelData) }]
                    if (root.vertical) root.menu.showBeside(sw, items)
                    else root.menu.show(sw, items)
                }
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: sw.askRemove() }
                ToolTip.visible: hovered
                ToolTip.delay: 700
                ToolTip.text: "Tap to use · hold or right-click to remove"
                contentItem: Item {
                    Rectangle { antialiasing: true; anchors.centerIn: parent; width: 28; height: 28; radius: Theme.swatchRadius(28); color: "transparent"; border.width: 2; border.color: sw.modelData; visible: sw.on }
                    Rectangle { antialiasing: true; anchors.centerIn: parent; width: sw.on ? 20 : 22; height: width; radius: Theme.swatchRadius(width); color: sw.modelData; border.width: 1; border.color: Qt.rgba(0.5, 0.5, 0.5, 0.4); scale: sw.hovered ? 1.1 : 1; Behavior on scale { NumberAnimation { duration: 120 } } }
                }
            }
        }
        AbstractButton {
            id: add
            visible: root.hasColor && root.presets.length < root.prefs.maxRailColors
            width: root.vertical ? root.btn : 34
            height: root.vertical ? 34 : root.btn
            hoverEnabled: true
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: "Add a colour (for every tool)"
            onClicked: root.addColor(add)
            contentItem: Item {
                Rectangle {
                    antialiasing: true
                    anchors.centerIn: parent
                    width: 24; height: 24; radius: Theme.swatchRadius(24)
                    color: add.hovered ? Theme.surface2 : "transparent"
                    border.width: 1.5
                    border.color: add.hovered ? Theme.accent : Theme.muted
                    Icon { anchors.centerIn: parent; name: "plus"; size: 14; color: add.hovered ? Theme.accent : Theme.muted }
                }
            }
        }
    }
}
