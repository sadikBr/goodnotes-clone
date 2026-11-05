// Cropping a picture on the page: the whole picture, the part kept in a frame
// with handles (drag inside it to move it), and a bar with the shape of the
// cut (rectangle, rounded, circle), the proportions, Reset, Cancel and Done.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import Inkleaf

Item {
    id: root
    required property InkCanvas canvas
    property bool active: false
    visible: active
    focus: active
    // The canvas's own frame and handles hide while this one shows.
    onActiveChanged: canvas.hideSelection = active

    // The picture when cropping started (page units).
    property var box: ({ x: 0, y: 0, w: 1, h: 1 })
    property int page: 0
    property real rot: 0
    property string url: ""
    property real naturalAspect: 1
    /// The whole picture, around the box (units from the box's corner).
    property var full: ({ x: 0, y: 0, w: 1, h: 1 })
    /// The part kept (same units).
    property var r: ({ x: 0, y: 0, w: 1, h: 1 })
    property string mask: "rect"
    /// Width ÷ height kept while resizing (0: free).
    property real ratio: 0
    readonly property var ratios: [{ v: 0, label: "Free" }, { v: 1, label: "Square" }, { v: 4 / 3, label: "4 : 3" }, { v: 3 / 4, label: "3 : 4" }, { v: 16 / 9, label: "16 : 9" }, { v: -1, label: "Original" }]

    // Where the box is on screen (it follows zooming and scrolling).
    property rect view: Qt.rect(0, 0, 1, 1)
    readonly property real k: view.width / Math.max(1, box.w)
    function place() { if (active) view = canvas.pageRectToItem(page, Qt.rect(box.x, box.y, box.w, box.h)) }
    Connections { target: root.canvas; function onViewChanged() { root.place() } }

    function start() {
        const im = canvas.selection.image
        if (!im) return
        url = canvas.selectedPictureUrl()
        if (!url) return
        page = canvas.selection.page
        box = { x: im.x, y: im.y, w: im.w, h: im.h }
        rot = im.rotation || 0
        naturalAspect = im.naturalW > 0 && im.naturalH > 0 ? im.naturalW / im.naturalH : im.w / im.h
        const c = im.crop
        const fw = im.w / c[2], fh = im.h / c[3]
        full = { x: -c[0] * fw, y: -c[1] * fh, w: fw, h: fh }
        r = { x: 0, y: 0, w: im.w, h: im.h }
        mask = im.mask || "rect"
        ratio = 0
        active = true
        place()
        forceActiveFocus()
    }
    function cancel() { active = false }
    function done() {
        if (!active) return
        // The new box: the kept part, turned with the picture about the old box's centre.
        const dx = r.x + r.w / 2 - box.w / 2, dy = r.y + r.h / 2 - box.h / 2
        const cs = Math.cos(rot), sn = Math.sin(rot)
        const cx = box.x + box.w / 2 + dx * cs - dy * sn, cy = box.y + box.h / 2 + dx * sn + dy * cs
        canvas.cropPicture({ x: cx - r.w / 2, y: cy - r.h / 2, w: r.w, h: r.h },
                           [(r.x - full.x) / full.w, (r.y - full.y) / full.h, r.w / full.w, r.h / full.h], mask)
        active = false
    }
    function reset() {
        // The whole picture, kept where it is.
        r = { x: full.x, y: full.y, w: full.w, h: full.h }
        ratio = 0
        mask = "rect"
    }
    /// Keeps `ratio` (width ÷ height), around the frame's centre, inside the picture.
    function applyRatio(v) {
        ratio = v
        const want = v < 0 ? full.w / full.h : v
        if (!want) return
        let w = r.w, h = r.w / want
        if (h > full.h) { h = full.h; w = h * want }
        if (w > full.w) { w = full.w; h = w / want }
        const cx = r.x + r.w / 2, cy = r.y + r.h / 2
        r = clampMove({ x: cx - w / 2, y: cy - h / 2, w: w, h: h })
    }
    function clampMove(q) {
        return { x: Math.max(full.x, Math.min(full.x + full.w - q.w, q.x)), y: Math.max(full.y, Math.min(full.y + full.h - q.h, q.y)), w: q.w, h: q.h }
    }
    readonly property real minSide: 12

    Keys.onEscapePressed: cancel()
    Keys.onReturnPressed: done()
    Keys.onEnterPressed: done()

    // Everything else dims; a tap on it finishes.
    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, Theme.dark ? 0.35 : 0.22) }
    MouseArea { anchors.fill: parent; onClicked: root.done(); onWheel: w => w.accepted = false }

    // The picture, turned as on the page.
    Item {
        id: stage
        x: root.view.x; y: root.view.y
        width: root.view.width; height: root.view.height
        rotation: root.rot * 180 / Math.PI
        readonly property real k: root.k

        Image {
            x: root.full.x * stage.k; y: root.full.y * stage.k
            width: root.full.w * stage.k; height: root.full.h * stage.k
            source: root.url
            sourceSize: Qt.size(2048, 2048)
            fillMode: Image.Stretch
            smooth: true
            mipmap: true
        }
        // What's cut away, dimmed (the picture, less the frame's shape).
        Shape {
            anchors.fill: parent
            antialiasing: true
            ShapePath {
                fillRule: ShapePath.OddEvenFill
                fillColor: Qt.rgba(0, 0, 0, 0.55)
                strokeWidth: -1
                PathSvg {
                    readonly property real k: stage.k
                    readonly property var f: root.full
                    readonly property var q: root.r
                    readonly property string outer: "M " + f.x * k + " " + f.y * k + " h " + f.w * k + " v " + f.h * k + " h " + (-f.w * k) + " Z "
                    readonly property string inner: {
                        const x = q.x * k, y = q.y * k, w = q.w * k, h = q.h * k
                        if (root.mask === "circle") {
                            const rx = w / 2, ry = h / 2
                            return "M " + x + " " + (y + ry) + " A " + rx + " " + ry + " 0 1 0 " + (x + w) + " " + (y + ry) + " A " + rx + " " + ry + " 0 1 0 " + x + " " + (y + ry) + " Z"
                        }
                        const c = root.mask === "rounded" ? Math.min(w, h) * 0.14 : 0
                        if (c <= 0) return "M " + x + " " + y + " h " + w + " v " + h + " h " + (-w) + " Z"
                        return "M " + (x + c) + " " + y + " H " + (x + w - c) + " A " + c + " " + c + " 0 0 1 " + (x + w) + " " + (y + c) + " V " + (y + h - c) +
                               " A " + c + " " + c + " 0 0 1 " + (x + w - c) + " " + (y + h) + " H " + (x + c) + " A " + c + " " + c + " 0 0 1 " + x + " " + (y + h - c) +
                               " V " + (y + c) + " A " + c + " " + c + " 0 0 1 " + (x + c) + " " + y + " Z"
                    }
                    path: outer + inner
                }
            }
        }

        // The frame: drag it to move what's kept; thirds while you do.
        Item {
            id: frame
            x: root.r.x * stage.k; y: root.r.y * stage.k
            width: root.r.w * stage.k; height: root.r.h * stage.k
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: 1.5
                border.color: "#ffffff"
                antialiasing: true
                radius: root.mask === "circle" ? Math.min(width, height) / 2 : root.mask === "rounded" ? Math.min(width, height) * 0.14 : 0
                opacity: root.mask === "rect" ? 1 : 0.55
            }
            Rectangle { visible: root.mask !== "rect"; anchors.fill: parent; color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.5) }
            Repeater {
                model: [1, 2]
                Rectangle { required property int modelData; visible: mover.pressed || frame.sizing; x: frame.width * modelData / 3; width: 1; height: frame.height; color: Qt.rgba(1, 1, 1, 0.6) }
            }
            Repeater {
                model: [1, 2]
                Rectangle { required property int modelData; visible: mover.pressed || frame.sizing; y: frame.height * modelData / 3; height: 1; width: frame.width; color: Qt.rgba(1, 1, 1, 0.6) }
            }
            property bool sizing: false
            MouseArea {
                id: mover
                anchors.fill: parent
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property point from
                property var start
                onPressed: m => { from = mapToItem(stage, m.x, m.y); start = root.r }
                onPositionChanged: m => {
                    const p = mapToItem(stage, m.x, m.y)
                    root.r = root.clampMove({ x: start.x + (p.x - from.x) / stage.k, y: start.y + (p.y - from.y) / stage.k, w: start.w, h: start.h })
                }
            }
        }

        // The handles: corners always, edges when the proportions are free.
        Repeater {
            model: [
                { sx: -1, sy: -1 }, { sx: 1, sy: -1 }, { sx: 1, sy: 1 }, { sx: -1, sy: 1 },
                { sx: 0, sy: -1, edge: true }, { sx: 1, sy: 0, edge: true }, { sx: 0, sy: 1, edge: true }, { sx: -1, sy: 0, edge: true }
            ]
            delegate: Item {
                id: h
                required property var modelData
                readonly property bool edge: !!modelData.edge
                visible: !edge || root.ratio === 0
                readonly property real cx: frame.x + frame.width * (modelData.sx + 1) / 2
                readonly property real cy: frame.y + frame.height * (modelData.sy + 1) / 2
                // Edges: a bar along the frame; corners: an L.
                readonly property bool across: modelData.sy !== 0 && modelData.sx === 0
                x: cx - 18; y: cy - 18
                width: 36; height: 36
                Rectangle {
                    visible: h.edge
                    anchors.centerIn: parent
                    width: h.across ? 24 : 6; height: h.across ? 6 : 24
                    radius: 3
                    color: "#ffffff"
                    border.width: 1
                    border.color: Qt.rgba(0, 0, 0, 0.35)
                    antialiasing: true
                }
                // A dark edge under the white, so it shows on any picture.
                Repeater {
                    model: [{ c: Qt.rgba(0, 0, 0, 0.35), w: 7 }, { c: "#ffffff", w: 4.5 }]
                    delegate: Shape {
                        required property var modelData
                        visible: !h.edge
                        anchors.fill: parent
                        antialiasing: true
                        ShapePath {
                            strokeColor: modelData.c
                            strokeWidth: modelData.w
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin
                            fillColor: "transparent"
                            // From the corner, along both sides of the frame.
                            startX: 18; startY: 18 - h.modelData.sy * 16
                            PathLine { x: 18; y: 18 }
                            PathLine { x: 18 - h.modelData.sx * 16; y: 18 }
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: h.edge ? (h.across ? Qt.SizeVerCursor : Qt.SizeHorCursor) : (h.modelData.sx === h.modelData.sy ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
                    property point from
                    property var start
                    onPressed: m => { from = mapToItem(stage, m.x, m.y); start = root.r; frame.sizing = true }
                    onReleased: frame.sizing = false
                    onCanceled: frame.sizing = false
                    onPositionChanged: m => {
                        const p = mapToItem(stage, m.x, m.y)
                        const dx = (p.x - from.x) / stage.k, dy = (p.y - from.y) / stage.k
                        const sx = h.modelData.sx, sy = h.modelData.sy, f = root.full, mn = root.minSide
                        let x0 = start.x, y0 = start.y, x1 = start.x + start.w, y1 = start.y + start.h
                        if (sx < 0) x0 = Math.max(f.x, Math.min(x1 - mn, x0 + dx))
                        if (sx > 0) x1 = Math.min(f.x + f.w, Math.max(x0 + mn, x1 + dx))
                        if (sy < 0) y0 = Math.max(f.y, Math.min(y1 - mn, y0 + dy))
                        if (sy > 0) y1 = Math.min(f.y + f.h, Math.max(y0 + mn, y1 + dy))
                        const want = root.ratio < 0 ? f.w / f.h : root.ratio
                        if (want > 0 && sx !== 0 && sy !== 0) {
                            // Keep the proportions: the larger change leads, the opposite corner stays.
                            let w = x1 - x0, hh = y1 - y0
                            if (w / hh > want) w = hh * want
                            else hh = w / want
                            // Within the picture from the fixed corner.
                            const maxW = sx > 0 ? f.x + f.w - x0 : x1 - f.x, maxH = sy > 0 ? f.y + f.h - y0 : y1 - f.y
                            if (w > maxW) { w = maxW; hh = w / want }
                            if (hh > maxH) { hh = maxH; w = hh * want }
                            if (sx > 0) x1 = x0 + w; else x0 = x1 - w
                            if (sy > 0) y1 = y0 + hh; else y0 = y1 - hh
                        }
                        root.r = { x: x0, y: y0, w: x1 - x0, h: y1 - y0 }
                    }
                }
            }
        }
    }

    // The bar: under the picture, or over it when there's no room.
    Item {
        id: bar
        readonly property real below: root.view.y + root.view.height + 22
        x: Math.max(8, Math.min(root.width - width - 8, root.view.x + root.view.width / 2 - width / 2))
        y: below + height < root.height - 8 ? below : Math.max(8, root.view.y - height - 22)
        width: barRow.implicitWidth + 16
        height: barRow.implicitHeight + 12
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
        MouseArea { anchors.fill: parent }
        PopupMenu { id: menu }
        RowLayout {
            id: barRow
            anchors.centerIn: parent
            spacing: 6
            Icon { name: "crop"; size: 16; color: Theme.accent; Layout.leftMargin: 6 }
            Label { text: "Crop"; font.weight: Font.DemiBold; font.pixelSize: 13; color: Theme.text; Layout.rightMargin: 4 }
            // The shape of the cut, each drawn.
            ToolGroup {
                Repeater {
                    model: [{ v: "rect", tip: "Rectangle" }, { v: "rounded", tip: "Rounded corners" }, { v: "circle", tip: "Circle" }]
                    delegate: AbstractButton {
                        id: mb
                        required property var modelData
                        readonly property bool on: root.mask === modelData.v
                        implicitWidth: 34; implicitHeight: 30
                        hoverEnabled: true
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: modelData.tip
                        scale: pressed ? 0.94 : 1
                        Behavior on scale { NumberAnimation { duration: 120 } }
                        onClicked: {
                            root.mask = modelData.v
                            // A circle is usually round.
                            if (modelData.v === "circle" && root.ratio === 0) root.applyRatio(1)
                        }
                        background: Item {
                            Rectangle { visible: mb.on; antialiasing: true; y: 1; width: parent.width; height: parent.height; radius: Theme.radiusSm; color: Qt.rgba(0, 0, 0, Theme.dark ? 0.3 : 0.08) }
                            Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusSm; color: mb.on ? Theme.surface : mb.hovered ? Theme.alpha(Theme.text, 0.06) : "transparent" }
                        }
                        contentItem: Item {
                            Rectangle {
                                anchors.centerIn: parent
                                width: 16; height: 13
                                antialiasing: true
                                radius: mb.modelData.v === "circle" ? 6.5 : mb.modelData.v === "rounded" ? 4 : 1
                                color: mb.on ? Theme.alpha(Theme.accent, 0.18) : "transparent"
                                border.width: 1.6
                                border.color: mb.on ? Theme.accent : mb.hovered ? Theme.text : Theme.muted
                            }
                        }
                    }
                }
            }
            // The proportions.
            AbstractButton {
                id: ratioBtn
                implicitHeight: 30
                implicitWidth: ratioRow.implicitWidth + 16
                hoverEnabled: true
                ToolTip.visible: hovered && !menu.opened
                ToolTip.delay: 500
                ToolTip.text: "Proportions"
                onClicked: menu.show(ratioBtn, root.ratios.map(o => ({ label: o.label, checked: root.ratio === o.v, action: () => root.applyRatio(o.v) })))
                background: Rectangle { antialiasing: true; radius: Theme.radius; color: Theme.surface; border.color: ratioBtn.hovered ? Theme.accent : Theme.border }
                contentItem: RowLayout {
                    id: ratioRow
                    spacing: 4
                    Label { text: (root.ratios.find(o => o.v === root.ratio) || root.ratios[0]).label; font.pixelSize: 13; color: Theme.text; Layout.leftMargin: 8 }
                    Icon { name: "down"; size: 13; color: Theme.muted; Layout.rightMargin: 4 }
                }
            }
            SmallTool { iconName: "reset"; tip: "Show the whole picture again"; onClicked: root.reset() }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.border }
            UiButton { kind: "ghost"; text: "Cancel"; implicitHeight: 30; onClicked: root.cancel() }
            UiButton { kind: "primary"; text: "Done"; implicitHeight: 30; onClicked: root.done() }
        }
    }
}
