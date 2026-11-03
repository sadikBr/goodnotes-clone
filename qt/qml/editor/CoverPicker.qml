// On a cover page, the paper button picks the cover design and colour
// (src/editor/editor.ts openCoverPicker).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    required property InkCanvas canvas
    required property string notebookId
    property string title
    readonly property var meta: canvas.pages[0] || ({})
    readonly property string design: meta.template === "cover" ? "classic" : String(meta.template || "cover").slice(6)
    readonly property string color: meta.paperColor || "#2563eb"
    readonly property var designs: Js.run("COVER_DESIGNS.slice", [0]) || []
    readonly property bool landscape: (meta.width || 794) > (meta.height || 1123)
    parent: Overlay.overlay
    padding: 14
    width: 420
    focus: true
    // Modal without dimming: a press outside only closes it (it doesn't draw on the
    // page or reopen it from its button), as in the web app.
    modal: true
    dim: false
    Overlay.modal: Item {}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function show(anchor) {
        const p = anchor.mapToItem(parent, 0, 0)
        x = Math.max(8, Math.min(parent.width - width - 8, p.x + anchor.width - width))
        y = p.y + anchor.height + 8
        open()
    }
    function apply(d, c) {
        close()
        canvas.flush()
        try {
            const nb = InkleafCore.call("notebook", { id: notebookId })
            Js.run("applyCover", [nb, d, c])
        } catch (e) {}
        canvas.reloadPages(true)
    }

    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
    }
    contentItem: ColumnLayout {
        spacing: 10
        Label { text: "Cover design"; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted }
        Flow {
            Layout.fillWidth: true
            spacing: 10
            Repeater {
                model: root.designs
                delegate: AbstractButton {
                    id: card
                    required property var modelData
                    readonly property bool on: modelData.id === root.design
                    width: 82; height: 136
                    hoverEnabled: true
                    onClicked: if (!on) root.apply(modelData.id, root.color)
                    background: Item {}
                    contentItem: Column {
                        spacing: 4
                        Item {
                            width: 82; height: 116
                            Rectangle { antialiasing: true; anchors.fill: parent; anchors.margins: card.on ? -3 : -1; radius: 6; color: card.on ? Theme.accent : card.hovered ? Theme.borderStrong : Theme.border }
                            Image {
                                anchors.fill: parent
                                fillMode: Image.PreserveAspectFit
                                sourceSize: Qt.size(164, 232)
                                source: root.visible ? "image://cover/" + card.modelData.id + "/" + root.color.replace("#", "") + "/" + (root.landscape ? 1 : 0) + "/" + encodeURIComponent(root.title || "Untitled") : ""
                            }
                        }
                        Label { width: 82; text: card.modelData.label; font.pixelSize: 11; color: card.on ? Theme.accent : Theme.muted; horizontalAlignment: Text.AlignHCenter }
                    }
                }
            }
        }
        Label { text: "Cover colour"; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted; Layout.topMargin: 4 }
        Swatches {
            colors: Theme.coverColors
            value: root.color
            onPicked: c => root.apply(root.design, c)
        }
    }
}
