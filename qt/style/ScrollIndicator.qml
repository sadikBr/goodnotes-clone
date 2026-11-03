// A thin scroll position line (no dragging).
import QtQuick
import QtQuick.Templates as T

T.ScrollIndicator {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, implicitContentHeight + topPadding + bottomPadding)
    padding: 2
    contentItem: Rectangle { antialiasing: true;
        implicitWidth: 4
        implicitHeight: 4
        radius: 2
        color: Qt.rgba(0.5, 0.48, 0.44, 0.45)
        visible: control.size < 1.0
        opacity: control.active ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
    }
}
