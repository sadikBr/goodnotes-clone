// The links on the page: a small chip at each linked thing's corner (where it
// goes on it); tap it to follow the link. Its outline shows while you point at it.
import QtQuick
import QtQuick.Controls
import Inkleaf

Item {
    id: root
    required property InkCanvas canvas
    /// Follow `link` (from page `page`).
    signal follow(var link, int page)
    property var links: []
    function refresh() { links = visible ? canvas.visibleLinks() : [] }
    Timer { id: soon; interval: 30; onTriggered: root.refresh() }
    Connections {
        target: root.canvas
        function onViewChanged() { soon.restart() }
        function onHistoryChanged() { soon.restart() }
        function onPagesChanged() { soon.restart() }
    }
    onVisibleChanged: soon.restart()
    Component.onCompleted: soon.restart()

    function labelOf(l) {
        if (l.kind === "page") { const i = canvas.pageIndexOf(l.page); return i < 0 ? "Page gone" : "Page " + (i + 1) }
        if (l.kind === "notebook") return l.title || "Notebook"
        if (l.kind === "url") return String(l.url || "").replace(/^[a-z]+:\/\//i, "").replace(/^www\./, "").replace(/\/.*$/, "")
        return "Link"
    }
    function iconOf(l) { return l.kind === "url" ? "external" : l.kind === "notebook" ? "book" : "link" }

    Repeater {
        model: root.links
        delegate: Item {
            id: lk
            required property var modelData
            readonly property var link: modelData.link
            // The linked things, outlined while the chip is pointed at.
            Rectangle {
                x: lk.modelData.x - 4; y: lk.modelData.y - 4
                width: lk.modelData.w + 8; height: lk.modelData.h + 8
                radius: Theme.radius
                antialiasing: true
                color: Theme.alpha(Theme.accent, 0.06)
                border.width: 1.5
                border.color: Theme.alpha(Theme.accent, 0.7)
                opacity: chip.hovered ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 140 } }
            }
            AbstractButton {
                id: chip
                x: Math.round(lk.modelData.x + lk.modelData.w - width + 8)
                y: Math.round(lk.modelData.y - height / 2 - 2)
                height: 24
                width: chipRow.implicitWidth + 14
                hoverEnabled: true
                scale: pressed ? 0.94 : hovered ? 1.04 : 1
                Behavior on scale { NumberAnimation { duration: 120 } }
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: lk.link.kind === "url" ? "Open " + lk.link.url : lk.link.kind === "notebook" ? "Open “" + (lk.link.title || "the notebook") + "”" : "Go to " + root.labelOf(lk.link).toLowerCase()
                onClicked: root.follow(lk.link, lk.modelData.page)
                background: Item {
                    Shadow { anchors.fill: parent; radius: Theme.radius; level: 2 }
                    Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radius; color: chip.hovered ? Theme.accent : Theme.surface; border.color: chip.hovered ? Theme.accent : Theme.border }
                }
                contentItem: Item {
                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 4
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: root.iconOf(lk.link); size: 13; color: chip.hovered ? Theme.accentContrast : Theme.accent }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.labelOf(lk.link)
                            width: Math.min(implicitWidth, 140)
                            elide: Text.ElideRight
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            color: chip.hovered ? Theme.accentContrast : Theme.text
                        }
                    }
                }
            }
        }
    }
}
