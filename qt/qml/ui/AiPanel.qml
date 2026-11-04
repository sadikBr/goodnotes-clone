// The AI assistant: a sheet on the right to ask about the library, a
// notebook or a selection (with its picture), with suggestions to start
// from. Answers can be copied, or put on the page as text.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    parent: Overlay.overlay
    x: parent.width - width - 12
    y: 12
    width: Math.min(parent.width - 24, 440)
    height: parent.height - 24
    padding: 0
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape

    /// What it's about ("" notebook: the whole library).
    property string notebookId: ""
    property string heading: "Ask AI"
    /// A picture sent with the first question (a selection), as a data: URL.
    property string image: ""
    /// Text that goes with it (typed text in the selection, the page being looked at…).
    property string extra: ""
    /// Include the notes (the notebook's, or the library's most related).
    property bool useNotes: true
    /// [{ label, prompt, fill? (put in the box to finish), attach? (a picture to send) }] to start from.
    property var suggestions: []
    /// In a notebook: answers can go on the page.
    property bool canInsert: false
    signal insertRequested(string text)
    signal settingsRequested()

    /// { role: "user" | "assistant" | "error", text, live (still arriving) }
    ListModel { id: convoModel }
    readonly property int count: convoModel.count
    property bool waiting: false
    /// Its first words have arrived.
    readonly property bool streaming: waiting && count > 0 && convoModel.get(count - 1).live === true
    /// The answer being streamed (askStream's id), to stop it.
    property int streamId: -1
    function stop() {
        if (streamId >= 0) InkleafCore.cancelAsk(streamId)
    }
    function add(m) {
        convoModel.append({ role: m.role, text: m.text, live: !!m.live })
        Qt.callLater(() => list.positionViewAtEnd())
    }
    function reset() {
        stop()
        asking++
        convoModel.clear()
        system = ""
        waiting = false
        streamId = -1
    }
    property int asking: 0
    property string system: ""

    readonly property string basePrompt: "You are the assistant in Inkleaf, a note-taking app for handwritten and typed notes, also used by teachers in Morocco for their lesson log (the daftar annossous). Answer in the language the user writes in (Arabic, French, English…). Be concise and practical. Use Markdown sparingly: short lists, **bold** for key words. For maths and science, show the steps clearly in plain text."

    /// Opens on something new: { heading, notebookId, image, extra, useNotes, suggestions, canInsert, prompt (asked at once) }.
    function start(o) {
        reset()
        heading = o.heading || "Ask AI"
        notebookId = o.notebookId || ""
        image = o.image || ""
        extra = o.extra || ""
        useNotes = o.useNotes !== false
        suggestions = o.suggestions || []
        canInsert = !!o.canInsert
        input.text = ""
        Ai.refresh()
        open()
        input.forceActiveFocus()
        if (o.prompt && Ai.ready) send(o.prompt)
    }

    /// An answer's Markdown as plain text for the page (bullets kept as •).
    function plain(md) {
        return String(md)
            .replace(/```[a-z]*\n?/g, "")
            .replace(/^#{1,6}\s+/gm, "")
            .replace(/^\s*[-*+]\s+/gm, "• ")
            .replace(/\*\*(.+?)\*\*/g, "$1")
            .replace(/__(.+?)__/g, "$1")
            .replace(/(^|[^*])\*(?!\s)(.+?)\*/g, "$1$2")
            .replace(/`([^`]+)`/g, "$1")
            .replace(/\[([^\]]+)\]\([^)]+\)/g, "$1")
            .replace(/\n{3,}/g, "\n\n")
            .trim()
    }

    function send(text) {
        text = String(text || "").trim()
        if (!text || waiting || !Ai.ready) return
        add({ role: "user", text: text })
        input.text = ""
        waiting = true
        const ticket = asking
        const go = () => {
            // The conversation so far (answers that failed left out); the picture with the first question.
            const convo = []
            for (let i = 0; i < convoModel.count; ++i) {
                const m = convoModel.get(i)
                if ((m.role === "user" || m.role === "assistant") && m.text) convo.push({ role: m.role, text: m.text, images: [] })
            }
            if (image && convo.length) convo[0].images = [image]
            // The answer appears as it's written.
            let at = -1
            root.streamId = Ai.askStream(system, convo, piece => {
                if (ticket !== root.asking) return
                if (at < 0) {
                    root.add({ role: "assistant", text: "", live: true })
                    at = convoModel.count - 1
                }
                convoModel.setProperty(at, "text", convoModel.get(at).text + piece)
                if (list.atYEnd || list.contentHeight - list.contentY - list.height < 80) Qt.callLater(() => list.positionViewAtEnd())
            }, (answer, err) => {
                if (ticket !== root.asking) return
                root.waiting = false
                root.streamId = -1
                if (at >= 0) {
                    convoModel.setProperty(at, "live", false)
                    // A failure partway (or a refusal): what came is replaced by why.
                    if (err) { convoModel.setProperty(at, "role", "error"); convoModel.setProperty(at, "text", err) }
                    else if (answer) convoModel.setProperty(at, "text", answer)
                } else {
                    root.add(err ? { role: "error", text: err } : { role: "assistant", text: answer || "(No answer.)" })
                }
                Qt.callLater(() => list.positionViewAtEnd())
            })
        }
        if (system) { go(); return }
        // First question: the instructions, with the notes it can use.
        let sys = basePrompt
        if (extra) sys += "\n\n" + extra
        if (!useNotes) { system = sys; go(); return }
        InkleafCore.callAsync("notesForAi", { notebookId: notebookId || null, question: text, budget: Ai.notesBudget }, (notes, err) => {
            if (ticket !== root.asking) return
            if (err) {
                root.waiting = false
                root.add({ role: "error", text: String(err).startsWith("locked:") ? "This notebook is locked: unlock it first." : err })
                return
            }
            sys += notes
                ? "\n\nThe user's notes follow (" + (root.notebookId ? "this notebook" : "the parts of their library most related to the question") + "; typed text, and handwriting read by AI marked [handwriting]):\n<notes>\n" + notes + "\n</notes>\nWhen the question is about the notes, answer from them and name the notebook and page you used; say so when they don't hold the answer."
                : "\n\n(There's no readable text in " + (root.notebookId ? "this notebook" : "the library") + " yet. Handwriting becomes readable once “Make handwriting searchable” is on in Settings → AI.)"
            root.system = sys
            go()
        })
        Qt.callLater(() => list.positionViewAtEnd())
    }

    Overlay.modeless: null
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "x"; from: root.parent ? root.parent.width : 0; duration: 220; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 100 } }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.leftMargin: 18
            spacing: 10
            Rectangle {
                antialiasing: true
                Layout.preferredWidth: 30; Layout.preferredHeight: 30; radius: 9
                color: Theme.alpha(Theme.accent, 0.14)
                Icon { anchors.centerIn: parent; name: "sparkle"; size: 17; color: Theme.accent }
            }
            Label { text: root.heading; font.pixelSize: 16; font.weight: Font.Bold; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
            IconButton { iconName: "plus"; tip: "New conversation"; visible: root.count > 0; onClicked: root.reset() }
            IconButton { iconName: "close"; tip: "Close (Esc)"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        // Not set up yet.
        ColumnLayout {
            visible: !Ai.ready
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            spacing: 12
            Item { Layout.fillHeight: true }
            Icon { name: "sparkle"; size: 40; color: Theme.muted; Layout.alignment: Qt.AlignHCenter }
            Label {
                text: "Set up an AI first"
                font.pixelSize: 16; font.weight: Font.DemiBold; color: Theme.text
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: "Use a model on your computer (Ollama, LM Studio…) or your own API key for Claude, ChatGPT or Gemini."
                color: Theme.muted; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            UiButton { text: "Open Settings → AI"; kind: "primary"; Layout.alignment: Qt.AlignHCenter; onClicked: { root.close(); AppSettings.settingsPage = "ai"; root.settingsRequested() } }
            Item { Layout.fillHeight: true }
        }

        ListView {
            id: list
            visible: Ai.ready
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            topMargin: 14
            bottomMargin: 14
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            model: convoModel
            header: ColumnLayout {
                width: list.width
                spacing: 10
                // The picture that goes with the question.
                Rectangle {
                    visible: root.image.length > 0
                    Layout.leftMargin: 18; Layout.rightMargin: 18
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(180, pic.implicitHeight * Math.min(1, (list.width - 36) / Math.max(1, pic.implicitWidth)) + 16)
                    radius: Theme.radius
                    color: Theme.surface2
                    border.color: Theme.border
                    Image { id: pic; anchors.fill: parent; anchors.margins: 8; source: root.image; fillMode: Image.PreserveAspectFit; smooth: true; mipmap: true }
                }
                // Nothing asked yet: what this can do, the suggestions as
                // cards, and which AI answers.
                ColumnLayout {
                    visible: root.count === 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 18; Layout.rightMargin: 18
                    Layout.topMargin: root.image.length > 0 ? 0 : 18
                    spacing: 8
                    Rectangle {
                        visible: root.image.length === 0
                        antialiasing: true
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 64; Layout.preferredHeight: 64; radius: 32
                        color: Theme.alpha(Theme.accent, 0.12)
                        Icon { anchors.centerIn: parent; name: "sparkle"; size: 28; color: Theme.accent }
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 4
                        text: root.image.length > 0 ? "What about this?" : "What would you like to know?"
                        font.pixelSize: 16; font.weight: Font.Bold; color: Theme.text
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: Theme.muted; font.pixelSize: 12; lineHeight: 1.15
                        text: root.notebookId ? "Answers come from this notebook's typed text" + (Ai.readingOn ? " and handwriting." : ". Turn on “Make handwriting searchable” in Settings → AI for handwriting too.")
                              : root.useNotes ? "Answers come from your library's typed text" + (Ai.readingOn ? " and handwriting (locked notebooks are left out)." : ". Turn on “Make handwriting searchable” in Settings → AI for handwriting too.")
                              : "Ask anything about it."
                    }
                    // Which AI answers.
                    Rectangle {
                        antialiasing: true
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 2
                        Layout.preferredHeight: 24
                        Layout.preferredWidth: Math.min(list.width - 36, aiLabel.implicitWidth + 20)
                        radius: 12
                        color: Theme.surface2
                        border.color: Theme.border
                        Label {
                            id: aiLabel
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, parent.width - 16)
                            elide: Text.ElideRight
                            font.pixelSize: 11
                            color: Theme.muted
                            text: {
                                const st = Ai.status || {}
                                const names = { anthropic: "Claude", openai: "ChatGPT", gemini: "Gemini", ollama: "Ollama", custom: "Your AI server" }
                                return (names[st.provider] || "AI") + (st.effectiveModel ? " · " + st.effectiveModel : "") + (Ai.local ? " · on this computer" : "")
                            }
                        }
                    }
                    Item { Layout.preferredHeight: 6 }
                    Repeater {
                        model: root.suggestions
                        delegate: AbstractButton {
                            id: chip
                            required property var modelData
                            Layout.fillWidth: true
                            hoverEnabled: true
                            implicitHeight: 42
                            onClicked: {
                                // A picture to go with it (the page), worked out when asked.
                                if (modelData.attach) root.image = modelData.attach() || ""
                                if (modelData.fill) {
                                    input.text = modelData.prompt
                                    input.forceActiveFocus()
                                    input.cursorPosition = input.length
                                } else {
                                    root.send(modelData.prompt)
                                }
                            }
                            background: Rectangle {
                                antialiasing: true
                                radius: 12
                                color: chip.pressed ? Theme.alpha(Theme.accent, 0.16) : chip.hovered ? Theme.alpha(Theme.accent, 0.08) : Theme.surface2
                                border.color: chip.hovered ? Theme.accent : Theme.border
                                Behavior on color { ColorAnimation { duration: 120 } }
                            }
                            contentItem: RowLayout {
                                spacing: 8
                                Label { id: chipLabel; text: chip.modelData.label; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true; Layout.leftMargin: 14 }
                                Icon { name: chip.modelData.fill ? "pen" : "send"; size: 15; color: chip.hovered ? Theme.accent : Theme.muted; Layout.rightMargin: 12 }
                            }
                        }
                    }
                }
            }
            delegate: Item {
                id: msg
                required property string role
                required property string text
                required property bool live
                readonly property bool mine: role === "user"
                width: list.width
                height: bubble.height + (actions.visible ? actions.height + 4 : 0)
                Rectangle {
                    id: bubble
                    antialiasing: true
                    x: msg.mine ? parent.width - width - 18 : 18
                    width: Math.min(parent.width - (msg.mine ? 72 : 36), body.implicitWidth + 24)
                    height: body.implicitHeight + 18
                    radius: 14
                    color: msg.mine ? Theme.alpha(Theme.accent, 0.14) : msg.role === "error" ? Theme.alpha(Theme.danger, 0.1) : Theme.surface2
                    border.color: msg.role === "error" ? Theme.alpha(Theme.danger, 0.4) : "transparent"
                    TextEdit {
                        id: body
                        x: 12; y: 9
                        width: Math.min(implicitWidth, msg.width - (msg.mine ? 72 : 36) - 24)
                        readOnly: true
                        selectByMouse: true
                        wrapMode: Text.Wrap
                        textFormat: msg.role === "assistant" ? TextEdit.MarkdownText : TextEdit.PlainText
                        text: msg.text
                        color: msg.role === "error" ? Theme.danger : Theme.text
                        selectionColor: Theme.alpha(Theme.accent, 0.3)
                        selectedTextColor: Theme.text
                        font.pixelSize: 14
                        font.family: Theme.font
                    }
                }
                RowLayout {
                    id: actions
                    visible: msg.role === "assistant" && !msg.live
                    anchors.top: bubble.bottom
                    anchors.topMargin: 4
                    x: 12
                    spacing: 2
                    UiButton {
                        kind: "ghost"; iconName: "copy"; text: "Copy"; implicitHeight: 30
                        onClicked: { clip.text = msg.text; clip.selectAll(); clip.copy(); text = "Copied" }
                    }
                    UiButton {
                        visible: root.canInsert
                        kind: "ghost"; iconName: "text"; text: "Put on page"; implicitHeight: 30
                        onClicked: root.insertRequested(root.plain(msg.text))
                    }
                }
            }
            footer: Item {
                width: list.width
                height: root.waiting && !root.streaming ? 40 : 0
                visible: root.waiting && !root.streaming
                RowLayout {
                    x: 18
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    BusyIndicator { running: root.waiting; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
                    Label { text: Ai.local ? "Thinking (local models can take a while)…" : "Thinking…"; color: Theme.muted }
                }
            }
        }
        // For copying (the clipboard from QML).
        TextEdit { id: clip; visible: false }

        Rectangle { visible: Ai.ready; Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
        RowLayout {
            visible: Ai.ready
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(120, Math.max(40, input.implicitHeight))
                TextArea {
                    id: input
                    placeholderText: root.count ? "Ask a follow-up…" : "Ask anything…"
                    leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 10
                    wrapMode: TextEdit.Wrap
                    color: Theme.text
                    placeholderTextColor: Theme.muted
                    font.pixelSize: 14
                    selectionColor: Theme.alpha(Theme.accent, 0.3)
                    background: Rectangle { antialiasing: true; radius: Theme.radius; color: Theme.surface2; border.width: input.activeFocus ? 2 : 1; border.color: input.activeFocus ? Theme.accent : Theme.border }
                    // Enter sends; Shift+Enter starts a new line.
                    Keys.onReturnPressed: e => { if (e.modifiers & Qt.ShiftModifier) e.accepted = false; else root.send(text) }
                    Keys.onEnterPressed: e => { if (e.modifiers & Qt.ShiftModifier) e.accepted = false; else root.send(text) }
                }
            }
            IconButton {
                iconName: root.waiting ? "close" : "send"
                tip: root.waiting ? "Stop" : "Send (Enter)"
                enabled: root.waiting ? root.streamId >= 0 : input.text.trim().length > 0
                onClicked: root.waiting ? root.stop() : root.send(input.text)
            }
        }
    }
}
