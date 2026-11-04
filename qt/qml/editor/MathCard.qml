// Maths on the selection: what's written (typed text read at once,
// handwriting read by the AI, or by the computer's handwriting recognition
// where there's no AI), worked out as you correct it, then: write the
// answer beside it, typeset it, show the steps, or graph it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    required property InkCanvas canvas
    signal stepsRequested(string expression)
    signal graphRequested(string functions)
    signal notify(string message, bool error)

    property string status: ""
    property bool reading: false
    property bool fromInk: false
    readonly property var result: MathKit.calculate(field.text)
    readonly property bool hasAnswer: !!(result.ok && result.answer && result.kind !== "function")
    /// Can be plotted: a function, or an expression in x.
    readonly property bool graphable: !!(result.ok && (result.kind === "function" || (result.kind === "expanded" && result.var === "x")))
    property int ticket: 0

    readonly property string mathPrompt: "You read handwritten maths. Write what's in the picture as ONE line of plain text a calculator can read: digits, + - * / ^ ( ) and = as written, letters for unknowns, sqrt( ) for square roots, pi, a decimal comma or point as written. Keep a trailing = if it's written. Reply with that line only, nothing else."

    function start() {
        ticket++
        const t = ticket
        field.text = ""
        reading = false
        const typed = canvas.selectionText().trim()
        fromInk = !!canvas.selection.hasInk
        open()
        if (!fromInk && typed) {
            field.text = typed
            status = ""
            return
        }
        if (Ai.ready) {
            reading = true
            status = "Reading your maths…"
            Ai.ask(mathPrompt, [{ role: "user", text: "Transcribe the maths.", images: [canvas.selectionImage(1200)] }], true, (text, err) => {
                if (t !== root.ticket) return
                root.reading = false
                if (err) { root.status = "Couldn't read it (" + err + "). Type it instead:"; return }
                field.text = text.replace(/^`+|`+$/g, "").split("\n")[0].trim()
                root.status = "Fix it if it was misread:"
                field.forceActiveFocus()
            })
            return
        }
        // No AI: the computer's own handwriting recognition, where it has one.
        const strokes = canvas.selectedInkStrokes()
        reading = true
        status = "Reading your maths…"
        InkleafCore.callAsync("recognizeHandwriting", { strokes: strokes }, (r, err) => {
            if (t !== root.ticket) return
            root.reading = false
            if (!err && r.available && r.candidates && r.candidates.length) {
                field.text = r.candidates[0]
                root.status = "Fix it if it was misread:"
            } else {
                root.status = "Type what you wrote (set up an AI in Settings → AI to read handwriting):"
            }
            field.forceActiveFocus()
        })
    }
    /// The answer as written after the expression: "10" after "2 + 8 =", "= 10" otherwise.
    function answerText() {
        const r = result
        if (r.kind === "value") return field.text.trim().endsWith("=") ? r.answer.replace(/ \(≈.*\)$/, "") : "= " + r.answer.replace(/ \(≈.*\)$/, "")
        if (r.kind === "expanded") return "= " + r.answer
        return r.answer
    }
    /// The text font in use, when it's a handwriting one (else Natural).
    function handFont() {
        const f = canvas.textFont
        return !f || f === "sans" || f === "book" ? "mynerve" : f
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 520)
    modal: true
    focus: true
    padding: 24
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(20 / 255, 18 / 255, 14 / 255, 0.35) }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 180; easing.type: Easing.OutCubic }
    }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            spacing: 10
            Layout.bottomMargin: 14
            Rectangle {
                antialiasing: true
                Layout.preferredWidth: 30; Layout.preferredHeight: 30; radius: 9
                color: Theme.alpha(Theme.accent, 0.14)
                Icon { anchors.centerIn: parent; name: "math"; size: 17; color: Theme.accent }
            }
            Label { text: "Maths"; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text; Layout.fillWidth: true }
            BusyIndicator { running: root.reading; visible: root.reading; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
        }
        Label { visible: root.status.length > 0; text: root.status; color: Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.bottomMargin: 6 }
        UiField {
            id: field
            Layout.fillWidth: true
            font.pixelSize: 18
            placeholderText: root.reading ? "" : "3x + 5 = 20"
            inputMethodHints: Qt.ImhNoPredictiveText
            onAccepted: if (root.hasAnswer) writeBtn.clicked()
        }
        // The answer, big.
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 12
            Layout.preferredHeight: answerCol.implicitHeight + 24
            radius: Theme.radius
            color: root.result.ok && field.text.trim() ? Theme.alpha(Theme.accent, 0.08) : Theme.surface2
            border.color: root.result.ok && field.text.trim() ? Theme.alpha(Theme.accent, 0.35) : Theme.border
            ColumnLayout {
                id: answerCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4
                Label {
                    visible: root.result.ok && !!root.result.pretty && field.text.trim().length > 0
                    text: root.result.pretty || ""
                    color: Theme.muted
                    font.pixelSize: 14
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: !field.text.trim() ? (root.reading ? "…" : "The answer shows here")
                          : !root.result.ok ? root.result.error
                          : root.result.kind === "function" ? "A function: draw its graph"
                          : root.answerText()
                    color: !field.text.trim() ? Theme.muted : root.result.ok ? Theme.text : Theme.danger
                    font.pixelSize: root.result.ok && field.text.trim() && root.result.kind !== "function" ? 26 : 14
                    font.weight: root.result.ok ? Font.DemiBold : Font.Normal
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 16
            spacing: 8
            UiButton {
                id: writeBtn
                visible: root.hasAnswer
                kind: "primary"
                iconName: "pen"
                text: "Write the answer"
                onClicked: {
                    if (root.canvas.writeBeside(root.answerText(), root.handFont())) root.close()
                }
            }
            UiButton {
                visible: root.fromInk && root.result.ok && field.text.trim().length > 0
                kind: "ghost"
                iconName: "text"
                text: "Typeset"
                onClicked: {
                    // A sum with its answer (2 + 8 = 10); an equation as it is.
                    const k = root.result.kind
                    const line = root.result.pretty + (root.hasAnswer && (k === "value" || k === "expanded") ? " " + root.answerText() : "")
                    if (root.canvas.applyFontToInk(line, "sans", false)) root.close()
                }
            }
            UiButton {
                visible: root.graphable
                kind: "ghost"
                iconName: "graph"
                text: "Graph it"
                onClicked: { root.graphRequested(field.text.trim()); root.close() }
            }
            UiButton {
                visible: field.text.trim().length > 0
                kind: "ghost"
                iconName: "sparkle"
                text: "Steps"
                onClicked: { root.stepsRequested(field.text.trim()); root.close() }
            }
        }
    }
}
