// Typing into a text box or sticky note, over its place on the page (the
// web editor's textarea): the element's font, size, colour and alignment.
import QtQuick
import QtQuick.Controls
import Inkleaf
import "Fonts.js" as Fonts

Item {
    id: root
    required property InkCanvas canvas
    readonly property var e: canvas.editing
    readonly property bool active: !!e.id
    readonly property bool sticky: e.kind === "sticky"
    visible: active
    property rect box: Qt.rect(0, 0, 0, 0)

    function place() {
        if (!active) return
        box = canvas.pageRectToItem(e.page, Qt.rect(e.x, e.y, e.w, sticky ? e.h : 10))
    }
    onActiveChanged: {
        place()
        if (active) {
            area.text = e.text || ""
            area.cursorPosition = area.text.length
            area.forceActiveFocus()
        }
    }
    Connections {
        target: root.canvas
        function onViewChanged() { root.place() }
        function onEditingChanged() {
            root.place()
            // Lists toggled from the bar change the text.
            if (root.active && root.e.text !== undefined && root.e.text !== area.text) {
                const caret = area.cursorPosition
                area.text = root.e.text
                area.cursorPosition = Math.min(caret, area.text.length)
            }
        }
    }

    Item {
        x: root.box.x
        y: root.box.y
        width: root.box.width
        height: root.sticky ? root.box.height : Math.max((root.e.fontSize || 20) * 1.6 * root.canvas.zoom, area.contentHeight + 4)
        rotation: (root.e.rotation || 0) * 180 / Math.PI
        Shadow { visible: root.sticky; anchors.fill: parent; radius: 2; level: 2 }
        Rectangle {
            anchors.fill: parent
            color: root.sticky ? (root.e.color || "#fef08a") : Qt.rgba(1, 1, 1, 0.6)
        }
        DashedBox { visible: !root.sticky; anchors.fill: parent; radius: 0; color: Theme.accent; lineWidth: 1.5 }
        TextArea {
            id: area
            anchors.fill: parent
            padding: root.sticky ? 12 * root.canvas.zoom : 0
            background: null
            wrapMode: TextEdit.Wrap
            color: root.sticky ? "#1f2937" : (root.e.color || "#111827")
            font.family: Fonts.preset(root.e.font).face
            font.pixelSize: Math.max(1, (root.sticky ? (root.e.fontSize || 18) : (root.e.fontSize || 20)) * root.canvas.zoom)
            font.bold: !!root.e.bold
            font.italic: !!root.e.italic
            horizontalAlignment: root.e.align === "center" ? TextEdit.AlignHCenter : root.e.align === "right" ? TextEdit.AlignRight : TextEdit.AlignLeft
            selectionColor: Theme.alpha(Theme.accent, 0.3)
            selectedTextColor: color
            onTextChanged: if (root.active) root.canvas.updateEdit(text)
            Keys.onEscapePressed: root.canvas.finishEdit()
            Keys.onPressed: ev => {
                if ((ev.key === Qt.Key_Return || ev.key === Qt.Key_Enter) && !(ev.modifiers & Qt.ShiftModifier) && area.selectionStart === area.selectionEnd) {
                    const next = root.canvas.continueList(area.text, area.cursorPosition)
                    if (next && next.value !== undefined) {
                        area.text = next.value
                        area.cursorPosition = next.caret
                        ev.accepted = true
                    }
                } else if ((ev.modifiers & Qt.ControlModifier) && ev.key === Qt.Key_B) {
                    root.canvas.editFormat({ bold: !root.e.bold })
                    ev.accepted = true
                } else if ((ev.modifiers & Qt.ControlModifier) && ev.key === Qt.Key_I) {
                    root.canvas.editFormat({ italic: !root.e.italic })
                    ev.accepted = true
                }
            }
        }
    }
}
