// A rounded box with a dashed outline.
import QtQuick
import QtQuick.Shapes

Shape {
    id: root
    property real radius: Theme.radiusLg
    property color color: "#999"
    property color fill: "transparent"
    property real lineWidth: 1.5
    ShapePath {
        strokeColor: root.color
        strokeWidth: root.lineWidth
        strokeStyle: ShapePath.DashLine
        dashPattern: [3, 2.5]
        fillColor: root.fill
        PathSvg {
            readonly property real i: root.lineWidth / 2
            readonly property real w: root.width - root.lineWidth
            readonly property real h: root.height - root.lineWidth
            readonly property real r: Math.min(root.radius, w / 2, h / 2)
            path: "M " + (i + r) + " " + i + " L " + (i + w - r) + " " + i + " A " + r + " " + r + " 0 0 1 " + (i + w) + " " + (i + r) + " L " + (i + w) + " " + (i + h - r) + " A " + r + " " + r + " 0 0 1 " + (i + w - r) + " " + (i + h) + " L " + (i + r) + " " + (i + h) + " A " + r + " " + r + " 0 0 1 " + i + " " + (i + h - r) + " L " + i + " " + (i + r) + " A " + r + " " + r + " 0 0 1 " + (i + r) + " " + i + " Z"
        }
    }
}
