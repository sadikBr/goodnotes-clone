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
                // A quiet link (the contents page's rows): a small arrow beside it.
                readonly property bool compact: !!lk.link.compact
                x: compact ? Math.round(lk.modelData.x + lk.modelData.w + 6) : Math.round(lk.modelData.x + lk.modelData.w - width + 8)
                y: compact ? Math.round(lk.modelData.y + lk.modelData.h / 2 - height / 2) : Math.round(lk.modelData.y - height / 2 - 2)
                height: compact ? 22 : 24
                width: compact ? 22 : chipRow.implicitWidth + 14
                hoverEnabled: true
                scale: pressed ? 0.94 : hovered ? 1.04 : 1
                Behavior on scale { NumberAnimation { duration: 120 } }
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: lk.link.kind === "url" ? "Open " + lk.link.url : lk.link.kind === "notebook" ? "Open “" + (lk.link.title || "the notebook") + "”" : "Go to " + root.labelOf(lk.link).toLowerCase()
                onClicked: root.follow(lk.link, lk.modelData.page)
                background: Item {
                    Shadow { anchors.fill: parent; radius: chip.compact ? width / 2 : Theme.radius; level: chip.compact ? 1 : 2 }
                    Rectangle { antialiasing: true; anchors.fill: parent; radius: chip.compact ? width / 2 : Theme.radius; color: chip.hovered ? Theme.accent : Theme.surface; border.color: chip.hovered ? Theme.accent : Theme.border }
                }
                contentItem: Item {
                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 4
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: chip.compact ? "next" : root.iconOf(lk.link); size: chip.compact ? 12 : 13; color: chip.hovered ? Theme.accentContrast : Theme.accent }
                        Label {
                            visible: !chip.compact
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
