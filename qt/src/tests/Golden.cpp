// Reference results (docs/MIGRATION.md): what the C++ code gave when it was
// checked identical to the TypeScript it replaced, kept so later changes are
// checked against it. inkleaf --golden record <folder> writes them;
// inkleaf --golden check <folder> [<diffs folder>] compares and fails on a
// difference. They are recorded by CI (its Qt version), and cover:
//   editor.json  the editing sessions, step by step (state, results, overlay)
//   daftar.json  the daftar's and covers' text boxes, lessons, holidays,
//                students, outlines, exports and whole flows (date fixed)
//   render/      pictures: every paper and cover, daftar pages, elements of
//                every kind, whole pages and the pen previews
#include "Golden.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtDebug>
#include <random>

#include "../Catalog.h"
#include "EditorSessions.h"
#include "JsonCompare.h"
#include "../Canvas.h"
#include "../PaperImage.h"
#include "../daftar/Cover.h"
#include "../daftar/Daftar.h"
#include "../daftar/DaftarApi.h"
#include "../daftar/DaftarEditor.h"
#include "../daftar/Store.h"
#include "../draw/Covers.h"
#include "../draw/DaftarPapers.h"
#include "../draw/Elements.h"
#include "../draw/Render.h"
#include "../editor/EditorCore.h"
#include "Samples.h"

namespace {

// ------------------------------------------------------------ the editor

QJsonObject overlaySummary(const QImage& img0) {
  const QImage img = img0.convertToFormat(QImage::Format_ARGB32);
  int n = 0, x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
  for (int y = 0; y < img.height(); ++y) {
    const QRgb* p = reinterpret_cast<const QRgb*>(img.constScanLine(y));
    for (int x = 0; x < img.width(); ++x)
      if (qAlpha(p[x]) > 24) {
        ++n;
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
      }
  }
  return {{"pixels", n}, {"box", n ? QJsonValue(QJsonArray{x0, y0, x1, y1}) : QJsonValue(QJsonValue::Null)}};
}

QJsonObject editorResults() {
  QJsonObject out;
  QHash<QString, QJsonValue> lastScenes;
  runEditorSessions([&](const QString& session, const QString& step, const QJsonValue& result, const QJsonValue& state0, const QImage& overlay) {
    QJsonArray steps = out[session].toArray();
    // The pages' elements only when they changed (they're most of it).
    QJsonObject state = state0.toObject();
    if (lastScenes.value(session) == state["scenes"]) state["scenes"] = "unchanged";
    else lastScenes[session] = state["scenes"];
    QJsonObject s{{"step", step}, {"state", state}, {"overlay", overlaySummary(overlay)}};
    if (!result.isUndefined()) s["result"] = result;
    steps.append(s);
    out[session] = steps;
  });
  return out;
}

// ------------------------------------------------------------ the daftar and covers

QJsonArray els(const std::vector<ed::El>& v) {
  QJsonArray a;
  for (const ed::El& e : v) a.append(e.toJson());
  return a;
}

daftar::Meta meta1() {
  daftar::Meta m;
  m.year = "2026-2027";
  m.region = "الرباط سلا القنيطرة";
  m.directorate = "تمارة الصخيرات";
  m.school = "ماء العينين";
  m.teacher = "لبنى أرسلان";
  m.payroll = "2320866";
  m.subject = "اللغة العربية";
  m.classes = {"3/6", "3/7"};
  m.holidays = {{"عطلة", "2026-12-20", "2026-12-28"}, {"عيد الاستقلال", "2026-11-18", "2026-11-18"}};
  m.timetable = {{1, "14:00", "16:00", "3/6"}, {1, "08:00", "10:00", "3/6"}, {2, "10:00", "12:00", "3/7"}, {6, "07:30", "19:00", "3/7"}, {0, "09:00", "10:00", "3/7"}};
  return m;
}
daftar::Meta meta2() {
  daftar::Meta m;
  m.year = "2027-2028";
  m.directorate = "مديرية إقليمية ذات اسم طويل جدا جدا جدا لتجربة تصغير الخط";
  m.school = "A";
  m.subject = "Mathématiques";
  for (int i = 0; i < 9; ++i) m.classes << QString("TCS-%1").arg(i + 1);
  for (int i = 0; i < 18; ++i) m.holidays.push_back({QString("عطلة منتصف الدورة الأولى والثانية رقم %1").arg(i), QString("2027-0%1-1%2").arg(1 + i % 8).arg(i % 9), i % 3 ? QString("2027-0%1-2%2").arg(1 + i % 8).arg(i % 9) : QString()});
  return m;
}

QJsonObject snapshot(const QString& id) {
  const QJsonObject nb = store::notebook(id);
  QJsonObject out{{"title", nb["title"]}, {"coverColor", nb["coverColor"]}};
  const auto m = daftar::parseDaftar(nb["meta"]);
  out["meta"] = m ? QJsonValue(m->toJson()) : QJsonValue(QJsonValue::Null);
  QJsonArray pages;
  for (const QJsonValue& v : store::pages(id)) {
    const QJsonObject p = v.toObject();
    pages.append(QJsonObject{{"template", p["template"]}, {"paperColor", p["paperColor"]}, {"section", p["section"]}, {"position", p["position"]}, {"width", p["width"]}, {"height", p["height"]}, {"hasThumbnail", !p["thumbnail"].toString().isEmpty()}, {"elements", els(store::elements(p["id"].toString()))}});
  }
  out["pages"] = pages;
  return out;
}

QJsonObject activePage(ed::EditorCore& core) {
  QJsonArray e;
  for (const ed::ElPtr& el : core.elements()) e.append(el->toJson());
  return {{"active", core.active}, {"elements", e}, {"canUndo", core.canUndo()}};
}

void saveActive(ed::EditorCore& c) {
  std::vector<ed::El> v;
  for (const ed::ElPtr& e : c.elements()) v.push_back(*e);
  store::saveElements(c.page()["id"].toString(), v);
}

QJsonObject daftarResults() {
  QJsonObject out;
  QJsonArray lastPages;
  auto put = [&](const QString& name, QJsonValue v) {
    QHash<QString, QString> ids;
    normalise(v, ids);
    // A notebook's pages only when they changed since the last flow step.
    if (v.isObject() && v.toObject().contains("pages")) {
      QJsonObject o = v.toObject();
      if (o["pages"].toArray() == lastPages) o["pages"] = "unchanged";
      else lastPages = o["pages"].toArray();
      v = o;
    }
    out[name] = v;
  };
  const std::pair<double, double> sizes[] = {{794, 1123}, {1123, 794}, {559, 794}};
  const QStringList templates = {"daftar-title", "daftar-profile", "daftar-profile-2", "daftar-timetable", "daftar-holidays", "daftar-diagnostic", "daftar-students", "daftar-memos", "daftar-absence", "daftar", "daftar-blue", "daftar-green", "daftar-pink", "daftar-navy", "daftar-orange"};
  int mi = 0;
  for (const daftar::Meta& m : {meta1(), meta2()}) {
    ++mi;
    for (const auto& [w, h] : sizes) {
      const QString at = QString("m%1 %2x%3").arg(mi).arg(w).arg(h);
      for (const QString& t : templates)
        for (const QString& cls : {QString(), QString("3/6"), QString("TCS-9")}) put(QString("pageElements %1 %2 %3").arg(at, t, cls), els(daftar::pageElements(t, m, cls, w, h)));
      put("coverElements " + at, els(daftar::coverElements(m, w, h)));
      put("holidayElements " + at, els(daftar::holidayElements(m, w, h, 3)));
      for (int row : {0, 5, 30, 60}) put(QString("outlineElements %1 row %2").arg(at).arg(row), els(daftar::outlineElements(daftar::outlines()[0].lines, row, w, h)));
    }
    for (const QDateTime& t : {QDateTime(QDate(2026, 9, 14), QTime(9, 5)), QDateTime(QDate(2026, 9, 14), QTime(15, 30)), QDateTime(QDate(2026, 9, 15), QTime(7, 0)), QDateTime(QDate(2026, 9, 20), QTime(9, 30))})
      for (const QString& cls : {QString("3/6"), QString("3/7")})
        for (const QStringList& logged : {QStringList{}, QStringList{"08:00"}, QStringList{"08:00", "14:00"}})
          put(QString("lessonEntry m%1 %2 %3 %4").arg(mi).arg(t.toString(Qt::ISODate), cls, logged.join(",")), els(daftar::lessonEntry(m, cls, 3, 794, 1123, t, QSet<QString>(logged.begin(), logged.end()))));
    for (const QString& term : {"year", "s1", "s2", "month"}) {
      const auto r = daftar::termRange(m, term, QDate(2026, 10, 3));
      put(QString("termRange m%1 %2").arg(mi).arg(term), r ? QJsonValue(QJsonObject{{"from", r->from}, {"to", r->to}}) : QJsonValue(QJsonValue::Null));
    }
  }
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> U(0, 1);
  QJsonArray rows;
  for (int k = 0; k < 120; ++k) {
    std::vector<daftar::InkRect> ink;
    for (int i = 0, n = int(U(rng) * 4); i < n; ++i) {
      const double x0 = U(rng) * 800 - 20, y0 = U(rng) * 1150 - 20;
      ink.push_back({x0, y0, x0 + U(rng) * 300, y0 + U(rng) * (k % 3 ? 60 : 700)});
    }
    const auto row = daftar::nextFreeRow(ink, 794, 1123, 1 + k % 5);
    rows.append(row ? QJsonValue(*row) : QJsonValue(QJsonValue::Null));
  }
  put("nextFreeRow", rows);
  for (const QString& y : {"2026-2027", "2030-2031"}) {
    QJsonArray n;
    for (const auto& h : daftar::nationalHolidays(y)) n.append(h.toJson());
    put("nationalHolidays " + y, n);
  }
  const QStringList pastes = {
      "رقم التلميذ\tالنسب\tالاسم\tالنوع\tتاريخ الازدياد\nD130045678\tالعلوي\tمحمد\tذكر\t12/03/2012\n1\tR131122334\tBennani\tSalma\tF\n\n  ",
      "1. Alaoui Mohamed\nD130000001 Idrissi Sara\nBennani;Omar",
      "N°;Nom;Prénom;Sexe\n1;El Amrani;Youssef;M\n2;Tazi;Imane;F\n3;;;\n",
      "Élève, Classe\nFatima Zahra, 3/6\nYassine, 3/6",
      "  12)   Ahmed  Benali  \r\n13- Khadija Ait\r\n\t\t\nJ145678901\tأيت\tيوسف\tأنثى\t2012-05-03\tمراكش\n",
  };
  DaftarApi api;
  for (int i = 0; i < pastes.size(); ++i) {
    QJsonArray n;
    for (const auto& s : daftar::parseStudents(pastes[i])) n.append(QJsonObject{{"code", s.code}, {"name", s.name}});
    put(QString("parseStudents %1").arg(i), n);
    put(QString("studentsSummary %1").arg(i), api.studentsSummary(pastes[i]));
  }
  std::vector<daftar::Student> st;
  for (int i = 0; i < 70; ++i) st.push_back({i % 4 ? QString("D%1").arg(1000000 + i) : QString(), QString("الطالب رقم %1 مع اسم طويل جدا").arg(i).left(8 + i % 30)});
  for (int off : {0, 60}) put(QString("studentElements %1").arg(off), els(daftar::studentElements(st, off, 794, 1123)));
  Catalog cat;
  put("templates", QJsonValue::fromVariant(cat.templates()));
  put("coverDesigns", QJsonValue::fromVariant(api.coverDesigns()));
  for (const auto& d : draw::coverDesigns())
    for (const QString& t : {QString(), QString("Maths"), QString("History and geography of the Mediterranean world"), QString("دفتر النصوص")}) put(QString("coverTextElements %1 %2").arg(d.id, t), els(cover::coverTextElements(d.id, t, "November 2026", 794, 1123)));

  // Whole flows, on a fixed day (a Monday in a school year).
  const QJsonObject f{{"teacher", "لبنى"}, {"subject", "الرياضيات"}, {"school", "ثانوية ابن سينا"}, {"year", "2026-2027"}, {"region", "جهة"}, {"directorate", "مديرية"}, {"payroll", "123"}, {"title", "دفتر 2026"}, {"classes", "3/6, 3/7"}, {"cover", "#5b2a5e"},
                      {"timetable", "[{\"day\":1,\"start\":\"08:00\",\"end\":\"10:00\",\"cls\":\"3/6\"},{\"day\":3,\"start\":\"14:00\",\"end\":\"15:00\",\"cls\":\"3/7\"}]"},
                      {"holidays", "[{\"name\":\" عطلة \",\"from\":\"2026-12-20\",\"to\":\"2026-12-19\"}]"}};
  const QString b = api.create(f.toVariantMap(), QString()).value("id").toString();
  put("flow create", snapshot(b));
  QJsonObject g = f;
  g["classes"] = "3/6, 2/1, 1/9";
  g["year"] = "2027-2028";
  api.update(b, g.toVariantMap());
  put("flow update", snapshot(b));
  QStringList list;
  for (int i = 0; i < 130; ++i) list << QString("D13%1\tالاسم%2\tالنسب").arg(2000000 + i).arg(i);
  ed::EditorCore none(QJsonArray{});
  put("flow pasteStudents result", QJsonValue::fromVariant(daftar::runInEditor(none, "pasteStudents", {b, "2/1", list.join("\n")})));
  put("flow pasteStudents", snapshot(b));
  for (const QString& cls : {"3/6", "2/1"})
    for (int round = 0; round < 12; ++round) {
      ed::EditorCore c(store::pages(b));
      c.activate(round % 4);
      const QVariantMap r = daftar::runInEditor(c, "newLesson", {b, cls}).toMap();
      put(QString("flow newLesson %1 %2").arg(cls).arg(round), QJsonObject{{"result", QJsonObject::fromVariantMap(r)}, {"page", activePage(c)}});
      if (r.value("again").toBool()) {
        ed::EditorCore c2(store::pages(b));
        const QVariantMap r2 = daftar::runInEditor(c2, "newLesson", {b, cls}).toMap();
        put(QString("flow newLesson again %1 %2").arg(cls).arg(round), QJsonObject{{"result", QJsonObject::fromVariantMap(r2)}, {"page", activePage(c2)}});
        saveActive(c2);
        continue;
      }
      const QVariant m = api.info(b);
      const QVariant r3 = daftar::runInEditor(c, "insertOutline", {m, daftar::outlines()[size_t(round % 5)].lines});
      put(QString("flow insertOutline %1 %2").arg(cls).arg(round), QJsonObject{{"result", QJsonValue::fromVariant(r3)}, {"page", activePage(c)}});
      saveActive(c);
    }
  put("flow lessons", snapshot(b));
  {
    ed::EditorCore c(store::pages(b));
    const QVariant m = api.info(b);
    for (const QString& cls : {"3/6", "2/1", "nope"})
      for (const QString& term : {"year", "s1", "s2", "month"}) put("flow exportPages " + cls + " " + term, QJsonValue::fromVariant(daftar::runInEditor(c, "exportPages", {m, cls, term})));
    for (int i : {0, 9, 12}) {
      c.activate(i);
      put(QString("flow goToClass %1").arg(i), QJsonValue::fromVariant(daftar::runInEditor(c, "goToClass", {"2/1"})));
      put(QString("flow addClassPage %1").arg(i), QJsonValue::fromVariant(daftar::runInEditor(c, "addClassPage", {b})));
    }
    put("flow added pages", snapshot(b));
  }
  for (const auto& d : draw::coverDesigns()) {
    api.applyCover(store::notebook(b).toVariantMap(), d.id, "#1f7a4a");
    put("flow applyCover " + d.id, snapshot(b));
  }
  const QJsonObject n = store::createNotebook({{"title", "Notes de cours"}, {"folderId", QJsonValue()}, {"coverColor", "#2563eb"}, {"template", "ruled"}, {"paperColor", "#ffffff"}, {"coverPage", true}});
  api.setUpCover(n.toVariantMap());
  put("flow setUpCover", snapshot(n["id"].toString()));
  return out;
}

// ------------------------------------------------------------ pictures

std::vector<std::pair<QString, std::function<QImage()>>> pictures() {
  std::vector<std::pair<QString, std::function<QImage()>>> out;
  for (const PaperTemplate& t : paperTemplates()) {
    const QString id = t.id;
    out.push_back({"paper-" + id, [=] { return draw::paperImage(id, id.startsWith("daftar") ? "#ffffff" : "#fffdf7", 794, 1123, 0.6); }});
  }
  out.push_back({"paper-grid-dark-landscape", [] { return draw::paperImage("grid", "#22262d", 1123, 794, 0.6); }});
  out.push_back({"paper-ruled-a5", [] { return draw::paperImage("ruled", "#fdf3c8", 559, 794, 0.8); }});
  out.push_back({"paper-cornell-part", [] { return draw::paperImage("cornell", "#fffdf7", 794, 1123, 3.0, QRectF(100, 200, 250, 180)); }});
  for (const draw::CoverDesign& d : draw::coverDesigns()) {
    const QString tmpl = d.tmpl;
    out.push_back({"cover-" + d.id, [=] { return draw::paperImage(tmpl, "#2563eb", 794, 1123, 0.5); }});
  }
  out.push_back({"cover-zellige-landscape-purple", [] { return draw::paperImage("cover-zellige", "#5b2a5e", 1123, 794, 0.5); }});
  out.push_back({"cover-kraft-green", [] { return draw::paperImage("cover-kraft", "#16a34a", 794, 1123, 0.5); }});
  for (const auto& [name, el] : samples::sampleElements()) {
    const QJsonObject e = el;
    out.push_back({name, [=] {
                     Canvas c(320, 280);
                     draw::drawElement(c, e);
                     return c.image();
                   }});
  }
  QJsonArray all;
  for (const auto& [name, el] : samples::sampleElements())
    if (el["kind"].toString() != "image") all.append(el);
  for (const char* t : {"ruled", "cover-zellige", "planner-week"}) {
    const QString tmpl = t;
    out.push_back({QString("page-%1").arg(t), [=] { return draw::renderPage(tmpl, "#fffdf7", 794, 1123, all, 0.8)->image(); }});
  }
  for (const char* style : {"fountain", "ballpoint", "pencil", "calligraphy", "brush", "marker", "watercolor", "neon", "construction", "tape"})
    for (bool dark : {false, true}) {
      const QString s = style;
      out.push_back({QString("sample-%1-%2").arg(style).arg(dark ? "dark" : "light"), [=] {
                       Canvas c(112, 44);
                       c.setTransform(2, 0, 0, 2, 0, 0);
                       nibSample(c, s, dark ? "#111827" : "#ffffff", 3, dark);
                       return c.image();
                     }});
    }
  out.push_back({"cover-preview-waves", [] { return cover::coverPreview("waves", "#dc2626", "Physics notes", 240, false); }});
  return out;
}

/// Pixels that differ clearly (anti-aliasing aside), and how many in all.
int clearDiffs(const QImage& a0, const QImage& b0, QImage* map) {
  const QImage a = a0.convertToFormat(QImage::Format_ARGB32), b = b0.convertToFormat(QImage::Format_ARGB32);
  if (a.size() != b.size()) return -1;
  *map = QImage(a.size(), QImage::Format_ARGB32);
  map->fill(Qt::white);
  int n = 0;
  for (int y = 0; y < a.height(); ++y) {
    const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
    const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
    for (int x = 0; x < a.width(); ++x)
      if (std::max({std::abs(qRed(pa[x]) - qRed(pb[x])), std::abs(qGreen(pa[x]) - qGreen(pb[x])), std::abs(qBlue(pa[x]) - qBlue(pb[x])), std::abs(qAlpha(pa[x]) - qAlpha(pb[x]))}) > 24) {
        ++n;
        map->setPixel(x, y, qRgb(255, 0, 0));
      }
  }
  return n;
}

bool writeJson(const QString& path, const QJsonObject& o) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
  return true;
}
QJsonObject readJson(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(f.readAll()).object();
}

/// Differences between recorded and new results, a little looser on numbers than the side-by-side checks.
int compareResults(const QString& what, const QJsonObject& want, const QJsonObject& got) {
  int failed = 0;
  QStringList keys = want.keys();
  for (const QString& k : got.keys())
    if (!keys.contains(k)) keys << k;
  for (const QString& k : keys) {
    QStringList diffs;
    if (!want.contains(k)) diffs << "new (not recorded)";
    else if (!got.contains(k)) diffs << "missing (recorded, not made now)";
    else compareLoose(want[k], got[k], "", diffs);
    if (diffs.isEmpty()) continue;
    ++failed;
    qWarning().noquote() << "DIFFERS" << what << k;
    for (const QString& d : diffs.mid(0, 6)) qWarning().noquote() << "   " << d;
  }
  return failed;
}

}  // namespace

int golden(const QString& mode, const QString& dir, const QString& diffDir) {
  QTemporaryDir data;
  qputenv("INKLEAF_DATA_DIR", data.path().toUtf8());
  daftar::setNowForTests(QDateTime(QDate(2026, 9, 14), QTime(9, 5)));
  QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
  const bool record = mode == "record";
  if (!record && mode != "check") {
    qWarning("--golden record|check <folder> [<diffs folder>]");
    return 2;
  }
  QDir().mkpath(dir + "/render");
  if (!diffDir.isEmpty()) QDir().mkpath(diffDir);
  int failed = 0, total = 0;

  const QJsonObject editor = editorResults(), daftarNow = daftarResults();
  total += editor.size() + daftarNow.size();
  if (record) {
    writeJson(dir + "/editor.json", editor);
    writeJson(dir + "/daftar.json", daftarNow);
  } else {
    // Each editor session is compared step by step.
    const QJsonObject want = readJson(dir + "/editor.json");
    for (const QString& session : editor.keys()) {
      const QJsonArray a = want[session].toArray(), b = editor[session].toArray();
      QStringList diffs;
      if (a.size() != b.size()) diffs << QString("%1 steps recorded, %2 now").arg(a.size()).arg(b.size());
      for (int i = 0; i < std::min(a.size(), b.size()) && diffs.size() < 6; ++i) {
        QJsonObject x = a[i].toObject(), y = b[i].toObject();
        const QJsonObject ox = x.take("overlay").toObject(), oy = y.take("overlay").toObject();
        QStringList d;
        compareLoose(x, y, "", d);
        // The overlay's picture: about the same ink in about the same place.
        const double px = ox["pixels"].toDouble(), py = oy["pixels"].toDouble();
        if (std::abs(px - py) > std::max(40.0, 0.04 * px)) d << QString("overlay: %1 inked pixels recorded, %2 now").arg(px).arg(py);
        const QJsonArray bx = ox["box"].toArray(), by = oy["box"].toArray();
        for (int k = 0; k < std::min(bx.size(), by.size()); ++k)
          if (std::abs(bx[k].toDouble() - by[k].toDouble()) > 3) d << "overlay: drawn somewhere else";
        for (const QString& line : d) diffs << QString("step %1 (%2) %3").arg(i).arg(x["step"].toString(), line);
      }
      if (diffs.isEmpty()) continue;
      ++failed;
      qWarning().noquote() << "DIFFERS editor session" << session;
      for (const QString& d : diffs.mid(0, 8)) qWarning().noquote() << "   " << d;
    }
    failed += compareResults("daftar", readJson(dir + "/daftar.json"), daftarNow);
  }

  QJsonObject sizes;
  for (const auto& [name, make] : pictures()) {
    ++total;
    const QImage img = make();
    const QString file = dir + "/render/" + name + ".png";
    if (record) {
      img.save(file);
      continue;
    }
    const QImage want(file);
    QImage map;
    const int n = want.isNull() ? -2 : clearDiffs(want, img, &map);
    // A few pixels of anti-aliasing apart is the same picture.
    if (n >= 0 && n <= std::max(4, int(img.width() * img.height() * 0.0005))) continue;
    ++failed;
    qWarning().noquote() << "DIFFERS picture" << name << (n == -2 ? QString("(not recorded)") : n < 0 ? QString("(another size)") : QString("%1 pixels").arg(n));
    if (!diffDir.isEmpty()) {
      img.save(diffDir + "/" + name + "-now.png");
      if (!want.isNull()) want.save(diffDir + "/" + name + "-recorded.png");
      if (n > 0) map.save(diffDir + "/" + name + "-diff.png");
    }
  }
  if (record) {
    qInfo().noquote() << QString("golden: %1 results recorded in %2").arg(total).arg(dir);
    return 0;
  }
  qInfo().noquote() << QString("golden: %1 of %2 results differ").arg(failed).arg(total);
  return failed ? 1 : 0;
}
