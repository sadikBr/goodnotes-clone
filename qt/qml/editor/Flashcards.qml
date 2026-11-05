// Flashcards from your notes. Make one: select the question on a page, then the
// answer (or type it). Study: each card's question, then its answer (tap to
// turn it over), and how well you knew it: Again, Hard, Good or Easy. Cards come
// back when they're due (spaced repetition). Cards show the page as it is now,
// and live in the notebook's settings (so they sync with it).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Item {
    id: root
    required property InkCanvas canvas
    signal notify(string message, bool error)

    // ------------------------------------------------------------ the cards
    property var cards: []
    function load() { const v = canvas.notebookSetting("cards"); cards = Array.isArray(v) ? v : [] }
    function save(next) { cards = next; canvas.setNotebookSetting("cards", next) }
    Connections {
        target: root.canvas
        function onNotebookIdChanged() { root.load() }
        function onPagesChanged() { root.load() }
    }
    Component.onCompleted: load()
    readonly property real now: Date.now()
    function dueCount() { const t = Date.now(); return cards.filter(c => (c.due || 0) <= t).length }

    /// A side of a card as a picture (the page as it is now), or "" (its page is gone).
    function picture(side) {
        if (!side) return ""
        const i = canvas.pageIndexOf(side.page)
        if (i < 0) return ""
        return canvas.regionPicture(i, Qt.rect(side.x - 10, side.y - 10, side.w + 20, side.h + 20), 900)
    }

    // ------------------------------------------------------------ making one
    /// The question, while you choose the answer: { page, x, y, w, h } (page id).
    property var making: null
    property var answerArea: null
    function startCard() {
        const a = canvas.selectionArea()
        if (!a || a.w === undefined) return notify("Select the question first", false)
        const i = a.page
        making = { page: canvas.pages[i].id, x: a.x, y: a.y, w: a.w, h: a.h }
        canvas.clearSelection()
    }
    function newCard(back, backText) {
        return { id: String(Date.now()) + Math.floor(Math.random() * 1000), front: making, back: back, backText: backText || "", ease: 2.5, interval: 0, due: 0, reps: 0, created: Date.now() }
    }
    function finishCard(card) {
        save(cards.concat([card]))
        making = null
        canvas.clearSelection()
        notify("Card added · " + cards.length + (cards.length === 1 ? " card" : " cards") + " in this notebook", false)
    }
    Connections {
        target: root.canvas
        function onSelectionChanged() {
            if (!root.making) return
            const a = root.canvas.selectionArea()
            root.answerArea = a && a.w !== undefined ? a : null
        }
    }
    FormDialog { id: ask }

    // The banner while making a card.
    Rectangle {
        id: banner
        visible: !!root.making
        z: 4
        anchors.horizontalCenter: parent.horizontalCenter
        y: 14
        height: bannerRow.implicitHeight + 14
        width: Math.min(parent.width - 24, bannerRow.implicitWidth + 24)
        radius: Theme.radiusLg
        antialiasing: true
        color: Theme.surface
        border.color: Theme.border
        Shadow { anchors.fill: parent; radius: parent.radius; level: 3; z: -1 }
        RowLayout {
            id: bannerRow
            anchors.centerIn: parent
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 30; Layout.preferredHeight: 30
                radius: Theme.radius
                antialiasing: true
                color: Theme.accentSoft
                Icon { anchors.centerIn: parent; name: "cards"; size: 17; color: Theme.accent }
            }
            ColumnLayout {
                spacing: 0
                Label { text: "Question saved ✓"; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.accent }
                Label { text: root.answerArea ? "Use what's selected as the answer?" : "Now select the answer on the page"; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text }
            }
            UiButton {
                kind: "primary"; text: "Use as answer"; implicitHeight: 32
                enabled: !!root.answerArea
                onClicked: {
                    const a = root.answerArea
                    root.finishCard(root.newCard({ page: root.canvas.pages[a.page].id, x: a.x, y: a.y, w: a.w, h: a.h }, ""))
                }
            }
            UiButton {
                kind: "ghost"; text: "Type it"; implicitHeight: 32
                onClicked: ask.ask("The answer", [{ name: "text", label: "Answer", value: "" }], "Add card", r => root.finishCard(root.newCard(null, r.text.trim())), { validate: v => v.text.trim() ? null : "Write the answer" })
            }
            UiButton { kind: "ghost"; text: "No answer"; implicitHeight: 32; ToolTip.visible: hovered; ToolTip.delay: 500; ToolTip.text: "Just the question: you answer it in your head"; onClicked: root.finishCard(root.newCard(null, "")) }
            SmallTool { iconName: "close"; tip: "Cancel this card"; onClicked: root.making = null }
        }
    }

    // ------------------------------------------------------------ studying
    function open() { load(); study.start(); panel.open() }

    // Spaced repetition: the next interval (days) and ease for each answer.
    function schedule(c, grade) {
        let ease = c.ease || 2.5, iv = c.interval || 0
        if (grade === 0) { ease = Math.max(1.3, ease - 0.2); iv = 0 }
        else if (grade === 1) { ease = Math.max(1.3, ease - 0.15); iv = Math.max(1, iv * 1.2) }
        else if (grade === 2) iv = iv < 1 ? 1 : iv * ease
        else { ease = ease + 0.15; iv = iv < 1 ? 4 : iv * ease * 1.3 }
        iv = Math.min(iv, 3650)
        return { ease: ease, interval: iv, due: grade === 0 ? Date.now() + 60 * 1000 : Date.now() + iv * 86400000 }
    }
    function whenText(days) {
        if (days < 1 / 24) return "< 1 min"
        if (days < 1) return Math.round(days * 24) + " h"
        if (days < 30) return Math.round(days) + (Math.round(days) === 1 ? " day" : " days")
        if (days < 365) return Math.round(days / 30) + " mo"
        return (days / 365).toFixed(1) + " yr"
    }
    function dueText(c) {
        const d = ((c.due || 0) - Date.now()) / 86400000
        return d <= 0 ? "Due now" : "In " + whenText(d)
    }

    QtObject {
        id: study
        property var queue: []
        property int done: 0
        property int total: 0
        property bool flipped: false
        property bool anyway: false
        readonly property var card: queue.length ? root.cards.find(c => c.id === queue[0]) || null : null
        property string frontPic: ""
        property string backPic: ""
        onCardChanged: {
            flipped = false
            frontPic = card ? root.picture(card.front) : ""
            backPic = card && card.back ? root.picture(card.back) : ""
        }
        function start(all) {
            anyway = !!all
            const t = Date.now()
            const list = root.cards.filter(c => all || (c.due || 0) <= t).sort((a, b) => (a.due || 0) - (b.due || 0))
            // Shuffled a little: new and due cards in a fresh order each time.
            for (let i = list.length - 1; i > 0; --i) { const j = Math.floor(Math.random() * (i + 1)); const x = list[i]; list[i] = list[j]; list[j] = x }
            queue = list.map(c => c.id)
            done = 0
            total = queue.length
        }
        function grade(g) {
            const c = card
            if (!c) return
            const s = root.schedule(c, g)
            root.save(root.cards.map(x => x.id === c.id ? Object.assign({}, x, s, { reps: (x.reps || 0) + 1 }) : x))
            const rest = queue.slice(1)
            // Again: it comes back at the end of this round.
            queue = g === 0 ? rest.concat([c.id]) : rest
            if (g !== 0) done++
        }
    }
    readonly property var nextDue: { void cards; const f = cards.filter(c => (c.due || 0) > Date.now()).sort((a, b) => a.due - b.due); return f.length ? f[0] : null }

    Popup {
        id: panel
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 680)
        height: Math.min(parent.height - 32, 620)
        padding: 0
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, Theme.dark ? 0.55 : 0.3) }
        property string tab: "study"
        onOpened: { tab = "study"; panel.forceActiveFocus() }
        background: Item {
            Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 4 }
            Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
        }
        contentItem: ColumnLayout {
            spacing: 0
            focus: true
            Keys.onSpacePressed: if (panel.tab === "study" && study.card) study.flipped = !study.flipped
            Keys.onPressed: e => {
                if (panel.tab !== "study" || !study.card || !study.flipped) return
                const g = { "1": 0, "2": 1, "3": 2, "4": 3 }[e.text]
                if (g !== undefined) { study.grade(g); e.accepted = true }
            }
            // Heading and tabs.
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 18
                Layout.bottomMargin: 10
                spacing: 10
                Rectangle {
                    Layout.preferredWidth: 36; Layout.preferredHeight: 36
                    radius: Theme.radius
                    antialiasing: true
                    color: Theme.accentSoft
                    Icon { anchors.centerIn: parent; name: "cards"; size: 19; color: Theme.accent }
                }
                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label { text: "Flashcards"; font.pixelSize: 17; font.weight: Font.DemiBold; color: Theme.text }
                    Label { text: { void root.cards; const d = root.dueCount(); return root.cards.length === 0 ? "None yet" : d + " to study · " + root.cards.length + " in this notebook" } font.pixelSize: 12; color: Theme.muted }
                }
                Segmented {
                    options: [{ value: "study", label: "Study" }, { value: "all", label: "All cards" }]
                    value: panel.tab
                    onPicked: v => panel.tab = v
                }
                SmallTool { iconName: "close"; tip: "Close"; onClicked: panel.close() }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

            // --- study
            ColumnLayout {
                visible: panel.tab === "study"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 18
                spacing: 14
                // Progress through this round.
                Rectangle {
                    visible: study.total > 0 && !!study.card
                    Layout.fillWidth: true
                    Layout.preferredHeight: 6
                    radius: 3
                    color: Theme.surface2
                    Rectangle {
                        width: parent.width * (study.total ? study.done / study.total : 0)
                        height: parent.height
                        radius: 3
                        color: Theme.accent
                        Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                    }
                }
                // The card, turned over with a tap.
                Item {
                    id: cardArea
                    visible: !!study.card
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Item {
                        id: flipper
                        anchors.fill: parent
                        property real angle: study.flipped ? 180 : 0
                        Behavior on angle { NumberAnimation { duration: 380; easing.type: Easing.OutCubic } }
                        readonly property bool backShown: angle > 90
                        transform: Rotation { origin.x: flipper.width / 2; origin.y: flipper.height / 2; axis { x: 0; y: 1; z: 0 } angle: flipper.angle }
                        Shadow { anchors.fill: face; radius: Theme.radiusXl; level: 2 }
                        Rectangle {
                            id: face
                            anchors.fill: parent
                            radius: Theme.radiusXl
                            antialiasing: true
                            color: Theme.dark ? "#202126" : "#ffffff"
                            border.color: flipper.backShown ? Theme.alpha(Theme.accent, 0.6) : Theme.border
                            border.width: 1.5
                            // (The back is drawn mirrored, so it reads right way round once turned.)
                            Item {
                                anchors.fill: parent
                                transform: Scale { origin.x: face.width / 2; xScale: flipper.backShown ? -1 : 1 }
                                Label {
                                    x: 18; y: 14
                                    text: flipper.backShown ? "ANSWER" : "QUESTION"
                                    font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 1
                                    color: flipper.backShown ? Theme.accent : Theme.muted
                                }
                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 40
                                    anchors.topMargin: 44
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    mipmap: true
                                    visible: !flipper.backShown || !!study.backPic
                                    source: flipper.backShown ? study.backPic : study.frontPic
                                }
                                Label {
                                    visible: flipper.backShown && !study.backPic
                                    anchors.centerIn: parent
                                    width: parent.width - 80
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                    text: study.card && study.card.backText ? study.card.backText : "Did you know it?"
                                    font.pixelSize: study.card && study.card.backText ? 26 : 18
                                    font.family: study.card && study.card.backText ? "Andika" : Theme.font
                                    color: study.card && study.card.backText ? Theme.text : Theme.muted
                                }
                                Label {
                                    visible: !flipper.backShown
                                    anchors.bottom: parent.bottom; anchors.bottomMargin: 12
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Tap to turn over"
                                    font.pixelSize: 11
                                    color: Theme.muted
                                }
                            }
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: study.flipped = !study.flipped }
                    }
                }
                // Show the answer, then how well you knew it.
                UiButton {
                    visible: !!study.card && !study.flipped
                    Layout.fillWidth: true
                    kind: "primary"
                    text: "Show answer  (Space)"
                    onClicked: study.flipped = true
                }
                RowLayout {
                    visible: !!study.card && study.flipped
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: [{ g: 0, label: "Again", color: "#ef4444" }, { g: 1, label: "Hard", color: "#f59e0b" }, { g: 2, label: "Good", color: "#22c55e" }, { g: 3, label: "Easy", color: "#3b82f6" }]
                        delegate: AbstractButton {
                            id: gb
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 52
                            hoverEnabled: true
                            scale: pressed ? 0.96 : 1
                            Behavior on scale { NumberAnimation { duration: 100 } }
                            readonly property string next: study.card ? root.whenText(gb.modelData.g === 0 ? 0 : root.schedule(study.card, gb.modelData.g).interval) : ""
                            onClicked: study.grade(modelData.g)
                            background: Rectangle {
                                antialiasing: true
                                radius: Theme.radius
                                color: gb.hovered ? gb.modelData.color : Theme.alpha(gb.modelData.color, 0.14)
                                border.color: Theme.alpha(gb.modelData.color, 0.5)
                            }
                            contentItem: ColumnLayout {
                                spacing: 0
                                Label { Layout.alignment: Qt.AlignHCenter; text: gb.modelData.label; font.pixelSize: 14; font.weight: Font.DemiBold; color: gb.hovered ? "#ffffff" : Theme.text }
                                Label { Layout.alignment: Qt.AlignHCenter; text: gb.next + "  ·  " + (gb.modelData.g + 1); font.pixelSize: 11; color: gb.hovered ? Qt.rgba(1, 1, 1, 0.85) : Theme.muted }
                            }
                        }
                    }
                }
                // Done (or nothing to study).
                ColumnLayout {
                    visible: !study.card
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 10
                    Item { Layout.fillHeight: true }
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 76; Layout.preferredHeight: 76
                        radius: 38
                        antialiasing: true
                        color: Theme.accentSoft
                        Icon { anchors.centerIn: parent; name: root.cards.length ? "check" : "cards"; size: 34; color: Theme.accent }
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.cards.length === 0 ? "No cards yet" : study.done > 0 ? "Well done! That's all for now" : "Nothing to study right now"
                        font.pixelSize: 18; font.weight: Font.DemiBold; color: Theme.text
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.maximumWidth: 420
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: root.cards.length === 0 ? "Select a question on a page and choose ⋯ → Make a flashcard; then select its answer, or type it."
                              : root.nextDue ? "The next card is due in " + root.whenText((root.nextDue.due - Date.now()) / 86400000) + "." : ""
                        font.pixelSize: 13; color: Theme.muted
                    }
                    UiButton {
                        visible: root.cards.length > 0
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 6
                        text: "Study all " + root.cards.length + " anyway"
                        onClicked: study.start(true)
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            // --- all cards
            Flickable {
                visible: panel.tab === "all"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: grid.implicitHeight + 36
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                Label {
                    visible: root.cards.length === 0
                    x: 18; y: 18
                    width: parent.width - 36
                    wrapMode: Text.Wrap
                    text: "No cards yet. Select a question on a page and choose ⋯ → Make a flashcard."
                    font.pixelSize: 13; color: Theme.muted
                }
                GridLayout {
                    id: grid
                    x: 18; y: 18
                    width: parent.width - 36
                    columns: 3
                    columnSpacing: 12
                    rowSpacing: 12
                    Repeater {
                        model: panel.opened && panel.tab === "all" ? root.cards : []
                        delegate: AbstractButton {
                            id: cc
                            required property var modelData
                            readonly property int pageIndex: root.canvas.pageIndexOf(modelData.front.page)
                            Layout.preferredWidth: Math.floor((grid.width - 24) / 3)
                            implicitHeight: 150
                            hoverEnabled: true
                            onClicked: { panel.close(); if (pageIndex >= 0) root.canvas.goToPage(pageIndex) }
                            background: Rectangle {
                                antialiasing: true
                                radius: Theme.radiusLg
                                color: Theme.dark ? "#202126" : "#ffffff"
                                border.color: cc.hovered ? Theme.accent : Theme.border
                            }
                            contentItem: Item {
                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    anchors.bottomMargin: 30
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: false
                                    source: root.picture(cc.modelData.front)
                                }
                                Label {
                                    anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 10
                                    text: (cc.pageIndex >= 0 ? "Page " + (cc.pageIndex + 1) + " · " : "") + root.dueText(cc.modelData)
                                    font.pixelSize: 11; color: Theme.muted
                                }
                                SmallTool {
                                    visible: cc.hovered
                                    anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 4
                                    iconName: "trash"
                                    tip: "Delete this card"
                                    onClicked: root.save(root.cards.filter(c => c.id !== cc.modelData.id))
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
