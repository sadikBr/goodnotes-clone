// A slider in the app's colours.
import QtQuick
import QtQuick.Controls
import Inkleaf

Slider {
    id: root
    implicitWidth: 200
    implicitHeight: 24
    background: Rectangle { antialiasing: true;
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: 4
        radius: 2
        color: Theme.surface3
        Rectangle { antialiasing: true; width: root.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.accent }
    }
    handle: Rectangle { antialiasing: true;
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: 18; height: 18; radius: 9
        color: "#ffffff"
        border.color: root.pressed ? Theme.accent : Theme.borderStrong
        border.width: root.pressed ? 2 : 1
    }
}
