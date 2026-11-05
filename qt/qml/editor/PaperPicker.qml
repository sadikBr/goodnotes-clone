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
    width: 452
    focus: true
    // Modal without dimming: a press outside only closes it (it doesn't draw on the
    // page or reopen it from its button), as in the web app.
    modal: true
    dim: false
    Overlay.modal: Item {}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property var categories: [{ id: "basic", label: "Basic", short: "Basic" }, { id: "notes", label: "Notes", short: "Notes" }, { id: "science", label: "Maths & science", short: "Maths" }, { id: "writing", label: "Writing & art", short: "Writing" }, { id: "planner", label: "Planner", short: "Planner" }, { id: "teacher", label: "Teacher", short: "Teacher" }]
    property var all: []
    Component.onCompleted: all = Catalog.templates()

    function show(anchor, pageIndex) {
        page = pageIndex
        const t = all.find(t => t.id === current)
        category = t ? t.category : "basic"
        const p = anchor.mapToItem(parent, 0, 0)
        x = Math.max(8, Math.min(parent.width - width - 8, p.x + anchor.width - width))
        y = p.y + anchor.height + 8
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
        GridLayout {
            columns: 4
            columnSpacing: 10
            rowSpacing: 10
            Repeater {
                model: root.all.filter(t => t.category === root.category && !t.hidden)
                delegate: AbstractButton {
                    id: card
                    required property var modelData
                    readonly property bool on: modelData.id === root.current
                    implicitWidth: 96
                    implicitHeight: 136
                    hoverEnabled: true
                    onClicked: { root.close(); if (!on) root.canvas.setPaper(root.page, modelData.id, "", false) }
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
                                source: "image://paper/" + card.modelData.id + "/" + root.paper.replace("#", "") + "/" + (card.modelData.preview === "page" ? "whole" : "corner")
                                asynchronous: false
                                cache: true
                            }
                        }
                        Label { text: card.modelData.label; font.pixelSize: 12; color: card.on ? Theme.accent : Theme.text; font.weight: card.on ? Font.DemiBold : Font.Normal; Layout.alignment: Qt.AlignHCenter; elide: Text.ElideRight; Layout.maximumWidth: 92 }
                    }
                }
            }
        }
        Label { text: "Paper colour"; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted; Layout.topMargin: 6 }
        Swatches {
            colors: Theme.paperColors
            value: root.paper
            onPicked: c => { root.close(); root.canvas.setPaper(root.page, "", c, false) }
        }
        UiButton {
            Layout.fillWidth: true
            text: "Apply this paper to all pages"
            onClicked: { root.close(); root.canvas.setPaper(root.page, root.current, root.paper, true) }
        }
    }
}
