// The assistant (Assistant in C++): a sheet on the right where you ask in
// your own words. It answers from your notes, or plans what to make and
// change; a plan shows as a card with its steps, Do it and Cancel, then
// Undo for all of it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    parent: Overlay.overlay
    x: parent.width - width - 12
    y: 12
    width: Math.min(parent.width - 24, 460)
    height: parent.height - 24
    padding: 0
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    Overlay.modeless: null
    signal settingsRequested()

    /// Where it's opened: a notebook ({ id, title, page, daftar }) or the library (null).
    property var place: null
    /// Ideas to start from: [{ label, prompt, fill? }].
    readonly property var suggestions: place && place.daftar ? [
        { label: "Plan next week's lessons", prompt: "Plan next week's lessons in this daftar for each class, following the timetable and skipping holidays.", icon: "docs" },
        { label: "Lessons for a class on", prompt: "Add the lessons for class  on the topic: ", fill: true, icon: "docs" },
        { label: "What did I cover so far?", prompt: "What have I covered so far in this daftar, by class? What comes next?", icon: "contents" },
        { label: "A worksheet for a lesson", prompt: "Make a worksheet with 8 questions and an answer page, in a new notebook, for the lesson on ", fill: true, icon: "text" }
    ] : place ? [
        { label: "Summarise this notebook", prompt: "Write a one-page summary of this notebook at the end of it, with headings.", icon: "text" },
        { label: "A quiz from my notes", prompt: "Make a 10-question quiz from this notebook, with answer lines, and an answer key on the page after.", icon: "text" },
        { label: "Add headings and a contents page", prompt: "Read this notebook, add headings for its main parts, and add a contents page.", icon: "contents" },
        { label: "Ask about these notes", prompt: "", fill: true, icon: "sparkle" }
    ] : [
        { label: "Set up my classes", prompt: "Make a folder for my subject with a notebook for each of my classes. My subject and classes: ", fill: true, icon: "folder" },
        { label: "Tidy my library", prompt: "Tidy my library: put notebooks that aren't in a folder into folders by subject.", icon: "folder" },
        { label: "Make a worksheet", prompt: "Make a worksheet notebook with 10 questions, answer lines and an answer key, on: ", fill: true, icon: "text" },
        { label: "What did I write about", prompt: "What did I write about ", fill: true, icon: "sparkle" }
    ]

    function show(p) {
        place = p || null
        Assistant.setContext(p ? p.id : "", p ? p.title : "", p ? p.page : -1)
        Ai.refresh()
        open()
        input.forceActiveFocus()
    }
    function ask(text) {
        text = String(text || "").trim()
        if (!text || Assistant.busy) return
        Assistant.setContext(place ? place.id : "", place ? place.title : "", place ? place.page : -1)
        Assistant.send(text)
        input.text = ""
    }
    Connections {
        target: Assistant
        function onChanged() { Qt.callLater(() => list.positionViewAtEnd()) }
    }

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
        // Heading: what it is, where it's working.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.leftMargin: 18
            spacing: 10
            Rectangle {
                antialiasing: true
                Layout.preferredWidth: 34; Layout.preferredHeight: 34; radius: Theme.radius
                color: Theme.alpha(Theme.accent, 0.14)
                Icon { anchors.centerIn: parent; name: "sparkle"; size: 18; color: Theme.accent }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label { text: "Assistant"; font.pixelSize: 16; font.weight: Font.Bold; color: Theme.text }
                Label {
                    Layout.fillWidth: true
                    text: root.place ? "In “" + root.place.title + "”" : "In your library"
                    font.pixelSize: 12; color: Theme.muted; elide: Text.ElideRight
                }
            }
            IconButton { iconName: "plus"; tip: "New conversation"; visible: Assistant.messages.length > 0 && !Assistant.busy; onClicked: Assistant.reset() }
            IconButton { iconName: "close"; tip: "Close (Esc)"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        // Not set up yet.
        ColumnLayout {
            visible: !Ai.ready
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 28
            spacing: 12
            Item { Layout.fillHeight: true }
            Icon { name: "sparkle"; size: 40; color: Theme.muted; Layout.alignment: Qt.AlignHCenter }
            Label { text: "Set up an AI first"; font.pixelSize: 16; font.weight: Font.DemiBold; color: Theme.text; Layout.alignment: Qt.AlignHCenter }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: "The assistant uses the AI you choose in Settings → AI: Claude, GPT or Gemini with a key, or a model on your computer that can use tools."
                font.pixelSize: 13; color: Theme.muted
            }
            UiButton { text: "Open Settings"; kind: "primary"; Layout.alignment: Qt.AlignHCenter; onClicked: { root.close(); root.settingsRequested() } }
            Item { Layout.fillHeight: true }
        }

        // The conversation.
        ListView {
            id: list
            visible: Ai.ready
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 12
            topMargin: 16
            bottomMargin: 16
            model: Assistant.messages
            ScrollBar.vertical: ScrollBar {}
            // Nothing yet: what it can do.
            header: Item {
                width: list.width
                height: Assistant.messages.length === 0 ? intro.implicitHeight + 8 : 0
                visible: Assistant.messages.length === 0
                ColumnLayout {
                    id: intro
                    x: 18
                    width: parent.width - 36
                    spacing: 10
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: root.place ? "What should I do with this notebook?" : "What can I make or find for you?"
                        font.pixelSize: 18; font.weight: Font.DemiBold; color: Theme.text
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: "Ask in your own words, in any language. I'll show you a plan before I change anything, and one tap undoes it."
                        font.pixelSize: 13; color: Theme.muted
                    }
                    Item { Layout.preferredHeight: 4 }
                    Repeater {
                        model: root.suggestions
                        delegate: AbstractButton {
                            id: sg
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 44
                            hoverEnabled: true
                            onClicked: {
                                if (modelData.fill) { input.text = modelData.prompt; input.forceActiveFocus(); input.cursorPosition = input.text.length }
                                else root.ask(modelData.prompt)
                            }
                            background: Rectangle { antialiasing: true; radius: Theme.radius; color: sg.hovered ? Theme.surface2 : "transparent"; border.color: sg.hovered ? Theme.borderStrong : Theme.border }
                            contentItem: RowLayout {
                                spacing: 10
                                Rectangle {
                                    Layout.leftMargin: 8
                                    Layout.preferredWidth: 28; Layout.preferredHeight: 28; radius: Theme.radiusSm; antialiasing: true
                                    color: Theme.alpha(Theme.accent, 0.12)
                                    Icon { anchors.centerIn: parent; name: sg.modelData.icon; size: 15; color: Theme.accent }
                                }
                                Label { Layout.fillWidth: true; text: sg.modelData.label + (sg.modelData.fill ? "…" : ""); font.pixelSize: 13; font.weight: Font.Medium; color: Theme.text; elide: Text.ElideRight }
                                Icon { name: "next"; size: 14; color: Theme.muted; Layout.rightMargin: 10; visible: sg.hovered }
                            }
                        }
                    }
                }
            }
            delegate: Item {
                id: msg
                required property var modelData
                required property int index
                width: list.width
                height: body.implicitHeight
                Loader {
                    id: body
                    x: 18
                    width: parent.width - 36
                    sourceComponent: msg.modelData.role === "user" ? userBubble : msg.modelData.role === "plan" ? planCard : msg.modelData.role === "error" ? errorNote : answer
                    property var m: msg.modelData
                }
            }
            // While it works.
            footer: Item {
                width: list.width
                height: Assistant.busy ? 44 : 0
                visible: Assistant.busy
                RowLayout {
                    x: 18
                    width: parent.width - 36
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    BusyIndicator { Layout.preferredWidth: 20; Layout.preferredHeight: 20; running: Assistant.busy }
                    Label { Layout.fillWidth: true; text: Assistant.status || "Thinking…"; elide: Text.ElideRight; font.pixelSize: 13; color: Theme.muted }
                }
            }
        }

        // The box to ask in.
        Rectangle { visible: Ai.ready; Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
        RowLayout {
            visible: Ai.ready
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            TextArea {
                id: input
                Layout.fillWidth: true
                Layout.maximumHeight: 140
                placeholderText: Assistant.planWaiting ? "Or ask for something else…" : "Ask, or say what to make…"
                wrapMode: TextEdit.Wrap
                font.pixelSize: 14
                color: Theme.text
                placeholderTextColor: Theme.muted
                selectByMouse: true
                leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 10
                background: Rectangle { antialiasing: true; radius: Theme.radiusLg; color: Theme.surface2; border.color: input.activeFocus ? Theme.accent : Theme.border }
                Keys.onReturnPressed: e => { if (e.modifiers & Qt.ShiftModifier) e.accepted = false; else root.ask(input.text) }
                Keys.onEnterPressed: root.ask(input.text)
            }
            AbstractButton {
                id: sendBtn
                Layout.alignment: Qt.AlignBottom
                implicitWidth: 40; implicitHeight: 40
                hoverEnabled: true
                enabled: Assistant.busy || input.text.trim().length > 0
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: Assistant.busy ? "Stop" : "Send (Enter)"
                onClicked: Assistant.busy ? Assistant.stop() : root.ask(input.text)
                background: Rectangle { antialiasing: true; radius: Theme.radiusLg; color: !sendBtn.enabled ? Theme.surface2 : sendBtn.hovered ? Theme.accentHover : Theme.accent }
                contentItem: Item {
                    Icon { visible: !Assistant.busy; anchors.centerIn: parent; name: "send"; size: 17; color: sendBtn.enabled ? Theme.accentContrast : Theme.muted }
                    Rectangle { visible: Assistant.busy; anchors.centerIn: parent; width: 12; height: 12; radius: 2; color: Theme.accentContrast }
                }
            }
        }
    }

    // --- the kinds of message

    Component {
        id: userBubble
        Item {
            width: parent ? parent.width : 0
            implicitHeight: bubble.height
            Rectangle {
                id: bubble
                anchors.right: parent.right
                width: Math.min(parent.width * 0.86, ut.implicitWidth + 28)
                height: ut.implicitHeight + 18
                radius: Theme.radiusLg
                antialiasing: true
                color: Theme.accentSoft
                Label { id: ut; x: 14; y: 9; width: parent.width - 28; text: parent.parent.parent.m.text; wrapMode: Text.Wrap; font.pixelSize: 14; color: Theme.text }
            }
        }
    }
    Component {
        id: answer
        Label {
            width: parent ? parent.width : 0
            text: parent.m.text
            textFormat: Text.MarkdownText
            wrapMode: Text.Wrap
            font.pixelSize: 14
            color: Theme.text
            lineHeight: 1.15
            onLinkActivated: l => Qt.openUrlExternally(l)
        }
    }
    Component {
        id: errorNote
        Rectangle {
            width: parent ? parent.width : 0
            implicitHeight: et.implicitHeight + 20
            radius: Theme.radius
            antialiasing: true
            color: Theme.alpha(Theme.danger, 0.08)
            border.color: Theme.alpha(Theme.danger, 0.3)
            RowLayout {
                x: 12; y: 10
                width: parent.width - 24
                spacing: 8
                Icon { Layout.alignment: Qt.AlignTop; name: "alert"; size: 16; color: Theme.danger }
                Label { id: et; Layout.fillWidth: true; text: parent.parent.parent.m.text; wrapMode: Text.Wrap; font.pixelSize: 13; color: Theme.text }
            }
        }
    }
    // A plan: its steps, then Do it / Cancel; once done, Undo.
    Component {
        id: planCard
        Rectangle {
            id: card
            width: parent ? parent.width : 0
            readonly property var m: parent.m
            readonly property string state_: m.state
            implicitHeight: col.implicitHeight + 28
            radius: Theme.radiusLg
            antialiasing: true
            color: Theme.surface
            border.width: state_ === "pending" ? 1.5 : 1
            border.color: state_ === "pending" ? Theme.accent : Theme.border
            Shadow { anchors.fill: parent; radius: parent.radius; level: card.state_ === "pending" ? 2 : 0; z: -1 }
            ColumnLayout {
                id: col
                x: 14; y: 14
                width: parent.width - 28
                spacing: 8
                RowLayout {
                    spacing: 8
                    Icon {
                        name: card.state_ === "done" ? "check" : card.state_ === "undone" ? "undo" : card.state_ === "failed" ? "alert" : "listNumber"
                        size: 16
                        color: card.state_ === "done" ? Theme.accent : card.state_ === "failed" ? Theme.danger : Theme.muted
                    }
                    Label {
                        Layout.fillWidth: true
                        text: card.state_ === "pending" ? "The plan · " + card.m.steps.length + (card.m.steps.length === 1 ? " step" : " steps")
                            : card.state_ === "done" ? "Done"
                            : card.state_ === "undone" ? "Undone: everything is as it was"
                            : card.state_ === "failed" ? "Stopped partway"
                            : card.state_ === "cancelled" ? "Cancelled: nothing was changed"
                            : "Not done"
                        font.pixelSize: 13; font.weight: Font.DemiBold
                        color: card.state_ === "pending" || card.state_ === "done" ? Theme.text : Theme.muted
                    }
                }
                Repeater {
                    model: card.m.steps
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 10
                        opacity: card.state_ === "cancelled" || card.state_ === "replaced" || card.state_ === "undone" ? 0.5 : 1
                        Rectangle {
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: 24; Layout.preferredHeight: 24; radius: Theme.radiusSm; antialiasing: true
                            color: modelData.done ? Theme.accent : modelData.failed ? Theme.alpha(Theme.danger, 0.14) : Theme.surface2
                            Label { visible: !modelData.done && !modelData.failed; anchors.centerIn: parent; text: index + 1; font.pixelSize: 11; font.weight: Font.Bold; color: Theme.muted }
                            Icon { visible: !!modelData.done || !!modelData.failed; anchors.centerIn: parent; name: modelData.failed ? "alert" : "check"; size: 13; color: modelData.failed ? Theme.danger : Theme.accentContrast }
                        }
                        Icon { Layout.alignment: Qt.AlignTop; Layout.topMargin: 4; name: modelData.icon || "page"; size: 15; color: Theme.muted }
                        Label { Layout.fillWidth: true; Layout.topMargin: 3; text: modelData.summary; wrapMode: Text.Wrap; font.pixelSize: 13; color: Theme.text }
                    }
                }
                Label {
                    visible: !!card.m.error
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "Step " + (card.m.steps.findIndex(s => s.failed) + 1) + " didn't work: " + (card.m.error || "") + ". The steps before it were done; Undo takes them back."
                    font.pixelSize: 12; color: Theme.danger
                }
                RowLayout {
                    visible: card.state_ === "pending"
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8
                    UiButton { kind: "primary"; text: "Do it"; iconName: "check"; implicitHeight: 34; onClicked: Assistant.approve() }
                    UiButton { kind: "ghost"; text: "Cancel"; implicitHeight: 34; onClicked: Assistant.cancelPlan() }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    visible: (card.state_ === "done" || card.state_ === "failed") && Assistant.canUndo
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8
                    UiButton { text: "Undo all of it"; iconName: "undo"; implicitHeight: 32; onClicked: Assistant.undo() }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
}
