// The drawing instrument over the page (src/editor/ruler.ts): ruler,
// protractor or set square, drawn from the web code's shapes. Drag the body to
// move it, a round grip to turn it (snapping to common angles; Shift for 15°
// steps), the protractor's knobs to measure an angle; the wheel turns it too.
import QtQuick
import QtQuick.Controls
import Inkleaf

Item {
    id: root
    required property InkCanvas canvas
    readonly property var ins: canvas.instrument
    readonly property bool shown: !!ins.visible
    readonly property real zm: canvas.zoom
    visible: shown
    // The pivot on screen (follows scrolling and zoom).
    property point pivot: Qt.point(0, 0)
    function place() {
        if (!shown) return
        const r = canvas.pageRectToItem(ins.page, Qt.rect(ins.cx, ins.cy, 0, 0))
        pivot = Qt.point(r.x, r.y)
    }
    Connections {
        target: root.canvas
        function onViewChanged() { root.place() }
        function onInstrumentChanged() { root.place() }
    }

    function local(px, py) {
        // Item point → the instrument's local units.
        const p = canvas.itemToPage(Qt.point(px, py))
        const c = Math.cos(ins.angle), s = Math.sin(ins.angle)
        const dx = p.x - ins.cx, dy = p.y - ins.cy
        return Qt.point(dx * c + dy * s, -dx * s + dy * c)
    }
    function snap(rad, coarse) { return Js.run("snapAngle", [rad, coarse]) }

    Item {
        id: body
        x: root.pivot.x
        y: root.pivot.y
        rotation: (root.ins.angle || 0) * 180 / Math.PI
        Image {
            x: (root.ins.box ? root.ins.box.x : 0) * root.zm
            y: (root.ins.box ? root.ins.box.y : 0) * root.zm
            width: (root.ins.box ? root.ins.box.w : 0) * root.zm
            height: (root.ins.box ? root.ins.box.h : 0) * root.zm
            sourceSize: Qt.size(Math.min(4096, width * 2), Math.min(4096, height * 2))
            source: root.shown ? "data:image/svg+xml;utf8," + encodeURIComponent(root.ins.svg) : ""
            smooth: true
        }
        // Rotation grips.
        Repeater {
            model: root.ins.grips || []
            delegate: Rectangle {
                id: grip
                required property var modelData
                x: modelData[0] * root.zm - 11
                y: modelData[1] * root.zm - 11
                width: 22; height: 22; radius: 11
                color: Qt.rgba(1, 1, 1, 0.9)
                border.width: 1.5
                border.color: Qt.rgba(15 / 255, 23 / 255, 42 / 255, 0.45)
                Rectangle { anchors.centerIn: parent; width: 10; height: 10; radius: 5; color: "transparent"; border.width: 2; border.color: Qt.rgba(15 / 255, 23 / 255, 42 / 255, 0.18) }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.CrossCursor
                    property real a0: 0
                    property real angle0: 0
                    function angleAt(m) {
                        const p = canvas.itemToPage(mapToItem(root, m.x, m.y))
                        return Math.atan2(p.y - root.ins.cy, p.x - root.ins.cx)
                    }
                    onPressed: m => { a0 = angleAt(m); angle0 = root.ins.angle }
                    onPositionChanged: m => root.canvas.placeInstrument(root.ins.cx, root.ins.cy, root.snap(angle0 + angleAt(m) - a0, !!(m.modifiers & Qt.ShiftModifier)))
                }
            }
        }
        // Protractor arm knobs.
        Repeater {
            model: root.ins.knobs || []
            delegate: Rectangle {
                required property var modelData
                required property int index
                x: modelData[0] * root.zm - 9
                y: modelData[1] * root.zm - 9
                width: 18; height: 18; radius: 9
                color: "#db2777"
                border.width: 2
                border.color: "#ffffff"
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onPositionChanged: m => {
                        const q = root.local(mapToItem(root, m.x, m.y).x, mapToItem(root, m.x, m.y).y)
                        let deg = Math.atan2(-q.y, q.x) * 180 / Math.PI
                        if (deg < -90) deg = 180
                        deg = Math.min(180, Math.max(0, deg))
                        root.canvas.setInstrumentArm(parent.index, (m.modifiers & Qt.ShiftModifier) ? Math.round(deg / 5) * 5 : Math.round(deg))
                    }
                }
            }
        }
    }

    // Dragging the body moves it; presses off the body reach the page.
    MouseArea {
        anchors.fill: parent
        z: -1
        hoverEnabled: false
        property point start
        property point c0
        property bool dragging: false
        onPressed: m => {
            const p = root.canvas.itemToPage(Qt.point(m.x, m.y))
            if (!root.shown || !root.canvas.instrumentContains(p.x, p.y)) { m.accepted = false; return }
            start = p
            c0 = Qt.point(root.ins.cx, root.ins.cy)
            dragging = true
            cursorShape = Qt.ClosedHandCursor
        }
        onPositionChanged: m => {
            if (!dragging) return
            const p = root.canvas.itemToPage(Qt.point(m.x, m.y))
            root.canvas.placeInstrument(c0.x + p.x - start.x, c0.y + p.y - start.y, root.ins.angle)
        }
        onReleased: { dragging = false; cursorShape = Qt.ArrowCursor }
        onWheel: w => {
            const p = root.canvas.itemToPage(Qt.point(w.x, w.y))
            if (!root.shown || (w.modifiers & Qt.ControlModifier) || !root.canvas.instrumentContains(p.x, p.y)) { w.accepted = false; return }
            const step = ((w.modifiers & Qt.ShiftModifier) ? 15 : 1) * Math.PI / 180
            const deg = Math.round((root.ins.angle + Math.sign(w.angleDelta.y || w.angleDelta.x) * step) * 180 / Math.PI)
            root.canvas.placeInstrument(root.ins.cx, root.ins.cy, deg * Math.PI / 180)
        }
    }

    // The angle (unrotated), and the length or angle being drawn.
    component Pill: Rectangle {
        property string text
        property real lx: 0
        property real ly: 0
        readonly property real c: Math.cos(root.ins.angle || 0)
        readonly property real s: Math.sin(root.ins.angle || 0)
        x: root.pivot.x + (lx * c - ly * s) * root.zm - width / 2
        y: root.pivot.y + (lx * s + ly * c) * root.zm - height / 2
        width: pillText.implicitWidth + 16
        height: 18
        radius: 9
        Label { id: pillText; anchors.centerIn: parent; text: parent.text; color: "#ffffff"; font.pixelSize: 12; font.weight: Font.DemiBold }
    }
    Pill {
        text: root.ins.label || ""
        lx: root.ins.labelAt ? root.ins.labelAt[0] : 0
        ly: root.ins.labelAt ? root.ins.labelAt[1] : 0
        color: Qt.rgba(15 / 255, 23 / 255, 42 / 255, 0.78)
    }
    Pill {
        visible: !!root.ins.readout
        text: root.ins.readout || ""
        lx: root.ins.labelAt ? root.ins.labelAt[0] : 0
        ly: root.ins.labelAt ? root.ins.labelAt[1] + 7 * 96 / 25.4 : 0
        color: Theme.accent
    }
}
