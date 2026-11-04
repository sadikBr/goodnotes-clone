// An image whose source changes often (cover previews as you type or pick a
// colour): it keeps showing the last picture until the new one is ready,
// instead of going blank while it's drawn.
import QtQuick

Item {
    id: root
    property url source
    property size sourceSize
    property int fillMode: Image.Stretch

    Image {
        id: shown
        anchors.fill: parent
        fillMode: root.fillMode
        sourceSize: root.sourceSize
    }
    Image {
        id: next
        anchors.fill: parent
        visible: false
        fillMode: root.fillMode
        sourceSize: root.sourceSize
        source: root.source
        onStatusChanged: if (status === Image.Ready) shown.source = source
    }
    onSourceChanged: if (String(source) === "") shown.source = ""
}
