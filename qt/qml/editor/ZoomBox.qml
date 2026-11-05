// The zoom box: write large in a panel at the bottom, and it lands small on
// the page, in the frame shown there (drag it to move it, its side to make it
// smaller or larger). Ending a stroke in the panel's right-hand zone moves the
// frame along the line; at the end of the line it goes to the next one.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Item {
    id: root
    required property InkCanvas canvas
    property bool active: false
    visible: active
    readonly property real panelHeight: panel.height

    // The frame on the page (page units); its height follows the panel's shape.
    property int page: 0
    property real tx: 0
    property real ty: 0
    property real tw: 240
    readonly property real th: tw * view.height / Math.max(1, view.width)
    /// Where lines start (the frame goes back here for a new line).
    property real lineStart: 0
    readonly property size pageSz: active ? canvas.pageSize(page) : Qt.size(1, 1)
    readonly property real margin: 20
    /// How much larger the panel shows the page than the canvas does.
    readonly property real mag: view.width / Math.max(1, tw * canvas.zoom)

    // Where the frame is on screen (it follows zooming and scrolling).
    property rect frameView: Qt.rect(0, 0, 0, 0)
    function place() { if (active) frameView = canvas.pageRectToItem(page, Qt.rect(tx, ty, tw, th)) }
    onTxChanged: place()
    onTyChanged: place()
    onTwChanged: place()
    onThChanged: place()
    Connections { target: root.canvas; function onViewChanged() { root.place() } }

    function start() {
        page = Math.max(0, canvas.currentPage)
        const sz = canvas.pageSize(page)
        tw = Math.max(120, Math.min(300, sz.width * 0.4))
        // At the left margin, a little below the top of what's on screen.
        const pr = canvas.pageRectToItem(page, Qt.rect(0, 0, sz.width, sz.height))
        const topOnScreen = Math.max(0, (16 - pr.y) / canvas.zoom)
        lineStart = Math.round(sz.width * 0.08)
        tx = lineStart
        ty = Math.min(sz.height - 80, topOnScreen + 60)
        active = true
        place()
        keep()
    }
    function stop() { active = false }
    function clampFrame() {
        tw = Math.max(60, Math.min(pageSz.width - 2 * margin, tw))
        tx = Math.max(0, Math.min(pageSz.width - tw, tx))
        ty = Math.max(0, Math.min(pageSz.height - th, ty))
    }
    function keep() { canvas.ensureVisible(page, Qt.rect(tx, ty, tw, th)) }
    function moveBy(dx) {
        tx = Math.max(0, Math.min(pageSz.width - tw, tx + dx))
        keep()
    }
    /// Along the line; at its end, the next line.
    function advance() {
        if (tx + tw >= pageSz.width - margin - 1) return newLine()
        tx = Math.min(pageSz.width - margin - tw, tx + tw * 0.62)
        keep()
    }
    function newLine() {
        tx = Math.min(lineStart, pageSz.width - tw)
        if (ty + th * 1.8 <= pageSz.height) ty = ty + th * 0.85
        else if (page < canvas.pageCount - 1) { page = page + 1; ty = 40; canvas.goToPage(page) }
        keep()
    }

    // The frame on the page.
    Item {
        id: frame
        x: root.frameView.x; y: root.frameView.y
        width: root.frameView.width; height: root.frameView.height
        z: 2
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: Theme.radiusSm
            antialiasing: true
            color: Theme.alpha(Theme.accent, 0.07)
            border.width: 2
            border.color: Theme.accent
        }
        // The zone that moves it along, lightly.
        Rectangle {
            x: parent.width * 0.78; width: parent.width * 0.22; height: parent.height
            color: Theme.alpha(Theme.accent, 0.12)
        }
        // Drag it to move it.
        MouseArea {
            anchors.fill: parent
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property point from
            property real sx
            property real sy
            onPressed: m => { from = mapToItem(root, m.x, m.y); sx = root.tx; sy = root.ty }
            onPositionChanged: m => {
                const p = mapToItem(root, m.x, m.y)
                root.tx = sx + (p.x - from.x) / root.canvas.zoom
                root.ty = sy + (p.y - from.y) / root.canvas.zoom
                root.clampFrame()
            }
            onReleased: root.lineStart = root.tx
        }
        // Its side: smaller or larger (the panel shows it larger or smaller).
        Rectangle {
            x: parent.width - 4; y: parent.height / 2 - 14
            width: 8; height: 28
            radius: 4
            antialiasing: true
            color: Theme.accent
            border.width: 1.5
            border.color: "#ffffff"
            MouseArea {
                anchors.fill: parent
                anchors.margins: -10
                cursorShape: Qt.SizeHorCursor
                property real from
                property real sw
                onPressed: m => { from = mapToItem(root, m.x, m.y).x; sw = root.tw }
                onPositionChanged: m => { root.tw = sw + (mapToItem(root, m.x, m.y).x - from) / root.canvas.zoom; root.clampFrame() }
            }
        }
    }

    // The panel.
    Item {
        id: panel
        z: 3
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 12
        height: Math.max(150, Math.min(260, root.height * 0.32))
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 4 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
        // Nothing here reaches the page under it.
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: w => w.accepted = true }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8
            // Moving it, and its size.
            ColumnLayout {
                Layout.fillHeight: true
                spacing: 2
                SmallTool { iconName: "close"; tip: "Close the zoom box"; onClicked: root.stop() }
                Item { Layout.fillHeight: true }
                SmallTool { iconName: "back"; tip: "Move back"; onClicked: root.moveBy(-root.tw * 0.62) }
                SmallTool { iconName: "next"; tip: "Move on"; onClicked: root.advance() }
                SmallTool { iconName: "newline"; tip: "Next line"; onClicked: root.newLine() }
                Item { Layout.fillHeight: true }
            }
            Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: Theme.border }
            // The page, large: write here.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ZoomBoxView {
                    id: view
                    anchors.fill: parent
                    canvas: root.canvas
                    page: root.page
                    region: Qt.rect(root.tx, root.ty, root.tw, root.th)
                }
                // The zone that moves it along when a stroke ends in it.
                Rectangle {
                    x: parent.width * 0.78; width: parent.width * 0.22; height: parent.height
                    color: Theme.alpha(Theme.accent, zoneHover.hovered ? 0.14 : 0.08)
                    Rectangle { width: 1.5; height: parent.height; color: Theme.alpha(Theme.accent, 0.5) }
                    Icon { anchors.centerIn: parent; name: "next"; size: 22; color: Theme.alpha(Theme.accent, 0.55) }
                    HoverHandler { id: zoneHover }
                }
                // Writing: the pen with its pressure, or the mouse or a finger.
                function toPage(p) { return Qt.point(root.tx + p.x / width * root.tw, root.ty + p.y / height * root.th) }
                property bool writing: false
                property real lastX: 0
                function begin(p, pressure, stylus) {
                    const q = toPage(p)
                    writing = true
                    lastX = p.x
                    root.canvas.zoomBoxPress(root.page, q.x, q.y, pressure, stylus, root.mag)
                }
                function along(p, pressure, stylus) {
                    if (!writing) return
                    const q = toPage(p)
                    lastX = p.x
                    root.canvas.zoomBoxDrag(q.x, q.y, pressure, stylus)
                }
                function end() {
                    if (!writing) return
                    writing = false
                    root.canvas.zoomBoxRelease()
                    if (lastX > width * 0.78) root.advance()
                }
                PointHandler {
                    id: pen
                    acceptedDevices: PointerDevice.Stylus
                    onActiveChanged: active ? parent.begin(point.position, point.pressure, true) : parent.end()
                    onPointChanged: if (active) parent.along(point.position, point.pressure, true)
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !pen.active
                    cursorShape: Qt.CrossCursor
                    onPressed: m => { if (!pen.active) parent.begin(Qt.point(m.x, m.y), 0.5, false) }
                    onPositionChanged: m => { if (!pen.active) parent.along(Qt.point(m.x, m.y), 0.5, false) }
                    onReleased: if (!pen.active) parent.end()
                    onCanceled: parent.end()
                }
            }
        }
    }
}
