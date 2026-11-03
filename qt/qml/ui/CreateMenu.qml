// The "New" menu: what can be created, each with a line saying what it is.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    /// [{ id, icon, title, sub, key?, tint?, action }]
    property var options: []
    parent: Overlay.overlay
    width: 340
    padding: 6
    focus: true
    // Modal without dimming: a press outside only closes it (it doesn't draw on the
    // page or reopen it from its button), as in the web app.
    modal: true
    dim: false
    Overlay.modal: Item {}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function show(anchor, list) {
        options = list
        const p = anchor.mapToItem(parent, 0, 0)
        const h = list.length * 60 + 12
        const below = p.y + anchor.height + 8 + h < parent.height
        x = Math.max(8, Math.min(parent.width - width - 8, p.x))
        y = below ? p.y + anchor.height + 8 : Math.max(8, p.y - 8 - h)
        open()
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 150; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 80 } }
    background: Item {
        Shadow { anchors.fill: parent; radius: 16; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: 16; color: Theme.surface; border.color: Theme.border }
    }
    contentItem: ColumnLayout {
        spacing: 0
        Repeater {
            model: root.options
            delegate: CreateItem {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                option: modelData
                focus: index === 0
                Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus()
                Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus()
                onClicked: {
                    root.close()
                    Qt.callLater(modelData.action)
                }
            }
        }
    }
}
