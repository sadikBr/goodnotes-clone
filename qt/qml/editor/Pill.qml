// A rounded group of header buttons (src/styles.css .header-pill).
import QtQuick
import QtQuick.Layouts
import Inkleaf

Rectangle {
    default property alias content: row.data
    property int pad: 3
    implicitWidth: row.implicitWidth + pad * 2
    implicitHeight: row.implicitHeight + pad * 2
    radius: 13
    color: Theme.surface2
    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 2
    }
}
