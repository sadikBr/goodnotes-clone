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
    InkToText { id: inkToText; canvas: root.canvas }
    /// Ask the AI about the selection: "explain", "solve", "translate", "summarise", "ask".
    signal askAi(string what)
    /// Work out the maths in the selection.
    signal mathsRequested()
    /// Edit the selected graph.
    signal graphEditRequested(var graph)
    /// Save the selection to my pieces.
    signal savePieceRequested()
    ColorPopover { id: colorPop }

    Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
    Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
    MouseArea { anchors.fill: parent }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 6

        // The AI.
        AbstractButton {
            id: aiBtn
            implicitHeight: 30
            implicitWidth: aiRow.implicitWidth + 16
            hoverEnabled: true
            ToolTip.visible: !!(hovered && !menu.opened) && !tipDismissed
            property bool tipDismissed: false
            onPressedChanged: if (pressed) tipDismissed = true
            onHoveredChanged: if (!hovered) tipDismissed = false
            ToolTip.text: "Ask AI about the selection"
            onClicked: menu.show(aiBtn, [
                { label: "Explain", action: () => root.askAi("explain") },
                { label: "Solve step by step", action: () => root.askAi("solve") },
                { label: "Summarise", action: () => root.askAi("summarise") },
                { label: "Translate…", action: () => root.askAi("translate") }
            ].concat(root.s.hasInk ? [{ label: "Turn handwriting into text", action: () => inkToText.start() }] : [], [
                { label: "Ask a question…", action: () => root.askAi("ask") }
            ]))
            background: Rectangle { antialiasing: true; radius: Theme.radius; color: parent.hovered ? Theme.alpha(Theme.accent, 0.12) : "transparent" }
            contentItem: RowLayout {
                id: aiRow
                spacing: 6
                Icon { name: "sparkle"; size: 16; color: Theme.accent; Layout.leftMargin: 8 }
                Label { text: "Ask AI"; font.weight: Font.DemiBold; font.pixelSize: 13; color: Theme.text; Layout.rightMargin: 8 }
            }
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.border }

        // Maths: work out what's written; a graph: change it.
        SmallTool {
            visible: !!(root.s.hasInk || root.s.hasText) && !root.s.graph
            iconName: "math"
            tip: "Maths: work it out, write the answer, typeset, graph"
            onClicked: root.mathsRequested()
        }
        UiButton {
            visible: !!root.s.graph
            kind: "ghost"
            iconName: "graph"
            text: "Edit graph"
            implicitHeight: 30
            onClicked: root.graphEditRequested(root.s.graph)
        }
        // Tables: a grid drawn by hand becomes one; a table's rows, columns and cells.
        UiButton {
            visible: !!root.s.drawnTable
            kind: "ghost"
            iconName: "table"
            text: "Make table"
            implicitHeight: 30
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: "Turn the grid you drew into a table you can type in"
            onClicked: if (!root.canvas.tableFromSelection()) root.canvas.message("That doesn't look like a grid", true)
        }
        UiButton {
            id: tableBtn
            visible: !!root.s.table
            kind: "ghost"
            iconName: "table"
            text: "Table"
            implicitHeight: 30
            onClicked: {
                const t = root.s.table
                menu.show(tableBtn, [
                    { icon: "text", label: "Type in the cells", action: () => root.canvas.cellEditRequested({ id: t.id, row: 0, col: 0 }) },
                    { icon: "plus", label: "Add a row", action: () => root.canvas.changeTable(t.id, "addRow", -1) },
                    { icon: "plus", label: "Add a column", action: () => root.canvas.changeTable(t.id, "addCol", -1) }
                ].concat(t.rows > 1 ? [{ icon: "trash", label: "Remove the last row", action: () => root.canvas.changeTable(t.id, "removeRow", -1) }] : [],
                         t.cols > 1 ? [{ icon: "trash", label: "Remove the last column", action: () => root.canvas.changeTable(t.id, "removeCol", -1) }] : [],
                         [{ label: "Header row", checked: !!t.header, action: () => root.canvas.changeTable(t.id, "header", -1) }]))
            }
        }

        // Handwriting → text.
        AbstractButton {
            id: applyFont
            visible: !!root.s.hasInk
            implicitHeight: 30
            implicitWidth: afRow.implicitWidth + 16
            hoverEnabled: true
            ToolTip.visible: !!(hovered) && !tipDismissed
            // Tooltips go away once clicked (until the pointer leaves), as in the web app.
            property bool tipDismissed: false
            onPressedChanged: if (pressed) tipDismissed = true
            onHoveredChanged: if (!hovered) tipDismissed = false
            ToolTip.text: "Turn this handwriting into text in a handwriting font"
            onClicked: inkToText.start()
            background: Rectangle { antialiasing: true; radius: Theme.radius; color: parent.hovered ? Theme.surface2 : "transparent" }
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
                ToolTip.visible: !!(hovered) && !tipDismissed
                // Tooltips go away once clicked (until the pointer leaves), as in the web app.
                property bool tipDismissed: false
                onPressedChanged: if (pressed) tipDismissed = true
                onHoveredChanged: if (!hovered) tipDismissed = false
                ToolTip.text: "Line width"
            }
            SmallTool { visible: !!(!!root.s.shape && root.s.shape.closed); iconName: "fill"; active: !!(!!root.s.shape && root.s.shape.fill); tip: root.s.shape && root.s.shape.fill ? "Remove fill" : "Fill shape"; onClicked: root.canvas.changeShape("fill") }
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
                ToolTip.visible: !!(hovered) && !tipDismissed
                // Tooltips go away once clicked (until the pointer leaves), as in the web app.
                property bool tipDismissed: false
                onPressedChanged: if (pressed) tipDismissed = true
                onHoveredChanged: if (!hovered) tipDismissed = false
                ToolTip.text: "Handwriting font for the selected text"
                onClicked: menu.show(fontBtn, Fonts.presets.map(f => ({ label: f.label, font: f.face, fontSize: 16, checked: f.id === root.s.font, action: () => root.canvas.styleSelection({ font: f.id }) })))
                background: Rectangle { antialiasing: true; radius: Theme.radius; color: Theme.surface; border.color: parent.hovered ? Theme.accent : Theme.border }
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

        // Colour: one button showing the selection's colour (a rainbow when
        // mixed), the palette in a popover. Not for a graph alone (each
        // curve has its own).
        AbstractButton {
            id: colorBtn
            visible: !root.s.graph
            implicitWidth: colorRow.implicitWidth + 12
            implicitHeight: 30
            hoverEnabled: true
            ToolTip.visible: hovered && !colorPop.opened
            ToolTip.delay: 500
            ToolTip.text: "Colour"
            onClicked: colorPop.show(colorBtn, root.inkColors.concat(["#374151", "#6b7280", "#ffffff", "#7c2d12", "#1e3a8a", "#0891b2", "#0d9488", "#65a30d", "#ca8a04", "#be123c", "#db2777"]), root.s.color || "#111827", c => root.canvas.recolorSelection(c))
            background: Rectangle { antialiasing: true; radius: Theme.radius; color: colorBtn.hovered || colorPop.opened ? Theme.surface2 : "transparent" }
            contentItem: RowLayout {
                id: colorRow
                spacing: 4
                Item {
                    Layout.leftMargin: 6
                    Layout.preferredWidth: 20; Layout.preferredHeight: 20
                    Rectangle { visible: !!root.s.color; anchors.fill: parent; antialiasing: true; radius: Theme.swatchRadius(20); color: root.s.color || "transparent"; border.color: Qt.rgba(0.5, 0.5, 0.5, 0.45) }
                    Image {
                        visible: !root.s.color
                        anchors.fill: parent
                        sourceSize: Qt.size(40, 40)
                        source: Theme.rainbow(false)
                    }
                }
                Icon { name: "down"; size: 12; color: Theme.muted; Layout.rightMargin: 4 }
            }
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.border }
        SmallTool {
            id: moreBtn
            iconName: "more"
            tip: "More"
            onClicked: menu.show(moreBtn, [
                { icon: "copy", label: "Duplicate", key: "Ctrl+D", action: () => root.canvas.duplicateSelection() },
                { icon: "page", label: "Copy", key: "Ctrl+C", action: () => root.canvas.copySelection() },
                { icon: "stack", label: "Save to my pieces", action: () => root.savePieceRequested() }
            ], 240)
        }
        SmallTool { iconName: "trash"; tip: "Delete"; onClicked: root.canvas.deleteSelection() }
    }
}
