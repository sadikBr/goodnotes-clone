// Setting up a daftar annossous, one step at a time (and changing it later):
// 1. the school and you, 2. your classes (added as chips, renamed or removed;
// a renamed class keeps its pages), 3. the weekly timetable, painted on a
// grid with a class as the brush, 4. holidays, 5. the cover colour (new
// daftars only). Arabic first, French under it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Inkleaf

Popup {
    id: root
    /// Made or saved: the values DaftarApi.create / update take.
    signal finished(var values)

    property bool isNew: true
    property int step: 0
    property var v: ({})
    /// [{ name, orig }]: `orig` is the name the class had when the dialog opened ("" for a new one).
    property var classes: []
    /// Lessons on the grid's times: [{ day, start, end, cls }].
    property var lessons: []
    /// [{ name, from, to }]
    property var holidays: []
    property string cover: "#5b2a5e"
    /// The cover's pattern: the designs with a panel for the daftar's details.
    property string design: "zellige"
    readonly property var designs: [{ id: "zellige", ar: "زليج", fr: "Zellige" }, { id: "arabesque", ar: "أرابيسك", fr: "Arabesque" }, { id: "berber", ar: "زربية أمازيغية", fr: "Berber rug" }]
    function coverUrl(d) { return "image://cover/" + d + "/" + cover.replace("#", "") + "/0/" + encodeURIComponent(v.title || "دفتر النصوص") }
    property string error: ""

    readonly property var days: DaftarApi.days()
    /// The class colours: the first eight are given in turn, the rest can be chosen.
    // One of each colour family (no two shades of one colour), light enough for dark
    // writing; the first eight are the same as daftar/Daftar.cpp CLASS_COLORS.
    readonly property var classPalette: ["#f87171", "#facc15", "#4ade80", "#60a5fa", "#c084fc", "#fb923c", "#2dd4bf", "#f472b6",
                                         "#a3e635", "#22d3ee", "#818cf8", "#e879f9", "#c8a27a", "#b5b35c", "#a8a29e"]
    /// Colours chosen for classes: { name: "#hex" }.
    property var chosen: ({})
    readonly property var coverColors: ["#5b2a5e", "#1f7a4a", "#1f6fd1", "#2d2f72", "#d1245f", "#e0612a", "#0d9488", "#475569"]
    readonly property var steps: {
        const s = [{ ar: "المؤسسة", fr: "School" }, { ar: "الأقسام", fr: "Classes" }, { ar: "استعمال الزمن", fr: "Timetable" }, { ar: "العطل", fr: "Holidays" }]
        return isNew ? s.concat([{ ar: "الغلاف", fr: "Cover" }]) : s
    }
    readonly property var names: classes.map(c => c.name)
    function colorOf(cls) { if (chosen[cls]) return chosen[cls]; const i = names.indexOf(cls); return classPalette[(i < 0 ? 7 : i) % 8] }
    function setColor(cls, c) { const n = Object.assign({}, chosen); n[cls] = c; chosen = n }
    function parseMap(s) { try { const x = JSON.parse(s || "{}"); return x && typeof x === "object" && !Array.isArray(x) ? x : {} } catch (e) { return {} } }

    function parse(s) { try { const x = JSON.parse(s || "[]"); return Array.isArray(x) ? x : [] } catch (e) { return [] } }
    /// Opens it, filled from DaftarApi.formValues.
    function show(values, fresh) {
        isNew = fresh
        v = Object.assign({}, values)
        classes = DaftarApi.parseClasses(values.classes || "").map(n => ({ name: n, orig: fresh ? "" : n }))
        lessons = parse(values.timetable)
        holidays = parse(values.holidays)
        chosen = parseMap(values.classColors)
        cover = "#5b2a5e"
        design = "zellige"
        error = ""
        step = 0
        brush = classes.length ? classes[0].name : ""
        halfHours = lessons.some(l => /:30$/.test(l.start) || /:30$/.test(l.end))
        open()
    }
    // Typing goes straight into the step's first box.
    onStepChanged: if (step === 1) Qt.callLater(() => classInput.forceActiveFocus())
    function setField(k, x) { const n = Object.assign({}, v); n[k] = x; v = n }

    // --- classes
    function addClasses(text) {
        const fresh = DaftarApi.parseClasses(text).filter(n => names.indexOf(n) < 0)
        if (!fresh.length) return false
        classes = classes.concat(fresh.map(n => ({ name: n, orig: "" })))
        if (!brush) brush = fresh[0]
        return true
    }
    function renameClass(i, to) {
        to = to.trim()
        const from = classes[i].name
        if (!to || to === from || names.indexOf(to) >= 0) return
        const list = classes.slice()
        list[i] = Object.assign({}, list[i], { name: to })
        classes = list
        lessons = lessons.map(l => l.cls === from ? Object.assign({}, l, { cls: to }) : l)
        if (chosen[from]) { const n = Object.assign({}, chosen); n[to] = n[from]; delete n[from]; chosen = n }
        if (brush === from) brush = to
    }
    function removeClass(i) {
        const gone = classes[i].name
        classes = classes.filter((c, k) => k !== i)
        lessons = lessons.filter(l => l.cls !== gone)
        if (chosen[gone]) { const n = Object.assign({}, chosen); delete n[gone]; chosen = n }
        if (brush === gone) brush = classes.length ? classes[0].name : ""
    }

    // --- the timetable grid
    property string brush: ""
    property bool halfHours: false
    readonly property int slotMins: halfHours ? 30 : 60
    readonly property int firstMin: 8 * 60
    readonly property int lastMin: 18 * 60
    readonly property int slotCount: (lastMin - firstMin) / slotMins
    function mins(t) { const p = String(t).split(":"); return Number(p[0]) * 60 + Number(p[1] || 0) }
    function hhmm(m) { return (m < 600 ? "0" : "") + Math.floor(m / 60) + ":" + (m % 60 < 10 ? "0" : "") + (m % 60) }
    /// Lessons that fit the grid, and the ones that don't (kept as they are).
    function onGrid(l) { const a = mins(l.start), b = mins(l.end); return l.day >= 1 && l.day <= 6 && a >= firstMin && b <= lastMin && a % slotMins === 0 && b % slotMins === 0 }
    readonly property var offGrid: lessons.filter(l => !onGrid(l))
    function classAt(day, slot) {
        const m = firstMin + slot * slotMins
        const l = lessons.find(x => x.day === day && onGrid(x) && mins(x.start) <= m && m < mins(x.end))
        return l ? l.cls : ""
    }
    /// Puts `cls` ("" clears) in one slot, then joins neighbouring slots of a class into lessons.
    function paint(day, slot, cls) {
        if (classAt(day, slot) === cls) return
        const cells = []
        for (let s = 0; s < slotCount; ++s) cells.push(s === slot ? cls : classAt(day, s))
        const kept = lessons.filter(l => l.day !== day || !onGrid(l))
        const runs = []
        for (let s = 0; s < slotCount; ++s) {
            if (!cells[s]) continue
            const last = runs[runs.length - 1]
            if (last && last.cls === cells[s] && last.endSlot === s) last.endSlot = s + 1
            else runs.push({ cls: cells[s], startSlot: s, endSlot: s + 1 })
        }
        lessons = kept.concat(runs.map(r => ({ day: day, start: hhmm(firstMin + r.startSlot * slotMins), end: hhmm(firstMin + r.endSlot * slotMins), cls: r.cls })))
            .sort((a, b) => a.day - b.day || mins(a.start) - mins(b.start))
    }
    function weeklyHours(cls) {
        return lessons.filter(l => l.cls === cls).reduce((t, l) => t + (mins(l.end) - mins(l.start)) / 60, 0)
    }

    // --- holidays
    function cleanDate(s) {
        const m = String(s).trim().match(/^(\d{4})-(\d{1,2})-(\d{1,2})$/)
        if (!m) return ""
        const d = new Date(Number(m[1]), Number(m[2]) - 1, Number(m[3]))
        return d.getMonth() === Number(m[2]) - 1 ? m[1] + "-" + (Number(m[2]) < 10 ? "0" : "") + Number(m[2]) + "-" + (Number(m[3]) < 10 ? "0" : "") + Number(m[3]) : ""
    }
    function setHoliday(i, k, x) {
        const list = holidays.slice()
        list[i] = Object.assign({}, list[i]); list[i][k] = x
        if (k === "from" && (!list[i].to || list[i].to < x)) list[i].to = x
        holidays = list
    }

    // --- finishing
    function values() {
        const renames = {}
        for (const c of classes) if (c.orig && c.orig !== c.name) renames[c.orig] = c.name
        return Object.assign({}, v, { classes: names.join(", "), timetable: JSON.stringify(lessons), holidays: JSON.stringify(holidays.filter(h => h.name || h.from)), cover: cover, design: design, renames: JSON.stringify(renames), classColors: JSON.stringify(chosen) })
    }
    function next() {
        error = ""
        if (step === 1 && !classes.length) { error = "Add at least one class · أضف قسما على الأقل"; return }
        if (step < steps.length - 1) step++
        else finish()
    }
    function finish() {
        const r = values()
        if (!classes.length) { step = 1; error = "Add at least one class · أضف قسما على الأقل"; return }
        const problem = DaftarApi.validate(r)
        if (problem) { error = problem; return }
        close()
        finished(r)
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 900)
    height: Math.min(parent.height - 32, 680)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: Qt.rgba(20 / 255, 18 / 255, 14 / 255, 0.45) }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 180; easing.type: Easing.OutCubic }
    }
    background: Item {
        Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
        Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
    }

    /// A label in Arabic with its French under it.
    component Both: ColumnLayout {
        property string ar
        property string fr
        spacing: 0
        Label { text: parent.ar; font.pixelSize: 14; font.weight: Font.DemiBold; color: Theme.text; Layout.fillWidth: true; horizontalAlignment: Text.AlignLeft }
        Label { visible: text.length > 0; text: parent.fr; font.pixelSize: 12; color: Theme.muted; Layout.fillWidth: true }
    }
    component Field: ColumnLayout {
        id: fld
        property string key
        property string ar
        property string fr
        property string hint: ""
        property string value: ""
        signal edited(string key, string text)
        Layout.fillWidth: true
        // Both columns the same width.
        Layout.preferredWidth: 100
        spacing: 5
        Both { ar: fld.ar; fr: fld.fr }
        UiField { Layout.fillWidth: true; text: fld.value; placeholderText: fld.hint; onTextEdited: fld.edited(fld.key, text) }
    }
    component Square: Rectangle { antialiasing: true; radius: Theme.radius }

    // A class's colour, picked from the class palette.
    Popup {
        id: classColorPop
        property string cls: ""
        function pick(anchor, name) {
            cls = name
            const p = anchor.mapToItem(parent, 0, 0)
            x = Math.max(8, Math.min(parent.width - width - 8, p.x - 12))
            y = p.y + anchor.height + 8
            open()
        }
        parent: Overlay.overlay
        padding: 12
        width: 8 * 30 + 7 * 6 + 24
        modal: true
        dim: false
        Overlay.modal: Item {}
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Item {
            Shadow { anchors.fill: parent; radius: Theme.radiusLg; level: 3 }
            Rectangle { antialiasing: true; anchors.fill: parent; radius: Theme.radiusLg; color: Theme.surface; border.color: Theme.border }
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label { text: "لون القسم · Colour of " + classColorPop.cls; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.muted }
            Swatches {
                Layout.fillWidth: true
                colors: root.classPalette
                value: root.colorOf(classColorPop.cls)
                size: 30
                spacing: 6
                onPicked: c => { root.setColor(classColorPop.cls, c); classColorPop.close() }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0
        // Title and steps.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 14
            spacing: 8
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Label { text: root.isNew ? "دفتر النصوص جديد" : "إعدادات الدفتر"; font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text }
                Label { text: root.isNew ? "New daftar annossous" : "Daftar settings"; font.pixelSize: 12; color: Theme.muted }
            }
            Repeater {
                model: root.steps
                delegate: AbstractButton {
                    id: sb
                    required property var modelData
                    required property int index
                    readonly property bool on: root.step === index
                    readonly property bool done: root.step > index
                    implicitHeight: 40
                    implicitWidth: sbRow.implicitWidth + 16
                    hoverEnabled: true
                    // Ahead only once there are classes (the timetable needs them).
                    enabled: index <= 1 || root.classes.length > 0
                    onClicked: { root.error = ""; root.step = index }
                    background: Square { color: sb.on ? Theme.accentSoft : sb.hovered ? Theme.surface2 : "transparent" }
                    contentItem: RowLayout {
                        id: sbRow
                        spacing: 7
                        Square {
                            Layout.preferredWidth: 20; Layout.preferredHeight: 20; Layout.leftMargin: 2; radius: Theme.radiusSm
                            color: sb.on || sb.done ? Theme.accent : Theme.surface3
                            Label { anchors.centerIn: parent; text: sb.done ? "✓" : String(sb.index + 1); font.pixelSize: 11; font.weight: Font.Bold; color: sb.on || sb.done ? Theme.accentContrast : Theme.muted }
                        }
                        ColumnLayout {
                            spacing: -1
                            Label { text: sb.modelData.ar; font.pixelSize: 13; font.weight: sb.on ? Font.Bold : Font.Medium; color: sb.on ? Theme.text : Theme.muted }
                            Label { text: sb.modelData.fr; font.pixelSize: 11; color: Theme.muted }
                        }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.step

            // 1. School and teacher.
            Flickable {
                contentHeight: schoolCol.implicitHeight + 40
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                GridLayout {
                    id: schoolCol
                    x: 24; y: 20
                    width: parent.width - 48
                    columns: width > 560 ? 2 : 1
                    columnSpacing: 18
                    rowSpacing: 16
                    Field { key: "teacher"; value: root.v.teacher || ""; onEdited: (k, t) => root.setField(k, t); ar: "الأستاذ(ة)"; fr: "Teacher" }
                    Field { key: "subject"; value: root.v.subject || ""; onEdited: (k, t) => root.setField(k, t); ar: "المادة"; fr: "Subject"; hint: "الرياضيات · Maths" }
                    Field { key: "school"; value: root.v.school || ""; onEdited: (k, t) => root.setField(k, t); ar: "المؤسسة"; fr: "School" }
                    Field { key: "year"; value: root.v.year || ""; onEdited: (k, t) => root.setField(k, t); ar: "الموسم الدراسي"; fr: "School year"; hint: "2026-2027" }
                    Field { key: "region"; value: root.v.region || ""; onEdited: (k, t) => root.setField(k, t); ar: "الجهة / الأكاديمية"; fr: "Region" }
                    Field { key: "directorate"; value: root.v.directorate || ""; onEdited: (k, t) => root.setField(k, t); ar: "المديرية الإقليمية"; fr: "Directorate" }
                    Field { key: "payroll"; value: root.v.payroll || ""; onEdited: (k, t) => root.setField(k, t); ar: "رقم التأجير"; fr: "Payroll number (optional)" }
                    Field { visible: root.isNew; key: "title"; value: root.v.title || ""; onEdited: (k, t) => root.setField(k, t); ar: "اسم الدفتر"; fr: "Title in the library" }
                }
            }

            // 2. Classes.
            Flickable {
                contentHeight: classCol.implicitHeight + 40
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: classCol
                    x: 24; y: 20
                    width: parent.width - 48
                    spacing: 14
                    Both { ar: "أضف الأقسام التي تدرسها"; fr: "Add the classes you teach. Each gets its own pages, student list and colour." }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        UiField {
                            id: classInput
                            Layout.fillWidth: true
                            placeholderText: "3/6, 3/7, 2APIC-1 …"
                            onAccepted: if (root.addClasses(text)) text = ""
                        }
                        UiButton { text: "إضافة · Add"; iconName: "plus"; kind: "primary"; enabled: classInput.text.trim().length > 0; onClicked: { if (root.addClasses(classInput.text)) classInput.text = ""; classInput.forceActiveFocus() } }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: root.classes
                            delegate: Square {
                                id: chip
                                required property var modelData
                                required property int index
                                property bool editing: false
                                height: 40
                                width: chipRow.implicitWidth + 16
                                color: Theme.surface
                                border.color: Theme.border
                                RowLayout {
                                    id: chipRow
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 10
                                    spacing: 8
                                    // Its colour: tap to choose another.
                                    AbstractButton {
                                        id: colorBtn
                                        implicitWidth: 22; implicitHeight: 22
                                        hoverEnabled: true
                                        ToolTip.visible: hovered
                                        ToolTip.delay: 400
                                        ToolTip.text: "Colour of " + chip.modelData.name
                                        onClicked: classColorPop.pick(colorBtn, chip.modelData.name)
                                        contentItem: Item {
                                            Rectangle {
                                                antialiasing: true
                                                anchors.centerIn: parent
                                                width: colorBtn.hovered ? 20 : 16; height: width
                                                radius: Theme.swatchRadius(width)
                                                color: root.colorOf(chip.modelData.name)
                                                border.color: Theme.dark ? Qt.rgba(1, 1, 1, 0.2) : Qt.rgba(0, 0, 0, 0.15)
                                                Behavior on width { NumberAnimation { duration: 100 } }
                                            }
                                        }
                                    }
                                    Label {
                                        visible: !chip.editing
                                        text: chip.modelData.name
                                        font.pixelSize: 14; font.weight: Font.DemiBold; color: Theme.text
                                        MouseArea { anchors.fill: parent; onClicked: { chip.editing = true; renameField.text = chip.modelData.name; renameField.forceActiveFocus(); renameField.selectAll() } }
                                        HoverHandler { cursorShape: Qt.IBeamCursor }
                                    }
                                    TextField {
                                        id: renameField
                                        visible: chip.editing
                                        Layout.preferredWidth: Math.max(70, contentWidth + 20)
                                        font.pixelSize: 14
                                        color: Theme.text
                                        background: Rectangle { radius: 5; color: Theme.surface2; border.color: Theme.accent }
                                        onAccepted: { root.renameClass(chip.index, text); chip.editing = false }
                                        onActiveFocusChanged: if (!activeFocus && chip.editing) { root.renameClass(chip.index, text); chip.editing = false }
                                    }
                                    Label {
                                        visible: !chip.editing && !!chip.modelData.orig && chip.modelData.orig !== chip.modelData.name
                                        text: "← " + chip.modelData.orig
                                        font.pixelSize: 11; color: Theme.muted
                                    }
                                    AbstractButton {
                                        implicitWidth: 24; implicitHeight: 24
                                        hoverEnabled: true
                                        ToolTip.visible: hovered
                                        ToolTip.delay: 400
                                        ToolTip.text: chip.modelData.orig ? "Remove (its pages stay in the daftar)" : "Remove"
                                        onClicked: root.removeClass(chip.index)
                                        background: Square { radius: Theme.radiusSm; color: parent.hovered ? Theme.surface2 : "transparent" }
                                        contentItem: Item { Icon { anchors.centerIn: parent; name: "close"; size: 14; color: Theme.muted } }
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        visible: root.classes.length === 0
                        text: "لا توجد أقسام بعد · No classes yet. Type them above; several at once work too (3/6, 3/7)."
                        color: Theme.muted; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    Label {
                        visible: root.classes.length > 0
                        text: "Tap a name to rename it" + (root.isNew ? "" : " (its pages and lessons follow the new name)") + ", or its colour to choose another."
                        color: Theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                }
            }

            // 3. The timetable, painted on a grid.
            ColumnLayout {
                spacing: 12
                // The brushes: each class in its colour with its hours a week, and Clear.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.topMargin: 16
                    spacing: 12
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: root.names.concat([""])
                            delegate: AbstractButton {
                                id: br
                                required property string modelData
                                readonly property bool on: root.brush === modelData
                                readonly property real hours: modelData ? root.weeklyHours(modelData) : 0
                                height: 36
                                width: brRow.implicitWidth + 20
                                hoverEnabled: true
                                onClicked: root.brush = modelData
                                background: Rectangle {
                                    antialiasing: true
                                    radius: Theme.radius
                                    color: br.on ? (br.modelData ? root.colorOf(br.modelData) : Theme.surface3) : br.hovered ? Theme.surface2 : Theme.surface
                                    border.width: br.on ? 2 : 1
                                    border.color: br.on ? (br.modelData ? Theme.mix(root.colorOf(br.modelData), "#000000", 0.35) : Theme.borderStrong) : Theme.border
                                }
                                contentItem: Item {
                                    RowLayout {
                                        id: brRow
                                        anchors.centerIn: parent
                                        spacing: 7
                                        Rectangle { visible: !!br.modelData && !br.on; Layout.preferredWidth: 12; Layout.preferredHeight: 12; radius: Theme.swatchRadius(12); color: root.colorOf(br.modelData); border.color: Qt.rgba(0, 0, 0, 0.15) }
                                        Icon { visible: !br.modelData; name: "eraser"; size: 15; color: br.on ? Theme.text : Theme.muted }
                                        Label { text: br.modelData || "مسح · Clear"; font.pixelSize: 13; font.weight: Font.DemiBold; color: br.on && br.modelData ? "#1c1917" : Theme.text }
                                        Label {
                                            visible: !!br.modelData
                                            text: br.hours ? br.hours + " h" : "—"
                                            font.pixelSize: 11
                                            color: br.on ? "#44403c" : Theme.muted
                                        }
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        spacing: 4
                        Layout.alignment: Qt.AlignTop
                        Segmented { Layout.alignment: Qt.AlignRight; options: [{ value: false, label: "1 h" }, { value: true, label: "30 min" }]; value: root.halfHours; onPicked: x => root.halfHours = x }
                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: root.lessons.reduce((t, l) => t + (root.mins(l.end) - root.mins(l.start)) / 60, 0) + " h / week · ساعة في الأسبوع"
                            font.pixelSize: 11; color: Theme.muted
                        }
                    }
                }
                // The week, in a card.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.bottomMargin: 8
                    radius: Theme.radiusLg
                    color: Theme.surface
                    border.color: Theme.border
                    Item {
                        id: week
                        anchors.fill: parent
                        anchors.margins: 1
                        readonly property real timeW: 54
                        readonly property real headH: 40
                        readonly property real colW: (width - timeW) / 6
                        readonly property real rowH: Math.max(14, (height - headH - 6) / root.slotCount)
                        readonly property int today: (new Date().getDay() + 6) % 7 + 1  // 1 = Monday … 7 = Sunday
                        clip: true
                        // Day names (today stands out).
                        Repeater {
                            model: 6
                            delegate: Rectangle {
                                required property int index
                                readonly property bool isToday: index + 1 === week.today
                                x: week.timeW + index * week.colW; y: 0
                                width: week.colW; height: week.headH
                                color: isToday ? Theme.accentSoft : Theme.surface2
                                Column {
                                    anchors.centerIn: parent
                                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: root.days[index + 1].ar; font.pixelSize: 13; font.weight: Font.DemiBold; color: parent.parent.isToday ? Theme.accent : Theme.text }
                                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: root.days[index + 1].fr; font.pixelSize: 11; color: Theme.muted }
                                }
                            }
                        }
                        Rectangle { x: 0; y: 0; width: week.timeW; height: week.headH; color: Theme.surface2 }
                        Rectangle { x: 0; y: week.headH; width: week.width; height: 1; color: Theme.border }
                        // Hour rows, every other one shaded; the half hours as fainter lines.
                        Repeater {
                            model: root.slotCount
                            delegate: Item {
                                required property int index
                                readonly property int m: root.firstMin + index * root.slotMins
                                x: 0; y: week.headH + index * week.rowH
                                width: week.width; height: week.rowH
                                Rectangle { x: week.timeW; width: parent.width - week.timeW; height: parent.height; color: Math.floor((parent.m - root.firstMin) / 60) % 2 ? Theme.alpha(Theme.text, Theme.dark ? 0.03 : 0.02) : "transparent" }
                                Rectangle { x: week.timeW; width: parent.width - week.timeW; height: 1; color: parent.m % 60 === 0 ? Theme.border : Theme.alpha(Theme.border, 0.5) }
                                Label { visible: parent.m % 60 === 0; x: 0; width: week.timeW - 10; y: 3; horizontalAlignment: Text.AlignRight; text: root.hhmm(parent.m); font.pixelSize: 11; color: Theme.muted }
                            }
                        }
                        Repeater {
                            model: 6
                            delegate: Rectangle { required property int index; x: week.timeW + index * week.colW; y: 0; width: 1; height: week.height; color: Theme.border }
                        }
                        // The lessons, as blocks in their class's colour.
                        Repeater {
                            model: root.lessons.filter(l => root.onGrid(l))
                            delegate: Rectangle {
                                id: block
                                required property var modelData
                                antialiasing: true
                                radius: Theme.radiusSm
                                x: week.timeW + (modelData.day - 1) * week.colW + 4
                                y: week.headH + (root.mins(modelData.start) - root.firstMin) / root.slotMins * week.rowH + 2
                                width: week.colW - 8
                                height: (root.mins(modelData.end) - root.mins(modelData.start)) / root.slotMins * week.rowH - 4
                                color: root.colorOf(modelData.cls)
                                Rectangle { width: 4; height: parent.height - 8; y: 4; x: 4; radius: 2; color: Theme.mix(root.colorOf(block.modelData.cls), "#000000", 0.35) }
                                Column {
                                    x: 14; anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 18
                                    Label { width: parent.width; text: block.modelData.cls; font.pixelSize: 13; font.weight: Font.Bold; color: "#1c1917"; elide: Text.ElideRight }
                                    Label { visible: block.height > 38; width: parent.width; text: block.modelData.start + " – " + block.modelData.end; font.pixelSize: 11; color: "#44403c"; elide: Text.ElideRight }
                                }
                            }
                        }
                        // Where the brush would go, under the pointer.
                        Rectangle {
                            id: ghost
                            visible: painter.containsMouse && !painter.pressed && root.brush.length > 0 && root.classAt(painter.cellDay, painter.cellSlot) === ""
                            antialiasing: true
                            radius: Theme.radiusSm
                            x: week.timeW + (painter.cellDay - 1) * week.colW + 4
                            y: week.headH + painter.cellSlot * week.rowH + 2
                            width: week.colW - 8; height: week.rowH - 4
                            color: Theme.alpha(root.colorOf(root.brush), 0.55)
                            border.color: Theme.mix(root.colorOf(root.brush), "#000000", 0.3)
                            border.width: 1
                            Label { anchors.centerIn: parent; text: "+ " + root.brush; font.pixelSize: 12; font.weight: Font.DemiBold; color: "#1c1917"; visible: week.rowH > 20 }
                        }
                        // Nothing yet: what to do.
                        Label {
                            visible: root.lessons.length === 0
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: week.headH / 2
                            horizontalAlignment: Text.AlignHCenter
                            text: "اختر قسما ثم اضغط على حصصه أو اسحب عليها\nPick a class above, then tap or drag over its hours"
                            font.pixelSize: 13
                            color: Theme.muted
                            lineHeight: 1.3
                        }
                        // Tap or drag to paint.
                        MouseArea {
                            id: painter
                            x: week.timeW; y: week.headH
                            width: week.width - week.timeW
                            height: root.slotCount * week.rowH
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            property string paintWith: ""
                            property int cellDay: 1
                            property int cellSlot: 0
                            function cell(mx, my) { return { day: Math.min(5, Math.max(0, Math.floor(mx / week.colW))) + 1, slot: Math.min(root.slotCount - 1, Math.max(0, Math.floor(my / week.rowH))) } }
                            onPositionChanged: m => {
                                const c = cell(m.x, m.y)
                                cellDay = c.day; cellSlot = c.slot
                                if (pressed) root.paint(c.day, c.slot, paintWith)
                            }
                            onPressed: m => {
                                const c = cell(m.x, m.y)
                                // Tapping a class's own hour clears it.
                                paintWith = root.brush && root.classAt(c.day, c.slot) === root.brush ? "" : root.brush
                                root.paint(c.day, c.slot, paintWith)
                            }
                        }
                    }
                }
                // Lessons at other times (kept as they are).
                Flow {
                    visible: root.offGrid.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.bottomMargin: 10
                    spacing: 6
                    Label { text: "Other times:"; font.pixelSize: 12; color: Theme.muted; height: 28; verticalAlignment: Text.AlignVCenter }
                    Repeater {
                        model: root.offGrid
                        delegate: Rectangle {
                            required property var modelData
                            antialiasing: true
                            radius: Theme.radiusSm
                            height: 28; width: ogRow.implicitWidth + 12
                            color: root.colorOf(modelData.cls)
                            RowLayout {
                                id: ogRow
                                anchors.centerIn: parent
                                spacing: 4
                                Label { text: modelData.cls + " · " + (root.days[modelData.day] || { fr: "" }).fr + " " + modelData.start + "–" + modelData.end; font.pixelSize: 12; color: "#1c1917" }
                                AbstractButton {
                                    implicitWidth: 18; implicitHeight: 18
                                    onClicked: root.lessons = root.lessons.filter(l => l !== modelData)
                                    contentItem: Item { Icon { anchors.centerIn: parent; name: "close"; size: 12; color: "#44403c" } }
                                }
                            }
                        }
                    }
                }
            }

            // 4. Holidays.
            Flickable {
                contentHeight: holCol.implicitHeight + 40
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: holCol
                    x: 24; y: 20
                    width: parent.width - 48
                    spacing: 10
                    Both { Layout.fillWidth: true; ar: "العطل والإجازات"; fr: "Holidays and vacations (optional). New lesson tells you when today is one." }
                    Repeater {
                        model: root.holidays.length
                        delegate: RowLayout {
                            id: hrow
                            required property int index
                            readonly property var h: root.holidays[index] || ({})
                            Layout.fillWidth: true
                            spacing: 8
                            UiField { Layout.fillWidth: true; text: hrow.h.name || ""; placeholderText: "عيد الفطر · Aïd al-Fitr"; onTextEdited: root.setHoliday(hrow.index, "name", text) }
                            UiField { Layout.preferredWidth: 130; text: hrow.h.from || ""; placeholderText: "YYYY-MM-DD"; onEditingFinished: { const d = root.cleanDate(text); if (d) root.setHoliday(hrow.index, "from", d); else text = hrow.h.from || "" } }
                            Label { text: "–"; color: Theme.muted }
                            UiField { Layout.preferredWidth: 130; text: hrow.h.to || ""; placeholderText: "YYYY-MM-DD"; onEditingFinished: { const d = root.cleanDate(text); if (d) root.setHoliday(hrow.index, "to", d); else text = hrow.h.to || "" } }
                            Label { Layout.preferredWidth: 34; text: hrow.h.from && hrow.h.to >= hrow.h.from ? DaftarApi.holidayDays(hrow.h) + " d" : ""; color: Theme.muted; font.pixelSize: 12 }
                            AbstractButton {
                                implicitWidth: 30; implicitHeight: 30
                                hoverEnabled: true
                                onClicked: root.holidays = root.holidays.filter((x, k) => k !== hrow.index)
                                background: Square { radius: Theme.radiusSm; color: parent.hovered ? Theme.surface2 : "transparent" }
                                contentItem: Item { Icon { anchors.centerIn: parent; name: "close"; size: 15; color: Theme.muted } }
                            }
                        }
                    }
                    Label {
                        visible: root.holidays.length === 0
                        text: "لا توجد عطل بعد · No holidays yet."
                        color: Theme.muted; font.pixelSize: 13
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        UiButton { text: "Add a holiday"; iconName: "plus"; onClicked: root.holidays = root.holidays.concat([{ name: "", from: "", to: "" }]) }
                        UiButton {
                            text: "Add national holidays"; iconName: "star"
                            onClicked: {
                                const have = root.holidays.map(x => x.from)
                                const fresh = DaftarApi.nationalHolidaysFor(root.v.year || "").filter(x => have.indexOf(x.from) < 0)
                                if (fresh.length) root.holidays = root.holidays.concat(fresh).sort((a, b) => (a.from || "9").localeCompare(b.from || "9"))
                            }
                        }
                    }
                }
            }

            // 5. Cover (new daftars): the pattern and its colour, with the cover as it will be.
            RowLayout {
                spacing: 28
                Item {
                    Layout.leftMargin: 28
                    Layout.topMargin: 22
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 220; Layout.preferredHeight: 311
                    Shadow { anchors.fill: parent; radius: Theme.radiusSm; level: 3 }
                    LiveImage { anchors.fill: parent; sourceSize: Qt.size(440, 622); source: root.opened && root.step === 4 ? root.coverUrl(root.design) : "" }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.rightMargin: 24
                    Layout.topMargin: 22
                    spacing: 12
                    Both { ar: "الزخرفة"; fr: "Pattern" }
                    RowLayout {
                        spacing: 10
                        Repeater {
                            model: root.designs
                            delegate: AbstractButton {
                                id: dz
                                required property var modelData
                                readonly property bool on: root.design === modelData.id
                                implicitWidth: 112; implicitHeight: 186
                                hoverEnabled: true
                                onClicked: root.design = modelData.id
                                background: Square { color: dz.on ? Theme.accentSoft : dz.hovered ? Theme.surface2 : "transparent"; border.width: 1.5; border.color: dz.on ? Theme.accent : "transparent" }
                                contentItem: Item {
                                    Item {
                                        id: dzPic
                                        x: (dz.width - 84) / 2; y: 8
                                        width: 84; height: 119
                                        Shadow { anchors.fill: parent; radius: 3; level: 1 }
                                        LiveImage { anchors.fill: parent; sourceSize: Qt.size(168, 238); source: root.opened && root.step === 4 ? root.coverUrl(dz.modelData.id) : "" }
                                    }
                                    Column {
                                        anchors.top: dzPic.bottom; anchors.topMargin: 6
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: dz.modelData.ar; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text }
                                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: dz.modelData.fr; font.pixelSize: 11; color: Theme.muted }
                                    }
                                }
                            }
                        }
                    }
                    Both { Layout.topMargin: 6; ar: "اللون"; fr: "Colour" }
                    Swatches { Layout.fillWidth: true; colors: root.coverColors; value: root.cover; size: 32; onPicked: c => root.cover = c }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.leftMargin: 20
            spacing: 8
            UiButton { text: "Cancel"; kind: "ghost"; onClicked: root.close() }
            Label { Layout.fillWidth: true; text: root.error; color: Theme.danger; font.pixelSize: 13; elide: Text.ElideRight; horizontalAlignment: Text.AlignRight }
            UiButton { visible: root.step > 0; text: "Back"; onClicked: { root.error = ""; root.step-- } }
            UiButton { visible: !root.isNew; text: "Save"; kind: root.step === root.steps.length - 1 ? "primary" : "default"; onClicked: root.finish() }
            UiButton {
                visible: root.isNew || root.step < root.steps.length - 1
                kind: "primary"
                text: root.step < root.steps.length - 1 ? "Next · " + root.steps[root.step + 1].fr : "Create daftar"
                onClicked: root.next()
            }
        }
    }
}
