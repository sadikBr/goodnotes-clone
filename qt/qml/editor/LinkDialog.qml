// Linking the selection: to a page of this notebook, to another notebook,
// or to a website; or taking its link off.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    required property InkCanvas canvas
    required property string notebookId
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 520)
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, Theme.dark ? 0.5 : 0.28) }

    /// "page", "notebook" or "url".
    property string kind: "page"
    property string pageId: ""
    property string notebookTo: ""
    property string url: ""
    /// The selection's link now (null: none).
    property var current: null
    property var notebooks: []
    property string filter: ""

    function show() {
        current = canvas.selection.link && canvas.selection.link.kind !== "mixed" ? canvas.selection.link : null
        kind = current ? current.kind : "page"
        pageId = current && current.kind === "page" ? current.page : ""
        notebookTo = current && current.kind === "notebook" ? current.notebook : ""
        url = current && current.kind === "url" ? current.url : ""
        filter = ""
        try {
            notebooks = InkleafCore.call("library").notebooks.filter(n => n.id !== root.notebookId && !n.trashedAt && (!n.locked || n.unlocked)).sort((a, b) => b.updatedAt - a.updatedAt)
        } catch (e) { notebooks = [] }
        open()
        if (kind === "url") urlField.forceActiveFocus()
    }
    /// The website as typed, made into an address ("example.com" → "https://example.com").
    function cleanUrl(t) {
        t = String(t || "").trim()
        if (!t) return ""
        if (/^[a-z][a-z0-9+.-]*:/i.test(t)) return t
        return "https://" + t
    }
    readonly property bool ready: kind === "page" ? !!pageId : kind === "notebook" ? !!notebookTo : /\S+\.\S+|^[a-z]+:/i.test(url.trim())
    function apply() {
        if (!ready) return
        let link
        if (kind === "page") link = { kind: "page", page: pageId }
        else if (kind === "notebook") {
            const nb = notebooks.find(n => n.id === notebookTo)
            link = { kind: "notebook", notebook: notebookTo, title: nb ? nb.title : "" }
        } else link = { kind: "url", url: cleanUrl(url) }
        canvas.linkSelection(link)
        close()
    }

    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 4 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }
    contentItem: ColumnLayout {
        spacing: 0
        Keys.onReturnPressed: root.apply()
        Keys.onEnterPressed: root.apply()
        // Heading.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 12
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 34; Layout.preferredHeight: 34
                radius: Theme.radius
                antialiasing: true
                color: Theme.accentSoft
                Icon { anchors.centerIn: parent; name: "link"; size: 18; color: Theme.accent }
            }
            ColumnLayout {
                spacing: 1
                Label { text: root.current ? "Change the link" : "Link to…"; font.pixelSize: 17; font.weight: Font.DemiBold; color: Theme.text }
                Label { text: "Tap the link's chip on the page to follow it"; font.pixelSize: 12; color: Theme.muted }
            }
        }
        Segmented {
            Layout.fillWidth: true
            Layout.leftMargin: 20; Layout.rightMargin: 20
            options: [{ value: "page", label: "A page" }, { value: "notebook", label: "A notebook" }, { value: "url", label: "A website" }]
            value: root.kind
            onPicked: v => { root.kind = v; if (v === "url") urlField.forceActiveFocus() }
        }

        // A page: its picture and number.
        Flickable {
            visible: root.kind === "page"
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            Layout.topMargin: 12
            contentHeight: pageGrid.implicitHeight + 8
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            GridLayout {
                id: pageGrid
                x: 20
                width: parent.width - 40
                columns: 5
                columnSpacing: 10
                rowSpacing: 10
                Repeater {
                    model: root.opened && root.kind === "page" ? root.canvas.pages : []
                    delegate: AbstractButton {
                        id: pc
                        required property var modelData
                        required property int index
                        readonly property bool on: root.pageId === modelData.id
                        readonly property bool here: index === root.canvas.currentPage
                        Layout.preferredWidth: Math.floor((pageGrid.width - 40) / 5)
                        implicitHeight: thumb.height + 26
                        hoverEnabled: true
                        onClicked: root.pageId = modelData.id
                        onDoubleClicked: { root.pageId = modelData.id; root.apply() }
                        background: Item {}
                        contentItem: Item {
                            Rectangle {
                                id: thumb
                                width: parent.width
                                height: width * (pc.modelData.height || 1123) / (pc.modelData.width || 794)
                                radius: Theme.radiusSm
                                antialiasing: true
                                color: pc.on ? Theme.accent : pc.hovered ? Theme.borderStrong : Theme.border
                                RoundImage {
                                    anchors.fill: parent
                                    anchors.margins: pc.on ? 2.5 : 1
                                    color: pc.modelData.paperColor || "#fffdf7"
                                    source: pc.modelData.thumbnail || ""
                                    radiusTopLeft: 4; radiusTopRight: 4; radiusBottomLeft: 4; radiusBottomRight: 4
                                }
                                // Picked: a tick.
                                Rectangle {
                                    visible: pc.on
                                    anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 5
                                    width: 20; height: 20; radius: Theme.radiusSm
                                    antialiasing: true
                                    color: Theme.accent
                                    Icon { anchors.centerIn: parent; name: "check"; size: 13; color: Theme.accentContrast }
                                }
                            }
                            Label {
                                anchors.top: thumb.bottom; anchors.topMargin: 5
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: (pc.here ? "This page · " : "") + (pc.index + 1)
                                font.pixelSize: 11
                                font.weight: pc.on ? Font.DemiBold : Font.Normal
                                color: pc.on ? Theme.accent : Theme.muted
                            }
                        }
                    }
                }
            }
        }

        // Another notebook.
        ColumnLayout {
            visible: root.kind === "notebook"
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            Layout.topMargin: 12
            spacing: 8
            UiField {
                Layout.fillWidth: true
                Layout.leftMargin: 20; Layout.rightMargin: 20
                placeholderText: "Find a notebook"
                onTextChanged: root.filter = text
            }
            Label {
                visible: root.notebooks.length === 0
                Layout.leftMargin: 20
                text: "There are no other notebooks yet."
                font.pixelSize: 12
                color: Theme.muted
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: root.notebooks.filter(n => !root.filter || n.title.toLowerCase().indexOf(root.filter.toLowerCase()) >= 0)
                ScrollBar.vertical: ScrollBar {}
                delegate: AbstractButton {
                    id: nr
                    required property var modelData
                    readonly property bool on: root.notebookTo === modelData.id
                    x: 14
                    width: ListView.view.width - 28
                    implicitHeight: 46
                    hoverEnabled: true
                    onClicked: root.notebookTo = modelData.id
                    onDoubleClicked: { root.notebookTo = modelData.id; root.apply() }
                    background: Rectangle { antialiasing: true; radius: Theme.radius; color: nr.on ? Theme.accentSoft : nr.hovered ? Theme.surface2 : "transparent" }
                    contentItem: RowLayout {
                        spacing: 10
                        // A little book in its cover colour.
                        Rectangle {
                            Layout.leftMargin: 6
                            Layout.preferredWidth: 24; Layout.preferredHeight: 32
                            radius: 3
                            antialiasing: true
                            color: nr.modelData.coverColor || "#64748b"
                            Rectangle { x: 3; width: 1.5; height: parent.height; color: Qt.rgba(0, 0, 0, 0.18) }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label { Layout.fillWidth: true; text: nr.modelData.title; elide: Text.ElideRight; font.pixelSize: 13; font.weight: Font.DemiBold; color: nr.on ? Theme.accent : Theme.text }
                            Label { text: nr.modelData.pageCount + (nr.modelData.pageCount === 1 ? " page" : " pages"); font.pixelSize: 11; color: Theme.muted }
                        }
                        Icon { visible: nr.on; name: "check"; size: 16; color: Theme.accent; Layout.rightMargin: 8 }
                    }
                }
            }
        }

        // A website.
        ColumnLayout {
            visible: root.kind === "url"
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            Layout.topMargin: 16
            Layout.leftMargin: 20; Layout.rightMargin: 20
            spacing: 8
            Label { text: "Address"; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted }
            UiField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: "example.com/lesson"
                text: root.url
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                onTextChanged: root.url = text
                onAccepted: root.apply()
            }
            Label {
                visible: root.ready
                Layout.fillWidth: true
                text: "Opens " + root.cleanUrl(root.url) + " in your browser"
                elide: Text.ElideMiddle
                font.pixelSize: 12
                color: Theme.muted
            }
            Item { Layout.fillHeight: true }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 12; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.leftMargin: 20
            spacing: 8
            UiButton { visible: !!root.current; kind: "danger"; iconName: "trash"; text: "Remove link"; onClicked: { root.canvas.linkSelection({}); root.close() } }
            Item { Layout.fillWidth: true }
            UiButton { text: "Cancel"; kind: "ghost"; onClicked: root.close() }
            UiButton { text: root.current ? "Save" : "Link"; kind: "primary"; enabled: root.ready; onClicked: root.apply() }
        }
    }
}
