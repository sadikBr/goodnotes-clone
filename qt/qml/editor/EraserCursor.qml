// The eraser under the pointer, over the canvas: a ring the eraser's size
// (dark and light, so it shows on any paper) that pops in when the eraser is
// picked, presses in while rubbing out (filled, in the eraser's pink), and
// sheds crumbs that tumble away as you rub. A precise eraser's ring is solid;
// a whole-stroke eraser's is dashed.
import QtQuick
import QtQuick.Shapes
import Inkleaf

Item {
    id: root
    required property InkCanvas canvas
    enabled: false  // never takes the pointer

    readonly property bool on: (canvas.tool === "eraser" || canvas.rubbing) && canvas.pointer.x >= 0
    readonly property bool rubbing: canvas.rubbing
    readonly property real radius: Math.max(4, ((canvas.widths && canvas.widths.eraser) || 14) / 2 * canvas.zoom)
    readonly property color pink: "#f472b6"
    readonly property bool whole: canvas.eraserMode === "stroke"

    // ------------------------------------------------------------ the ring
    Item {
        id: ring
        x: root.canvas.pointer.x
        y: root.canvas.pointer.y
        visible: opacity > 0
        opacity: root.on ? 1 : 0
        scale: !root.on ? 0.4 : root.rubbing ? 0.86 : 1
        Behavior on opacity { NumberAnimation { duration: 140 } }
        Behavior on scale { SpringAnimation { spring: 5; damping: 0.32; epsilon: 0.005 } }

        // A soft fill: barely there at rest, the eraser's pink while rubbing.
        Rectangle {
            antialiasing: true
            anchors.centerIn: parent
            width: root.radius * 2; height: width; radius: width / 2
            color: root.rubbing ? Theme.alpha(root.pink, 0.22) : Qt.rgba(1, 1, 1, 0.10)
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        // Light outside, dark inside: visible on white paper and on dark covers.
        Rectangle {
            antialiasing: true
            anchors.centerIn: parent
            width: root.radius * 2 + 2; height: width; radius: width / 2
            color: "transparent"
            border.width: 1.5
            border.color: Qt.rgba(1, 1, 1, 0.85)
        }
        Shape {
            anchors.centerIn: parent
            width: root.radius * 2; height: width
            layer.enabled: root.on
            layer.samples: 4
            ShapePath {
                strokeWidth: root.rubbing ? 2 : 1.5
                strokeColor: root.rubbing ? Qt.darker(root.pink, 1.35) : Qt.rgba(0.15, 0.15, 0.17, 0.85)
                fillColor: "transparent"
                strokeStyle: root.whole ? ShapePath.DashLine : ShapePath.SolidLine
                dashPattern: [3, 2.5]
                PathAngleArc { centerX: root.radius; centerY: root.radius; radiusX: root.radius - 0.5; radiusY: root.radius - 0.5; startAngle: 0; sweepAngle: 360 }
            }
            // Whole-stroke erasers turn slowly, so the dashes read as "takes it all".
            RotationAnimation on rotation { running: root.on && root.whole; from: 0; to: 360; duration: 6000; loops: Animation.Infinite }
        }
        // A tiny centre dot while resting: where exactly it will erase.
        Rectangle {
            antialiasing: true
            anchors.centerIn: parent
            width: 3; height: 3; radius: 1.5
            visible: !root.rubbing && root.radius > 8
            color: Qt.rgba(0.15, 0.15, 0.17, 0.7)
        }
    }

    // ------------------------------------------------------------ crumbs
    // A small pool, reused: each crumb drops off the eraser's edge, drifts,
    // falls a little and fades.
    property int next: 0
    property point last: Qt.point(-1, -1)
    function shed() {
        const p = canvas.pointer
        if (last.x >= 0 && Math.hypot(p.x - last.x, p.y - last.y) < 7) return
        const dx = last.x >= 0 ? p.x - last.x : 0, dy = last.y >= 0 ? p.y - last.y : 0
        last = Qt.point(p.x, p.y)
        const n = 2
        for (let k = 0; k < n; ++k) {
            const c = crumbs.itemAt(next)
            next = (next + 1) % crumbs.count
            if (c) c.fly(p.x, p.y, dx, dy)
        }
    }
    Connections {
        target: root.canvas
        function onPointerChanged() {
            if (root.rubbing) root.shed()
            else root.last = Qt.point(-1, -1)
        }
    }
    Repeater {
        id: crumbs
        model: 28
        delegate: Rectangle {
            id: crumb
            antialiasing: true
            opacity: 0
            visible: opacity > 0
            property real vx: 0
            property real vy: 0
            property real size: 3
            width: size; height: size * (0.7 + Math.random() * 0.5); radius: size / 3
            color: Math.random() < 0.6 ? root.pink : Qt.rgba(0.55, 0.55, 0.58, 1)
            function fly(px, py, dx, dy) {
                anim.stop()
                // From the trailing edge of the eraser, thrown back the way it came.
                const a = Math.random() * Math.PI * 2
                const r = root.radius * (0.55 + Math.random() * 0.45)
                size = 2 + Math.random() * 3.2
                x = px + Math.cos(a) * r - size / 2
                y = py + Math.sin(a) * r - size / 2
                const len = Math.max(1, Math.hypot(dx, dy))
                vx = -dx / len * (6 + Math.random() * 14) + (Math.random() - 0.5) * 16
                vy = -dy / len * (6 + Math.random() * 14) + (Math.random() - 0.5) * 10
                rotation = Math.random() * 90
                opacity = 0.9
                anim.start()
            }
            ParallelAnimation {
                id: anim
                NumberAnimation { target: crumb; property: "x"; to: crumb.x + crumb.vx; duration: 520; easing.type: Easing.OutQuad }
                NumberAnimation { target: crumb; property: "y"; to: crumb.y + crumb.vy + 14; duration: 520; easing.type: Easing.InQuad }
                NumberAnimation { target: crumb; property: "opacity"; to: 0; duration: 520; easing.type: Easing.InQuad }
                NumberAnimation { target: crumb; property: "rotation"; to: crumb.rotation + 120; duration: 520 }
            }
        }
    }
}
