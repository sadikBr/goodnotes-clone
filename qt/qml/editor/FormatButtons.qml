// Bold, italic, and lists & alignment behind one chip (src/editor/editor.ts
// formatControls / paragraphChip): for the text being typed, or new text.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

ToolGroup {
    id: root
    required property InkCanvas canvas
    // The text being typed, else the format new text gets.
    readonly property var e: canvas.editing.id ? canvas.editing : canvas.textFormat
    readonly property bool bold: !!e.bold
    readonly property bool italic: !!e.italic
    readonly property string align: e.align || "left"
    readonly property string list: canvas.editing.id ? (Js.run("textFormat.currentList", [canvas.editing.text || ""]) || "") : ""
    readonly property var alignIcon: ({ left: "alignLeft", center: "alignCenter", right: "alignRight" })

    SmallTool { grouped: true; label: "B"; bold: true; active: root.bold; tip: "Bold (Ctrl+B)"; onClicked: root.canvas.editFormat({ bold: !root.bold }) }
    SmallTool { grouped: true; label: "I"; serif: true; active: root.italic; tip: "Italic (Ctrl+I)"; onClicked: root.canvas.editFormat({ italic: !root.italic }) }
    // .choice-chip: the alignment (and list) in use, and a caret.
    AbstractButton {
        id: para
        implicitHeight: 30
        implicitWidth: paraRow.implicitWidth + 14
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        ToolTip.visible: hovered && !pop.opened && !tipDismissed
        ToolTip.text: "Lists & alignment"
        property bool tipDismissed: false
        onPressedChanged: if (pressed) tipDismissed = true
        onHoveredChanged: if (!hovered) tipDismissed = false
        onClicked: pop.opened ? pop.close() : pop.open()
        background: Rectangle { antialiasing: true; radius: 10; color: Theme.surface; border.color: para.hovered || pop.opened ? Theme.accent : Theme.border }
        contentItem: Item {
            RowLayout {
                id: paraRow
                anchors.centerIn: parent
                spacing: 5
                Icon { name: root.alignIcon[root.align]; size: 17; color: Theme.text }
                Icon { visible: root.list.length > 0; name: root.list === "number" ? "listNumber" : "listBullet"; size: 17; color: Theme.text }
                Icon { name: "down"; size: 14; color: Theme.muted }
            }
        }
        Popup {
            id: pop
            y: para.height + 8
            x: Math.round((para.width - width) / 2)
            padding: 12
            modal: true
            dim: false
            Overlay.modal: Item {}
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            background: Item {
                Shadow { anchors.fill: parent; radius: 14; level: 3 }
                Rectangle { antialiasing: true; anchors.fill: parent; radius: 14; color: Theme.surface; border.color: Theme.border }
            }
            contentItem: ColumnLayout {
                spacing: 6
                Label { text: "LISTS"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.6; color: Theme.muted }
                ToolGroup {
                    SmallTool { grouped: true; iconName: "listBullet"; active: root.list === "bullet"; tip: "Bulleted list"; onClicked: { pop.close(); root.canvas.editFormat({ list: "bullet" }) } }
                    SmallTool { grouped: true; iconName: "listNumber"; active: root.list === "number"; tip: "Numbered list"; onClicked: { pop.close(); root.canvas.editFormat({ list: "number" }) } }
                }
                Label { text: "ALIGNMENT"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.6; color: Theme.muted; Layout.topMargin: 4 }
                ToolGroup {
                    SmallTool { grouped: true; iconName: "alignLeft"; active: root.align === "left"; tip: "Align left"; onClicked: { pop.close(); root.canvas.editFormat({ align: "left" }) } }
                    SmallTool { grouped: true; iconName: "alignCenter"; active: root.align === "center"; tip: "Align centre"; onClicked: { pop.close(); root.canvas.editFormat({ align: "center" }) } }
                    SmallTool { grouped: true; iconName: "alignRight"; active: root.align === "right"; tip: "Align right"; onClicked: { pop.close(); root.canvas.editFormat({ align: "right" }) } }
                }
            }
        }
    }
}
