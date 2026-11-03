// A drop-down choice, styled like the dialogs' fields. Options are
// [{ value, label, group? }]; consecutive options with a group get a heading.
import QtQuick
import QtQuick.Controls
import Inkleaf

ComboBox {
    id: root
    property var options: []
    property var value
    signal picked(var value)

    // Options with group headings slotted in (headings can't be chosen).
    readonly property var rows: {
        const out = []
        let last = null
        for (const o of options) {
            if (o.group && o.group !== last) out.push({ heading: true, label: o.group })
            last = o.group || null
            out.push(o)
        }
        return out
    }
    model: rows
    textRole: "label"
    currentIndex: rows.findIndex(r => !r.heading && r.value === value)
    onActivated: i => { if (!rows[i].heading) root.picked(rows[i].value) }
    implicitHeight: 40
    font.pixelSize: 14
    leftPadding: 12

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface2
        border.width: root.activeFocus || root.popup.visible ? 2 : 1
        border.color: root.activeFocus || root.popup.visible ? Theme.accent : (root.hovered ? Theme.borderStrong : Theme.border)
    }
    contentItem: Label {
        leftPadding: 0
        text: root.displayText
        color: Theme.text
        font: root.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        x: root.width - width - 12
        y: (root.height - height) / 2
        name: "down"; size: 16; color: Theme.muted
    }
    delegate: ItemDelegate {
        id: d
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : 200
        height: modelData.heading ? 30 : 34
        enabled: !modelData.heading
        hoverEnabled: true
        highlighted: root.highlightedIndex === index
        background: Rectangle { radius: 8; color: !d.modelData.heading && (d.highlighted || d.hovered) ? Theme.surface2 : "transparent" }
        contentItem: Label {
            text: d.modelData.label
            leftPadding: d.modelData.heading ? 4 : 8
            color: d.modelData.heading ? Theme.muted : (root.currentIndex === d.index ? Theme.accent : Theme.text)
            font.pixelSize: d.modelData.heading ? 11 : 14
            font.weight: d.modelData.heading || root.currentIndex === d.index ? Font.DemiBold : Font.Normal
            font.capitalization: d.modelData.heading ? Font.AllUppercase : Font.MixedCase
            font.letterSpacing: d.modelData.heading ? 0.6 : 0
            verticalAlignment: Text.AlignVCenter
        }
    }
    popup: Popup {
        y: root.height + 4
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 360)
        padding: 6
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Item {
            Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
            Rectangle { anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
        }
    }
}
