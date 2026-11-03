// The bar over a selection (src/editor/editor.ts renderSelectionBar): turn
// handwriting into text, shape kind, line width and fill, text font, size and
// format, colours, duplicate and delete.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf
import "Fonts.js" as Fonts

Item {
    id: root
    required property InkCanvas canvas
    readonly property var s: canvas.selection
    readonly property bool shown: (s.count || 0) > 0 && !!s.box
    readonly property var inkColors: ["#111827", "#2563eb", "#dc2626", "#16a34a", "#9333ea", "#ea580c"]
    readonly property var shapeKinds: [{ id: "line", label: "Line", icon: "sLine" }, { id: "arrow", label: "Arrow", icon: "sArrow" }, { id: "rectangle", label: "Rectangle", icon: "sRectangle" }, { id: "ellipse", label: "Ellipse / circle", icon: "sEllipse" }, { id: "triangle", label: "Triangle", icon: "sTriangle" }, { id: "diamond", label: "Diamond", icon: "sDiamond" }, { id: "star", label: "Star", icon: "sStar" }]
    visible: shown
    implicitWidth: row.implicitWidth + 16
    implicitHeight: row.implicitHeight + 12

    // Above the selection, or under it when there's no room.
    function place() {
        if (!shown) return
        const r = canvas.pageRectToItem(s.page, Qt.rect(s.box.x, s.box.y, s.box.w, s.box.h))
        const pw = parent ? parent.width : 800
        x = Math.max(8, Math.min(pw - width - 8, r.x + r.width / 2 - width / 2))
        y = r.y - height - 34 > 8 ? r.y - height - 34 : Math.min((parent ? parent.height : 600) - height - 8, r.y + r.height + 30)
    }
    onShownChanged: place()
    onWidthChanged: place()
    Connections {
        target: root.canvas
        function onViewChanged() { root.place() }
        function onSelectionChanged() { root.place() }
    }

    PopupMenu { id: menu }
    ColorPopover { id: colorPop }

    Shadow { anchors.fill: parent; radius: 14; level: 3 }
    Rectangle { anchors.fill: parent; radius: 14; color: Theme.surface; border.color: Theme.border }
    MouseArea { anchors.fill: parent }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 6

        // Handwriting → text.
        AbstractButton {
            id: applyFont
            visible: !!root.s.hasInk
            implicitHeight: 30
            implicitWidth: afRow.implicitWidth + 16
            hoverEnabled: true
            ToolTip.visible: hovered
            ToolTip.text: "Turn this handwriting into text in a handwriting font"
            onClicked: menu.show(applyFont, Fonts.presets.map(f => ({ label: f.label, font: f.face, fontSize: 16, action: () => root.canvas.applyFontToInk(f.id) })))
            background: Rectangle { radius: 8; color: parent.hovered ? Theme.surface2 : "transparent" }
            contentItem: RowLayout {
                id: afRow
                spacing: 6
                Icon { name: "text"; size: 16; color: Theme.text; Layout.leftMargin: 8 }
                Label { text: "Apply font"; font.weight: Font.DemiBold; font.pixelSize: 13; color: Theme.text; Layout.rightMargin: 8 }
            }
        }
        Rectangle { visible: !!root.s.hasInk; Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.border }

        // A single shape.
        RowLayout {
            visible: !!root.s.shape
            spacing: 4
            SmallTool {
                id: kindBtn
                iconName: root.s.shape ? (root.shapeKinds.find(k => k.id === root.s.shape.kind) || { icon: "shape" }).icon : "shape"
                tip: "Change shape"
                onClicked: menu.show(kindBtn, root.shapeKinds.filter(k => root.s.shape.closed ? k.id !== "line" && k.id !== "arrow" : k.id === "line" || k.id === "arrow").map(k => ({ label: k.label, icon: k.icon, checked: k.id === root.s.shape.kind, action: () => root.canvas.changeShape(k.id) })))
            }
            UiSlider {
                Layout.preferredWidth: 90
                from: 0.5; to: 20; stepSize: 0.5
                value: root.s.shape ? root.s.shape.width : 3
                onMoved: root.canvas.changeShape("width", value)
                ToolTip.visible: hovered
                ToolTip.text: "Line width"
            }
            SmallTool { visible: !!root.s.shape && root.s.shape.closed; iconName: "fill"; active: !!root.s.shape && root.s.shape.fill; tip: root.s.shape && root.s.shape.fill ? "Remove fill" : "Fill shape"; onClicked: root.canvas.changeShape("fill") }
        }
        Rectangle { visible: !!root.s.shape; Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.border }

        // Text boxes and sticky notes.
        RowLayout {
            visible: (root.s.texts || 0) > 0
            spacing: 2
            AbstractButton {
                id: fontBtn
                implicitHeight: 30
                implicitWidth: fbRow.implicitWidth + 14
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: "Handwriting font for the selected text"
                onClicked: menu.show(fontBtn, Fonts.presets.map(f => ({ label: f.label, font: f.face, fontSize: 16, checked: f.id === root.s.font, action: () => root.canvas.styleSelection({ font: f.id }) })))
                background: Rectangle { radius: 8; color: Theme.surface; border.color: parent.hovered ? Theme.accent : Theme.border }
                contentItem: RowLayout {
                    id: fbRow
                    spacing: 4
                    Label { text: root.s.font ? Fonts.preset(root.s.font).label : "Mixed fonts"; font.family: root.s.font ? Fonts.preset(root.s.font).face : Theme.font; font.pixelSize: 13; color: Theme.text; Layout.leftMargin: 8 }
                    Icon { name: "down"; size: 13; color: Theme.muted; Layout.rightMargin: 6 }
                }
            }
            SmallTool { label: "A−"; tip: "Smaller text"; onClicked: root.canvas.styleSelection({ scale: 1 / 1.15 }) }
            SmallTool { label: "A+"; tip: "Larger text"; onClicked: root.canvas.styleSelection({ scale: 1.15 }) }
            SmallTool { label: "B"; bold: true; active: !!root.s.bold; tip: "Bold"; onClicked: root.canvas.styleSelection({ bold: !root.s.bold }) }
            SmallTool { label: "I"; serif: true; active: !!root.s.italic; tip: "Italic"; onClicked: root.canvas.styleSelection({ italic: !root.s.italic }) }
            SmallTool { iconName: "listBullet"; active: root.s.list === "bullet"; tip: "Bulleted list"; onClicked: root.canvas.styleSelection({ list: "bullet" }) }
            SmallTool { iconName: "listNumber"; active: root.s.list === "number"; tip: "Numbered list"; onClicked: root.canvas.styleSelection({ list: "number" }) }
            SmallTool {
                readonly property var next: ({ left: "center", center: "right", right: "left" })
                iconName: root.s.align === "center" ? "alignCenter" : root.s.align === "right" ? "alignRight" : "alignLeft"
                tip: "Alignment: " + (root.s.align || "left") + " (click to change)"
                onClicked: root.canvas.styleSelection({ align: next[root.s.align || "left"] })
            }
        }
        Rectangle { visible: (root.s.texts || 0) > 0; Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.border }

        // Colours.
        Repeater {
            model: root.inkColors
            delegate: AbstractButton {
                required property string modelData
                implicitWidth: 22; implicitHeight: 22
                hoverEnabled: true
                scale: hovered ? 1.1 : 1
                onClicked: root.canvas.recolorSelection(modelData)
                background: Rectangle { radius: 11; color: parent.modelData; border.color: Qt.rgba(0.5, 0.5, 0.5, 0.4) }
            }
        }
        AbstractButton {
            id: custom
            implicitWidth: 22; implicitHeight: 22
            hoverEnabled: true
            onClicked: colorPop.show(custom, ["#111827", "#374151", "#6b7280", "#9ca3af", "#ffffff", "#7c2d12", "#1e3a8a", "#2563eb", "#0891b2", "#0d9488", "#16a34a", "#65a30d", "#ca8a04", "#ea580c", "#dc2626", "#be123c", "#db2777", "#9333ea"], root.s.color || "#111827", c => root.canvas.recolorSelection(c))
            background: Image {
                sourceSize: Qt.size(44, 44)
                source: "data:image/svg+xml;utf8," + encodeURIComponent("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path d='M12 12L12 0A12 12 0 0 1 22.4 6Z' fill='#ef4444'/><path d='M12 12L22.4 6A12 12 0 0 1 22.4 18Z' fill='#eab308'/><path d='M12 12L22.4 18A12 12 0 0 1 12 24Z' fill='#22c55e'/><path d='M12 12L12 24A12 12 0 0 1 1.6 18Z' fill='#06b6d4'/><path d='M12 12L1.6 18A12 12 0 0 1 1.6 6Z' fill='#3b82f6'/><path d='M12 12L1.6 6A12 12 0 0 1 12 0Z' fill='#a855f7'/><circle cx='12' cy='12' r='5' fill='white'/></svg>")
            }
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.border }
        SmallTool { iconName: "copy"; tip: "Duplicate (Ctrl+D)"; onClicked: root.canvas.duplicateSelection() }
        SmallTool { iconName: "trash"; tip: "Delete"; onClicked: root.canvas.deleteSelection() }
    }
}
