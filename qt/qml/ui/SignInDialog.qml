// "Sign in to Google Drive": while the user allows access in the browser
// (opened by Services.signIn), which then comes back to the app by itself
// (src/syncSettings.ts signIn).
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
            text: "Continue in your browser: choose your Google account and allow Inkleaf to keep its files in your Drive. Inkleaf only sees its own hidden folder there, not your other files."
        }
        UiButton {
            Layout.alignment: Qt.AlignHCenter
            text: "Open the sign-in page again"
            onClicked: Qt.openUrlExternally(root.code.verificationUrl)
        }
        Label { Layout.alignment: Qt.AlignHCenter; text: "Waiting for you to allow access…"; font.pixelSize: 12; color: Theme.muted }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            UiButton { text: "Cancel"; kind: "ghost"; onClicked: root.services.cancelSignIn() }
        }
    }
}
