// Tooltips as in the web app: small, dark and rounded.
import QtQuick
import QtQuick.Templates as T

T.ToolTip {
    id: control
    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: -implicitHeight - 6
    implicitWidth: Math.min(contentItem.implicitWidth + leftPadding + rightPadding, 360)
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    margins: 6
    leftPadding: 9
    rightPadding: 9
    topPadding: 5
    bottomPadding: 5
    delay: 450
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent
    font.pixelSize: 12
    font.weight: Font.Medium
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 80 } }
    contentItem: Text {
        text: control.text
        font: control.font
        color: "#f4f3ef"
        wrapMode: Text.Wrap
    }
    background: Rectangle { antialiasing: true; radius: 8; color: "#22231f" }
}
