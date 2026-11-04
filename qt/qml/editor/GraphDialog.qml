// A graph's functions and window, with a live picture of it: for a new graph
// (Insert) or the selected one (Save).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    required property InkCanvas canvas
    /// Editing the selected graph (else a new one goes on the page).
    property bool editing: false
    property real xmin: -5
    property real xmax: 5
    property real ymin: -5
    property real ymax: 5
    property bool grid: true
    readonly property var spec: ({ functions: fns.text, xmin: xmin, xmax: xmax, ymin: ymin, ymax: ymax, grid: grid })
    readonly property var checks: MathKit.checkFunctions(fns.text)
    readonly property bool valid: fns.text.trim().length > 0 && checks.every(c => c.ok) && xmax > xmin && ymax > ymin

    /// Opens on a graph ({ functions, xmin, … }; null: a new one).
    function show(g, edit) {
        editing = !!edit
        fns.text = g && g.functions ? g.functions : "y = x² − 2x"
        xmin = g ? g.xmin : -5
        xmax = g ? g.xmax : 5
        ymin = g ? g.ymin : -5
        ymax = g ? g.ymax : 5
        grid = g ? g.grid !== false : true
        open()
        fns.forceActiveFocus()
        fns.selectAll()
    }
    function apply() {
        if (!valid) return
        if (editing) canvas.updateGraph(spec)
        else canvas.insertGraph(spec)
        close()
    }
    /// The y window that shows the curves (x stays as it is).
    function fitY() {
        const r = MathKit.fitY(fns.text, xmin, xmax)
        if (r.ymin === undefined) return
        ymin = r.ymin
        ymax = r.ymax
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 720)
    modal: true
    focus: true
    padding: 24
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

    component Num: ColumnLayout {
        id: num
        property string label
        property real value
        signal edited(real v)
        spacing: 4
        Label { text: num.label; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted }
        UiField {
            Layout.preferredWidth: 84
            text: String(num.value).replace(".", ",").replace("-", "−")
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            onEditingFinished: {
                const v = Number(text.replace(",", ".").replace("−", "-"))
                if (isFinite(v)) num.edited(v)
                else text = String(num.value).replace(".", ",").replace("-", "−")
            }
        }
    }

    contentItem: RowLayout {
        spacing: 22
        ColumnLayout {
            Layout.preferredWidth: 300
            Layout.maximumWidth: 300
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignTop
            spacing: 6
            Label { text: root.editing ? "Edit graph" : "New graph"; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text; Layout.bottomMargin: 10 }
            Label { text: "Functions, one per line"; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 104
                TextArea {
                    id: fns
                    wrapMode: TextEdit.NoWrap
                    color: Theme.text
                    font.pixelSize: 15
                    leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 10
                    placeholderText: "y = x² − 2x\nf(x) = sin(x)"
                    placeholderTextColor: Theme.muted
                    selectionColor: Theme.alpha(Theme.accent, 0.3)
                    background: Rectangle { antialiasing: true; radius: Theme.radius; color: Theme.surface2; border.width: fns.activeFocus ? 2 : 1; border.color: fns.activeFocus ? Theme.accent : Theme.border }
                }
            }
            Label {
                readonly property int bad: root.checks.findIndex(c => !c.ok)
                visible: bad >= 0
                text: bad >= 0 ? "Line " + (bad + 1) + ": " + root.checks[bad].error : ""
                color: Theme.danger
                font.pixelSize: 12
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: "Write it as you would on paper: y = 2x + 1, f(x) = x³ − 3x, sin x, √x, 1/x, |x|."
                color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.bottomMargin: 8
            }
            GridLayout {
                columns: 2
                columnSpacing: 10
                rowSpacing: 8
                Num { label: "x from"; value: root.xmin; onEdited: v => root.xmin = v }
                Num { label: "x to"; value: root.xmax; onEdited: v => root.xmax = v }
                Num { label: "y from"; value: root.ymin; onEdited: v => root.ymin = v }
                Num { label: "y to"; value: root.ymax; onEdited: v => root.ymax = v }
            }
            RowLayout {
                Layout.topMargin: 6
                spacing: 8
                UiSwitch { checked: root.grid; onClicked: root.grid = checked }
                Label { text: "Grid"; color: Theme.text; Layout.fillWidth: true }
                UiButton { text: "Fit y"; kind: "ghost"; enabled: root.valid; onClicked: root.fitY() }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.topMargin: 14
                spacing: 8
                Item { Layout.fillWidth: true }
                UiButton { text: "Cancel"; kind: "ghost"; onClicked: root.close() }
                UiButton { text: root.editing ? "Save" : "Insert"; kind: "primary"; enabled: root.valid; onClicked: root.apply() }
            }
        }
        // The graph as it will look.
        Rectangle {
            Layout.fillWidth: true
            Layout.minimumWidth: 260
            Layout.preferredWidth: 340
            Layout.preferredHeight: Math.max(220, width * 0.8)
            Layout.alignment: Qt.AlignTop
            radius: Theme.radius
            color: Theme.surface2
            border.color: Theme.border
            LiveImage {
                anchors.fill: parent
                anchors.margins: 10
                fillMode: Image.PreserveAspectFit
                source: root.opened && root.valid ? MathKit.graphPicture(root.spec, Math.round(width), Math.round(height)) : ""
            }
            Label {
                visible: !root.valid
                anchors.centerIn: parent
                text: "Nothing to draw yet"
                color: Theme.muted
            }
        }
    }
}
