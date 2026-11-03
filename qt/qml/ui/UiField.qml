// A text field (the web app's inputs): rounded, soft fill, accent focus ring.
import QtQuick
import QtQuick.Controls
import Inkleaf

TextField {
    id: root
    implicitHeight: 40
    leftPadding: 12
    rightPadding: 12
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectionColor: Theme.alpha(Theme.accent, 0.3)
    selectedTextColor: Theme.text
    font.pixelSize: 14
    // Left-aligned like the web app's inputs, whatever the script.
    horizontalAlignment: TextInput.AlignLeft
    background: Rectangle { antialiasing: true;
        radius: Theme.radius
        color: Theme.surface2
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.accent : Theme.border
    }
}
