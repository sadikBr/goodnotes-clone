// The library: notebooks on a shelf (folders, search and trash follow).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Rectangle {
    id: root
    color: Theme.bg
    signal openRequested(string id, string title)
    signal toggleDark()

    property var library: ({ folders: [], notebooks: [] })
    function reload() {
        try {
            const lib = InkleafCore.call("library")
            lib.notebooks.sort((a, b) => b.updatedAt - a.updatedAt)
            library = lib
        } catch (e) {
            console.warn(e)
        }
    }
    Component.onCompleted: reload()
    Connections {
        target: InkleafCore
        function onLibraryChanged() { root.reload() }
    }

    function greeting() {
        const h = new Date().getHours()
        return h < 12 ? "Good morning" : h < 18 ? "Good afternoon" : "Good evening"
    }
    function relative(ms) {
        const min = Math.round((Date.now() - ms) / 60000)
        if (min < 1) return "just now"
        if (min < 60) return min + " min ago"
        const h = Math.round(min / 60)
        if (h < 24) return h + " h ago"
        const d = Math.round(h / 24)
        return d === 1 ? "yesterday" : d + " days ago"
    }
    function newNotebook() {
        const nb = InkleafCore.call("createNotebook", { input: { title: "Untitled notebook", folderId: null, coverColor: "#1f7a5a", template: "ruled", paperColor: "#fffdf7" } })
        root.openRequested(nb.id, nb.title)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 248
            color: Theme.surface
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 6
                RowLayout {
                    spacing: 10
                    Layout.bottomMargin: 10
                    Image { source: "qrc:/qt/qml/Inkleaf/assets/icon.svg"; sourceSize: Qt.size(60, 60); Layout.preferredWidth: 30; Layout.preferredHeight: 30 }
                    Label { text: "Inkleaf"; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text }
                }
                Button {
                    id: newBtn
                    Layout.fillWidth: true
                    implicitHeight: 46
                    hoverEnabled: true
                    onClicked: root.newNotebook()
                    background: Rectangle {
                        radius: 14
                        color: Theme.surface
                        border.color: newBtn.hovered ? Theme.borderStrong : Theme.border
                    }
                    contentItem: RowLayout {
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 34; Layout.preferredHeight: 34; radius: 10; color: Theme.accent
                            Icon { anchors.centerIn: parent; name: "plus"; color: Theme.accentContrast }
                        }
                        Label { text: "New notebook"; font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text; Layout.fillWidth: true }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.topMargin: 8; implicitHeight: 38; radius: 10; color: Theme.accentSoft
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 12; spacing: 10
                        Icon { name: "docs"; color: Theme.accent }
                        Label { text: "Documents"; color: Theme.accent; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Label { text: root.library.notebooks.length; color: Theme.accent; font.pixelSize: 12; Layout.rightMargin: 12 }
                    }
                }
                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                ItemDelegate {
                    Layout.fillWidth: true
                    onClicked: root.toggleDark()
                    background: Rectangle { radius: 10; color: parent.hovered ? Theme.surface2 : "transparent" }
                    contentItem: RowLayout {
                        spacing: 10
                        Icon { name: "moon"; color: Theme.muted }
                        Label { text: "Dark mode"; color: Theme.text; Layout.fillWidth: true }
                        Switch { checked: Theme.dark; enabled: false; scale: 0.8 }
                    }
                }
                Label {
                    text: "Native preview · " + Qt.application.version
                    color: Theme.muted; font.pixelSize: 11; Layout.leftMargin: 8
                }
            }
        }

        // Shelf
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 0
                ColumnLayout {
                    Layout.margins: 36
                    Layout.bottomMargin: 18
                    spacing: 4
                    Label { text: root.greeting(); font.pixelSize: 28; font.weight: Font.Bold; color: Theme.text }
                    Label {
                        text: root.library.notebooks.length === 0 ? "Let's start something new" : root.library.notebooks.length + (root.library.notebooks.length === 1 ? " notebook" : " notebooks")
                        color: Theme.muted; font.pixelSize: 14
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: 36
                    Layout.rightMargin: 36
                    spacing: 26
                    Repeater {
                        model: root.library.notebooks
                        delegate: Item {
                            required property var modelData
                            width: 196
                            height: 300
                            Rectangle {
                                id: cover
                                width: parent.width
                                height: 252
                                radius: 6
                                color: modelData.coverColor
                                clip: true
                                // A page or cover picture when there is one; otherwise the title on a label.
                                Image {
                                    anchors.fill: parent
                                    anchors.margins: modelData.hasCover ? 0 : 14
                                    anchors.leftMargin: modelData.hasCover ? 0 : 20
                                    source: modelData.thumbnail || ""
                                    fillMode: Image.PreserveAspectCrop
                                    visible: !!modelData.thumbnail
                                    asynchronous: true
                                }
                                Rectangle { width: 8; height: parent.height; color: Qt.rgba(0, 0, 0, 0.14) }
                                Rectangle {
                                    visible: !modelData.thumbnail
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 60; width: parent.width - 44; height: 38; radius: 3
                                    color: Qt.rgba(1, 1, 1, 0.9)
                                    Label { anchors.fill: parent; anchors.margins: 8; text: modelData.title; elide: Text.ElideRight; font.weight: Font.DemiBold; color: "#1d1f23"; verticalAlignment: Text.AlignVCenter }
                                }
                                MouseArea {
                                    id: hit
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.openRequested(modelData.id, modelData.title)
                                }
                                scale: hit.containsMouse ? 1.02 : 1
                                Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                            }
                            Label {
                                anchors.top: cover.bottom; anchors.topMargin: 10
                                width: parent.width; elide: Text.ElideRight
                                text: modelData.title; font.weight: Font.DemiBold; color: Theme.text
                            }
                            Label {
                                anchors.top: cover.bottom; anchors.topMargin: 30
                                text: modelData.pageCount + (modelData.pageCount === 1 ? " page · " : " pages · ") + root.relative(modelData.updatedAt)
                                color: Theme.muted; font.pixelSize: 12
                            }
                        }
                    }
                }
                Item { Layout.preferredHeight: 40 }
            }
        }
    }
}
