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
    /// The panel's width (the same for pages, bookmarks and contents).
    readonly property real wantWidth: 236
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
        // Pages, only the bookmarked ones, or the contents: one switch of icons,
        // and what's shown, with how many, under it.
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            implicitHeight: 34
            radius: Theme.radiusLg
            antialiasing: true
            color: Theme.surface2
            RowLayout {
                anchors.fill: parent
                anchors.margins: 3
                spacing: 2
                Repeater {
                    model: [{ v: "pages", icon: "stack", tip: "Pages" }, { v: "bookmarks", icon: "bookmark", tip: "Bookmarked pages" }, { v: "contents", icon: "contents", tip: "Contents: headings and a PDF's chapters" }]
                    delegate: AbstractButton {
                        id: tab
                        required property var modelData
                        readonly property bool on: root.mode === modelData.v
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        hoverEnabled: true
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: modelData.tip
                        onClicked: root.mode = modelData.v
                        background: Item {
                            Rectangle { visible: tab.on; antialiasing: true; y: 1; width: parent.width; height: parent.height; radius: Theme.radius; color: Qt.rgba(0, 0, 0, Theme.dark ? 0.3 : 0.08) }
                            Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radius; color: tab.on ? Theme.surface : tab.hovered ? Theme.alpha(Theme.text, 0.05) : "transparent" }
                        }
                        contentItem: Item { Icon { anchors.centerIn: parent; name: tab.modelData.icon; size: 16; color: tab.on ? Theme.accent : tab.hovered ? Theme.text : Theme.muted } }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.leftMargin: 16
            Layout.rightMargin: 14
            Layout.bottomMargin: 2
            spacing: 6
            Label {
                text: root.showContents ? "Contents" : root.onlyBookmarks ? "Bookmarks" : "Pages"
                font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text
            }
            Rectangle {
                antialiasing: true
                Layout.preferredHeight: 17
                Layout.preferredWidth: countLabel.implicitWidth + 10
                radius: Theme.radiusSm
                color: Theme.surface2
                Label { id: countLabel; anchors.centerIn: parent; text: root.showContents ? root.contents.length : root.onlyBookmarks ? root.bookmarks : root.canvas.pageCount; font.pixelSize: 10; font.weight: Font.Bold; color: Theme.muted }
            }
            Item { Layout.fillWidth: true }
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
        // The contents: the PDF's own, a daftar's classes and your headings;
        // tap one to go there. One line each: "Slide 3: Objectives" shows
        // "Objectives", with "Slide 3" small above it.
        ListView {
            id: toc
            visible: root.showContents
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            topMargin: 4
            bottomMargin: 8
            spacing: 1
            model: root.contents
            ScrollBar.vertical: ScrollBar {}
            // Keeps the one you're on in view.
            readonly property int hereIndex: { let h = -1; for (let i = 0; i < root.contents.length; ++i) if (root.contents[i].page <= root.canvas.currentPage) h = i; return h }
            onHereIndexChanged: if (hereIndex >= 0) positionViewAtIndex(hereIndex, ListView.Contain)
            delegate: AbstractButton {
                id: entry
                required property var modelData
                required property int index
                readonly property bool mine: modelData.kind === "heading"
                readonly property bool section: modelData.kind === "section"
                readonly property int level: Math.min(3, modelData.level || 0)
                readonly property bool here: index === toc.hereIndex
                // "Slide 3: Objectives", "Chapter 2 – Energy": the label and the title.
                readonly property var parts: {
                    const t = String(modelData.title || "")
                    const m = t.match(/^((?:slide|chapter|chapitre|part|partie|unit|unité|lesson|leçon|section|module|week|semaine|day|jour|الدرس|الفصل|الوحدة)?\s*[0-9IVXLivxl]+[0-9.]*)\s*[:.\-–—]\s+(.+)$/i)
                    return m ? { label: m[1], title: m[2] } : { label: "", title: t }
                }
                x: 8
                width: toc.width - 16
                implicitHeight: (parts.label ? 40 : 32)
                hoverEnabled: true
                onClicked: root.canvas.goToPage(modelData.page)
                ToolTip.visible: hovered && titleLabel.truncated
                ToolTip.delay: 600
                ToolTip.text: modelData.title
                function more() {
                    menu.show(moreBtn, [
                        { icon: "pen", label: "Rename", action: () => ask.ask("Rename heading", [{ name: "title", label: "Heading", value: entry.modelData.title }], "Save", r => root.canvas.changeHeading(entry.modelData.id, { title: r.title })) },
                        entry.level > 0 ? { icon: "up", label: "Make it a heading", action: () => root.canvas.changeHeading(entry.modelData.id, { level: entry.level - 1 }) }
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
                    Behavior on color { ColorAnimation { duration: 120 } }
                }
                // Deeper levels sit on a guide line.
                Repeater {
                    model: entry.level
                    Rectangle { required property int index; x: 12 + index * 12; y: 0; width: 1; height: entry.height; color: entry.here ? Theme.alpha(Theme.accent, 0.4) : Theme.border }
                }
                // Room on both sides, so nothing touches the entry's edge.
                leftPadding: 0
                rightPadding: 8
                contentItem: RowLayout {
                    spacing: 8
                    Item { Layout.preferredWidth: 4 + entry.level * 12 }
                    // Your headings: a small accent mark; a daftar's class: a dot.
                    Rectangle {
                        visible: entry.mine || entry.section
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: entry.section ? 7 : 3
                        Layout.preferredHeight: entry.section ? 7 : 14
                        radius: entry.section ? 3.5 : 1.5
                        color: Theme.accent
                        opacity: entry.here ? 1 : 0.7
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            visible: !!entry.parts.label
                            Layout.fillWidth: true
                            text: entry.parts.label
                            elide: Text.ElideRight
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.3
                            color: entry.here ? Theme.accent : Theme.muted
                        }
                        Label {
                            id: titleLabel
                            Layout.fillWidth: true
                            text: entry.parts.title
                            elide: Text.ElideRight
                            font.pixelSize: entry.level > 0 ? 12 : 13
                            font.weight: entry.here ? Font.DemiBold : entry.level > 0 ? Font.Normal : Font.Medium
                            color: entry.here ? Theme.accent : entry.level > 0 ? Theme.mix(Theme.text, Theme.muted, 0.35) : Theme.text
                        }
                    }
                    Item {
                        Layout.preferredWidth: Math.max(26, pageNo.implicitWidth + 12)
                        Layout.fillHeight: true
                        // The page, as a small soft badge.
                        Rectangle {
                            visible: !(entry.mine && (entry.hovered || menu.opened))
                            anchors.centerIn: parent
                            width: pageNo.implicitWidth + 12
                            height: 20
                            radius: Theme.radiusSm
                            antialiasing: true
                            color: entry.here ? Theme.alpha(Theme.accent, 0.16) : entry.hovered ? Theme.surface3 : Theme.surface2
                            Label {
                                id: pageNo
                                anchors.centerIn: parent
                                text: entry.modelData.page + 1
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                color: entry.here ? Theme.accent : Theme.muted
                            }
                        }
                        IconButton {
                            id: moreBtn
                            visible: entry.mine && (entry.hovered || menu.opened)
                            anchors.centerIn: parent
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
            text: "Nothing here yet. Add a heading for the page you're on to jump back to it from here. A PDF's own chapters show here too."
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
