// The colour popover (src/colorPicker.ts): the tool's palette, and any colour
// by hue, saturation and brightness, or by hex code.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    property var palette: []
    property string value: "#111827"
    property var onPick: null
    parent: Overlay.overlay
    padding: 12
    width: 260
    focus: true
    // Modal without dimming: a press outside only closes it (it doesn't draw on the
    // page or reopen it from its button), as in the web app.
    modal: true
    dim: false
    Overlay.modal: Item {}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function show(anchor, colors, current, cb) {
        palette = colors
        value = current
        onPick = cb
        const c = Qt.color(current)
        hue = Math.max(0, c.hsvHue)
        sat = c.hsvSaturation
        val = c.hsvValue
        const p = anchor.mapToItem(parent, 0, 0)
        x = Math.max(8, Math.min(parent.width - width - 8, p.x + anchor.width / 2 - width / 2))
        y = p.y + anchor.height + 10
        open()
    }
    function pick(c) {
        value = c
        if (onPick) onPick(c)
    }
    property real hue: 0
    property real sat: 0
    property real val: 0
    function fromHsv() { pick(Qt.hsva(hue, sat, val, 1).toString()) }

    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
    }
    contentItem: ColumnLayout {
        spacing: 12
        // Seven to a row, filling the width.
        Swatches {
            id: sw
            Layout.fillWidth: true
            colors: root.palette
            value: root.value
            size: 28
            spacing: Math.max(4, (width - 7 * size) / 6)
            onPicked: c => { root.pick(c); root.close() }
        }
        // Saturation (across) and brightness (down) for the hue.
        Rectangle { antialiasing: true;
            id: sv
            Layout.fillWidth: true
            Layout.preferredHeight: 136
            radius: Theme.radius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#ffffff" }
                GradientStop { position: 1; color: Qt.hsva(root.hue, 1, 1, 1) }
            }
            Rectangle { antialiasing: true;
                anchors.fill: parent
                radius: Theme.radius
                gradient: Gradient {
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 1; color: "#000000" }
                }
            }
            // The knob: white, with a dark edge, so it shows on any colour.
            Rectangle { antialiasing: true;
                x: root.sat * sv.width - 9; y: (1 - root.val) * sv.height - 9
                width: 18; height: 18; radius: Theme.radiusXs + 1
                color: "transparent"; border.color: Qt.rgba(0, 0, 0, 0.45); border.width: 1
                Rectangle { antialiasing: true; anchors.fill: parent; anchors.margins: 1; radius: Theme.radiusXs; color: "transparent"; border.color: "#ffffff"; border.width: 2.5 }
            }
            MouseArea {
                anchors.fill: parent
                function at(m) {
                    root.sat = Math.max(0, Math.min(1, m.x / width))
                    root.val = 1 - Math.max(0, Math.min(1, m.y / height))
                    root.fromHsv()
                }
                onPressed: m => at(m)
                onPositionChanged: m => at(m)
            }
        }
        // Hue.
        Rectangle { antialiasing: true;
            id: hueBar
            Layout.fillWidth: true
            Layout.preferredHeight: 18
            radius: Theme.radiusSm
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#ff0000" }
                GradientStop { position: 0.17; color: "#ffff00" }
                GradientStop { position: 0.33; color: "#00ff00" }
                GradientStop { position: 0.5; color: "#00ffff" }
                GradientStop { position: 0.67; color: "#0000ff" }
                GradientStop { position: 0.83; color: "#ff00ff" }
                GradientStop { position: 1.0; color: "#ff0000" }
            }
            Rectangle {
                antialiasing: true
                x: Math.max(-2, Math.min(hueBar.width - width + 2, root.hue * hueBar.width - width / 2)); y: -3
                width: 14; height: hueBar.height + 6; radius: Theme.radiusXs
                color: Qt.hsva(root.hue, 1, 1, 1)
                border.color: Qt.rgba(0, 0, 0, 0.45); border.width: 1
                Rectangle { antialiasing: true; anchors.fill: parent; anchors.margins: 1; radius: Theme.radiusXs - 1; color: "transparent"; border.color: "#ffffff"; border.width: 2.5 }
            }
            MouseArea {
                anchors.fill: parent
                function at(m) { root.hue = Math.max(0, Math.min(0.999, m.x / width)); root.fromHsv() }
                onPressed: m => at(m)
                onPositionChanged: m => at(m)
            }
        }
        RowLayout {
            spacing: 8
            Rectangle { antialiasing: true; Layout.preferredWidth: 34; Layout.preferredHeight: 34; radius: Theme.radius; color: root.value; border.color: Theme.dark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0, 0, 0, 0.14) }
            UiField {
                Layout.fillWidth: true
                implicitHeight: 34
                text: root.value
                font.family: "monospace"
                // "fbee07" or "#FBEE07": both work.
                onAccepted: { const t = text.trim().replace(/^#?/, "#"); if (/^#[0-9a-f]{6}$/i.test(t)) { root.pick(t.toLowerCase()); root.close() } }
            }
        }
    }
}
