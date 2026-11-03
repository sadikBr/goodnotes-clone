// The Settings screen: writing, stylus and touch, and appearance in one
// place (src/settingsScreen.ts). Changes apply at once.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 640)
    height: Math.min(parent.height - 32, 760)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    readonly property var s: AppSettings.store

    Overlay.modal: Rectangle { color: Qt.rgba(20 / 255, 18 / 255, 14 / 255, 0.45) }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 180; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 100 } }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
        Rectangle { anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }

    component SectionHead: RowLayout {
        property string iconName
        property string text
        Layout.fillWidth: true
        Layout.topMargin: 18
        Layout.bottomMargin: 6
        spacing: 8
        Icon { name: parent.iconName; size: 16; color: Theme.muted }
        Label { text: parent.text.toUpperCase(); font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 0.7; color: Theme.muted }
    }
    component SettingRow: Item {
        id: row
        property string title
        property string help
        property bool first: false
        default property alias control: holder.data
        Layout.fillWidth: true
        implicitHeight: Math.max(text.implicitHeight, holder.childrenRect.height) + 24
        Rectangle { visible: !row.first; width: parent.width; height: 1; color: Theme.border }
        ColumnLayout {
            id: text
            anchors.left: parent.left
            anchors.right: holder.left
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Label { text: row.title; font.weight: Font.Medium; color: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: row.help; font.pixelSize: 12; color: Theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.15 }
        }
        Item {
            id: holder
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: childrenRect.width
            height: childrenRect.height
        }
    }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Label { text: "Settings"; font.pixelSize: 18; font.weight: Font.Bold; color: Theme.text; Layout.fillWidth: true }
            UiButton { text: "Done"; kind: "primary"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: body.implicitHeight + 32
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: body
                x: 24
                y: 8
                width: parent.width - 48
                spacing: 0

                SectionHead { iconName: "pen"; text: "Writing" }
                SettingRow {
                    first: true
                    title: "Smoothing"; help: "Evens out shaky lines as you write. Off draws exactly what you write."
                    Segmented { options: [{ value: "off", label: "Off" }, { value: "light", label: "Light" }, { value: "normal", label: "Normal" }, { value: "strong", label: "Strong" }]; value: root.s.smoothing; onPicked: v => root.s.smoothing = v }
                }
                SettingRow {
                    title: "Pressure"; help: "How hard you press for a full line. Light: a gentle touch is enough. Firm: press harder for thick lines."
                    Segmented { options: [{ value: "light", label: "Light" }, { value: "normal", label: "Normal" }, { value: "firm", label: "Firm" }]; value: root.s.pressure; onPicked: v => root.s.pressure = v }
                }
                SettingRow {
                    title: "Cursor under the pen"; help: "Shown for the pen, highlighter and shapes."
                    Segmented { options: [{ value: "cross", label: "Cross" }, { value: "dot", label: "Dot" }, { value: "none", label: "Hidden" }]; value: root.s.penCursor; onPicked: v => root.s.penCursor = v }
                }

                SectionHead { iconName: "stylus"; text: "Stylus and touch" }
                SettingRow {
                    first: true
                    title: "Side button"; help: "Hold the pen's side button while writing to use this tool instead."
                    Segmented { options: [{ value: "eraser", label: "Eraser" }, { value: "lasso", label: "Lasso" }, { value: "none", label: "Nothing" }]; value: root.s.sideButton; onPicked: v => root.s.sideButton = v }
                }
                SettingRow {
                    title: "Eraser end"; help: "Turning the pen over erases (on pens that have an eraser end)."
                    UiSwitch { checked: root.s.eraserEnd; onClicked: root.s.eraserEnd = checked }
                }
                SettingRow {
                    title: "Palm rejection"; help: "Ignores your hand resting on the screen while you use the pen."
                    UiSwitch { checked: root.s.palmRejection; onClicked: root.s.palmRejection = checked }
                }
                SettingRow {
                    title: "Pen-only mode"; help: "Only the pen writes; touch and the mouse scroll and zoom. Turns on by itself the first time you use a pen."
                    UiSwitch { checked: root.s.penOnly; onClicked: root.s.penOnly = checked }
                }

                SectionHead { iconName: "sun"; text: "Appearance" }
                SettingRow {
                    first: true
                    title: "Theme"; help: "System follows your computer's light or dark setting."
                    Segmented { options: [{ value: "light", label: "Light" }, { value: "dark", label: "Dark" }, { value: "system", label: "System" }]; value: root.s.theme; onPicked: v => root.s.theme = v }
                }

                SectionHead { iconName: "download"; text: "Sync, backups and updates" }
                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: "Google Drive sync, backups and updates come to the native app next. Until then, set them up in the current Inkleaf app — both apps share the same library."
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    font.pixelSize: 13
                    lineHeight: 1.2
                }
            }
        }
    }
}
