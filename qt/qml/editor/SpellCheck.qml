// Spelling for handwriting: the AI reads the words written on a page (or in
// the selection), in whatever language they're in, and the misspelled ones get
// a red wavy line. Tap one: its suggestions (one puts the word there as text
// in a handwriting font, in place of the ink), or Ignore.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import Inkleaf
import "Fonts.js" as Fonts

Item {
    id: root
    required property InkCanvas canvas
    signal notify(string message, bool error)
    /// The words to look at: [{ page, ids, read, suggestions, box }].
    property var marks: []
    property bool busy: false
    /// Changes when the page moves under the marks (they follow it).
    property int viewRev: 0
    Connections {
        target: root.canvas
        function onViewChanged() { root.viewRev++ }
        function onHistoryChanged() { refreshSoon.restart() }
        function onPagesChanged() { root.marks = [] }
    }
    Timer { id: refreshSoon; interval: 60; onTriggered: root.refresh() }
    /// Marks follow their words; a word rubbed out or rewritten loses its mark.
    function refresh() {
        const next = []
        for (const m of marks) {
            const b = canvas.strokesBox(m.page, m.ids)
            if (b && b.w !== undefined) next.push(Object.assign({}, m, { box: b }))
        }
        if (next.length !== marks.length || next.some((m, i) => m.box.x !== marks[i].box.x || m.box.y !== marks[i].box.y)) marks = next
    }

    readonly property string prompt: "You check the spelling of handwriting. In the picture, each handwritten word has a small red number just before it (above, to its left). " +
        "Read each numbered word in the language it is written in (Arabic, French, English or any other). " +
        "Find the words that are misspelled. Don't count names, abbreviations, numbers, maths or symbols, and don't correct grammar or style. " +
        "Reply with JSON only, no other text: {\"mistakes\":[{\"n\":3,\"read\":\"as written\",\"suggestions\":[\"best\",\"other\"]}]}, " +
        "with up to 3 suggestions each, best first, in the same language and script. If there are none, reply {\"mistakes\":[]}."

    /// Checks page `page` (only the selected writing if `selectionOnly`).
    function check(page, selectionOnly) {
        if (busy) return
        if (!Ai.ready) return notify("Spelling is checked by the AI: set it up in Settings → AI first", true)
        const words = canvas.inkWords(page, selectionOnly)
        if (!words.length) return notify(selectionOnly ? "There's no handwriting in the selection" : "There's no handwriting on this page to check", false)
        const image = canvas.numberedWordsImage(page, words)
        busy = true
        // This page's old marks go (it's checked again).
        marks = marks.filter(m => m.page !== page)
        Ai.ask(prompt, [{ role: "user", text: "Check the spelling of the " + words.length + " numbered words.", images: [image] }], true, (answer, err) => {
            busy = false
            if (err) return notify("Couldn't check the spelling: " + err, true)
            let found = []
            try {
                const t = String(answer)
                const json = JSON.parse(t.slice(t.indexOf("{"), t.lastIndexOf("}") + 1))
                found = (json.mistakes || []).filter(m => m && typeof m.n === "number")
            } catch (e) {
                return notify("The AI's answer couldn't be read; try again", true)
            }
            const add = []
            for (const f of found) {
                const w = words.find(w => w.n === f.n)
                if (!w) continue
                const sugg = (f.suggestions || []).map(s => String(s).trim()).filter(s => s && s !== f.read).slice(0, 3)
                if (!sugg.length) continue
                add.push({ page: page, ids: w.ids, read: String(f.read || ""), suggestions: sugg, box: { x: w.x, y: w.y, w: w.w, h: w.h } })
            }
            marks = marks.concat(add)
            notify(add.length === 0 ? "No spelling mistakes found" : add.length === 1 ? "1 word to look at: tap its red line" : add.length + " words to look at: tap their red lines", false)
        })
    }
    function fix(m, word) {
        canvas.selectElements(m.page, m.ids)
        canvas.applyFontToInk(word, "school", false)
        marks = marks.filter(x => x !== m)
    }
    function ignore(m) { marks = marks.filter(x => x !== m) }

    // "Checking…" while the AI reads.
    Rectangle {
        visible: root.busy
        z: 3
        anchors.horizontalCenter: parent.horizontalCenter
        y: 14
        height: 34
        width: busyRow.implicitWidth + 26
        radius: Theme.radiusLg
        antialiasing: true
        color: Theme.surface
        border.color: Theme.border
        Shadow { anchors.fill: parent; radius: parent.radius; level: 3; z: -1 }
        Row {
            id: busyRow
            anchors.centerIn: parent
            spacing: 8
            BusyIndicator { width: 18; height: 18; running: root.busy; anchors.verticalCenter: parent.verticalCenter }
            Label { text: "Checking the spelling…"; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
        }
    }

    // The red wavy lines.
    Repeater {
        model: root.marks
        delegate: Item {
            id: mk
            required property var modelData
            readonly property rect r: { void root.viewRev; return root.canvas.pageRectToItem(modelData.page, Qt.rect(modelData.box.x, modelData.box.y, modelData.box.w, modelData.box.h)) }
            x: r.x; y: r.y + r.height + 1
            width: Math.max(12, r.width); height: 9
            Shape {
                anchors.fill: parent
                antialiasing: true
                ShapePath {
                    strokeColor: area.containsMouse || pop.opened ? "#b91c1c" : "#ef4444"
                    strokeWidth: 1.8
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathSvg {
                        path: {
                            let d = "M 0 4.5"
                            const step = 3.2
                            for (let x = step, i = 0; x <= mk.width + 0.01; x += step, ++i) d += " L " + x.toFixed(1) + " " + (i % 2 ? 4.5 : 1.5)
                            return d
                        }
                    }
                }
            }
            MouseArea {
                id: area
                anchors.fill: parent
                anchors.margins: -5
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: pop.open()
            }
            // Its suggestions.
            Popup {
                id: pop
                // Under the word, or over it when there's no room below.
                readonly property bool above: mk.y + mk.height + height + 16 > root.height
                y: above ? -(height + mk.r.height + 10) : mk.height + 6
                x: Math.max(-mk.x + 8, Math.min(0, mk.width - width))
                margins: 8
                padding: 10
                modal: true
                dim: false
                Overlay.modal: Item {}
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                background: Item {
                    Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
                    Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
                }
                contentItem: ColumnLayout {
                    spacing: 8
                    RowLayout {
                        spacing: 6
                        Icon { name: "spell"; size: 16; color: "#ef4444" }
                        Label { text: mk.modelData.read ? "“" + mk.modelData.read + "” may be misspelled" : "This may be misspelled"; font.pixelSize: 12; color: Theme.muted }
                    }
                    Flow {
                        Layout.preferredWidth: Math.max(220, implicitWidth)
                        spacing: 6
                        Repeater {
                            model: mk.modelData.suggestions
                            delegate: AbstractButton {
                                id: sg
                                required property string modelData
                                implicitHeight: 32
                                implicitWidth: sgLabel.implicitWidth + 22
                                hoverEnabled: true
                                ToolTip.visible: hovered
                                ToolTip.delay: 600
                                ToolTip.text: "Put “" + modelData + "” here as text, in place of the ink"
                                onClicked: { pop.close(); root.fix(mk.modelData, modelData) }
                                background: Rectangle { antialiasing: true; radius: Theme.radius; color: sg.hovered ? Theme.accent : Theme.accentSoft }
                                contentItem: Label {
                                    id: sgLabel
                                    text: sg.modelData
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 15
                                    font.family: Fonts.preset("school").face
                                    font.weight: Font.DemiBold
                                    color: sg.hovered ? Theme.accentContrast : Theme.accent
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        UiButton { kind: "ghost"; text: "Ignore"; implicitHeight: 30; onClicked: { pop.close(); root.ignore(mk.modelData) } }
                        Item { Layout.fillWidth: true }
                        UiButton { kind: "ghost"; iconName: "lasso"; text: "Select it"; implicitHeight: 30; onClicked: { pop.close(); root.canvas.selectElements(mk.modelData.page, mk.modelData.ids) } }
                    }
                }
            }
        }
    }
}
