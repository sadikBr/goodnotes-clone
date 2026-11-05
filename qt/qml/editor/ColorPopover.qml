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
        Swatches {
            Layout.fillWidth: true
            colors: root.palette
            value: root.value
            size: 26
            spacing: 6
            onPicked: c => { root.pick(c); root.close() }
        }
        // Saturation (across) and brightness (down) for the hue.
        Rectangle { antialiasing: true;
            id: sv
            Layout.fillWidth: true
            Layout.preferredHeight: 130
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
            Rectangle { antialiasing: true;
                x: root.sat * sv.width - 8; y: (1 - root.val) * sv.height - 8
                width: 16; height: 16; radius: Theme.radiusXs
                color: "transparent"; border.color: "#ffffff"; border.width: 2
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
            Layout.preferredHeight: 14
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
            Rectangle { antialiasing: true; x: root.hue * hueBar.width - 8; y: -2; width: 16; height: 18; radius: Theme.radiusXs; color: "transparent"; border.color: "#ffffff"; border.width: 2 }
            MouseArea {
                anchors.fill: parent
                function at(m) { root.hue = Math.max(0, Math.min(0.999, m.x / width)); root.fromHsv() }
                onPressed: m => at(m)
                onPositionChanged: m => at(m)
            }
        }
        RowLayout {
            spacing: 8
            Rectangle { antialiasing: true; Layout.preferredWidth: 28; Layout.preferredHeight: 28; radius: Theme.swatchRadius(28); color: root.value; border.color: Qt.rgba(0.5, 0.5, 0.5, 0.4) }
            UiField {
                Layout.fillWidth: true
                implicitHeight: 34
                text: root.value
                onAccepted: if (/^#[0-9a-f]{6}$/i.test(text)) { root.pick(text.toLowerCase()); root.close() }
            }
        }
    }
}
