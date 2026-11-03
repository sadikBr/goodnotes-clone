// A modal form (the web app's formDialog / confirmDialog):
//   dialog.ask("New folder", [{ name, label, type?, value, options?, colors? }],
//              "Create", values => ..., { message?, danger?, aside? })
// Field types: "text" (default), "select", "swatches", "segmented".
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
    signal changed(string name, var value)

    function ask(t, f, ok, cb, opts) {
        opts = opts || {}
        title = t
        okLabel = ok || "OK"
        message = opts.message || ""
        danger = !!opts.danger
        aside = opts.aside || null
        onOk = cb
        const v = {}
        for (const x of f) v[x.name] = x.value !== undefined ? x.value : (x.type === "swatches" ? (typeof x.colors[0] === "string" ? x.colors[0] : x.colors[0].color) : "")
        values = v
        fields = f
        open()
    }
    function set(name, value) {
        const v = Object.assign({}, values)
        v[name] = value
        values = v
        changed(name, value)
    }
    function submit() {
        for (const f of fields)
            if ((f.type || "text") === "text" && f.required !== false && String(values[f.name]).trim() === "") return
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
    width: Math.min(parent.width - 32, aside ? 860 : 420)
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
        Rectangle { anchors.fill: parent; radius: Theme.radiusXl; color: Theme.surface; border.color: Theme.border }
    }

    contentItem: RowLayout {
        spacing: 0
        // Enter submits from anywhere in the form (e.g. after picking a colour).
        Keys.onReturnPressed: root.submit()
        Keys.onEnterPressed: root.submit()
        // The side panel (e.g. a cover preview).
        Rectangle {
            visible: root.aside !== null
            Layout.preferredWidth: 330
            Layout.fillHeight: true
            radius: Theme.radiusXl
            color: Theme.surface2
            // Square off the inner corners.
            Rectangle { anchors.right: parent.right; width: parent.radius; height: parent.height; color: parent.color }
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
                Label { text: root.title; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap; Layout.bottomMargin: 18 }
                Label { visible: root.message.length > 0; text: root.message; color: Theme.muted; wrapMode: Text.Wrap; lineHeight: 1.25; Layout.fillWidth: true; Layout.bottomMargin: 18 }
                Repeater {
                    model: root.fields
                    delegate: ColumnLayout {
                        id: row
                        required property var modelData
                        required property int index
                        visible: (modelData.type || "text") !== "hidden"
                        Layout.fillWidth: true
                        Layout.bottomMargin: 16
                        spacing: 7
                        Label { visible: !!row.modelData.label; text: row.modelData.label || ""; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted }
                        Loader {
                            Layout.fillWidth: true
                            property var field: row.modelData
                            property bool first: row.index === 0
                            sourceComponent: { const t = row.modelData.type || "text"; return t === "select" ? selectField : t === "swatches" ? swatchField : t === "segmented" ? segField : t === "covers" ? coverField : textField }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    UiButton { text: "Cancel"; kind: "ghost"; onClicked: root.close() }
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
        Flow {
            id: coversFlow
            readonly property var field: parent.field
            spacing: 8
            Repeater {
                model: coversFlow.field.options
                delegate: AbstractButton {
                    id: cv
                    required property var modelData
                    readonly property bool on: root.values[coversFlow.field.name] === modelData.value
                    width: 64
                    height: 104
                    hoverEnabled: true
                    onClicked: root.set(coversFlow.field.name, modelData.value)
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.label
                    background: Item {}
                    contentItem: Column {
                        spacing: 4
                        Item {
                            width: 64; height: 86
                            Rectangle { anchors.fill: parent; anchors.margins: cv.on ? -3 : -1; radius: 6; color: cv.on ? Theme.accent : cv.hovered ? Theme.borderStrong : Theme.border }
                            Rectangle {
                                visible: cv.modelData.value === "none"
                                anchors.fill: parent
                                radius: 4
                                color: Theme.surface2
                                Label { anchors.centerIn: parent; text: "None"; color: Theme.muted; font.pixelSize: 12 }
                            }
                            Image {
                                visible: cv.modelData.value !== "none"
                                anchors.fill: parent
                                sourceSize: Qt.size(128, 172)
                                fillMode: Image.PreserveAspectFit
                                source: cv.modelData.value === "none" ? "" : root.coverUrl(cv.modelData.value)
                                asynchronous: false
                            }
                        }
                        Label { width: 64; text: cv.modelData.label; font.pixelSize: 11; color: cv.on ? Theme.accent : Theme.muted; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
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
