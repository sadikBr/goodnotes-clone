// A rounded well holding small tool buttons (src/styles.css .tool-group):
// surface-2, 4px padding, 2px between buttons.
import QtQuick
import QtQuick.Layouts
import Inkleaf

Rectangle {
    id: root
    default property alias content: row.data
    antialiasing: true
    property real corner: Theme.radiusLg
    radius: corner
    color: Theme.surface2
    implicitWidth: row.implicitWidth + 8
    implicitHeight: row.implicitHeight + 8
    RowLayout {
        id: row
        x: 4
        y: 4
        spacing: 2
    }
}
