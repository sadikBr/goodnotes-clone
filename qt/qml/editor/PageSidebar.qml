// The page thumbnails (src/editor/editor.ts renderPageList): go to a page,
// its ⋯ menu (add after, duplicate, move, bookmark, delete), Add page.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Rectangle {
    id: root
    required property InkCanvas canvas
    /// Set for a daftar: its pages are grouped by class (page.section).
    property bool daftar: false
    signal addDaftarPage()
    color: Theme.surface
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
    clip: true

    PopupMenu { id: menu }
    FormDialog { id: ask }
    /// "pages", "bookmarks" (only the bookmarked pages) or "contents".
    property string mode: "pages"
    readonly property bool onlyBookmarks: mode === "bookmarks"
    readonly property bool showContents: mode === "contents"
    property var contents: []
    function reloadContents() { contents = showContents ? root.canvas.contents() : [] }
    onShowContentsChanged: reloadContents()
    Connections {
        target: root.canvas
        function onPagesChanged() { root.reloadContents() }
        function onNotebookSettingsChanged() { root.reloadContents() }
    }
    function askHeading(index) {
        ask.ask("Add to contents", [{ name: "title", label: "Heading for page " + (index + 1), value: "" },
                                    { name: "level", label: "Kind", type: "segmented", value: 0, options: [{ value: 0, label: "Heading" }, { value: 1, label: "Sub-heading" }] }], "Add", r => {
            if (r.title.trim()) { root.canvas.addHeading(index, r.title.trim(), r.level); root.mode = "contents" }
        }, { validate: v => v.title.trim() ? null : "Give it a name" })
    }
    readonly property int bookmarks: root.canvas.pages.filter(p => !!p.favourite).length

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // Pages, only the bookmarked ones, or the contents: one switch.
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 4
            implicitHeight: 32
            radius: Theme.radiusLg
            antialiasing: true
            color: Theme.surface2
            RowLayout {
                anchors.fill: parent
                anchors.margins: 2
                spacing: 2
                Repeater {
                    model: [{ v: "pages", label: "Pages", tip: "All the pages" }, { v: "bookmarks", icon: "bookmark", tip: "Only bookmarked pages" }, { v: "contents", label: "Contents", tip: "Headings and a PDF's chapters" }]
                    delegate: AbstractButton {
                        id: tab
                        required property var modelData
                        readonly property bool on: root.mode === modelData.v
                        Layout.fillWidth: !modelData.icon
                        Layout.preferredWidth: modelData.icon ? 30 : -1
                        Layout.fillHeight: true
                        hoverEnabled: true
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: modelData.tip + (modelData.v === "bookmarks" ? " (" + root.bookmarks + ")" : "")
                        onClicked: root.mode = modelData.v
                        background: Item {
                            Rectangle { visible: tab.on; antialiasing: true; y: 1; width: parent.width; height: parent.height; radius: Theme.radius; color: Qt.rgba(0, 0, 0, Theme.dark ? 0.3 : 0.08) }
                            Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radius; color: tab.on ? Theme.surface : tab.hovered ? Theme.alpha(Theme.text, 0.05) : "transparent" }
                        }
                        contentItem: Item {
                            Label { visible: !tab.modelData.icon; anchors.centerIn: parent; text: tab.modelData.label || ""; font.pixelSize: 12; font.weight: Font.DemiBold; color: tab.on ? Theme.text : Theme.muted }
                            Icon { visible: !!tab.modelData.icon; anchors.centerIn: parent; name: tab.modelData.icon || "bookmark"; size: 15; color: tab.on ? Theme.accent : Theme.muted }
                        }
                    }
                }
            }
        }
        Label {
            visible: !root.showContents && root.onlyBookmarks && root.bookmarks === 0
            text: "No bookmarks yet. Choose “Bookmark page” in a page's ⋯ menu to keep it here."
            color: Theme.muted
            font.pixelSize: 12
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Layout.margins: 16
        }
        ListView {
            id: list
            visible: !root.showContents
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            topMargin: 8
            bottomMargin: 8
            spacing: 0
            model: root.canvas.pages
            ScrollBar.vertical: ScrollBar {}
            currentIndex: root.canvas.currentPage
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                readonly property bool active: index === root.canvas.currentPage
                // A daftar's list has a heading where each class's section starts.
                readonly property string section: root.daftar && modelData.section && modelData.section !== ((root.canvas.pages[index - 1] || {}).section) ? modelData.section : ""
                width: list.width
                visible: !root.onlyBookmarks || !!modelData.favourite
                height: visible ? thumb.height + 24 + thumb.y + 16 : 0
                HoverHandler { id: hover }
                Rectangle { antialiasing: true;
                    visible: cell.section !== ""
                    x: 22; y: 2
                    width: parent.width - 44
                    height: 22
                    radius: Theme.radiusSm
                    color: Theme.accentSoft
                    Label { anchors.centerIn: parent; width: parent.width - 12; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; text: cell.section; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.2; color: Theme.accent }
                }
                AbstractButton {
                    id: thumb
                    x: 18
                    y: cell.section !== "" ? 34 : 0
                    width: parent.width - 36
                    height: width * (cell.modelData.height || 1123) / (cell.modelData.width || 794)
                    hoverEnabled: true
                    onClicked: root.canvas.goToPage(cell.index)
                    transform: Translate { y: thumb.hovered && !cell.active ? -2 : 0; Behavior on y { NumberAnimation { duration: 200 } } }
                    background: Item {
                        Rectangle { antialiasing: true; anchors.fill: parent; anchors.margins: cell.active ? -2.5 : -1; radius: Theme.radius; color: cell.active ? Theme.accent : Theme.border }
                        Shadow { anchors.fill: parent; radius: Theme.radiusSm; level: thumb.hovered ? 2 : 1 }
                        RoundImage {
                            anchors.fill: parent
                            color: cell.modelData.paperColor || "#fffdf7"
                            source: cell.modelData.thumbnail || ""
                            radiusTopLeft: 6; radiusTopRight: 6; radiusBottomLeft: 6; radiusBottomRight: 6
                        }
                    }
                }
                IconButton {
                    id: more
                    anchors.top: thumb.top
                    anchors.right: thumb.right
                    anchors.margins: 6
                    width: 28; height: 28
                    iconSize: 16
                    iconName: "more"
                    tip: "Page " + (cell.index + 1) + " options"
                    opacity: hover.hovered || menu.opened ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                    Rectangle { antialiasing: true; anchors.fill: parent; z: -1; radius: Theme.radius; color: Theme.surface }
                    onClicked: menu.show(more, [
                        { icon: "plus", label: "Add page after", action: () => root.daftar && cell.modelData.section ? root.addDaftarPage() : root.canvas.addPage(cell.index + 1, "") },
                        { icon: "copy", label: "Duplicate page", action: () => root.canvas.duplicatePage(cell.index) },
                        { separator: true },
                        { icon: "up", label: "Move up", action: () => root.canvas.movePage(cell.index, cell.index - 1) },
                        { icon: "down", label: "Move down", action: () => root.canvas.movePage(cell.index, cell.index + 1) },
                        { icon: "bookmark", label: cell.modelData.favourite ? "Remove bookmark" : "Bookmark page", action: () => root.canvas.togglePageFavourite(cell.index) },
                        { icon: "contents", label: "Add to contents…", action: () => root.askHeading(cell.index) },
                        { separator: true },
                        { icon: "trash", label: "Delete page", danger: true, action: () => root.canvas.deletePage(cell.index) }
                    ])
                }
                Label {
                    anchors.top: thumb.bottom
                    anchors.topMargin: 6
                    anchors.horizontalCenter: thumb.horizontalCenter
                    text: (cell.modelData.favourite ? "★ " : "") + (cell.index + 1)
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: cell.active ? Theme.accent : Theme.muted
                }
            }
        }
        // The contents: the PDF's own and your headings; tap one to go there.
        ListView {
            id: toc
            visible: root.showContents
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            topMargin: 6
            bottomMargin: 8
            spacing: 2
            model: root.contents
            ScrollBar.vertical: ScrollBar {}
            delegate: AbstractButton {
                id: entry
                required property var modelData
                required property int index
                readonly property bool mine: modelData.kind === "heading"
                // The entry for the page you're on (the last one at or before it).
                readonly property bool here: modelData.page <= root.canvas.currentPage && (index === root.contents.length - 1 || root.contents[index + 1].page > root.canvas.currentPage)
                x: 10
                width: toc.width - 20
                implicitHeight: Math.max(34, entryRow.implicitHeight + 12)
                hoverEnabled: true
                onClicked: root.canvas.goToPage(modelData.page)
                function more() {
                    menu.show(moreBtn, [
                        { icon: "pen", label: "Rename", action: () => ask.ask("Rename heading", [{ name: "title", label: "Heading", value: entry.modelData.title }], "Save", r => root.canvas.changeHeading(entry.modelData.id, { title: r.title })) },
                        entry.modelData.level > 0 ? { icon: "up", label: "Make it a heading", action: () => root.canvas.changeHeading(entry.modelData.id, { level: entry.modelData.level - 1 }) }
                                                  : { icon: "down", label: "Make it a sub-heading", action: () => root.canvas.changeHeading(entry.modelData.id, { level: 1 }) },
                        { separator: true },
                        { icon: "trash", label: "Remove from contents", danger: true, action: () => root.canvas.removeHeading(entry.modelData.id) }
                    ])
                }
                TapHandler { enabled: entry.mine; acceptedButtons: Qt.RightButton; onTapped: entry.more() }
                background: Rectangle {
                    antialiasing: true
                    radius: Theme.radius
                    color: entry.here ? Theme.accentSoft : entry.hovered ? Theme.surface2 : "transparent"
                    // The current one: a bar at its side.
                    Rectangle { visible: entry.here; x: 0; y: 7; width: 3; height: parent.height - 14; radius: 1.5; color: Theme.accent }
                }
                contentItem: RowLayout {
                    id: entryRow
                    spacing: 7
                    Item { Layout.preferredWidth: 4 + entry.modelData.level * 12 }
                    Icon {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                        name: entry.mine ? "heading" : entry.modelData.kind === "section" ? "stack" : "page"
                        size: entry.modelData.level > 0 ? 12 : 14
                        color: entry.here ? Theme.accent : Theme.muted
                    }
                    Label {
                        Layout.fillWidth: true
                        text: entry.modelData.title
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pixelSize: entry.modelData.level > 0 ? 12 : 13
                        font.weight: entry.modelData.level > 0 ? Font.Normal : Font.DemiBold
                        color: entry.here ? Theme.accent : Theme.text
                    }
                    Item {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 18
                        Label { visible: !(entry.mine && (entry.hovered || menu.opened)); anchors.right: parent.right; text: entry.modelData.page + 1; font.pixelSize: 11; color: Theme.muted }
                        IconButton {
                            id: moreBtn
                            visible: entry.mine && (entry.hovered || menu.opened)
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 24; height: 24
                            iconSize: 14
                            iconName: "more"
                            tip: "Rename, indent or remove"
                            onClicked: entry.more()
                        }
                    }
                }
            }
        }
        Label {
            visible: root.showContents && root.contents.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 18
            verticalAlignment: Text.AlignTop
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            text: "No contents yet. Add a heading for the page you're on and jump back to it from here; a PDF's own chapters show here too."
            font.pixelSize: 12
            color: Theme.muted
        }
        AbstractButton {
            id: addHeading
            visible: root.showContents
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 6
            Layout.bottomMargin: 16
            implicitHeight: 40
            hoverEnabled: true
            onClicked: root.askHeading(root.canvas.currentPage)
            background: Item {
                DashedBox { anchors.fill: parent; radius: Theme.radius; color: addHeading.hovered ? Theme.accent : Theme.borderStrong; fill: addHeading.hovered ? Theme.accentSoft : "transparent"; lineWidth: 1.5 }
            }
            contentItem: Item {
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 6
                    Icon { name: "heading"; size: 15; color: addHeading.hovered ? Theme.accent : Theme.muted }
                    Label { text: "Add heading"; font.weight: Font.DemiBold; color: addHeading.hovered ? Theme.accent : Theme.muted }
                }
            }
        }
        AbstractButton {
            id: add
            visible: !root.showContents
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 6
            Layout.bottomMargin: 16
            implicitHeight: 40
            hoverEnabled: true
            onClicked: root.daftar ? root.addDaftarPage() : root.canvas.addPage(-1, "")
            background: Item {
                DashedBox { anchors.fill: parent; radius: Theme.radius; color: add.hovered ? Theme.accent : Theme.borderStrong; fill: add.hovered ? Theme.accentSoft : "transparent"; lineWidth: 1.5 }
            }
            contentItem: Item {
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 6
                    Icon { name: "plus"; size: 17; color: add.hovered ? Theme.accent : Theme.muted }
                    Label { text: "Add page"; font.weight: Font.DemiBold; color: add.hovered ? Theme.accent : Theme.muted }
                }
            }
        }
    }
}
