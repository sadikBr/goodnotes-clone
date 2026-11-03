// A bordered choice chip with a caret (src/styles.css .choice-chip / .value-chip).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

AbstractButton {
    id: root
    property string label
    property string unit: ""
    property Component art: null
    property bool open: false
    property string tip
    hoverEnabled: true
    implicitHeight: 34
    implicitWidth: row.implicitWidth + 19
    ToolTip.visible: hovered && tip.length > 0 && !open
    ToolTip.text: tip
    background: Rectangle { antialiasing: true;
        radius: 11
        color: Theme.surface
        border.color: root.hovered || root.open ? Theme.accent : Theme.border
    }
    contentItem: Item {
        RowLayout {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            x: root.art ? 6 : 11
            spacing: 5
            Loader { active: root.art !== null; sourceComponent: root.art; Layout.alignment: Qt.AlignVCenter }
            Label { text: root.label; font.weight: Font.DemiBold; color: Theme.text; font.pixelSize: 14; elide: Text.ElideRight; Layout.maximumWidth: 120 }
            Label { visible: root.unit.length > 0; text: root.unit; color: Theme.muted; font.pixelSize: 12 }
            Icon { name: "down"; size: 14; color: Theme.muted }
        }
    }
}
