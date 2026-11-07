// A new notebook in two steps that fit a tablet: 1. Cover (title, colour,
// design), 2. Paper (kind, size, orientation, colour), with the result shown
// beside them as you choose. Presets start it for other kinds of documents
// (a sketchbook: blank landscape pages).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    /// [{ value, label, group }] paper kinds, and [{ value, label }] cover designs.
    property var templates: []
    property var designs: []
    /// Chosen when Create is pressed: { title, template, size, orientation, paper, cover, design }.
    signal created(var values)

    property int step: 0
    property string heading: "New notebook"
    property string title: ""
    property string template: "ruled"
    property string size: "a4"
    property string orientation: "portrait"
    property string paper: "#fffdf7"
    property string cover: Theme.coverColors[0]
    property string design: "classic"
    /// One of your templates instead of a paper ("" for none): [{ id, name, preview, width, height }].
    property var mine: []
    property string mineId: ""
    readonly property var mineT: mine.find(t => t.id === mineId) || null
    readonly property bool narrow: width < 640
    readonly property bool landscape: orientation === "landscape"

    /// Opens it, with `preset` values (and a heading) for another kind of document.
    function show(preset) {
        const p = preset || {}
        heading = p.heading || "New notebook"
        title = p.title || "Untitled notebook"
        template = p.template || "ruled"
        size = p.size || "a4"
        orientation = p.orientation || "portrait"
        paper = p.paper || "#fffdf7"
        cover = p.cover || Theme.coverColors[0]
        design = p.design || "classic"
        try { mine = InkleafCore.call("pageTemplates") } catch (e) { mine = [] }
        mineId = ""
        step = 0
        open()
        titleField.forceActiveFocus()
        titleField.selectAll()
    }
    function create() {
        close()
        created({ title: title.trim() || "Untitled notebook", template: template, size: size, orientation: orientation, paper: paper, cover: cover, design: design, mine: mineT })
    }
    function coverUrl(d) {
        return "image://cover/" + d + "/" + cover.replace("#", "") + "/" + (landscape ? 1 : 0) + "/" + encodeURIComponent(previewTitle || "Untitled")
    }
    property string previewTitle: ""
    onTitleChanged: titleDelay.restart()
    Timer { id: titleDelay; interval: 300; onTriggered: root.previewTitle = root.title }
    onOpened: previewTitle = title

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - (Theme.phone ? 16 : 32), 860)
    height: Theme.phone ? parent.height - 24 : Math.min(parent.height - 32, 620)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: Qt.rgba(20 / 255, 18 / 255, 14 / 255, 0.45) }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 180; easing.type: Easing.OutCubic }
    }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }

    component FieldLabel: Label { font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted; Layout.topMargin: 14; Layout.bottomMargin: 6 }

    contentItem: RowLayout {
        spacing: 0
        Keys.onReturnPressed: root.step === 0 ? root.step = 1 : root.create()
        Keys.onEnterPressed: root.step === 0 ? root.step = 1 : root.create()
        // What it will look like: the cover, then a page.
        Rectangle {
            visible: !root.narrow
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            radius: Theme.radiusXl
            color: Theme.surface2
            Rectangle { anchors.right: parent.right; width: parent.radius; height: parent.height; color: parent.color }
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
            Item {
                anchors.centerIn: parent
                width: root.landscape ? 250 : 200
                height: root.landscape ? 177 : 283
                Shadow { anchors.fill: parent; radius: Theme.radius; level: 3 }
                LiveImage {
                    visible: root.step === 0
                    anchors.fill: parent
                    sourceSize: Qt.size(parent.width * 2, parent.height * 2)
                    source: root.opened && root.step === 0 ? root.coverUrl(root.design) : ""
                }
                Image {
                    visible: root.step === 1 && !root.mineT
                    anchors.fill: parent
                    sourceSize: Qt.size(parent.width * 2, parent.height * 2)
                    source: root.opened && root.step === 1 && !root.mineT ? "image://paper/" + root.template + "/" + root.paper.replace("#", "") + "/whole" : ""
                    fillMode: Image.Stretch
                }
                Rectangle { visible: root.step === 1 && !!root.mineT; anchors.fill: parent; color: "#ffffff" }
                Image {
                    visible: root.step === 1 && !!root.mineT
                    anchors.fill: parent
                    source: root.mineT ? root.mineT.preview || "" : ""
                    fillMode: Image.PreserveAspectFit
                }
                transform: Rotation { origin.x: 100; origin.y: 141; axis { x: 0; y: 1; z: 0 } angle: -6 }
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 26
                text: root.step === 0 ? (root.designs.find(d => d.value === root.design) || { label: "" }).label + " cover"
                                      : root.mineT ? root.mineT.name + " · your template"
                                      : (root.templates.find(t => t.value === root.template) || { label: "" }).label + " · " + (Theme.paperSizes.find(p => p.id === root.size) || { label: "" }).label
                color: Theme.muted
                font.pixelSize: 12
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            // Title and the two steps.
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 22
                Layout.bottomMargin: 12
                spacing: 10
                Label { text: root.heading; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight }
                Repeater {
                    model: ["Cover", "Paper"]
                    delegate: AbstractButton {
                        id: stepBtn
                        required property string modelData
                        required property int index
                        readonly property bool on: root.step === index
                        implicitHeight: 30
                        implicitWidth: stepRow.implicitWidth + 20
                        onClicked: root.step = index
                        background: Rectangle { antialiasing: true; radius: Theme.radius; color: stepBtn.on ? Theme.alpha(Theme.accent, 0.14) : "transparent"; border.color: stepBtn.on ? "transparent" : Theme.border }
                        contentItem: RowLayout {
                            id: stepRow
                            spacing: 6
                            Rectangle {
                                antialiasing: true
                                Layout.preferredWidth: 18; Layout.preferredHeight: 18; radius: Theme.radiusXs; Layout.leftMargin: 4
                                color: stepBtn.on || root.step > stepBtn.index ? Theme.accent : Theme.surface3
                                Label { anchors.centerIn: parent; text: root.step > stepBtn.index ? "✓" : String(stepBtn.index + 1); font.pixelSize: 11; font.weight: Font.Bold; color: stepBtn.on || root.step > stepBtn.index ? Theme.accentContrast : Theme.muted }
                            }
                            Label { text: stepBtn.modelData; font.pixelSize: 13; font.weight: stepBtn.on ? Font.DemiBold : Font.Normal; color: stepBtn.on ? Theme.text : Theme.muted; Layout.rightMargin: 4 }
                        }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

            // 1. Cover.
            Flickable {
                visible: root.step === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: coverCol.implicitHeight + 24
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: coverCol
                    x: 22
                    width: parent.width - 44
                    spacing: 0
                    FieldLabel { text: "Title" }
                    UiField {
                        id: titleField
                        Layout.fillWidth: true
                        text: root.title
                        onTextEdited: root.title = text
                    }
                    FieldLabel { text: "Cover colour" }
                    Swatches { Layout.fillWidth: true; colors: Theme.coverColors; value: root.cover; onPicked: c => root.cover = c }
                    FieldLabel { text: "Design" }
                    // Every design, in rows that fit (no sideways scrolling).
                    GridLayout {
                        id: designGrid
                        Layout.fillWidth: true
                        columns: Math.max(2, Math.floor((width + 8) / 96))
                        columnSpacing: 8
                        rowSpacing: 8
                        Repeater {
                            model: root.designs
                            delegate: AbstractButton {
                                id: dv
                                required property var modelData
                                readonly property bool on: root.design === modelData.value
                                Layout.fillWidth: true
                                implicitHeight: 128
                                hoverEnabled: true
                                onClicked: root.design = modelData.value
                                background: Rectangle { antialiasing: true; radius: Theme.radius; color: dv.on ? Theme.accentSoft : dv.hovered ? Theme.surface2 : "transparent"; border.width: 1.5; border.color: dv.on ? Theme.accent : "transparent" }
                                contentItem: Item {
                                    Item {
                                        id: dpic
                                        x: Math.round((dv.width - 64) / 2); y: 6
                                        width: 64; height: 90
                                        Shadow { anchors.fill: parent; radius: 4; level: 1 }
                                        LiveImage { anchors.fill: parent; sourceSize: Qt.size(128, 180); fillMode: Image.PreserveAspectFit; source: root.opened ? root.coverUrl(dv.modelData.value) : "" }
                                    }
                                    Label { anchors.top: dpic.bottom; anchors.topMargin: 5; anchors.horizontalCenter: parent.horizontalCenter; width: dv.width - 8; text: dv.modelData.label; font.pixelSize: 12; color: Theme.text; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                                }
                            }
                        }
                    }
                }
            }

            // 2. Paper.
            Flickable {
                visible: root.step === 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: paperCol.implicitHeight + 24
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: paperCol
                    x: 22
                    width: parent.width - 44
                    spacing: 0
                    // Side by side; on a phone, one under the other.
                    GridLayout {
                        Layout.fillWidth: true
                        columns: Theme.phone ? 1 : 2
                        columnSpacing: 16
                        rowSpacing: 4
                        ColumnLayout {
                            spacing: 0
                            FieldLabel { text: "Size" }
                            Segmented { options: Theme.paperSizes.map(p => ({ value: p.id, label: p.label })); value: root.size; onPicked: v => root.size = v }
                        }
                        ColumnLayout {
                            spacing: 0
                            FieldLabel { text: "Orientation" }
                            Segmented { options: [{ value: "portrait", label: "Portrait" }, { value: "landscape", label: "Landscape" }]; value: root.orientation; onPicked: v => root.orientation = v }
                        }
                    }
                    FieldLabel { text: "Paper colour" }
                    Swatches { Layout.fillWidth: true; colors: Theme.paperColors; value: root.paper; onPicked: c => root.paper = c }
                    // Your templates first (they set the size themselves).
                    FieldLabel { visible: root.mine.length > 0; text: "Your templates" }
                    GridLayout {
                        visible: root.mine.length > 0
                        Layout.fillWidth: true
                        readonly property int cols: Math.max(3, Math.floor((paperCol.width + 8) / 84))
                        readonly property real cellW: (paperCol.width - (cols - 1) * 8) / cols
                        columns: cols
                        columnSpacing: 8
                        rowSpacing: 8
                        Repeater {
                            model: root.mine
                            delegate: AbstractButton {
                                id: mv
                                required property var modelData
                                readonly property bool on: root.mineId === modelData.id
                                Layout.preferredWidth: parent.cellW
                                implicitHeight: 104
                                hoverEnabled: true
                                onClicked: root.mineId = on ? "" : modelData.id
                                background: Rectangle { antialiasing: true; radius: Theme.radius; color: mv.on ? Theme.accentSoft : mv.hovered ? Theme.surface2 : "transparent"; border.width: 1.5; border.color: mv.on ? Theme.accent : "transparent" }
                                contentItem: Item {
                                    Rectangle {
                                        id: mpic
                                        readonly property real k: Math.min(50 / mv.modelData.width, 68 / mv.modelData.height)
                                        x: Math.round((mv.width - width) / 2); y: 6 + Math.round((68 - height) / 2)
                                        width: Math.round(mv.modelData.width * k); height: Math.round(mv.modelData.height * k)
                                        color: Theme.border
                                        Rectangle { anchors.fill: parent; anchors.margins: 1; color: "#ffffff" }
                                        Image { anchors.fill: parent; anchors.margins: 1; source: mv.modelData.preview || ""; fillMode: Image.PreserveAspectFit; asynchronous: true }
                                    }
                                    Label { y: 80; anchors.horizontalCenter: parent.horizontalCenter; width: mv.width - 6; text: mv.modelData.name; font.pixelSize: 11; color: Theme.text; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                                }
                            }
                        }
                    }
                    // The kinds of paper, by group, as small pages.
                    Repeater {
                        model: [...new Set(root.templates.map(t => t.group))]
                        delegate: ColumnLayout {
                            id: grp
                            required property string modelData
                            Layout.fillWidth: true
                            spacing: 0
                            FieldLabel { text: grp.modelData }
                            GridLayout {
                                Layout.fillWidth: true
                                // The same columns in every group, so they line up.
                                readonly property int cols: Math.max(3, Math.floor((paperCol.width + 8) / 84))
                                readonly property real cellW: (paperCol.width - (cols - 1) * 8) / cols
                                columns: cols
                                columnSpacing: 8
                                rowSpacing: 8
                                Repeater {
                                    model: root.templates.filter(t => t.group === grp.modelData)
                                    delegate: AbstractButton {
                                        id: pv
                                        required property var modelData
                                        readonly property bool on: !root.mineT && root.template === modelData.value
                                        Layout.preferredWidth: parent.cellW
                                        implicitHeight: 104
                                        hoverEnabled: true
                                        onClicked: { root.template = modelData.value; root.mineId = "" }
                                        background: Rectangle { antialiasing: true; radius: Theme.radius; color: pv.on ? Theme.accentSoft : pv.hovered ? Theme.surface2 : "transparent"; border.width: 1.5; border.color: pv.on ? Theme.accent : "transparent" }
                                        contentItem: Item {
                                            Rectangle {
                                                id: ppic
                                                x: Math.round((pv.width - 50) / 2); y: 6
                                                width: 50; height: 68
                                                color: Theme.border
                                                Image { anchors.fill: parent; anchors.margins: 1; sourceSize: Qt.size(100, 136); source: root.opened && root.step === 1 ? "image://paper/" + pv.modelData.value + "/" + root.paper.replace("#", "") + "/whole" : "" }
                                            }
                                            Label { anchors.top: ppic.bottom; anchors.topMargin: 5; anchors.horizontalCenter: parent.horizontalCenter; width: pv.width - 6; text: pv.modelData.label; font.pixelSize: 11; color: Theme.text; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                Layout.leftMargin: 22
                spacing: 8
                UiButton { text: "Cancel"; kind: "ghost"; onClicked: root.close() }
                Item { Layout.fillWidth: true }
                UiButton { visible: root.step === 1; text: "Back"; onClicked: root.step = 0 }
                UiButton { visible: root.step === 0; text: "Next: paper"; kind: "primary"; onClicked: root.step = 1 }
                UiButton { visible: root.step === 1; text: "Create"; kind: "primary"; onClicked: root.create() }
            }
        }
    }
}
