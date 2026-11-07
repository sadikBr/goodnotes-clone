// Presenting: a window for the projector or a second screen (full screen on
// it when there is one) with only the page you're on, as big as it fits,
// your ink as you write it and the laser pointer. The tools stay on your
// screen. Blank hides the page for a moment.
import QtQuick
import QtQuick.Window
import Inkleaf

Window {
    id: root
    required property InkCanvas canvas
    property bool blank: false
    readonly property int page: Math.max(0, canvas.currentPage)
    readonly property size pageSize: visible ? canvas.pageSize(page) : Qt.size(1, 1)
    readonly property bool second: Qt.application.screens.length > 1
    title: "Inkleaf · presenting"
    color: "#0b0b0d"
    width: 1280
    height: 800
    function start() {
        if (second) {
            screen = Qt.application.screens[1]
            x = screen.virtualX
            y = screen.virtualY
            visibility = Window.FullScreen
        } else {
            visibility = Window.Windowed
        }
        show()
    }
    function stop() { close() }

    // The page, as big as it fits.
    ZoomBoxView {
        id: view
        visible: !root.blank
        readonly property real k: Math.min(root.width / Math.max(1, root.pageSize.width), root.height / Math.max(1, root.pageSize.height))
        width: root.pageSize.width * k
        height: root.pageSize.height * k
        anchors.centerIn: parent
        canvas: root.canvas
        page: root.page
        region: Qt.rect(0, 0, root.pageSize.width, root.pageSize.height)
    }
    // The laser, over it.
    Canvas {
        id: laser
        anchors.fill: view
        visible: !root.blank
        property var info: ({})
        Connections {
            target: root.canvas
            function onLaserChanged() { laser.info = root.canvas.laserOnPage(root.page); laser.requestPaint() }
        }
        onPaint: {
            const g = getContext("2d")
            g.clearRect(0, 0, width, height)
            const d = info || {}
            const k = view.k
            const color = d.color || "#ff1f1f"
            g.lineCap = "round"; g.lineJoin = "round"
            for (const run of (d.runs || [])) {
                for (const [w, a] of [[14, 0.25], [6, 0.9]]) {
                    g.globalAlpha = a * (d.alpha === undefined ? 1 : d.alpha)
                    g.strokeStyle = color
                    g.lineWidth = w
                    g.beginPath()
                    g.moveTo(run[0] * k, run[1] * k)
                    for (let i = 2; i + 1 < run.length; i += 2) g.lineTo(run[i] * k, run[i + 1] * k)
                    g.stroke()
                }
            }
            if (d.dot) {
                g.globalAlpha = 0.3
                g.fillStyle = color
                g.beginPath(); g.arc(d.dot.x * k, d.dot.y * k, 16, 0, Math.PI * 2); g.fill()
                g.globalAlpha = 1
                g.beginPath(); g.arc(d.dot.x * k, d.dot.y * k, 7, 0, Math.PI * 2); g.fill()
                g.fillStyle = "#ffffff"
                g.beginPath(); g.arc(d.dot.x * k, d.dot.y * k, 2.5, 0, Math.PI * 2); g.fill()
            }
        }
    }
    // Esc leaves; a double click switches full screen.
    Shortcut { sequences: ["Esc", "F5"]; onActivated: root.close() }
    Shortcut { sequence: "."; onActivated: root.blank = !root.blank }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: idle.running ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: idle.restart()
        onDoubleClicked: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
        Timer { id: idle; interval: 2000 }
    }
}
