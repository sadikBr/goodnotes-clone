// A thin scroll bar that thickens under the pointer.
import QtQuick
import QtQuick.Templates as T

T.ScrollBar {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding)
    padding: 3
    visible: policy !== T.ScrollBar.AlwaysOff
    minimumSize: orientation === Qt.Horizontal ? height / width : width / height
    contentItem: Rectangle { antialiasing: true;
        implicitWidth: control.interactive ? 8 : 4
        implicitHeight: control.interactive ? 8 : 4
        radius: width / 2
        color: Qt.rgba(0.5, 0.48, 0.44, control.pressed ? 0.7 : control.hovered ? 0.55 : 0.4)
        opacity: control.policy === T.ScrollBar.AlwaysOn || (control.active && control.size < 1.0) ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
    }
}
