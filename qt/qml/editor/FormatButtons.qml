// Bold, italic, lists and alignment for text (the text being typed, or new text).
import QtQuick
import QtQuick.Layouts
import Inkleaf

RowLayout {
    id: root
    required property InkCanvas canvas
    // The text being typed, else the format new text gets.
    readonly property var e: canvas.editing.id ? canvas.editing : canvas.textFormat
    readonly property bool bold: !!e.bold
    readonly property bool italic: !!e.italic
    readonly property string align: e.align || "left"
    readonly property var nextAlign: ({ left: "center", center: "right", right: "left" })
    readonly property var alignIcon: ({ left: "alignLeft", center: "alignCenter", right: "alignRight" })
    spacing: 2
    SmallTool { label: "B"; bold: true; active: root.bold; tip: "Bold (Ctrl+B)"; onClicked: root.canvas.editFormat({ bold: !root.bold }) }
    SmallTool { label: "I"; serif: true; active: root.italic; tip: "Italic (Ctrl+I)"; onClicked: root.canvas.editFormat({ italic: !root.italic }) }
    SmallTool { iconName: "listBullet"; tip: "Bulleted list"; onClicked: root.canvas.editFormat({ list: "bullet" }) }
    SmallTool { iconName: "listNumber"; tip: "Numbered list"; onClicked: root.canvas.editFormat({ list: "number" }) }
    SmallTool { iconName: root.alignIcon[root.align]; tip: "Alignment: " + root.align + " (click to change)"; onClicked: root.canvas.editFormat({ align: root.nextAlign[root.align] }) }
}
