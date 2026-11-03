// A modal form (the web app's formDialog / confirmDialog):
//   dialog.ask("New folder", [{ name, label, type?, value, options?, colors? }],
//              "Create", values => ..., { message?, danger?, aside? })
// Field types: "text" (default), "select", "swatches", "segmented", "covers",
// "hidden", "custom" (field.component, given `field` and `dialog`).
// Options: width, twoColumns (fields with `half` share a row), validate
// (values => error text or null), message, danger, aside.
// `aside` is a Component shown beside the form; it can read `dialog.values`.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    property string title
    property string message
    property string okLabel: "OK"
    property bool danger: false
    property var fields: []
    property var values: ({})
    property Component aside: null
    property var onOk: null
    property int formWidth: 420
    property bool twoColumns: false
    property var validate: null
    property string error: ""
    signal changed(string name, var value)

    function ask(t, f, ok, cb, opts) {
        opts = opts || {}
        title = t
        okLabel = ok || "OK"
        message = opts.message || ""
        danger = !!opts.danger
        aside = opts.aside || null
        formWidth = opts.width || 420
        twoColumns = !!opts.twoColumns
        validate = opts.validate || null
        error = ""
        onOk = cb
        const v = {}
        for (const x of f) v[x.name] = x.value !== undefined ? x.value : (x.type === "swatches" ? (typeof x.colors[0] === "string" ? x.colors[0] : x.colors[0].color) : "")
        values = v
        previewTitle = v.title || ""
        fields = f
        open()
    }
    function set(name, value) {
        const v = Object.assign({}, values)
        v[name] = value
        values = v
        error = ""
        changed(name, value)
    }
    function submit() {
        for (const f of fields)
            if ((f.type || "text") === "text" && f.required !== false && !root.validate && String(values[f.name]).trim() === "") return
        if (validate) {
            const problem = validate(values)
            if (problem) { error = problem; return }
        }
        const cb = onOk
        const v = values
        close()
        if (cb) Qt.callLater(() => cb(v))
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    width: Math.min(parent.width - 32, aside ? 860 : formWidth)
    height: Math.min(parent.height - 32, implicitHeight)

    Overlay.modal: Rectangle {
        color: Qt.rgba(20 / 255, 18 / 255, 14 / 255, 0.45)
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 180; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 100 } }

    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusXl; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }

    contentItem: RowLayout {
        spacing: 0
        // Enter submits from anywhere in the form (e.g. after picking a colour).
        Keys.onReturnPressed: root.submit()
        Keys.onEnterPressed: root.submit()
        // The side panel (e.g. a cover preview): .modal-aside, surface-2 with a
        // faint glow of the accent at the top left.
        Rectangle { antialiasing: true;
            visible: root.aside !== null
            Layout.preferredWidth: 330
            Layout.fillHeight: true
            radius: Theme.radiusXl
            color: Theme.surface2
            clip: true
            // Square off the inner corners.
            Rectangle { anchors.right: parent.right; width: parent.radius; height: parent.height; color: parent.color }
            Image {
                anchors.fill: parent
                sourceSize: Qt.size(330, Math.max(1, Math.round(parent.height)))
                source: !root.visible || !root.aside || parent.height < 10 ? "" : "data:image/svg+xml;utf8," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="330" height="' + Math.max(1, Math.round(parent.height)) + '"><defs><radialGradient id="g" gradientUnits="userSpaceOnUse" cx="99" cy="' + Math.round(0.2 * parent.height) + '" r="' + Math.round(0.8 * parent.height) + '"><stop offset="0" stop-color="' + Theme.accent + '" stop-opacity="0.10"/><stop offset="0.6" stop-color="' + Theme.accent + '" stop-opacity="0"/></radialGradient></defs><rect width="330" height="' + Math.max(1, Math.round(parent.height)) + '" fill="url(#g)"/></svg>')
            }
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
            Loader { anchors.centerIn: parent; sourceComponent: root.aside; active: root.visible && root.aside !== null }
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: form.implicitHeight + 48
            contentHeight: form.implicitHeight + 48
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: form
                x: 24; y: 24
                width: parent.width - 48
                spacing: 0
                Label { text: root.title; horizontalAlignment: Text.AlignLeft; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap; Layout.bottomMargin: 18 }
                Label { visible: root.message.length > 0; text: root.message; color: Theme.muted; wrapMode: Text.Wrap; lineHeight: 1.25; Layout.fillWidth: true; Layout.bottomMargin: 18 }
                Flow {
                    id: fieldsFlow
                    Layout.fillWidth: true
                    spacing: 14
                    Layout.bottomMargin: 16
                    Repeater {
                        model: root.fields
                        delegate: Item {
                            id: row
                            required property var modelData
                            required property int index
                            readonly property bool half: root.twoColumns && !!modelData.half
                            visible: (modelData.type || "text") !== "hidden"
                            width: half ? (fieldsFlow.width - 14) / 2 : fieldsFlow.width
                            height: col.implicitHeight + 2
                            ColumnLayout {
                                id: col
                                width: parent.width
                                spacing: 7
                                Label { visible: !!row.modelData.label; text: "\u200E" + (row.modelData.label || ""); horizontalAlignment: Text.AlignLeft; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                Loader {
                                    Layout.fillWidth: true
                                    property var field: row.modelData
                                    property var dialog: root
                                    property bool first: row.index === 0
                                    sourceComponent: { const t = row.modelData.type || "text"; return t === "custom" ? row.modelData.component : t === "select" ? selectField : t === "swatches" ? swatchField : t === "segmented" ? segField : t === "covers" ? coverField : textField }
                                }
                            }
                        }
                    }
                }
                Label {
                    visible: root.error.length > 0
                    text: root.error
                    color: "#dc2626"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Layout.bottomMargin: 6
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    UiButton { text: "Cancel"; onClicked: root.close() }
                    UiButton { text: root.okLabel; kind: root.danger ? "danger" : "primary"; onClicked: root.submit() }
                }
            }
        }
    }

    Component {
        id: textField
        UiField {
            text: root.values[parent.field.name] ?? ""
            placeholderText: parent.field.placeholder || ""
            onTextEdited: root.set(parent.field.name, text)
            onAccepted: root.submit()
            Component.onCompleted: if (parent.first) { forceActiveFocus(); selectAll() }
        }
    }
    Component {
        id: selectField
        UiSelect {
            options: parent.field.options
            value: root.values[parent.field.name]
            onPicked: v => root.set(parent.field.name, v)
        }
    }
    Component {
        id: swatchField
        Swatches {
            colors: parent.field.colors
            value: root.values[parent.field.name] ?? ""
            onPicked: c => root.set(parent.field.name, c)
        }
    }
    // Cover designs with previews (the notebook's title and colour on each).
    property string previewTitle: ""
    Timer { id: titleDelay; interval: 350; onTriggered: root.previewTitle = root.values.title || "" }
    onValuesChanged: if ((values.title || "") !== previewTitle) titleDelay.restart()
    onOpened: previewTitle = values.title || ""
    function coverUrl(design, w) {
        return "image://cover/" + design + "/" + String(values.cover || "#2563eb").replace("#", "") + "/" + (values.orientation === "landscape" ? 1 : 0) + "/" + encodeURIComponent(previewTitle || "Untitled")
    }
    Component {
        id: coverField
        // .cover-designs: tiles at least 76 wide filling the row, 8 apart.
        Grid {
            id: coversFlow
            readonly property var field: parent.field
            readonly property int cols: Math.max(1, Math.floor((width + 8) / 84))
            readonly property real tileW: (width - (cols - 1) * 8) / cols
            width: parent.width
            columns: cols
            spacing: 8
            Repeater {
                model: coversFlow.field.options
                delegate: AbstractButton {
                    id: cv
                    required property var modelData
                    readonly property bool on: root.values[coversFlow.field.name] === modelData.value
                    width: coversFlow.tileW
                    height: 90 + 5 + 16 + 11
                    hoverEnabled: true
                    onClicked: root.set(coversFlow.field.name, modelData.value)
                    background: Rectangle {
                        antialiasing: true
                        radius: 10
                        color: cv.on ? Theme.accentSoft : cv.hovered ? Theme.surface2 : "transparent"
                        border.width: 1.5
                        border.color: cv.on ? Theme.accent : "transparent"
                    }
                    contentItem: Item {
                        Item {
                            id: pic
                            x: Math.round((cv.width - 64) / 2)
                            y: 6
                            width: 64; height: 90
                            Shadow { anchors.fill: parent; radius: 4; level: 1 }
                            Rectangle {
                                visible: cv.modelData.value === "none"
                                anchors.fill: parent
                                antialiasing: true
                                radius: 4
                                color: Theme.surface2
                                Label { anchors.centerIn: parent; text: "Aa"; color: Theme.muted; font.pixelSize: 18 }
                            }
                            Image {
                                visible: cv.modelData.value !== "none"
                                anchors.fill: parent
                                sourceSize: Qt.size(128, 180)
                                fillMode: Image.PreserveAspectFit
                                source: cv.modelData.value === "none" ? "" : root.coverUrl(cv.modelData.value)
                                asynchronous: false
                            }
                        }
                        Label {
                            anchors.top: pic.bottom
                            anchors.topMargin: 5
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: cv.width - 8
                            text: cv.modelData.label
                            font.pixelSize: 12
                            color: Theme.text
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
    Component {
        id: segField
        Segmented {
            options: parent.field.options
            value: root.values[parent.field.name]
            onPicked: v => root.set(parent.field.name, v)
        }
    }
}
