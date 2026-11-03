// A soft shadow under a rounded box, like CSS box-shadow: blurred layers
// drawn as stretched nine-patch images (see src/ShadowImage.cpp).
import QtQuick
import Inkleaf

Item {
    id: root
    /// 0 none, 1 small (buttons, cards at rest), 2 raised (hover), 3 large (menus, dialogs).
    property int level: 1
    property real radius: 10
    // [y offset, blur, opacity] per layer (the web app's --shadow-sm/--shadow/--shadow-lg).
    readonly property var layers: level === 3 ? [[2, 6, 0.10], [18, 44, 0.20]] : level === 2 ? [[2, 4, 0.12], [14, 26, 0.20]] : level === 1 ? [[1, 2, 0.08], [1, 3, 0.06]] : []
    readonly property real strength: Theme.dark ? 2.2 : 1
    Repeater {
        model: root.layers
        BorderImage {
            required property var modelData
            readonly property int blur: modelData[1]
            readonly property int r: Math.round(Math.min(root.radius, 48))
            source: "image://shadow/" + r + "/" + blur
            border { left: r + blur * 2 + 1; right: r + blur * 2 + 1; top: r + blur * 2 + 1; bottom: r + blur * 2 + 1 }
            x: -blur * 2
            y: modelData[0] - blur * 2
            width: root.width + blur * 4
            height: root.height + blur * 4
            opacity: Math.min(1, modelData[2] * root.strength)
            smooth: true
        }
    }
}
