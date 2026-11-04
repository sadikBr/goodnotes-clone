// A lesson plan, a test or a class register: its language, the daftar it
// takes the school, the teacher, the subject, the year (and a register's
// students) from, and the few things that differ between them.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    /// "lesson", "test" or "register".
    property string kind: "lesson"
    property var folder: null
    /// The notebook made, to open.
    signal created(var notebook)
    signal failed(string message)

    property var daftars: []
    property string lang: "ar"
    property string daftarId: ""
    property string cls: ""
    property string subject: ""
    property string level: ""
    property string lessonTitle: ""
    property string duration: ""
    property string number: "1"
    property int exercises: 3
    property int tests: 3
    property string names: ""
    readonly property var daftar: daftars.find(d => d.notebookId === daftarId) || null
    readonly property var classes: daftar ? daftar.classes : []
    readonly property var listed: kind === "register" && daftarId && cls ? DocKinds.students(daftarId, cls) : []
    readonly property var heading: ({ lesson: "New lesson plan · جذاذة", test: "New test · فرض", register: "New class register · سجل" })[kind]

    function show(k, folderId) {
        kind = k
        folder = folderId
        daftars = DocKinds.daftars()
        daftarId = daftars.length ? daftars[0].notebookId : ""
        cls = classes.length ? classes[0] : ""
        subject = daftar ? daftar.subject : ""
        level = ""; lessonTitle = ""; duration = k === "test" ? "1h" : ""; number = "1"; names = ""
        open()
    }
    function create() {
        const typed = names.split("\n").map(n => n.trim()).filter(n => n.length)
        const o = { lang: lang, daftarId: daftarId, cls: cls, subject: subject, level: level, title: lessonTitle, duration: duration,
                    number: number, exercises: exercises, tests: tests, students: listed.length ? listed : typed }
        const nb = kind === "lesson" ? DocKinds.lessonPlan(o, folder || "") : kind === "test" ? DocKinds.test(o, folder || "") : DocKinds.classRegister(o, folder || "")
        if (!nb || !nb.id) return
        close()
        created(nb)
    }
    Connections { target: DocKinds; function onFailed(m) { root.failed(m) } }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 520)
    height: Math.min(parent.height - 32, body.implicitHeight + 140)
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

    component FieldLabel: Label { font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted; Layout.topMargin: 12; Layout.bottomMargin: 5 }

    contentItem: ColumnLayout {
        spacing: 0
        Label { text: root.heading; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text; Layout.margins: 22; Layout.bottomMargin: 10 }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: body.implicitHeight + 10
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: body
                x: 22
                width: parent.width - 44
                spacing: 0
                FieldLabel { text: "Language · اللغة" }
                Segmented { options: [{ value: "ar", label: "العربية" }, { value: "fr", label: "Français" }]; value: root.lang; onPicked: v => root.lang = v }

                FieldLabel { text: root.daftars.length ? "From the daftar · من الدفتر" : "Daftar" }
                UiSelect {
                    visible: root.daftars.length > 0
                    Layout.fillWidth: true
                    options: root.daftars.map(d => ({ value: d.notebookId, label: d.title })).concat([{ value: "", label: "None (fill in by hand)" }])
                    value: root.daftarId
                    onPicked: v => {
                        root.daftarId = v
                        root.cls = root.classes.length ? root.classes[0] : ""
                        if (root.daftar && root.daftar.subject) root.subject = root.daftar.subject
                    }
                }
                Label {
                    visible: root.daftars.length === 0
                    text: "No daftar yet: the school, teacher and year are left for you to write in."
                    color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 0
                        FieldLabel { text: "Class · القسم" }
                        UiSelect {
                            visible: root.classes.length > 0
                            Layout.fillWidth: true
                            options: root.classes.map(c => ({ value: c, label: c }))
                            value: root.cls
                            onPicked: v => root.cls = v
                        }
                        UiField { visible: root.classes.length === 0; Layout.fillWidth: true; text: root.cls; placeholderText: "3/6"; onTextEdited: root.cls = text }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 0
                        FieldLabel { text: "Subject · المادة" }
                        UiField { Layout.fillWidth: true; text: root.subject; onTextEdited: root.subject = text }
                    }
                }

                // A lesson plan.
                ColumnLayout {
                    visible: root.kind === "lesson"
                    Layout.fillWidth: true
                    spacing: 0
                    FieldLabel { text: "Lesson · عنوان الدرس" }
                    UiField { Layout.fillWidth: true; text: root.lessonTitle; placeholderText: "…"; onTextEdited: root.lessonTitle = text }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        ColumnLayout { Layout.fillWidth: true; Layout.preferredWidth: 1; spacing: 0
                            FieldLabel { text: "Level · المستوى" }
                            UiField { Layout.fillWidth: true; text: root.level; onTextEdited: root.level = text } }
                        ColumnLayout { Layout.fillWidth: true; Layout.preferredWidth: 1; spacing: 0
                            FieldLabel { text: "Duration · المدة" }
                            UiField { Layout.fillWidth: true; text: root.duration; placeholderText: "55 min"; onTextEdited: root.duration = text } }
                    }
                }

                // A test.
                ColumnLayout {
                    visible: root.kind === "test"
                    Layout.fillWidth: true
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        ColumnLayout { Layout.fillWidth: true; Layout.preferredWidth: 1; spacing: 0
                            FieldLabel { text: "Number · الرقم" }
                            UiField { Layout.fillWidth: true; text: root.number; onTextEdited: root.number = text } }
                        ColumnLayout { Layout.fillWidth: true; Layout.preferredWidth: 1; spacing: 0
                            FieldLabel { text: "Level · المستوى" }
                            UiField { Layout.fillWidth: true; text: root.level; onTextEdited: root.level = text } }
                        ColumnLayout { Layout.fillWidth: true; Layout.preferredWidth: 1; spacing: 0
                            FieldLabel { text: "Duration · المدة" }
                            UiField { Layout.fillWidth: true; text: root.duration; onTextEdited: root.duration = text } }
                    }
                    FieldLabel { text: "Exercises · عدد التمارين" }
                    Segmented { options: [2, 3, 4, 5].map(n => ({ value: n, label: String(n) })); value: root.exercises; onPicked: v => root.exercises = v }
                }

                // A register.
                ColumnLayout {
                    visible: root.kind === "register"
                    Layout.fillWidth: true
                    spacing: 0
                    FieldLabel { text: "Tests in the marks table · عدد الفروض" }
                    Segmented { options: [2, 3, 4, 5].map(n => ({ value: n, label: String(n) })); value: root.tests; onPicked: v => root.tests = v }
                    Label {
                        visible: root.listed.length > 0
                        text: "✓ " + root.listed.length + " students from the daftar's list for " + root.cls + "."
                        color: Theme.accent; font.pixelSize: 12; font.weight: Font.DemiBold
                        Layout.topMargin: 12
                    }
                    FieldLabel { visible: root.listed.length === 0; text: "Students, one per line (or paste the list from Massar)" }
                    ScrollView {
                        visible: root.listed.length === 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        TextArea {
                            text: root.names
                            onTextChanged: root.names = text
                            color: Theme.text
                            wrapMode: TextEdit.NoWrap
                            placeholderText: "Ahmed Alami\nSara Bennani\n…"
                            placeholderTextColor: Theme.muted
                            background: Rectangle { antialiasing: true; radius: Theme.radius; color: Theme.surface2; border.color: Theme.border }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.leftMargin: 22
            spacing: 8
            Item { Layout.fillWidth: true }
            UiButton { text: "Cancel"; kind: "ghost"; onClicked: root.close() }
            UiButton { text: "Create"; kind: "primary"; onClicked: root.create() }
        }
    }
}
