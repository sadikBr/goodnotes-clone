// A folder drawn in its colour (the web app's folderPicture): back panel
// with a tab, the covers of its two latest notebooks peeking out, and the
// front panel, which tips forward when `open`.
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import Inkleaf

Item {
    id: root
    property color folderColor: "#64748b"
    /// Up to two notebooks: [{ coverColor, thumbnail }].
    property var sheets: []
    property int count: 0
    property bool open: false
    property bool locked: false
    property bool unlocked: false

    readonly property real backTop: height * 0.40
    readonly property real bottomY: height * 0.92

    // Back panel and its tab.
    Shape {
        id: tab
        readonly property real w: root.width * 0.42
        readonly property real h: Math.max(12, (root.bottomY - root.backTop) * 0.16)
        y: root.backTop - h
        width: w; height: h + 2
        ShapePath {
            strokeWidth: -1
            fillColor: Theme.mix(root.folderColor, "#000000", 0.22)
            startX: 0; startY: tab.h + 2
            PathLine { x: 0; y: 9 }
            PathArc { x: 9; y: 0; radiusX: 9; radiusY: 9 }
            PathLine { x: tab.w * 0.78 - 3; y: 0 }
            PathQuad { x: tab.w * 0.78 + 1.5; y: 2.5; controlX: tab.w * 0.78; controlY: 0 }
            PathLine { x: tab.w; y: tab.h + 2 }
        }
    }
    RoundImage {
        y: root.backTop; width: root.width; height: root.bottomY - root.backTop
        color: Theme.mix(root.folderColor, "#000000", 0.22)
        radiusTopLeft: 4; radiusTopRight: 12; radiusBottomRight: 12; radiusBottomLeft: 12
    }

    // The sheets inside (the latest notebook in front); an empty folder has none.
    Repeater {
        model: [1, 0].filter(i => i < root.sheets.length)
        delegate: Item {
            id: sheet
            required property int modelData
            readonly property var nb: root.sheets[modelData]
            readonly property bool second: modelData === 1
            x: root.width * (second ? 0.30 : 0.22)
            width: root.width * (second ? 0.66 : 0.69)
            y: root.height * 0.30 + height * (root.open ? (second ? -0.12 : -0.06) : (second ? -0.04 : 0.04))
            height: root.height * 0.40
            rotation: root.open ? (second ? 7 : -6) : (second ? 5 : -4)
            Behavior on y { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
            Behavior on rotation { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
            Rectangle { antialiasing: true; anchors.fill: parent; anchors.margins: -1; radius: 5; color: Qt.rgba(0, 0, 0, 0.12) }
            RoundImage {
                anchors.fill: parent
                color: sheet.nb ? sheet.nb.coverColor : "#fbfaf6"
                source: sheet.nb && sheet.nb.thumbnail ? sheet.nb.thumbnail : ""
                radiusTopLeft: 4; radiusTopRight: 4; radiusBottomLeft: 4; radiusBottomRight: 4
            }
        }
    }

    // Front panel.
    Item {
        id: front
        y: root.height * 0.54
        width: root.width
        height: root.bottomY - y
        transform: Rotation {
            origin.x: front.width / 2; origin.y: front.height
            axis { x: 1; y: 0; z: 0 }
            angle: root.open ? -14 : 0
            Behavior on angle { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
        }
        Rectangle { antialiasing: true; y: 2; width: parent.width; height: parent.height; radius: 11; color: Qt.rgba(0, 0, 0, 0.10); anchors.horizontalCenter: parent.horizontalCenter }
        Rectangle { antialiasing: true; y: 1; width: parent.width; height: parent.height; radius: 11; color: Qt.rgba(0, 0, 0, 0.18) }
        Rectangle { antialiasing: true;
            anchors.fill: parent
            radius: 10
            gradient: Gradient {
                GradientStop { position: 0; color: Theme.mix(root.folderColor, "#ffffff", 0.18) }
                GradientStop { position: 0.38; color: root.folderColor }
                GradientStop { position: 1; color: Theme.mix(root.folderColor, "#000000", 0.08) }
            }
        }
        Rectangle { x: 8; width: parent.width - 16; height: 1; color: Qt.rgba(1, 1, 1, 0.35) }
        // The seam.
        Rectangle { x: parent.width * 0.08; y: parent.height * 0.18; width: parent.width * 0.84; height: 1; color: Qt.rgba(1, 1, 1, 0.22) }
        Rectangle { x: parent.width * 0.08; y: parent.height * 0.18 + 1; width: parent.width * 0.84; height: 1; color: Qt.rgba(0, 0, 0, 0.12) }
        Rectangle { antialiasing: true;
            visible: root.count > 0
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.rightMargin: 10; anchors.bottomMargin: 9
            height: 22; radius: Theme.radiusSm
            width: Math.max(22, countLabel.implicitWidth + 14)
            color: Qt.rgba(1, 1, 1, 0.92)
            Label { id: countLabel; anchors.centerIn: parent; text: root.count; font.pixelSize: 12; font.weight: Font.Bold; color: Theme.mix(root.folderColor, "#000000", 0.3) }
        }
        // Password protected.
        Rectangle { antialiasing: true;
            visible: root.locked
            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.leftMargin: 10; anchors.bottomMargin: 9
            width: 22; height: 22; radius: Theme.radiusSm
            color: Qt.rgba(1, 1, 1, 0.92)
            Icon { anchors.centerIn: parent; name: root.unlocked ? "unlock" : "lock"; size: 13; color: Theme.mix(root.folderColor, "#000000", 0.3) }
        }
    }
}
