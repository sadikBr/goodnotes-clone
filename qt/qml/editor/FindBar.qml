// Find in the notebook (Ctrl+F): a bar at the top of the pages with the
// matches' count and arrows, a list of them under it, and each match marked
// on its page (the current one strongest). Typed text, tables, PDF text and
// handwriting the AI has read.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Item {
    id: root
    required property InkCanvas canvas
    property bool active: false
    visible: active
    property var results: []
    property int current: -1
    /// The list of matches: open while you type, folded away once you go to one.
    property bool listShown: true
    /// Changes when the page moves under the marks.
    property int viewRev: 0
    Connections {
        target: root.canvas
        function onViewChanged() { root.viewRev++ }
        function onHistoryChanged() { if (root.active && field.text.trim()) research.restart() }
    }

    function open() {
        active = true
        field.forceActiveFocus()
        field.selectAll()
        if (field.text.trim()) run()
    }
    function close() {
        active = false
        results = []
        current = -1
        canvas.forceActiveFocus()
    }
    function run() {
        const q = field.text.trim()
        results = q ? canvas.find(q) : []
        // The first match from the page you're on.
        let k = results.findIndex(r => r.page >= canvas.currentPage)
        current = results.length ? (k < 0 ? 0 : k) : -1
        if (current >= 0) show(current)
    }
    Timer { id: typing; interval: 220; onTriggered: root.run() }
    Timer { id: research; interval: 400; onTriggered: { const c = root.current; root.results = root.canvas.find(field.text.trim()); root.current = Math.min(c, root.results.length - 1) } }
    function show(i) {
        if (i < 0 || i >= results.length) return
        current = i
        const r = results[i]
        if (r.w !== undefined) canvas.ensureVisible(r.page, Qt.rect(r.x, r.y, r.w, r.h))
        else canvas.goToPage(r.page)
        list.positionViewAtIndex(i, ListView.Contain)
    }
    function next() { if (results.length) { listShown = false; show((current + 1) % results.length) } }
    function prev() { if (results.length) { listShown = false; show((current - 1 + results.length) % results.length) } }

    // The marks on the pages.
    Repeater {
        model: root.active ? root.results : []
        delegate: Rectangle {
            required property var modelData
            required property int index
            readonly property bool here: index === root.current
            readonly property rect r: { void root.viewRev; return modelData.w !== undefined ? root.canvas.pageRectToItem(modelData.page, Qt.rect(modelData.x, modelData.y, modelData.w, modelData.h)) : Qt.rect(0, 0, 0, 0) }
            visible: modelData.w !== undefined && r.y + r.height > -20 && r.y < root.height + 20
            x: r.x - 3; y: r.y - 2
            width: r.width + 6; height: r.height + 4
            radius: Theme.radiusSm
            antialiasing: true
            color: here ? Qt.rgba(1, 0.78, 0.1, 0.42) : Qt.rgba(1, 0.85, 0.2, 0.22)
            border.width: here ? 2 : 0
            border.color: "#f59e0b"
        }
    }

    // The bar.
    Item {
        id: bar
        anchors.horizontalCenter: parent.horizontalCenter
        y: 12
        width: Math.min(parent.width - 24, 460)
        height: col.implicitHeight
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: w => w.accepted = true }
        ColumnLayout {
            id: col
            width: parent.width
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 6
                spacing: 4
                Icon { Layout.leftMargin: 8; name: "search"; size: 16; color: Theme.muted }
                TextField {
                    id: field
                    Layout.fillWidth: true
                    placeholderText: "Find in this notebook"
                    font.pixelSize: 14
                    color: Theme.text
                    placeholderTextColor: Theme.muted
                    selectByMouse: true
                    background: Item {}
                    onTextChanged: { root.listShown = true; typing.restart() }
                    Keys.onReturnPressed: e => {
                        if (typing.running) { typing.stop(); root.run(); return }
                        (e.modifiers & Qt.ShiftModifier) ? root.prev() : root.next()
                    }
                    Keys.onEnterPressed: root.next()
                    Keys.onEscapePressed: root.close()
                    Keys.onDownPressed: root.next()
                    Keys.onUpPressed: root.prev()
                }
                Label {
                    visible: field.text.trim().length > 0
                    text: root.results.length ? (root.current + 1) + " / " + root.results.length : "No matches"
                    font.pixelSize: 12
                    color: root.results.length ? Theme.muted : Theme.danger
                    Layout.rightMargin: 4
                }
                IconButton { iconName: "listNumber"; tip: root.listShown ? "Hide the list" : "Show all matches"; active: root.listShown; visible: root.results.length > 1; implicitWidth: 30; implicitHeight: 30; iconSize: 15; onClicked: root.listShown = !root.listShown }
                IconButton { iconName: "up"; tip: "Previous (Shift+Enter)"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; enabled: root.results.length > 0; onClicked: root.prev() }
                IconButton { iconName: "down"; tip: "Next (Enter)"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; enabled: root.results.length > 0; onClicked: root.next() }
                IconButton { iconName: "close"; tip: "Close (Esc)"; implicitWidth: 30; implicitHeight: 30; iconSize: 16; onClicked: root.close() }
            }
            // The matches.
            Rectangle { visible: root.results.length > 0 && root.listShown; Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            ListView {
                id: list
                visible: root.results.length > 0 && root.listShown
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 240)
                clip: true
                model: root.results
                ScrollBar.vertical: ScrollBar {}
                delegate: AbstractButton {
                    id: hit
                    required property var modelData
                    required property int index
                    readonly property bool here: index === root.current
                    width: list.width
                    implicitHeight: 38
                    hoverEnabled: true
                    onClicked: { root.listShown = false; root.show(index) }
                    background: Rectangle { color: hit.here ? Theme.accentSoft : hit.hovered ? Theme.surface2 : "transparent" }
                    contentItem: RowLayout {
                        spacing: 10
                        Rectangle {
                            Layout.leftMargin: 12
                            Layout.preferredWidth: pg.implicitWidth + 12
                            Layout.preferredHeight: 20
                            radius: Theme.radiusSm
                            antialiasing: true
                            color: hit.here ? Theme.alpha(Theme.accent, 0.16) : Theme.surface2
                            Label { id: pg; anchors.centerIn: parent; text: hit.modelData.page + 1; font.pixelSize: 11; font.weight: Font.DemiBold; color: hit.here ? Theme.accent : Theme.muted }
                        }
                        Icon {
                            name: hit.modelData.kind === "pdf" ? "pdf" : hit.modelData.kind === "handwriting" ? "pen" : "text"
                            size: 13
                            color: Theme.muted
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.rightMargin: 12
                            textFormat: Text.StyledText
                            elide: Text.ElideRight
                            font.pixelSize: 13
                            color: Theme.text
                            text: {
                                const s = String(hit.modelData.snippet || "")
                                const esc = t => t.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                                const a = hit.modelData.at || 0, n = hit.modelData.len || 0
                                return esc(s.slice(0, a)) + "<b>" + esc(s.slice(a, a + n)) + "</b>" + esc(s.slice(a + n))
                            }
                        }
                    }
                }
            }
        }
    }
}
