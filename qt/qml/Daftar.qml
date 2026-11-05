// The daftar annossous dialogs (the web app's daftarSetup.ts and the editor's
// daftar bar): school details, classes, a weekly timetable and holidays; the
// student list, lesson outlines and a class's export.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Item {
    id: root
    required property FormDialog dialog
    signal notify(string message, bool error)

    readonly property var days: DaftarApi.days()

    // The step-by-step setup (new daftars and Daftar settings).
    DaftarSetup { id: setup; onFinished: r => root.setupDone(r) }
    property var pending: null

    /** "New daftar": resolves `done(notebook)` once it's made. */
    function askNew(folderId, done) {
        pending = { isNew: true, folderId: folderId, done: done }
        setup.show(DaftarApi.formValues(null, "دفتر النصوص"), true)
    }

    /** Daftar settings for an open daftar. */
    function askSettings(nb, meta, done) {
        pending = { isNew: false, nb: nb, meta: meta, done: done }
        setup.show(DaftarApi.formValues(meta, nb.title || ""), false)
    }

    function setupDone(r) {
        const p = pending
        pending = null
        if (!p) return
        if (p.isNew) {
            root.notify("Preparing your daftar…", false)
            Qt.callLater(() => {
                const nb = DaftarApi.create(r, p.folderId || "")
                if (nb && nb.id) p.done(nb)
                else root.notify("The daftar couldn't be made.", true)
            })
            return
        }
        Qt.callLater(() => {
            const renamed = Object.keys(JSON.parse(r.renames || "{}"))
            const removed = p.meta.classes.filter(c => DaftarApi.parseClasses(r.classes).indexOf(c) < 0 && renamed.indexOf(c) < 0)
            root.notify(removed.length ? "Updating… The pages of " + removed.join(", ") + " are kept." : "Updating your daftar…", false)
            DaftarApi.update(p.nb.id, r)
            p.done()
        })
    }

    // A text area for pasted rows / outline lines (the web app's .paste-area).
    component Area: ScrollView {
        id: area
        property alias text: ta.text
        property alias placeholderText: ta.placeholderText
        property int rows: 8
        signal edited()
        implicitHeight: Math.max(120, rows * 19.5 + 20)
        TextArea {
            id: ta
            wrapMode: TextEdit.Wrap
            color: Theme.text
            placeholderTextColor: Theme.muted
            font.pixelSize: 13
            leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 10
            selectionColor: Theme.alpha(Theme.accent, 0.3)
            onTextChanged: area.edited()
            Component.onCompleted: forceActiveFocus()
            background: Rectangle { antialiasing: true;
                radius: Theme.radius
                color: Theme.surface
                border.width: ta.activeFocus ? 2 : 1
                border.color: ta.activeFocus ? Theme.accent : Theme.border
            }
        }
    }

    Component {
        id: pasteField
        ColumnLayout {
            id: pf
            readonly property var field: parent.field
            readonly property FormDialog form: parent.dialog
            spacing: 6
            Area {
                id: pa
                Layout.fillWidth: true
                rows: 10
                placeholderText: "D130045678\tالعلوي\tمحمد\nD131122334\tبناني\tسلمى\n…"
                onEdited: pf.form.set(pf.field.name, text)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: Theme.muted
                text: pa.text.trim() ? DaftarApi.studentsSummary(pa.text) : "Copy the rows from Massar or Excel (code, surname, first name) and paste them here."
            }
        }
    }

    Component {
        id: linesField
        Area {
            readonly property var field: parent.field
            readonly property FormDialog form: parent.dialog
            rows: 7
            placeholderText: "التمهيد\nالنشاط 1\nالنشاط 2\nالتقويم"
            onEdited: form.set(field.name, text)
        }
    }

    function askStudents(meta, cls, done) {
        dialog.ask("Student list · لائحة التلاميذ", [
            { name: "cls", label: "القسم · Class", type: "select", value: cls, options: meta.classes.map(c => ({ value: c, label: c })) },
            { name: "text", label: "", type: "custom", component: pasteField, value: "" }
        ], "Write the list", r => Qt.callLater(() => done(r.cls, r.text)), {
            width: 560,
            validate: v => DaftarApi.parseStudents(v.text || "").length ? null : "Paste at least one student's name."
        })
    }

    function askOutline(done) {
        dialog.ask("My outline · مخطط", [
            { name: "name", label: "Name", value: "", required: false },
            { name: "lines", label: "Lines (one per line of the daftar)", type: "custom", component: linesField, value: "" }
        ], "Save and write it", r => Qt.callLater(() => done({ name: r.name.trim(), lines: r.lines.split(/\r?\n/).map(l => l.trim()).filter(l => l.length) })), {
            validate: v => !String(v.name).trim() ? "Give the outline a name." : !String(v.lines).trim() ? "Write at least one line." : null
        })
    }

    function askExport(meta, cls, done) {
        dialog.ask("Export a class · تصدير القسم", [
            { name: "cls", label: "القسم · Class", type: "select", value: cls, options: meta.classes.map(c => ({ value: c, label: c })) },
            { name: "term", label: "الفترة · Period", type: "select", value: "year", options: [
                { value: "year", label: "السنة كاملة · Whole year" },
                { value: "s1", label: "الدورة الأولى · 1st semester (Sept – Jan)" },
                { value: "s2", label: "الدورة الثانية · 2nd semester (Feb – July)" },
                { value: "month", label: "هذا الشهر · This month" }
            ] }
        ], "Export PDF", r => Qt.callLater(() => done(r.cls, r.term)))
    }
}
