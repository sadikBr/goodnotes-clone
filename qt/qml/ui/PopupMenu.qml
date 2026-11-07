// A small menu by a button (the web app's .popup-menu):
//   menu.show(button, [{ label, action, checked?, toggle?, danger?, key?, icon?, sub? }, ...])
// (sub: a short line under the label, saying what it does; toggle: true or
// false shows a switch, for a setting; { separator: true } is a line
// between groups; { header: "Whole notebook" } a small heading over one.)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    property var items: []
    property int menuWidth: 220
    /// The button it was opened from (to show that button as pressed).
    property Item anchorItem: null
    parent: Overlay.overlay
    padding: 6
    focus: true
    // Modal without dimming: a press outside only closes it (it doesn't draw on the
    // page or reopen it from its button), as in the web app.
    modal: true
    dim: false
    Overlay.modal: Item {}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    /// Opens below `anchor` (or above it when there's no room).
    function show(anchor, list, width) {
        items = list
        anchorItem = anchor
        const p = anchor.mapToItem(parent, 0, 0)
        const h = list.reduce((t, i) => t + (i.separator ? 9 : i.header ? 28 : i.sub ? 52 : 38), 0) + 12
        menuWidth = width || (list.some(i => !!i.sub) ? 280 : list.some(i => i.toggle !== undefined) ? 252 : list.some(i => !!i.image) ? 240 : 220)
        // On a phone: a sheet across the bottom of the screen, by your thumb.
        if (Theme.phone) {
            menuWidth = parent.width - 16
            x = 8
            y = Math.max(8, parent.height - h - 12)
            open()
            return
        }
        const below = p.y + anchor.height + 6 + h < parent.height
        x = Math.max(8, Math.min(parent.width - menuWidth - 8, p.x + anchor.width - menuWidth))
        y = below ? p.y + anchor.height + 6 : Math.max(8, p.y - 6 - h)
        open()
    }
    /// The same, to the right of `anchor` (for the side rail).
    function showBeside(anchor, list, width) {
        show(anchor, list, width)
        if (Theme.phone) return
        const p = anchor.mapToItem(parent, 0, 0)
        const h = list.reduce((t, i) => t + (i.separator ? 9 : i.header ? 28 : i.sub ? 52 : 38), 0) + 12
        x = Math.min(parent.width - menuWidth - 8, p.x + anchor.width + 10)
        y = Math.max(8, Math.min(parent.height - h - 8, p.y + anchor.height / 2 - h / 2))
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 140; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 80 } }

    background: Item {
        implicitWidth: root.menuWidth
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
    }
    contentItem: ColumnLayout {
        spacing: 0
        Repeater {
            model: root.items
            delegate: AbstractButton {
                id: it
                required property var modelData
                required property int index
                readonly property bool line: !!modelData.separator
                readonly property bool heading: !!modelData.header
                readonly property bool isToggle: modelData.toggle !== undefined
                Layout.fillWidth: true
                implicitHeight: line ? 9 : heading ? 28 : modelData.sub ? 52 : 38
                enabled: !line && !heading
                hoverEnabled: enabled
                focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                focus: index === 0
                Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus()
                Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus()
                Keys.onReturnPressed: clicked()
                onClicked: {
                    root.close()
                    if (modelData.action) Qt.callLater(modelData.action)
                }
                readonly property color fg: modelData.danger ? Theme.danger : modelData.checked ? Theme.accent : Theme.text
                background: Item {
                    // A line between groups.
                    Rectangle { visible: it.line; anchors.verticalCenter: parent.verticalCenter; x: 8; width: parent.width - 16; height: 1; color: Theme.border }
                    Label { visible: it.heading; text: it.modelData.header || ""; x: 12; anchors.bottom: parent.bottom; anchors.bottomMargin: 4; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.6; color: Theme.muted }
                    Rectangle { visible: !it.line && !it.heading; anchors.fill: parent; antialiasing: true; radius: Theme.radius; color: it.pressed ? Theme.surface3 : it.hovered || it.activeFocus ? Theme.surface2 : "transparent" }
                }
                contentItem: RowLayout {
                    visible: !it.line && !it.heading
                    spacing: 10
                    Item { Layout.preferredWidth: 2 }
                    // With a line under it: the icon in a soft square, like the create menu.
                    Rectangle {
                        visible: !!it.modelData.icon && !!it.modelData.sub
                        antialiasing: true
                        Layout.preferredWidth: 32; Layout.preferredHeight: 32; radius: Theme.radius
                        color: Theme.alpha(Theme.accent, 0.12)
                        Icon { anchors.centerIn: parent; name: it.modelData.icon || ""; size: 18; color: Theme.accent }
                    }
                    Icon { visible: !!it.modelData.icon && !it.modelData.sub; name: it.modelData.icon || ""; size: 18; color: it.modelData.danger ? Theme.danger : it.isToggle && it.modelData.toggle ? Theme.accent : Theme.muted }
                    Image { visible: !!it.modelData.image; source: it.modelData.image || ""; sourceSize: Qt.size(112, 44); Layout.preferredWidth: 56; Layout.preferredHeight: 22; cache: false }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label { text: "\u200E" + (it.modelData.label || ""); horizontalAlignment: Text.AlignLeft; color: it.fg; font.pixelSize: it.modelData.fontSize || 14; font.family: it.modelData.font || Theme.font; font.weight: it.modelData.checked || it.modelData.sub ? Font.DemiBold : Font.Normal; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { visible: !!it.modelData.sub; text: it.modelData.sub || ""; color: Theme.muted; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                    Icon { visible: !!it.modelData.checked; name: "check"; size: 16; color: Theme.accent }
                    // The key that does it too, as a key.
                    Rectangle {
                        visible: !!it.modelData.key
                        antialiasing: true
                        Layout.preferredHeight: 20
                        Layout.preferredWidth: Math.max(20, keyLabel.implicitWidth + 10)
                        radius: Theme.radiusXs
                        color: Theme.surface2
                        border.color: Theme.border
                        Label { id: keyLabel; anchors.centerIn: parent; text: it.modelData.key || ""; color: Theme.muted; font.pixelSize: 11; font.weight: Font.DemiBold }
                    }
                    // A setting: a small switch showing whether it's on.
                    Rectangle {
                        visible: it.isToggle
                        antialiasing: true
                        Layout.preferredWidth: 30; Layout.preferredHeight: 18; radius: Theme.radiusSm
                        color: it.modelData.toggle ? Theme.accent : Theme.surface3
                        border.color: it.modelData.toggle ? Theme.accent : Theme.border
                        Rectangle {
                            antialiasing: true
                            width: 14; height: 14; radius: Theme.radiusXs
                            y: 2
                            x: it.modelData.toggle ? parent.width - width - 2 : 2
                            color: "#ffffff"
                        }
                    }
                    Item { Layout.preferredWidth: 4 }
                }
            }
        }
    }
}
