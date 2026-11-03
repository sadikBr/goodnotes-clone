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

    readonly property var days: Js.run("daftarQt.DAYS.slice", [0]) || []
    readonly property var coverColors: [
        { color: "#5b2a5e", label: "Plum" }, { color: "#1f7a4a", label: "Green" }, { color: "#1f6fd1", label: "Blue" }, { color: "#2d2f72", label: "Navy" },
        { color: "#d1245f", label: "Raspberry" }, { color: "#e0612a", label: "Orange" }, { color: "#0d9488", label: "Teal" }, { color: "#475569", label: "Slate" }
    ]

    /** The daftar's fields, filled from `values` (see daftarQt.formValues). */
    function fields(v, isNew) {
        const f = [
            { name: "teacher", label: "الأستاذ(ة) · Teacher", value: v.teacher, half: true, required: false },
            { name: "subject", label: "المادة · Subject", value: v.subject, half: true, required: false },
            { name: "school", label: "المؤسسة · School", value: v.school, half: true, required: false },
            { name: "year", label: "الموسم الدراسي · School year", value: v.year, half: true, required: false },
            { name: "region", label: "الجهة / الأكاديمية · Region", value: v.region, half: true, required: false },
            { name: "directorate", label: "المديرية الإقليمية · Directorate", value: v.directorate, half: true, required: false },
            { name: "payroll", label: "رقم التأجير · Payroll number (optional)", value: v.payroll, half: true, required: false },
        ]
        if (isNew) f.push({ name: "title", label: "Title", value: v.title, half: true, required: false })
        f.push({ name: "classes", label: "الأقسام · Classes — separate with commas, e.g. 3/6, 3/7, 2/1", value: v.classes, required: false })
        f.push({ name: "timetable", label: "استعمال الزمن · Weekly timetable (optional: gives each new lesson its time)", type: "custom", component: timetableField, value: v.timetable })
        f.push({ name: "holidays", label: "العطل · Holidays and vacations (optional)", type: "custom", component: holidaysField, value: v.holidays })
        if (isNew) f.push({ name: "cover", label: "Cover colour", type: "swatches", value: "#5b2a5e", colors: coverColors })
        return f
    }
    function validate(v) { return Js.run("daftarQt.validate", [v]) }

    /** "New daftar": resolves `done(notebook)` once it's made. */
    function askNew(folderId, done) {
        const v = Js.run("daftarQt.formValues", [null, "دفتر النصوص"])
        dialog.ask("New daftar annossous · دفتر النصوص", fields(v, true), "Create daftar", r => {
            root.notify("Preparing your daftar…", false)
            Qt.callLater(() => {
                const nb = Js.run("daftarQt.create", [r, folderId || null])
                if (nb && nb.id) done(nb)
                else root.notify("The daftar couldn't be made.", true)
            })
        }, { width: 640, twoColumns: true, validate: validate })
    }

    /** Daftar settings for an open daftar. */
    function askSettings(nb, meta, done) {
        const v = Js.run("daftarQt.formValues", [meta, nb.title || ""])
        dialog.ask("Daftar settings · إعدادات الدفتر", fields(v, false), "Save", r => {
            Qt.callLater(() => {
                const before = meta.classes
                const removed = before.filter(c => Js.run("daftarQt.parseClasses", [r.classes]).indexOf(c) < 0)
                root.notify(removed.length ? "Updating… The pages of " + removed.join(", ") + " are kept." : "Updating your daftar…", false)
                Js.run("daftarQt.update", [nb.id, r])
                done()
            })
        }, { width: 640, twoColumns: true, validate: validate })
    }

    // ------------------------------------------------------------- fields

    component Small: UiField {
        implicitHeight: 34
        leftPadding: 8
        rightPadding: 8
        font.pixelSize: 13
        background: Rectangle {
            radius: 8
            color: Theme.surface
            border.width: parent.activeFocus ? 2 : 1
            border.color: parent.activeFocus ? Theme.accent : Theme.border
        }
    }
    component SmallSelect: UiSelect {
        implicitHeight: 34
        leftPadding: 8
        font.pixelSize: 13
        background: Rectangle {
            radius: 8
            color: Theme.surface
            border.width: parent.activeFocus || parent.popup.visible ? 2 : 1
            border.color: parent.activeFocus || parent.popup.visible ? Theme.accent : Theme.border
        }
    }
    component RemoveButton: AbstractButton {
        implicitWidth: 28
        implicitHeight: 28
        hoverEnabled: true
        ToolTip.visible: hovered
        ToolTip.text: "Remove"
        ToolTip.delay: 400
        background: Rectangle { radius: 8; color: parent.hovered ? Theme.surface2 : "transparent" }
        contentItem: Item { Icon { anchors.centerIn: parent; name: "close"; size: 15; color: Theme.muted } }
    }

    function parseList(s) {
        try { const v = JSON.parse(s || "[]"); return Array.isArray(v) ? v : [] } catch (e) { return [] }
    }
    function classList(s) { return Js.run("daftarQt.parseClasses", [s || ""]) || [] }
    function pad2(n) { return (n < 10 ? "0" : "") + n }
    // "8:5" / "0805" → "08:05" (or "" when it isn't a time).
    function cleanTime(s) {
        const m = String(s).trim().match(/^(\d{1,2})[:h.]?(\d{2})?$/)
        if (!m) return ""
        const hh = Number(m[1]), mm = Number(m[2] || 0)
        return hh < 24 && mm < 60 ? pad2(hh) + ":" + pad2(mm) : ""
    }
    function cleanDate(s) {
        const m = String(s).trim().match(/^(\d{4})-(\d{1,2})-(\d{1,2})$/)
        if (!m) return ""
        const d = new Date(Number(m[1]), Number(m[2]) - 1, Number(m[3]))
        return d.getMonth() === Number(m[2]) - 1 ? m[1] + "-" + pad2(Number(m[2])) + "-" + pad2(Number(m[3])) : ""
    }

    Component {
        id: timetableField
        ColumnLayout {
            id: tt
            readonly property var field: parent.field
            readonly property FormDialog form: parent.dialog
            readonly property var classes: root.classList(form.values.classes)
            property var lessons: root.parseList(form.values[field.name])
            spacing: 8
            function save(list) {
                lessons = list
                form.set(field.name, JSON.stringify(list))
            }
            // Keep class choices in step with the classes field.
            onClassesChanged: {
                if (!classes.length) return
                const next = lessons.map(l => classes.indexOf(l.cls) >= 0 ? l : Object.assign({}, l, { cls: classes[0] }))
                if (JSON.stringify(next) !== JSON.stringify(lessons)) save(next)
            }
            Repeater {
                model: tt.lessons.length
                delegate: RowLayout {
                    id: ttRow
                    required property int index
                    readonly property var l: tt.lessons[index] || ({})
                    readonly property real unit: (tt.width - 10 - 28 - 30) / 4.15
                    Layout.fillWidth: true
                    spacing: 6
                    function change(k, v) {
                        const list = tt.lessons.slice()
                        list[index] = Object.assign({}, list[index]); list[index][k] = v
                        tt.save(list)
                    }
                    SmallSelect {
                        Layout.preferredWidth: ttRow.unit * 1.25
                        options: [1, 2, 3, 4, 5, 6].map(d => ({ value: d, label: root.days[d].fr + " · " + root.days[d].ar }))
                        value: ttRow.l.day
                        onPicked: v => ttRow.change("day", v)
                    }
                    Small {
                        Layout.preferredWidth: ttRow.unit
                        text: ttRow.l.start || ""
                        placeholderText: "08:00"
                        onEditingFinished: { const t = root.cleanTime(text); if (t) ttRow.change("start", t); else text = ttRow.l.start }
                    }
                    Label { text: "–"; color: Theme.muted; Layout.preferredWidth: 10; horizontalAlignment: Text.AlignHCenter }
                    Small {
                        Layout.preferredWidth: ttRow.unit
                        text: ttRow.l.end || ""
                        placeholderText: "10:00"
                        onEditingFinished: { const t = root.cleanTime(text); if (t) ttRow.change("end", t); else text = ttRow.l.end }
                    }
                    SmallSelect {
                        Layout.preferredWidth: ttRow.unit * 0.9
                        options: (tt.classes.indexOf(ttRow.l.cls) >= 0 || !ttRow.l.cls ? tt.classes : [ttRow.l.cls].concat(tt.classes)).map(c => ({ value: c, label: c }))
                        value: ttRow.l.cls
                        onPicked: v => ttRow.change("cls", v)
                    }
                    RemoveButton {
                        onClicked: { const list = tt.lessons.slice(); list.splice(ttRow.index, 1); tt.save(list) }
                    }
                }
            }
            Label {
                visible: tt.lessons.length === 0
                text: "No lessons yet — without a timetable you write the dates yourself."
                color: Theme.muted; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            UiButton {
                kind: "ghost"; iconName: "plus"; text: "Add a lesson"
                onClicked: {
                    if (!tt.classes.length) return root.notify("Add your classes first", true)
                    const prev = tt.lessons[tt.lessons.length - 1]
                    let next
                    // Suggest the next slot: same class, two hours later (or the next day).
                    if (prev) {
                        const hm = prev.end.split(":").map(Number)
                        const later = hm[0] + 2 <= 18
                        next = { day: later ? prev.day : Math.min(6, prev.day + 1), start: later ? prev.end : "08:00", end: later ? root.pad2(hm[0] + 2) + ":" + root.pad2(hm[1]) : "10:00", cls: prev.cls }
                    } else {
                        next = { day: 1, start: "08:00", end: "10:00", cls: tt.classes[0] }
                    }
                    tt.save(tt.lessons.concat([next]))
                }
            }
        }
    }

    Component {
        id: holidaysField
        ColumnLayout {
            id: hd
            readonly property var field: parent.field
            readonly property FormDialog form: parent.dialog
            property var items: root.parseList(form.values[field.name])
            spacing: 8
            function save(list, sort) {
                if (sort) list = list.slice().sort((a, b) => (a.from || "9").localeCompare(b.from || "9"))
                items = list
                form.set(field.name, JSON.stringify(list))
            }
            Repeater {
                model: hd.items.length
                delegate: RowLayout {
                    id: hdRow
                    required property int index
                    readonly property var item: hd.items[index] || ({})
                    readonly property real unit: (hd.width - 10 - 34 - 28 - 30) / 3.6
                    Layout.fillWidth: true
                    spacing: 6
                    function change(k, v, sort) {
                        const list = hd.items.slice()
                        list[index] = Object.assign({}, list[index]); list[index][k] = v
                        if (k === "from" && (!list[index].to || list[index].to < v)) list[index].to = v
                        hd.save(list, sort)
                    }
                    Small {
                        Layout.preferredWidth: hdRow.unit * 1.6
                        text: hdRow.item.name || ""
                        placeholderText: "عيد الفطر · Aïd al-Fitr"
                        onTextEdited: hdRow.change("name", text, false)
                        Component.onCompleted: if (!hdRow.item.name && !hdRow.item.from) forceActiveFocus()
                    }
                    Small {
                        Layout.preferredWidth: hdRow.unit
                        text: hdRow.item.from || ""
                        placeholderText: "YYYY-MM-DD"
                        onEditingFinished: { const d = root.cleanDate(text); if (d) hdRow.change("from", d, true); else text = hdRow.item.from || "" }
                    }
                    Label { text: "–"; color: Theme.muted; Layout.preferredWidth: 10; horizontalAlignment: Text.AlignHCenter }
                    Small {
                        Layout.preferredWidth: hdRow.unit
                        text: hdRow.item.to || ""
                        placeholderText: "YYYY-MM-DD"
                        onEditingFinished: { const d = root.cleanDate(text); if (d) hdRow.change("to", d, true); else text = hdRow.item.to || "" }
                    }
                    Label {
                        Layout.preferredWidth: 34
                        horizontalAlignment: Text.AlignHCenter
                        text: hdRow.item.from && hdRow.item.to >= hdRow.item.from ? Js.run("daftarQt.holidayDays", [hdRow.item]) + " d" : ""
                        color: Theme.muted; font.pixelSize: 12
                    }
                    RemoveButton {
                        onClicked: { const list = hd.items.slice(); list.splice(hdRow.index, 1); hd.save(list, false) }
                    }
                }
            }
            Label {
                visible: hd.items.length === 0
                text: "No holidays yet. New lesson warns you when today is one."
                color: Theme.muted; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                UiButton { kind: "ghost"; iconName: "plus"; text: "Add a holiday"; onClicked: hd.save(hd.items.concat([{ name: "", from: "", to: "" }]), false) }
                UiButton {
                    kind: "ghost"; iconName: "star"; text: "Add national holidays"
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: "Fixed-date public holidays for the school year (religious holidays and vacations are added by hand)"
                    onClicked: {
                        const have = hd.items.map(x => x.from)
                        const fresh = (Js.run("daftarQt.nationalHolidaysFor", [hd.form.values.year || ""]) || []).filter(x => have.indexOf(x.from) < 0)
                        if (!fresh.length) return root.notify("The national holidays are already in the list", false)
                        hd.save(hd.items.concat(fresh), true)
                    }
                }
            }
        }
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
            background: Rectangle {
                radius: 10
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
                text: pa.text.trim() ? Js.run("daftarQt.studentsSummary", [pa.text]) : "Copy the rows from Massar or Excel (code, surname, first name) and paste them here."
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
            validate: v => (Js.run("daftarQt.parseStudents", [v.text || ""]) || []).length ? null : "Paste at least one student's name."
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
