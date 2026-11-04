// "Apply a font to handwriting" (src/editor/inkToText.ts): what was written
// (read by the system's handwriting recognition where there is one, else
// typed), a handwriting font to show it in, and whether to keep the ink.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf
import "Fonts.js" as Fonts

Popup {
    id: root
    required property InkCanvas canvas
    signal notify(string message, bool error)
    property string fontId: "mynerve"
    property string status: ""
    property var candidates: []
    property bool keepInk: false

    function start() {
        const strokes = canvas.selectedInkStrokes()
        if (!strokes.length) return
        fontId = canvas.textFont === "sans" ? "mynerve" : canvas.textFont
        status = "Reading your handwriting…"
        candidates = []
        keepInk = false
        field.text = ""
        open()
        field.forceActiveFocus()
        if (Ai.ready) {
            // The AI reads it (any language, maths too).
            status = "Reading your handwriting with AI…"
            const image = canvas.selectionImage(1400)
            Ai.transcribe(image, (text, err) => {
                if (!root.visible) return
                if (err) { root.status = "The AI couldn't read it (" + err + "). Type what you wrote and pick a font."; return }
                if (!text) { root.status = "No writing found. Type what you wrote and pick a font."; return }
                root.status = "Read by AI. Edit the text if needed:"
                if (!field.text) field.text = text
                field.selectAll()
            })
            return
        }
        InkleafCore.callAsync("recognizeHandwriting", { strokes: strokes }, (r, err) => {
            if (!root.visible) return
            if (err) { root.status = "Recognition failed (" + err + "). Type what you wrote and pick a font."; return }
            if (!r.available) { root.status = "Automatic handwriting recognition isn't available on this computer. Type what you wrote and pick a font."; return }
            if (!r.candidates || !r.candidates.length) { root.status = "Couldn't read that handwriting. Type what you wrote and pick a font."; return }
            root.status = r.candidates.length > 1 ? "Recognised. Pick the right reading or edit it:" : "Recognised. Edit the text if needed:"
            if (!field.text) field.text = r.candidates[0]
            root.candidates = r.candidates.length > 1 ? r.candidates : []
            field.selectAll()
        })
    }
    function apply() {
        const text = field.text.trim()
        if (!text) return
        close()
        canvas.applyFontToInk(text, fontId, keepInk)
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 560)
    modal: true
    focus: true
    padding: 24
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(20 / 255, 18 / 255, 14 / 255, 0.45) }
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
        Label { text: "Apply a font to handwriting"; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text; Layout.bottomMargin: 18 }
        Label { text: root.status; color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.bottomMargin: 10; lineHeight: 1.2 }
        // Other readings.
        Flow {
            visible: root.candidates.length > 0
            Layout.fillWidth: true
            Layout.bottomMargin: 10
            spacing: 6
            Repeater {
                model: root.candidates
                delegate: AbstractButton {
                    id: chip
                    required property string modelData
                    hoverEnabled: true
                    implicitHeight: 30
                    implicitWidth: chipLabel.implicitWidth + 26
                    onClicked: { field.text = modelData; field.forceActiveFocus() }
                    background: Rectangle { antialiasing: true; radius: height / 2; color: Theme.surface2; border.color: chip.hovered ? Theme.accent : Theme.border }
                    contentItem: Label { id: chipLabel; text: chip.modelData; color: Theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }
        Label { text: "Text"; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted; Layout.bottomMargin: 6 }
        UiField {
            id: field
            Layout.fillWidth: true
            Layout.bottomMargin: 14
            placeholderText: "Type what you wrote"
            maximumLength: 4000
            font.pixelSize: 16
            onAccepted: root.apply()
        }
        Label { text: "Font"; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted; Layout.bottomMargin: 6 }
        // .font-grid: cards at least 118 wide, previewing the text in each font.
        Grid {
            id: grid
            Layout.fillWidth: true
            Layout.bottomMargin: 14
            readonly property int cols: Math.max(1, Math.floor((width + 8) / 126))
            columns: cols
            spacing: 8
            Repeater {
                model: Fonts.presets
                delegate: AbstractButton {
                    id: card
                    required property var modelData
                    readonly property bool on: root.fontId === modelData.id
                    width: (grid.width - (grid.cols - 1) * 8) / grid.cols
                    height: 80
                    clip: true
                    hoverEnabled: true
                    onClicked: root.fontId = modelData.id
                    background: Rectangle {
                        antialiasing: true
                        radius: 10
                        color: card.on ? Theme.accentSoft : Theme.surface
                        border.width: 1.5
                        border.color: card.on ? Theme.accent : card.hovered ? Theme.muted : Theme.border
                    }
                    contentItem: ColumnLayout {
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: 6
                            Layout.rightMargin: 6
                            text: field.text.trim() || "Your text"
                            font.family: card.modelData.face
                            font.pixelSize: 22
                            color: Theme.text
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Label { Layout.alignment: Qt.AlignHCenter; text: card.modelData.label; font.pixelSize: 11; color: Theme.muted }
                    }
                }
            }
        }
        // .ink-keep
        AbstractButton {
            id: keep
            Layout.bottomMargin: 4
            implicitHeight: 24
            implicitWidth: keepRow.implicitWidth
            onClicked: root.keepInk = !root.keepInk
            background: Item {}
            contentItem: RowLayout {
                id: keepRow
                spacing: 8
                Rectangle {
                    antialiasing: true
                    implicitWidth: 16; implicitHeight: 16; radius: 4
                    color: root.keepInk ? Theme.accent : Theme.surface
                    border.color: root.keepInk ? Theme.accent : Theme.borderStrong
                    Icon { visible: root.keepInk; anchors.centerIn: parent; name: "check"; size: 12; color: Theme.accentContrast }
                }
                Label { text: "Keep my handwriting as well"; color: Theme.muted }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 14
            spacing: 8
            Item { Layout.fillWidth: true }
            UiButton { text: "Cancel"; onClicked: root.close() }
            UiButton { text: "Apply font"; kind: "primary"; enabled: field.text.trim().length > 0; onClicked: root.apply() }
        }
    }
}
