// One of the app's line icons (the same artwork as the web app), in any colour.
import QtQuick
import QtQuick.Layouts
import "IconData.js" as IconData

Image {
    id: root
    property string name
    property color color: Theme.text
    property int size: 20
    width: size
    height: size
    Layout.preferredWidth: size
    Layout.preferredHeight: size
    sourceSize: Qt.size(size * 2, size * 2)
    smooth: true
    source: {
        const body = IconData.paths[name] || ""
        const svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="48" height="48" fill="none" stroke="' + color + '" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">' + body + '</svg>'
        return "data:image/svg+xml;utf8," + encodeURIComponent(svg)
    }
}
