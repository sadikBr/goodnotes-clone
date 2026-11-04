// Developer check while the daftar and cover setup move from the scripts to
// C++ (docs/MIGRATION.md, step 7): inkleaf --compare-daftar runs both on the
// same inputs (every daftar page at three sizes, lessons at several times,
// holidays, pasted student lists, outlines, exports, cover titles) and runs
// whole flows on two copies of a library (creating, updating a daftar,
// pasting students, new lessons, outlines, a new page, exporting, covers),
// comparing every page afterwards.
#include "CompareDaftar.h"

#include <QCoreApplication>
#include <QJSValue>
#include <QTemporaryDir>
#include <QtDebug>
#include <functional>
#include <random>

#include "CompareJson.h"
#include "Catalog.h"
#include "Core.h"
#include "Js.h"
#include "daftar/Cover.h"
#include "daftar/Daftar.h"
#include "daftar/DaftarApi.h"
#include "daftar/DaftarEditor.h"
#include "daftar/Store.h"
#include "draw/Covers.h"
#include "draw/DaftarPapers.h"
#include "editor/DaftarBridge.h"
#include "editor/EditorCore.h"

namespace {

QJsonValue toJson(const QJSValue& v) {
  if (v.isUndefined()) return QJsonValue(QJsonValue::Null);
  const QString s = Js::instance().engine().evaluate("(v) => JSON.stringify(v === undefined ? null : v)").call({v}).toString();
  return QJsonDocument::fromJson(("[" + s + "]").toUtf8()).array().first();
}
QJsonArray els(const std::vector<ed::El>& v) {
  QJsonArray a;
  for (const ed::El& e : v) a.append(e.toJson());
  return a;
}
QJSValue js(const QJsonValue& v) { return Js::instance().toJs(v); }
QJSValue jsCall(const QString& path, const QJSValueList& args) {
  QJSValue r = Js::instance().awaitCall(path, args);
  if (r.isError()) qWarning().noquote() << "script:" << path << r.toString();
  return r;
}
QJSValue jsDate(const QDateTime& t) {
  return Js::instance().engine().evaluate("(y, m, d, h, mi) => new Date(y, m, d, h, mi)").call({t.date().year(), t.date().month() - 1, t.date().day(), t.time().hour(), t.time().minute()});
}
QJSValue jsSet(const QStringList& s) { return Js::instance().engine().evaluate("(a) => new Set(a)").call({Js::instance().toJs(QJsonArray::fromStringList(s))}); }

int g_failed = 0, g_total = 0;

void check(const QString& name, QJsonValue script, QJsonValue native) {
  ++g_total;
  QHash<QString, QString> a, b;
  normalise(script, a);
  normalise(native, b);
  QStringList diffs;
  compare(script, native, "", diffs);
  if (diffs.isEmpty()) return;
  ++g_failed;
  qWarning().noquote() << "DIFFERS" << name;
  for (const QString& d : diffs.mid(0, 8)) qWarning().noquote() << "   " << d;
}

QJsonObject meta1() {
  return QJsonObject{{"kind", "daftar"},
                     {"year", "2026-2027"},
                     {"region", "الرباط سلا القنيطرة"},
                     {"directorate", "تمارة الصخيرات"},
                     {"school", "ماء العينين"},
                     {"teacher", "لبنى أرسلان"},
                     {"payroll", "2320866"},
                     {"subject", "اللغة العربية"},
                     {"classes", QJsonArray{"3/6", "3/7"}},
                     {"holidays", QJsonArray{QJsonObject{{"name", "عطلة"}, {"from", "2026-12-20"}, {"to", "2026-12-28"}}, QJsonObject{{"name", "عيد الاستقلال"}, {"from", "2026-11-18"}, {"to", "2026-11-18"}}}},
                     {"timetable", QJsonArray{QJsonObject{{"day", 1}, {"start", "14:00"}, {"end", "16:00"}, {"cls", "3/6"}}, QJsonObject{{"day", 1}, {"start", "08:00"}, {"end", "10:00"}, {"cls", "3/6"}},
                                              QJsonObject{{"day", 2}, {"start", "10:00"}, {"end", "12:00"}, {"cls", "3/7"}}, QJsonObject{{"day", 6}, {"start", "07:30"}, {"end", "19:00"}, {"cls", "3/7"}},
                                              QJsonObject{{"day", 0}, {"start", "09:00"}, {"end", "10:00"}, {"cls", "3/7"}}}}};
}
QJsonObject meta2() {
  QJsonArray classes;
  for (int i = 0; i < 9; ++i) classes.append(QString("TCS-%1").arg(i + 1));
  QJsonArray hs;
  for (int i = 0; i < 18; ++i) hs.append(QJsonObject{{"name", QString("عطلة منتصف الدورة الأولى والثانية رقم %1").arg(i)}, {"from", QString("2027-0%1-1%2").arg(1 + i % 8).arg(i % 9)}, {"to", i % 3 ? QString("2027-0%1-2%2").arg(1 + i % 8).arg(i % 9) : QString()}});
  return QJsonObject{{"kind", "daftar"}, {"year", "2027-2028"}, {"region", ""}, {"directorate", "مديرية إقليمية ذات اسم طويل جدا جدا جدا لتجربة تصغير الخط"}, {"school", "A"}, {"teacher", ""}, {"payroll", ""}, {"subject", "Mathématiques"}, {"classes", classes}, {"holidays", hs}, {"timetable", QJsonArray{}}};
}

const QStringList& allTemplates() {
  static const QStringList t = {"daftar-title", "daftar-profile", "daftar-profile-2", "daftar-timetable", "daftar-holidays", "daftar-diagnostic", "daftar-students", "daftar-memos", "daftar-absence", "daftar", "daftar-blue", "daftar-green", "daftar-pink", "daftar-navy", "daftar-orange"};
  return t;
}

void pure() {
  const std::pair<double, double> sizes[] = {{794, 1123}, {1123, 794}, {559, 794}};
  const QJsonObject metas[] = {meta1(), meta2()};
  for (const QJsonObject& mj : metas) {
    const daftar::Meta m = daftar::Meta::fromJson(mj);
    for (const auto& [w, h] : sizes) {
      for (const QString& t : allTemplates())
        for (const QString& cls : {QString(), QString("3/6"), QString("TCS-9")})
          check(QString("pageElements %1 %2x%3 %4").arg(t).arg(w).arg(h).arg(cls), toJson(jsCall("daftar.pageElements", {t, js(mj), cls.isEmpty() ? QJSValue(QJSValue::NullValue) : QJSValue(cls), w, h})), els(daftar::pageElements(t, m, cls, w, h)));
      check(QString("coverElements %1x%2").arg(w).arg(h), toJson(jsCall("daftar.coverElements", {js(mj), w, h})), els(daftar::coverElements(m, w, h)));
      check(QString("holidayElements %1x%2").arg(w).arg(h), toJson(jsCall("daftar.holidayElements", {js(mj), w, h, 3})), els(daftar::holidayElements(m, w, h, 3)));
      for (const QStringList& lines : {QStringList{"النشاط 1", "النشاط 2"}, daftar::outlines()[0].lines})
        for (int row : {0, 5, 30, 60})
          check(QString("outlineElements row %1 %2x%3").arg(row).arg(w).arg(h), toJson(jsCall("daftar.outlineElements", {js(QJsonArray::fromStringList(lines)), row, w, h})), els(daftar::outlineElements(lines, row, w, h)));
    }
    // Lessons at several moments (14 Sept 2026 is a Monday).
    for (const QDateTime& t : {QDateTime(QDate(2026, 9, 14), QTime(9, 5)), QDateTime(QDate(2026, 9, 14), QTime(15, 30)), QDateTime(QDate(2026, 9, 14), QTime(12, 30)), QDateTime(QDate(2026, 9, 15), QTime(7, 0)), QDateTime(QDate(2026, 9, 19), QTime(18, 59)), QDateTime(QDate(2026, 9, 20), QTime(9, 30)), QDateTime(QDate(2027, 1, 1), QTime(0, 0))})
      for (const QString& cls : {QString("3/6"), QString("3/7"), QString("X")})
        for (const QStringList& logged : {QStringList{}, QStringList{"08:00"}, QStringList{"08:00", "14:00"}})
          for (int row : {0, 3, 40}) {
            const QString name = QString("lessonEntry %1 %2 %3 row %4").arg(t.toString(Qt::ISODate), cls, logged.join(","), QString::number(row));
            check(name, toJson(jsCall("daftar.lessonEntry", {js(mj), cls, row, 794, 1123, jsDate(t), jsSet(logged)})), els(daftar::lessonEntry(m, cls, row, 794, 1123, t, QSet<QString>(logged.begin(), logged.end()))));
          }
    for (const QDate& d : {QDate(2026, 12, 19), QDate(2026, 12, 20), QDate(2026, 12, 28), QDate(2026, 12, 29), QDate(2026, 11, 18)}) {
      const auto h = daftar::holidayOn(m, d);
      check("holidayOn " + d.toString(Qt::ISODate), toJson(jsCall("daftar.holidayOn", {js(mj), jsDate(QDateTime(d, QTime(10, 0)))})), h ? QJsonValue(h->toJson()) : QJsonValue(QJsonValue::Null));
    }
    for (const QString& term : {"year", "s1", "s2", "month"}) {
      const auto r = daftar::termRange(m, term, QDate::currentDate());
      check("termRange " + term, toJson(jsCall("daftar.termRange", {js(mj), term})), r ? QJsonValue(QJsonObject{{"from", r->from}, {"to", r->to}}) : QJsonValue(QJsonValue::Null));
    }
  }
  // Free rows below random ink.
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> U(0, 1);
  const draw::LessonLayout L = draw::lessonLayout(794, 1123);
  for (int k = 0; k < 300; ++k) {
    std::vector<daftar::InkRect> ink;
    QJsonArray a;
    const int n = int(U(rng) * 4);
    for (int i = 0; i < n; ++i) {
      const double x0 = U(rng) * 800 - 20, y0 = U(rng) * 1150 - 20;
      const daftar::InkRect r{x0, y0, x0 + U(rng) * 300, y0 + U(rng) * (k % 3 ? 60 : 700)};
      ink.push_back(r);
      a.append(QJsonObject{{"x0", r.x0}, {"y0", r.y0}, {"x1", r.x1}, {"y1", r.y1}});
    }
    const int need = 1 + k % 5;
    const auto row = daftar::nextFreeRow(ink, 794, 1123, need);
    check(QString("nextFreeRow %1").arg(k), toJson(jsCall("daftar.nextFreeRow", {js(a), 794, 1123, need})), row ? QJsonValue(*row) : QJsonValue(QJsonValue::Null));
  }
  Q_UNUSED(L);
  // Holidays.
  for (const QString& y : {"2026-2027", "2030-2031", "", "abc"}) {
    QJsonArray n;
    for (const auto& h : daftar::nationalHolidays(y)) n.append(h.toJson());
    check("nationalHolidays " + y, toJson(jsCall("daftar.nationalHolidays", {y})), n);
  }
  for (const auto& [f, t] : {std::pair<QString, QString>{"2026-12-20", "2026-12-28"}, {"2027-02-27", "2027-03-02"}, {"2027-05-01", "2027-05-01"}, {"2027-05-03", "2027-05-01"}}) {
    const QJsonObject h{{"name", "x"}, {"from", f}, {"to", t}};
    check("holidayDays " + f + " " + t, toJson(jsCall("daftar.holidayDays", {js(h)})), daftar::holidayDays(daftar::Holiday::fromJson(h)));
  }
  // Pasted student lists.
  const QStringList pastes = {
      "رقم التلميذ\tالنسب\tالاسم\tالنوع\tتاريخ الازدياد\nD130045678\tالعلوي\tمحمد\tذكر\t12/03/2012\n1\tR131122334\tBennani\tSalma\tF\n\n  ",
      "1. Alaoui Mohamed\nD130000001 Idrissi Sara\nBennani;Omar",
      "N°;Nom;Prénom;Sexe\n1;El Amrani;Youssef;M\n2;Tazi;Imane;F\n3;;;\n",
      "Élève, Classe\nFatima Zahra, 3/6\nYassine, 3/6",
      "  12)   Ahmed  Benali  \r\n13- Khadija Ait\r\n\t\t\nJ145678901\tأيت\tيوسف\tأنثى\t2012-05-03\tمراكش\n",
      "a\nb\nc1234567\nab\n٣ محمد",
      "",
  };
  for (int i = 0; i < pastes.size(); ++i) {
    QJsonArray n;
    for (const auto& s : daftar::parseStudents(pastes[i])) n.append(QJsonObject{{"code", s.code}, {"name", s.name}});
    check(QString("parseStudents %1").arg(i), toJson(jsCall("daftar.parseStudents", {pastes[i]})), n);
    DaftarApi api;
    check(QString("studentsSummary %1").arg(i), toJson(jsCall("daftarQt.studentsSummary", {pastes[i]})), api.studentsSummary(pastes[i]));
  }
  QStringList many;
  for (int i = 0; i < 350; ++i) many << QString("D13%1\tName%2\tFirst").arg(1000000 + i).arg(i);
  {
    QJsonArray n;
    for (const auto& s : daftar::parseStudents(many.join("\n"))) n.append(QJsonObject{{"code", s.code}, {"name", s.name}});
    check("parseStudents 350", toJson(jsCall("daftar.parseStudents", {many.join("\n")})), n);
  }
  std::vector<daftar::Student> st;
  QJsonArray stj;
  for (int i = 0; i < 70; ++i) {
    st.push_back({i % 4 ? QString("D%1").arg(1000000 + i) : QString(), QString("الطالب رقم %1 مع اسم طويل جدا").arg(i).left(8 + i % 30)});
    stj.append(QJsonObject{{"code", st.back().code}, {"name", st.back().name}});
  }
  for (int off : {0, 60})
    for (const auto& [w, h] : sizes) check(QString("studentElements %1 %2x%3").arg(off).arg(w).arg(h), toJson(jsCall("daftar.studentElements", {js(stj), off, w, h})), els(daftar::studentElements(st, off, w, h)));
  // The settings form.
  DaftarApi api;
  for (const QString& s : {"3/6, 3/7", "1AC-2, 1AC-3 ; TC-1\n2BAC،3/6", " , ;", "A,A,B"}) check("parseClasses " + s, toJson(jsCall("daftarQt.parseClasses", {s})), QJsonArray::fromStringList(api.parseClasses(s)));
  const QJsonObject forms[] = {
      {{"classes", ""}, {"timetable", "[]"}, {"holidays", "[]"}},
      {{"classes", "3/6"}, {"timetable", "[{\"day\":2,\"start\":\"10:00\",\"end\":\"09:00\",\"cls\":\"3/6\"}]"}, {"holidays", "[]"}},
      {{"classes", "3/6"}, {"timetable", "[]"}, {"holidays", "[{\"name\":\"\",\"from\":\"2026-01-01\",\"to\":\"\"}]"}},
      {{"classes", "3/6"}, {"timetable", "[]"}, {"holidays", "[{\"name\":\" عطلة \",\"from\":\"\",\"to\":\"\"}]"}},
      {{"classes", "3/6"}, {"timetable", "[]"}, {"holidays", "[{\"name\":\"\",\"from\":\"\",\"to\":\"\"}]"}},
      {{"classes", "3/6"}, {"timetable", "[{\"day\":1,\"start\":\"08:00\",\"end\":\"10:00\",\"cls\":\"3/6\"}]"}, {"holidays", "[]"}},
  };
  for (int i = 0; i < 6; ++i) check(QString("validate %1").arg(i), toJson(jsCall("daftarQt.validate", {js(forms[i])})), QJsonValue::fromVariant(api.validate(forms[i].toVariantMap())));
  check("formValues new", toJson(jsCall("daftarQt.formValues", {QJSValue(QJSValue::NullValue), "دفتر النصوص"})), QJsonObject::fromVariantMap(api.formValues(QVariant(), "دفتر النصوص")));
  check("formValues meta", toJson(jsCall("daftarQt.formValues", {js(meta1()), "x"})), QJsonObject::fromVariantMap(api.formValues(meta1().toVariantMap(), "x")));
  // Cover titles and dates.
  const QStringList titles = {"", "Maths", "History and geography of the Mediterranean world", "دفتر النصوص", "A very very very long notebook title that goes on and on and on"};
  for (const auto& d : draw::coverDesigns())
    for (const QString& t : titles)
      for (const auto& [w, h] : sizes) check(QString("coverTextElements %1 %2 %3x%4").arg(d.id, t.left(12)).arg(w).arg(h), toJson(jsCall("coverTextElements", {d.id, t, "November 2026", w, h})), els(cover::coverTextElements(d.id, t, "November 2026", w, h)));
  // The catalogue.
  Catalog cat;
  check("templates", toJson(Js::instance().fn("TEMPLATES")), QJsonValue::fromVariant(cat.templates()));
  check("coverDesigns", toJson(Js::instance().fn("COVER_DESIGNS")), QJsonValue::fromVariant(api.coverDesigns()));
  check("days", toJson(Js::instance().fn("daftarQt.DAYS")), QJsonValue::fromVariant(api.days()));
  check("outlines", toJson(Js::instance().fn("daftarQt.OUTLINES")), QJsonValue::fromVariant(api.outlines()));
  for (const QString& t : {"", "- a\n- b", "1. a\n2. b", "- a\nb", "\n\n- x\n", "• a", "1) a", "plain"}) {
    const QString text = QString(t).replace("\\n", "\n");
    const QJsonValue a = toJson(jsCall("textFormat.currentList", {text}));
    check("currentList " + t, a.isNull() ? QJsonValue("") : a, cat.currentList(text));
  }
  for (double r = -7; r < 7; r += 0.0137)
    for (bool coarse : {false, true}) check(QString("snapAngle %1 %2").arg(r).arg(coarse), toJson(jsCall("snapAngle", {r, coarse})), cat.snapAngle(r, coarse));
  check("monthYear", toJson(Js::instance().engine().evaluate("new Date().toLocaleDateString(undefined, { month: 'long', year: 'numeric' })")), cover::monthYear());
}

// ------------------------------------------------------------ whole flows

/// Everything stored for a notebook: its details and every page with its elements.
QJsonObject snapshot(const QString& id) {
  QJsonObject nb = store::notebook(id);
  QJsonObject out{{"title", nb["title"]}, {"coverColor", nb["coverColor"]}};
  const auto m = daftar::parseDaftar(nb["meta"]);
  out["meta"] = m ? QJsonValue(m->toJson()) : QJsonValue(QJsonValue::Null);
  QJsonArray pages;
  for (const QJsonValue& v : store::pages(id)) {
    const QJsonObject p = v.toObject();
    QJsonArray e;
    for (const ed::El& el : store::elements(p["id"].toString())) e.append(el.toJson());
    pages.append(QJsonObject{{"template", p["template"]}, {"paperColor", p["paperColor"]}, {"section", p["section"]}, {"position", p["position"]}, {"width", p["width"]}, {"height", p["height"]}, {"hasThumbnail", !p["thumbnail"].toString().isEmpty()}, {"elements", e}});
  }
  out["pages"] = pages;
  return out;
}

QJsonObject form(const QString& classes, const QString& year) {
  return {{"teacher", "لبنى"}, {"subject", "الرياضيات"}, {"school", "ثانوية ابن سينا"}, {"year", year}, {"region", "جهة"}, {"directorate", "مديرية"}, {"payroll", "123"}, {"title", "دفتر 2026"}, {"classes", classes}, {"cover", "#5b2a5e"},
          {"timetable", "[{\"day\":1,\"start\":\"08:00\",\"end\":\"10:00\",\"cls\":\"3/6\"},{\"day\":3,\"start\":\"14:00\",\"end\":\"15:00\",\"cls\":\"3/7\"},{\"day\":4,\"start\":\"10:00\",\"end\":\"12:00\",\"cls\":\"gone\"}]"},
          {"holidays", "[{\"name\":\" عطلة \",\"from\":\"2026-12-20\",\"to\":\"2026-12-19\"}]"}};
}

QJsonValue proxyResult(ed::EditorCore& core, const QString& fn, const QJSValueList& args) {
  auto* bridge = new DaftarBridge(&core);
  QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
  const std::unique_ptr<DaftarBridge> keep(bridge);
  QJSValueList list{Js::instance().call("daftarQt.editorProxy", {Js::instance().engine().newQObject(bridge)})};
  list << args;
  return toJson(jsCall("daftarQt." + fn, list));
}

QJsonObject activePage(ed::EditorCore& core) {
  QJsonArray e;
  for (const ed::ElPtr& el : core.elements()) e.append(el->toJson());
  return {{"active", core.active}, {"elements", e}, {"canUndo", core.canUndo()}};
}

void flows() {
  DaftarApi api;
  // Two daftars made from the same form: the script's (A) and C++'s (B).
  const QJsonObject f = form("3/6, 3/7", "2026-2027");
  const QString a = toJson(jsCall("daftarQt.create", {js(f), QJSValue(QJSValue::NullValue)})).toObject()["id"].toString();
  const QString b = api.create(f.toVariantMap(), QString()).value("id").toString();
  check("create", snapshot(a), snapshot(b));
  // Updated: a class added, one removed, the year changed.
  const QJsonObject g = form("3/6, 2/1, 1/9", "2027-2028");
  toJson(jsCall("daftarQt.update", {a, js(g)}));
  api.update(b, g.toVariantMap());
  check("update", snapshot(a), snapshot(b));
  // Students: more than one page's worth.
  QStringList list;
  for (int i = 0; i < 130; ++i) list << QString("D13%1\tالاسم%2\tالنسب").arg(2000000 + i).arg(i);
  ed::EditorCore none(QJsonArray{});
  check("pasteStudents result", toJson(jsCall("daftarQt.pasteStudents", {a, "2/1", list.join("\n")})), QJsonValue::fromVariant(daftar::runInEditor(none, "pasteStudents", {b, "2/1", list.join("\n")})));
  check("pasteStudents", snapshot(a), snapshot(b));
  // In the editor: lessons until a page fills, outlines, a new page, exports.
  for (const QString& cls : {"3/6", "2/1"}) {
    for (int round = 0; round < 14; ++round) {
      ed::EditorCore ca(store::pages(a)), cb(store::pages(b));
      // Start on a page of the class or elsewhere.
      ca.activate(round % 4);
      cb.activate(round % 4);
      QJsonObject ra = proxyResult(ca, "newLesson", {a, cls}).toObject();
      QJsonObject rb = QJsonObject::fromVariantMap(daftar::runInEditor(cb, "newLesson", {b, cls}).toMap());
      check(QString("newLesson %1 %2").arg(cls).arg(round), ra, rb);
      check(QString("newLesson page %1 %2").arg(cls).arg(round), activePage(ca), activePage(cb));
      if (ra["again"].toBool()) {
        ed::EditorCore ca2(store::pages(a)), cb2(store::pages(b));
        ra = proxyResult(ca2, "newLesson", {a, cls}).toObject();
        rb = QJsonObject::fromVariantMap(daftar::runInEditor(cb2, "newLesson", {b, cls}).toMap());
        check(QString("newLesson again %1 %2").arg(cls).arg(round), ra, rb);
        check(QString("newLesson again page %1 %2").arg(cls).arg(round), activePage(ca2), activePage(cb2));
        ca2.takeDirty();
        cb2.takeDirty();
        // Saved as the canvas would.
        for (auto* c : {&ca2, &cb2}) {
          std::vector<ed::El> v;
          for (const ed::ElPtr& e : c->elements()) v.push_back(*e);
          store::saveElements(c->page()["id"].toString(), v);
        }
        continue;
      }
      const QJsonObject metaA = toJson(jsCall("daftarQt.info", {a})).toObject();
      const QVariant metaB = api.info(b);
      const QStringList lines = daftar::outlines()[size_t(round % 5)].lines;
      check(QString("insertOutline %1 %2").arg(cls).arg(round), proxyResult(ca, "insertOutline", {js(metaA), js(QJsonArray::fromStringList(lines))}), QJsonObject::fromVariantMap(daftar::runInEditor(cb, "insertOutline", {metaB, lines}).toMap()));
      check(QString("insertOutline page %1 %2").arg(cls).arg(round), activePage(ca), activePage(cb));
      for (auto* c : {&ca, &cb}) {
        std::vector<ed::El> v;
        for (const ed::ElPtr& e : c->elements()) v.push_back(*e);
        store::saveElements(c->page()["id"].toString(), v);
      }
    }
    check("lessons " + cls, snapshot(a), snapshot(b));
  }
  {
    ed::EditorCore ca(store::pages(a)), cb(store::pages(b));
    const QJsonObject metaA = toJson(jsCall("daftarQt.info", {a})).toObject();
    const QVariant metaB = api.info(b);
    check("info", metaA, QJsonValue::fromVariant(metaB));
    for (const QString& cls : {"3/6", "2/1", "1/9", "nope"})
      for (const QString& term : {"year", "s1", "s2", "month"}) {
        const QJsonValue ra = proxyResult(ca, "exportPages", {js(metaA), cls, term});
        check("exportPages " + cls + " " + term, ra, QJsonValue::fromVariant(daftar::runInEditor(cb, "exportPages", {metaB, cls, term})));
      }
    for (int i : {0, 9, 12, 20}) {
      ca.activate(i);
      cb.activate(i);
      check(QString("goToClass from %1").arg(i), proxyResult(ca, "goToClass", {"2/1"}), QJsonValue::fromVariant(daftar::runInEditor(cb, "goToClass", {"2/1"})));
      check(QString("addClassPage from %1").arg(i), proxyResult(ca, "addClassPage", {a}), QJsonValue::fromVariant(daftar::runInEditor(cb, "addClassPage", {b})));
    }
    check("added pages", snapshot(a), snapshot(b));
  }
  // Covers: a design change, and a notebook given its first cover.
  for (const auto& d : draw::coverDesigns()) {
    const QJsonObject nbA = store::notebook(a), nbB = store::notebook(b);
    jsCall("applyCover", {js(nbA), d.id, "#1f7a4a"});
    api.applyCover(nbB.toVariantMap(), d.id, "#1f7a4a");
    check("applyCover " + d.id, snapshot(a), snapshot(b));
  }
  for (bool withCover : {false, true}) {
    QJsonObject input{{"title", "Notes de cours"}, {"folderId", QJsonValue()}, {"coverColor", "#2563eb"}, {"template", "ruled"}, {"paperColor", "#ffffff"}, {"coverPage", withCover}};
    const QJsonObject na = store::createNotebook(input), nb = store::createNotebook(input);
    jsCall("setUpCover", {js(na)});
    api.setUpCover(nb.toVariantMap());
    check(QString("setUpCover %1").arg(withCover), snapshot(na["id"].toString()), snapshot(nb["id"].toString()));
    jsCall("applyCover", {js(na), "waves", "#dc2626"});
    api.applyCover(nb.toVariantMap(), "waves", "#dc2626");
    check(QString("applyCover new %1").arg(withCover), snapshot(na["id"].toString()), snapshot(nb["id"].toString()));
  }
}

}  // namespace

int compareDaftar() {
  QTemporaryDir data;
  qputenv("INKLEAF_DATA_DIR", data.path().toUtf8());
  pure();
  try {
    flows();
  } catch (const store::StoreError& e) {
    ++g_failed;
    qWarning().noquote() << "FAILED (storage):" << e.message();
  }
  qInfo().noquote() << QString("compare-daftar: %1 of %2 checks differ").arg(g_failed).arg(g_total);
  return g_failed ? 1 : 0;
}
