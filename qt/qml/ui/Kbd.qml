// A key cap (the web app's kbd): bordered, a thicker bottom edge.
import QtQuick
import QtQuick.Controls
import Inkleaf

Item {
    id: root
    property string text
    implicitWidth: Math.max(18, Math.ceil(lbl.implicitWidth) + 10)
    implicitHeight: 20
    Rectangle { antialiasing: true; anchors.fill: parent; radius: 5; color: Theme.borderStrong }
    Rectangle { antialiasing: true; x: 1; y: 1; width: parent.width - 2; height: parent.height - 3; radius: 4; color: Theme.surface }
    Label { id: lbl; anchors.centerIn: parent; anchors.verticalCenterOffset: -1; text: root.text; font.pixelSize: 11; font.weight: Font.DemiBold; color: Theme.text }
}
