// Export what's selected: a picture (PNG) or a PDF, on the page's paper or
// with a see-through background (to drop onto slides, worksheets, other pages).
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    required property InkCanvas canvas
    property string title: "Inkleaf"
    signal notify(string message, bool error)
    /// "png" or "pdf"; and whether the paper shows behind it.
    property string format: "png"
    property bool transparent: true
    readonly property var store: Prefs { category: "selectionExport"; property string format: "png"; property bool transparent: true }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 32 : 380, 380)
    padding: 18
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, Theme.dark ? 0.45 : 0.25) }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }
    onAboutToShow: { format = store.format; transparent = store.transparent }

    function save() {
        store.format = format
        store.transparent = transparent
        dialog.selectedFile = InkleafCore.documentsFolder() + "/" + (title + " - selection").replace(/[\\/:*?"<>|]/g, "_") + "." + format
        dialog.nameFilters = [format === "pdf" ? "PDF (*.pdf)" : "PNG image (*.png)"]
        close()
        dialog.open()
    }
    FileDialog {
        id: dialog
        fileMode: FileDialog.SaveFile
        title: "Export the selection"
        onAccepted: {
            const err = root.canvas.exportSelection(selectedFile.toString(), root.format, root.transparent)
            if (err) root.notify(err, true)
            else root.notify("Exported to " + decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, "")), false)
        }
    }

    contentItem: ColumnLayout {
        spacing: 14
        Label { text: "Export the selection"; font.pixelSize: 17; font.weight: Font.Bold; color: Theme.text }
        // What it'll look like: on the paper, or on a checkerboard (see-through).
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            radius: Theme.radius
            clip: true
            color: root.transparent ? "transparent" : "#fffdf7"
            border.color: Theme.border
            Grid {
                visible: root.transparent
                anchors.fill: parent
                columns: Math.ceil(width / 10)
                Repeater {
                    model: Math.ceil(parent.width / 10) * 11
                    Rectangle { width: 10; height: 10; color: (Math.floor(index / Math.ceil(parent.width / 10)) + index % Math.ceil(parent.width / 10)) % 2 ? "#e5e7eb" : "#ffffff" }
                }
            }
            Label {
                anchors.centerIn: parent
                text: root.transparent ? "Just your ink and things —\nsee-through everywhere else" : "On the page's paper,\ncropped around it"
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 12
                color: "#4b5563"
            }
        }
        ColumnLayout {
            spacing: 6
            Label { text: "Format"; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted }
            Segmented {
                Layout.fillWidth: true
                options: [{ value: "png", label: "Picture (PNG)" }, { value: "pdf", label: "PDF" }]
                value: root.format
                onPicked: v => root.format = v
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: "Transparent background"; color: Theme.text; font.weight: Font.Medium }
                Label { text: "No paper behind it: paste it anywhere"; font.pixelSize: 12; color: Theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
            }
            UiSwitch { checked: root.transparent; onClicked: root.transparent = checked }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8
            Item { Layout.fillWidth: true }
            UiButton { text: "Cancel"; kind: "ghost"; onClicked: root.close() }
            UiButton { text: "Export…"; kind: "primary"; onClicked: root.save() }
        }
    }
}
