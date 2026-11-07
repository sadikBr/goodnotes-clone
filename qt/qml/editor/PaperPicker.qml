// "Paper for this page" (src/editor/editor.ts toggleTemplatePicker): papers in
// category tabs with previews, the paper colour, and applying it to all pages.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    required property InkCanvas canvas
    property int page: 0
    readonly property var meta: canvas.pages[page] || ({})
    readonly property string current: meta.template || "blank"
    readonly property string paper: meta.paperColor || "#fffdf7"
    property string category: "basic"
    parent: Overlay.overlay
    padding: 14
    // Five papers a row (three on a phone): the categories fit above them.
    readonly property int cols: Theme.phone ? 3 : 5
    width: cols * 96 + (cols - 1) * 10 + 28
    focus: true
    // Modal without dimming: a press outside only closes it (it doesn't draw on the
    // page or reopen it from its button), as in the web app.
    modal: true
    dim: false
    Overlay.modal: Item {}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property var categories: [{ id: "basic", label: "Basic", short: "Basic" }, { id: "notes", label: "Notes", short: "Notes" }, { id: "science", label: "Maths & science", short: "Maths" }, { id: "writing", label: "Writing & art", short: "Writing" }, { id: "planner", label: "Planner", short: "Planner" }, { id: "teacher", label: "Teacher", short: "Teacher" }, { id: "mine", label: "Your templates", short: "Yours" }]
    property var all: []
    /// Papers that come in colours: one card, with its colours as dots under it.
    readonly property var colourways: ({
        "daftar": [{ id: "daftar", color: "#5b2a5e", name: "Plum" }, { id: "daftar-blue", color: "#1f6fd1", name: "Blue" }, { id: "daftar-green", color: "#1f7a4a", name: "Green" },
                   { id: "daftar-pink", color: "#d1245f", name: "Pink" }, { id: "daftar-navy", color: "#2d2f72", name: "Navy" }, { id: "daftar-orange", color: "#e0612a", name: "Orange" }]
    })
    /// Your templates (pages you kept).
    property var mine: []
    function reloadMine() { try { mine = InkleafCore.call("pageTemplates") } catch (e) { mine = [] } }
    Component.onCompleted: all = Catalog.templates()
    onAboutToShow: reloadMine()
    FormDialog { id: ask }
    PopupMenu { id: menu }
    function keepPage() {
        ask.ask("Keep this page as a template", [{ name: "name", label: "Name", value: "My template" }], "Keep", r => {
            const err = root.canvas.savePageAsTemplate(root.page, r.name.trim())
            if (err) root.canvas.message(err, true)
            else { root.canvas.message("“" + r.name.trim() + "” is in your templates", false); root.reloadMine() }
        }, { message: "Everything on it, its paper and any PDF behind it, becomes paper you can use for new pages and notebooks. What you write on top stays erasable; the template doesn't." })
    }
    function useMine(t, allPages) {
        root.close()
        if (allPages) for (let i = 0; i < root.canvas.pages.length; ++i) root.canvas.usePageTemplate(i, t.id)
        else root.canvas.usePageTemplate(root.page, t.id)
    }

    function show(anchor, pageIndex) {
        page = pageIndex
        const t = all.find(t => t.id === current)
        category = t ? t.category : "basic"
        const p = anchor.mapToItem(parent, 0, 0)
        x = Math.max(8, Math.min(parent.width - width - 8, p.x + anchor.width - width))
        y = p.y + anchor.height + 8
        // On a phone (its button is in the ⋯ menu) it sits at the bottom, over the tools.
        if (Theme.phone) { x = (parent.width - width) / 2; y = Qt.binding(() => Math.max(8, parent.height - height - 12)) }
        open()
    }

    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
    }
    contentItem: ColumnLayout {
        spacing: 10
        Label { text: "Paper for this page"; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted }
        Segmented {
            Layout.fillWidth: true
            options: root.categories.map(c => ({ value: c.id, label: c.short }))
            value: root.category
            onPicked: v => root.category = v
        }
        // Your templates: keep this page, or use one you kept.
        GridLayout {
            visible: root.category === "mine"
            columns: root.cols
            columnSpacing: 10
            rowSpacing: 10
            AbstractButton {
                id: keepCard
                implicitWidth: 96
                implicitHeight: 136
                hoverEnabled: true
                onClicked: root.keepPage()
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: "Keep this page, as it is now, as paper to use again"
                background: Item {}
                contentItem: ColumnLayout {
                    spacing: 6
                    Item {
                        Layout.preferredWidth: 88
                        Layout.preferredHeight: 110
                        Layout.alignment: Qt.AlignHCenter
                        DashedBox { anchors.fill: parent; radius: Theme.radius; color: keepCard.hovered ? Theme.accent : Theme.borderStrong; fill: keepCard.hovered ? Theme.accentSoft : "transparent" }
                        Column {
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 30; height: 30; radius: Theme.radius
                                antialiasing: true
                                color: keepCard.hovered ? Theme.accent : Theme.surface2
                                Icon { anchors.centerIn: parent; name: "plus"; size: 16; color: keepCard.hovered ? Theme.accentContrast : Theme.muted }
                            }
                            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "This page"; font.pixelSize: 11; font.weight: Font.DemiBold; color: keepCard.hovered ? Theme.accent : Theme.muted }
                        }
                    }
                    Label { text: "Keep as template"; font.pixelSize: 12; color: keepCard.hovered ? Theme.accent : Theme.text; Layout.alignment: Qt.AlignHCenter }
                }
            }
            Repeater {
                model: root.category === "mine" ? root.mine : []
                delegate: AbstractButton {
                    id: tcard
                    required property var modelData
                    implicitWidth: 96
                    implicitHeight: 136
                    hoverEnabled: true
                    onClicked: root.useMine(modelData, false)
                    function more() {
                        menu.show(moreBtn, [
                            { icon: "page", label: "Use on this page", action: () => root.useMine(tcard.modelData, false) },
                            { icon: "plus", label: "New page from it", action: () => { root.close(); root.canvas.addTemplatePage(root.page + 1, tcard.modelData.id) } },
                            { icon: "stack", label: "Use on every page", action: () => root.useMine(tcard.modelData, true) },
                            { icon: "pen", label: "Rename", action: () => ask.ask("Rename template", [{ name: "name", label: "Name", value: tcard.modelData.name }], "Save", r => {
                                try { InkleafCore.call("renamePageTemplate", { id: tcard.modelData.id, name: r.name.trim() }); root.reloadMine() } catch (e) { root.canvas.message(String(e.message || e), true) }
                            }) },
                            { icon: "trash", label: "Delete", danger: true, action: () => ask.ask("Delete “" + tcard.modelData.name + "”?", [], "Delete", () => {
                                try { InkleafCore.call("deletePageTemplate", { id: tcard.modelData.id }); root.reloadMine() } catch (e) { root.canvas.message(String(e.message || e), true) }
                            }, { danger: true, message: "Pages already made from it keep it." }) }
                        ])
                    }
                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: tcard.more() }
                    onPressAndHold: more()
                    background: Item {}
                    contentItem: ColumnLayout {
                        spacing: 6
                        Item {
                            Layout.preferredWidth: 88
                            Layout.preferredHeight: 110
                            Layout.alignment: Qt.AlignHCenter
                            Rectangle { antialiasing: true; anchors.fill: parent; anchors.margins: -1; radius: Theme.radius; color: tcard.hovered ? Theme.borderStrong : Theme.border }
                            Rectangle { anchors.fill: parent; color: "#ffffff" }
                            Image {
                                anchors.fill: parent
                                source: tcard.modelData.preview || ""
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }
                            // ⋯: use it elsewhere, rename or delete it.
                            AbstractButton {
                                id: moreBtn
                                visible: tcard.hovered || menu.opened
                                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 4
                                width: 24; height: 24
                                hoverEnabled: true
                                onClicked: tcard.more()
                                background: Rectangle { antialiasing: true; radius: Theme.radiusSm; color: parent.hovered ? Theme.surface : Theme.alpha(Theme.surface, 0.9); border.color: Theme.border }
                                contentItem: Item { Icon { anchors.centerIn: parent; name: "more"; size: 14; color: Theme.text } }
                            }
                        }
                        Label { text: tcard.modelData.name; font.pixelSize: 12; color: Theme.text; Layout.alignment: Qt.AlignHCenter; elide: Text.ElideRight; Layout.maximumWidth: 92 }
                    }
                }
            }
        }
        Label {
            visible: root.category === "mine" && root.mine.length === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Make a page once (a form, a planner, a worksheet, a PDF page) and keep it here to use it again on any page or notebook."
            font.pixelSize: 12
            color: Theme.muted
        }
        GridLayout {
            visible: root.category !== "mine"
            columns: root.cols
            columnSpacing: 10
            rowSpacing: 10
            Repeater {
                model: root.all.filter(t => t.category === root.category && !t.hidden)
                delegate: AbstractButton {
                    id: card
                    required property var modelData
                    /// Its colours, if it has some (the lesson log).
                    readonly property var ways: root.colourways[modelData.id] || null
                    readonly property bool on: ways ? ways.some(w => w.id === root.current) : modelData.id === root.current
                    /// The one shown and used: the page's colour of it, or the first.
                    readonly property string shown: ways && on ? root.current : modelData.id
                    implicitWidth: 96
                    implicitHeight: 136
                    hoverEnabled: true
                    function use(id) { root.close(); if (id !== root.current) root.canvas.setPaper(root.page, id, "", false) }
                    onClicked: use(shown)
                    background: Item {}
                    contentItem: ColumnLayout {
                        spacing: 6
                        Item {
                            Layout.preferredWidth: 88
                            Layout.preferredHeight: 110
                            Layout.alignment: Qt.AlignHCenter
                            Rectangle { antialiasing: true; anchors.fill: parent; anchors.margins: card.on ? -3 : -1; radius: Theme.radius; color: card.on ? Theme.accent : card.hovered ? Theme.borderStrong : Theme.border }
                            Image {
                                anchors.fill: parent
                                sourceSize: Qt.size(176, 220)
                                source: "image://paper/" + card.shown + "/" + root.paper.replace("#", "") + "/" + (card.modelData.preview === "page" ? "whole" : "corner")
                                asynchronous: false
                                cache: true
                            }
                            // Its colours, on a pill at the bottom of the picture: tap one to use it.
                            Rectangle {
                                visible: !!card.ways
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 5
                                width: dotRow.implicitWidth + 10
                                height: 18
                                radius: 9
                                antialiasing: true
                                color: Theme.surface
                                border.color: Theme.border
                                Row {
                                    id: dotRow
                                    anchors.centerIn: parent
                                    spacing: 2
                                    Repeater {
                                        model: card.ways || []
                                        delegate: AbstractButton {
                                            id: dot
                                            required property var modelData
                                            readonly property bool picked: card.shown === modelData.id
                                            width: 12; height: 12
                                            hoverEnabled: true
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 400
                                            ToolTip.text: modelData.name
                                            onClicked: card.use(modelData.id)
                                            background: Item {
                                                Rectangle { visible: dot.picked; anchors.centerIn: parent; width: 12; height: 12; radius: 6; antialiasing: true; color: "transparent"; border.width: 1.5; border.color: dot.modelData.color }
                                                Rectangle { anchors.centerIn: parent; width: dot.picked ? 6 : dot.hovered ? 10 : 9; height: width; radius: width / 2; antialiasing: true; color: dot.modelData.color }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        Label { text: card.modelData.label; font.pixelSize: 12; color: card.on ? Theme.accent : Theme.text; font.weight: card.on ? Font.DemiBold : Font.Normal; Layout.alignment: Qt.AlignHCenter; elide: Text.ElideRight; Layout.maximumWidth: 92 }
                    }
                }
            }
        }
        Label { visible: root.category !== "mine"; text: "Paper colour"; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted; Layout.topMargin: 6 }
        Swatches {
            visible: root.category !== "mine"
            colors: Theme.paperColors
            value: root.paper
            onPicked: c => { root.close(); root.canvas.setPaper(root.page, "", c, false) }
        }
        UiButton {
            visible: root.category !== "mine"
            Layout.fillWidth: true
            text: "Apply this paper to all pages"
            onClicked: { root.close(); root.canvas.setPaper(root.page, root.current, root.paper, true) }
        }
    }
}
