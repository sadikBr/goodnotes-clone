// A notebook drawn as a book (the web app's .nb-cover): the cover in its
// colour with a spine, page edges peeking out on the right, and either the
// cover page full-size or the first page inset on the cover.
import QtQuick
import QtQuick.Controls
import Inkleaf

Item {
    id: root
    property color coverColor: "#2563eb"
    property string thumbnail: ""
    property bool hasCover: false
    property string title
    property bool favourite: false
    property bool raised: false
    /// Password protected; `unlocked`: open this session.
    property bool locked: false
    property bool unlocked: false
    readonly property bool full: hasCover && thumbnail.length > 0

    // Lifted: a soft shadow.
    Shadow { anchors.fill: parent; radius: 10; level: 2; opacity: root.raised ? 1 : 0; Behavior on opacity { NumberAnimation { duration: 200 } } }
    // At rest: a hairline under the book.
    RoundImage { x: 1; y: 3; width: parent.width - 2; height: parent.height - 2; color: Qt.rgba(40 / 255, 34 / 255, 24 / 255, 0.08); radiusTopLeft: 3; radiusBottomLeft: 3; radiusTopRight: 9; radiusBottomRight: 9 }
    RoundImage { y: 1; width: parent.width; height: parent.height; color: Qt.rgba(0, 0, 0, 0.14); radiusTopLeft: 4; radiusBottomLeft: 4; radiusTopRight: 10; radiusBottomRight: 10 }
    // Page edges (back to front).
    RoundImage { x: 18; y: 6; width: parent.width - 12; height: parent.height - 12; color: Qt.rgba(0, 0, 0, 0.10); radiusTopRight: 4; radiusBottomRight: 4 }
    RoundImage { x: 17; y: 6; width: parent.width - 12; height: parent.height - 12; color: "#f0eee6"; radiusTopRight: 4; radiusBottomRight: 4 }
    RoundImage { x: 10; y: 3; width: parent.width - 6; height: parent.height - 6; color: Qt.rgba(0, 0, 0, 0.13); radiusTopRight: 7; radiusBottomRight: 7 }
    RoundImage { x: 9; y: 3; width: parent.width - 6; height: parent.height - 6; color: "#fbfaf6"; radiusTopRight: 7; radiusBottomRight: 7 }
    // The cover.
    RoundImage {
        anchors.fill: parent
        color: root.coverColor
        source: root.full ? root.thumbnail : ""
        fit: "contain"
        radiusTopLeft: 4; radiusBottomLeft: 4; radiusTopRight: 10; radiusBottomRight: 10
    }
    // The first page, inset.
    RoundImage {
        visible: !!(!root.full && root.thumbnail.length > 0)
        x: 18; y: 8; width: parent.width - 26; height: parent.height - 16
        source: visible ? root.thumbnail : ""
        radiusTopLeft: 2; radiusTopRight: 2; radiusBottomLeft: 2; radiusBottomRight: 2
    }
    // No picture yet: the title on a label.
    Rectangle { antialiasing: true;
        visible: root.thumbnail.length === 0
        x: 22; y: parent.height * 0.22
        width: parent.width - 36
        height: label.implicitHeight + 20
        radius: 4
        color: Qt.rgba(1, 1, 1, 0.92)
        Label { id: label; x: 10; y: 10; width: parent.width - 20; text: root.title; wrapMode: Text.WrapAnywhere; maximumLineCount: 4; elide: Text.ElideRight; font.weight: Font.DemiBold; color: "#1d1f23" }
    }
    // The spine.
    RoundImage { width: root.full ? 6 : 10; height: parent.height; color: Qt.rgba(0, 0, 0, 0.18); radiusTopLeft: 4; radiusBottomLeft: 4 }
    // Favourite, password.
    Row {
        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
        spacing: 4
        Rectangle { antialiasing: true;
            visible: root.locked
            width: 26; height: 26; radius: 13
            color: Qt.rgba(1, 1, 1, 0.92)
            Icon { anchors.centerIn: parent; name: root.unlocked ? "unlock" : "lock"; size: 14; color: "#1d1f23" }
        }
        Rectangle { antialiasing: true;
            visible: root.favourite
            width: 26; height: 26; radius: 13
            color: Qt.rgba(1, 1, 1, 0.92)
            Icon { anchors.centerIn: parent; name: "star"; size: 15; color: "#d99a00"; filled: true }
        }
    }
}
