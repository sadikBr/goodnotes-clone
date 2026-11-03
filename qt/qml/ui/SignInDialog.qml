// "Sign in to Google Drive": the code to enter on Google's page, while the
// app waits for the user to allow access (src/syncSettings.ts signIn).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    required property Services services
    readonly property var code: services.signInCode
    visible: !!code
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 440)
    modal: true
    padding: 24
    closePolicy: Popup.NoAutoClose
    Overlay.modal: Rectangle { color: Qt.rgba(20 / 255, 18 / 255, 14 / 255, 0.45) }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 180; easing.type: Easing.OutCubic }
    }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label { text: "Sign in to Google Drive"; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.text
            lineHeight: 1.2
            text: "On Google's page, sign in, enter this code and allow Inkleaf to keep its files in your Drive. Inkleaf only sees its own hidden folder there, not your other files."
        }
        Rectangle { antialiasing: true;
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: codeText.implicitWidth + 36
            implicitHeight: codeText.implicitHeight + 20
            radius: 12
            color: Theme.surface2
            TextEdit {
                id: codeText
                anchors.centerIn: parent
                text: root.code ? root.code.userCode : ""
                readOnly: true
                selectByMouse: true
                color: Theme.text
                font.family: "monospace"
                font.pixelSize: 28
                font.weight: Font.Bold
                font.letterSpacing: 3.4
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            UiButton { text: "Open Google's page"; kind: "primary"; iconName: "download"; onClicked: Qt.openUrlExternally(root.code.verificationUrl) }
            UiButton {
                text: "Copy code"
                onClicked: {
                    codeText.selectAll()
                    codeText.copy()
                    codeText.deselect()
                    root.services.notify("Code copied", false)
                }
            }
        }
        Label { text: "Waiting for you to allow access…"; font.pixelSize: 12; color: Theme.muted }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            UiButton { text: "Cancel"; kind: "ghost"; onClicked: root.services.cancelSignIn() }
        }
    }
}
