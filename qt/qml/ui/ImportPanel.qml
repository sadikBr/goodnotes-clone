// PDF imports in progress (src/pdfImport.ts): each file's stage, an overall
// bar, and Open for each finished notebook. Nothing opens by itself.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Item {
    id: root
    signal openRequested(string id)
    property bool collapsed: false
    readonly property var jobs: PdfImporter.jobs
    readonly property int done: jobs.filter(j => j.state === "done").length
    readonly property int failed: jobs.filter(j => j.state === "failed").length
    readonly property int counted: jobs.filter(j => j.state !== "cancelled").length
    visible: jobs.length > 0
    width: Math.min(380, parent ? parent.width - 32 : 380)
    height: col.implicitHeight + 24
    Shadow { anchors.fill: parent; radius: 16; level: 3 }
    Rectangle { anchors.fill: parent; radius: 16; color: Theme.surface; border.color: Theme.border }
    ColumnLayout {
        id: col
        x: 14; y: 12
        width: parent.width - 28
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Icon { name: "download"; size: 18; color: Theme.accent }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.text
                text: PdfImporter.busy ? "Importing " + (root.counted === 1 ? "a PDF" : root.counted + " PDFs") + " · " + root.done + " of " + root.counted + " done"
                    : root.failed ? root.done + " imported, " + root.failed + " failed" : (root.done === 1 ? "PDF" : root.done + " PDFs") + " imported"
            }
            UiButton { visible: PdfImporter.busy && root.jobs.some(j => j.state === "waiting"); kind: "ghost"; text: "Stop the rest"; implicitHeight: 28; onClicked: PdfImporter.cancelWaiting() }
            IconButton { iconName: root.collapsed ? "up" : "down"; tip: root.collapsed ? "Show" : "Hide details"; implicitWidth: 28; implicitHeight: 28; iconSize: 16; onClicked: root.collapsed = !root.collapsed }
            IconButton { visible: !PdfImporter.busy; iconName: "close"; tip: "Close"; implicitWidth: 28; implicitHeight: 28; iconSize: 16; onClicked: PdfImporter.clearFinished() }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 6
            radius: 3
            color: Theme.surface3
            Rectangle { width: parent.width * PdfImporter.progress; height: parent.height; radius: 3; color: Theme.accent; Behavior on width { NumberAnimation { duration: 250 } } }
        }
        Repeater {
            model: root.collapsed ? [] : root.jobs
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 10
                Icon { name: modelData.state === "done" ? "check" : modelData.state === "failed" ? "close" : "page"; size: 18; color: modelData.state === "done" ? Theme.accent : modelData.state === "failed" ? Theme.danger : Theme.muted }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label { text: modelData.name; elide: Text.ElideRight; font.pixelSize: 13; font.weight: Font.Medium; color: Theme.text; Layout.fillWidth: true }
                    Label { text: modelData.label; font.pixelSize: 12; color: modelData.state === "failed" ? Theme.danger : Theme.muted; Layout.fillWidth: true; elide: Text.ElideRight }
                    Rectangle {
                        visible: modelData.state === "reading" || modelData.state === "pages" || modelData.state === "saving"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 3
                        Layout.topMargin: 3
                        radius: 2
                        color: Theme.surface3
                        Rectangle { width: parent.width * modelData.progress; height: 3; radius: 2; color: Theme.accent }
                    }
                }
                UiButton { visible: modelData.state === "done"; text: "Open"; implicitHeight: 28; onClicked: root.openRequested(modelData.notebookId) }
            }
        }
    }
}
