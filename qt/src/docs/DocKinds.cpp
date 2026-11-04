#include "DocKinds.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QDebug>
#include <algorithm>
#include <cmath>

#include "../Core.h"
#include "../daftar/Daftar.h"
#include "../daftar/Store.h"
#include "../editor/Model.h"
#include "../editor/Table.h"

namespace {

constexpr double A4W = 794, A4H = 1123;
const QString INK = QStringLiteral("#111827");
const QString LINE = QStringLiteral("#374151");
const QString SOFT = QStringLiteral("#cbd5e1");

struct Refresh {
  ~Refresh() { emit InkleafCore::instance()->libraryChanged(); }
};

/// Words in the document's language: Arabic, or French.
struct Words {
  bool ar;
  QString operator()(const char* fr, const char* arabic) const { return ar ? QString::fromUtf8(arabic) : QString::fromUtf8(fr); }
};

class Page {
public:
  explicit Page(double w = A4W, double h = A4H) : w(w), h(h) {}
  double w, h;
  std::vector<ed::El> els;

  ed::El& text(double x, double y, double width, const QString& t, double size, bool bold = false, const QString& align = QString(), const QString& color = INK) {
    ed::El e;
    e.id = ed::newId();
    e.kind = ed::Kind::Text;
    e.x = x, e.y = y, e.w = width;
    e.text = t;
    e.fontSize = size;
    e.color = color;
    if (bold) e.bold = true;
    if (!align.isEmpty()) e.align = align;
    e.z = double(els.size() + 1);
    els.push_back(std::move(e));
    return els.back();
  }
  /// A table: `cells` row by row (the first a header when `header`), columns
  /// and rows as shares of its box.
  ed::El& table(double x, double y, double width, double height, const std::vector<QStringList>& cells, std::vector<double> cols, std::vector<double> rows, bool header, double fontSize) {
    ed::El e;
    e.id = ed::newId();
    e.kind = ed::Kind::Table;
    e.x = x, e.y = y, e.w = width, e.h = height;
    e.color = LINE;
    e.width = 1.2;
    e.fontSize = fontSize;
    ed::TableData t;
    t.cols = std::move(cols);
    t.rows = std::move(rows);
    t.cells = cells;
    for (QStringList& r : t.cells)
      while (r.size() < int(t.cols.size())) r << QString();
    while (int(t.cells.size()) < int(t.rows.size())) t.cells.push_back(QStringList(QVector<QString>(int(t.cols.size())).toList()));
    t.header = header;
    ed::setTableData(e, t);
    e.z = double(els.size() + 1);
    els.push_back(std::move(e));
    return els.back();
  }
  void line(double x0, double y, double x1, const QString& color = SOFT, double width = 1) {
    ed::El e;
    e.id = ed::newId();
    e.kind = ed::Kind::Shape;
    e.shape.kind = "line";
    e.shape.points = {{x0, y}, {x1, y}};
    e.color = color;
    e.width = width;
    e.z = double(els.size() + 1);
    els.push_back(std::move(e));
  }
};

/// Right to left: the columns of each row the other way round.
std::vector<QStringList> rtl(std::vector<QStringList> rows, bool ar) {
  if (ar)
    for (QStringList& r : rows) std::reverse(r.begin(), r.end());
  return rows;
}
std::vector<double> rtl(std::vector<double> v, bool ar) {
  if (ar) std::reverse(v.begin(), v.end());
  return v;
}
std::vector<double> even(int n) { return std::vector<double>(size_t(std::max(1, n)), 1.0); }

/// "Label: value" or "Label: …".
QString field(const QString& label, const QString& value) { return label + " : " + (value.trimmed().isEmpty() ? QStringLiteral("…") : value.trimmed()); }

/// The daftar's settings, if one was picked.
std::optional<daftar::Meta> daftarMeta(const QVariantMap& o) {
  const QString id = o.value("daftarId").toString();
  if (id.isEmpty()) return std::nullopt;
  try {
    return daftar::parseDaftar(store::notebook(id)["meta"]);
  } catch (const store::StoreError&) {
    return std::nullopt;
  }
}
/// An option, else the daftar's value, else "".
QString pick(const QVariantMap& o, const char* key, const std::optional<daftar::Meta>& m, QString daftar::Meta::*member) {
  const QString v = o.value(key).toString().trimmed();
  if (!v.isEmpty() || !m) return v;
  return (*m).*member;
}

/// A new notebook of blank pages (no cover page), its pages filled in.
QVariantMap makeNotebook(const QString& title, const QString& folderId, const QString& cover, std::vector<Page> pages, const QString& kind) {
  if (pages.empty()) return {};
  const QJsonObject nb = store::createNotebook({{"title", title}, {"folderId", folderId.isEmpty() ? QJsonValue() : QJsonValue(folderId)}, {"coverColor", cover}, {"template", "blank"}, {"paperColor", "#ffffff"},
                                                {"width", pages[0].w}, {"height", pages[0].h}, {"coverPage", false}});
  const QString id = nb["id"].toString();
  QJsonArray existing = store::pages(id);
  for (size_t i = 0; i < pages.size(); ++i) {
    QString pageId;
    if (int(i) < existing.size()) pageId = existing.at(int(i)).toObject()["id"].toString();
    else pageId = store::addPage(id, int(i), "blank")["id"].toString();
    store::saveElements(pageId, pages[i].els);
  }
  store::setNotebookMeta(id, QString::fromUtf8(QJsonDocument(QJsonObject{{"kind", kind}}).toJson(QJsonDocument::Compact)));
  return store::notebook(id).toVariantMap();
}

}  // namespace

QVariantList DocKinds::daftars() const {
  QVariantList out;
  try {
    for (const QJsonValue& v : store::library()["notebooks"].toArray()) {
      const QJsonObject n = v.toObject();
      if (n["trashed"].toBool() || (n["locked"].toBool() && !n["unlocked"].toBool())) continue;
      const auto m = daftar::parseDaftar(n["meta"]);
      if (!m) continue;
      out << QVariantMap{{"notebookId", n["id"].toString()}, {"title", n["title"].toString()}, {"classes", m->classes}, {"school", m->school},
                         {"teacher", m->teacher}, {"subject", m->subject}, {"year", m->year}};
    }
  } catch (const store::StoreError& e) {
    qWarning().noquote() << "documents:" << e.message();
  }
  return out;
}

QStringList DocKinds::students(const QString& notebookId, const QString& cls) const {
  QStringList names;
  try {
    static const QRegularExpression code(QStringLiteral("^[A-Za-z]\\d{6,}$"));
    for (const QJsonValue& v : store::pages(notebookId)) {
      const QJsonObject p = v.toObject();
      if (p["section"].toString() != cls || p["template"].toString() != "daftar-students") continue;
      std::vector<ed::El> els = store::elements(p["id"].toString());
      // In the order they were written (the list's order).
      std::sort(els.begin(), els.end(), [](const ed::El& a, const ed::El& b) { return a.z < b.z; });
      for (const ed::El& e : els)
        if (e.role.value_or(QString()) == "daftar-student" && !code.match(e.text.trimmed()).hasMatch() && !e.text.trimmed().isEmpty()) names << e.text.trimmed();
    }
  } catch (const store::StoreError& e) {
    qWarning().noquote() << "documents:" << e.message();
  }
  return names;
}

QVariantMap DocKinds::lessonPlan(const QVariantMap& o, const QString& folderId) {
  const Refresh refresh;
  const Words t{o.value("lang").toString() != "fr"};
  const auto meta = daftarMeta(o);
  const QString subject = pick(o, "subject", meta, &daftar::Meta::subject);
  const QString teacher = pick(o, "teacher", meta, &daftar::Meta::teacher);
  const QString school = pick(o, "school", meta, &daftar::Meta::school);
  const QString lesson = o.value("title").toString().trimmed();
  Page p;
  const double m = 40, w = p.w - 2 * m;
  p.text(m, 34, w, t("Fiche pédagogique", "جذاذة درس"), 26, true, "center");
  p.text(m, 76, w, lesson.isEmpty() ? QStringLiteral("…") : lesson, 17, false, "center", "#374151");
  // Who, what, when.
  p.table(m, 116, w, 4 * 30,
          rtl({{t("Matière", "المادة"), subject, t("Niveau", "المستوى"), o.value("level").toString()},
               {t("Unité", "المجزوءة / الوحدة"), QString(), t("Durée", "المدة"), o.value("duration").toString()},
               {t("Enseignant(e)", "الأستاذ(ة)"), teacher, t("Date", "التاريخ"), QString()},
               {t("Établissement", "المؤسسة"), school, t("Classe", "القسم"), o.value("cls").toString()}}, t.ar),
          rtl({0.9, 1.6, 0.7, 1.0}, t.ar), even(4), false, 13);
  // Objectives and materials.
  p.table(m, 256, w, 4 * 28, {{t("Objectifs", "الأهداف")}, {QStringLiteral("1. ")}, {QStringLiteral("2. ")}, {QStringLiteral("3. ")}}, {1}, even(4), true, 13);
  p.table(m, 382, w, 34, rtl({{t("Matériel didactique", "الوسائل الديداكتيكية"), QString()}}, t.ar), rtl({1, 3.2}, t.ar), {1}, false, 13);
  // The lesson, stage by stage.
  const std::vector<QStringList> stages = {
      {t("Étapes", "المراحل"), t("Durée", "المدة"), t("Activités de l'enseignant(e)", "أنشطة الأستاذ(ة)"), t("Activités des élèves", "أنشطة المتعلمين"), t("Évaluation", "التقويم")},
      {t("Mise en situation", "التمهيد / الوضعية الانطلاقية")},
      {t("Construction des apprentissages", "بناء التعلمات")},
      {t("Application / consolidation", "الاستثمار / التطبيق")},
      {t("Évaluation", "التقويم")}};
  const double top = 434, height = p.h - 40 - top;
  p.table(m, top, w, height, rtl(stages, t.ar), rtl({1.25, 0.6, 2, 2, 1.15}, t.ar), {0.45, 1, 1.2, 1, 0.8}, true, 12);
  try {
    return makeNotebook(lesson.isEmpty() ? t("Fiche pédagogique", "جذاذة") : t("Fiche", "جذاذة") + " · " + lesson, folderId, "#0d9488", {p}, "lesson-plan");
  } catch (const store::StoreError& e) {
    emit failed(e.message());
    return {};
  }
}

QVariantMap DocKinds::test(const QVariantMap& o, const QString& folderId) {
  const Refresh refresh;
  const Words t{o.value("lang").toString() != "fr"};
  const auto meta = daftarMeta(o);
  const QString subject = pick(o, "subject", meta, &daftar::Meta::subject);
  const QString school = pick(o, "school", meta, &daftar::Meta::school);
  const QString year = pick(o, "year", meta, &daftar::Meta::year);
  const QString number = o.value("number").toString().trimmed().isEmpty() ? QStringLiteral("1") : o.value("number").toString().trimmed();
  const int count = std::clamp(o.value("exercises").toInt(), 1, 6);
  QVariantList points = o.value("points").toList();
  Page p;
  const double m = 40, w = p.w - 2 * m;
  const QString heading = t("Devoir surveillé n° ", "فرض محروس رقم ") + number;
  // The school's header, and the student's.
  p.table(m, 30, w, 2 * 32,
          rtl({{field(t("Établissement", "المؤسسة"), school), heading, field(t("Année scolaire", "السنة الدراسية"), year)},
               {field(t("Matière", "المادة"), subject), field(t("Niveau", "المستوى"), o.value("level").toString()), field(t("Durée", "المدة"), o.value("duration").toString())}}, t.ar),
          rtl({1.3, 1.1, 1.1}, t.ar), even(2), false, 12);
  p.table(m, 106, w, 34,
          rtl({{t("Nom et prénom :", "الاسم الكامل :"), t("Classe :", "القسم :"), t("N° :", "الرقم :"), t("Note :      / 20", "النقطة :      / 20")}}, t.ar),
          rtl({2.2, 1, 0.7, 1.1}, t.ar), {1}, false, 12);
  // The exercises, each with room to answer.
  const double top = 160, room = (p.h - 40 - top) / count;
  for (int i = 0; i < count; ++i) {
    const double y = top + i * room;
    const QString pts = i < points.size() && !points[i].toString().isEmpty() ? points[i].toString() : QStringLiteral("…");
    p.text(m, y + 4, w, t("Exercice ", "التمرين ") + QString::number(i + 1) + t(" (", " (") + pts + t(" pts)", " ن)"), 15, true, t.ar ? "right" : "left");
    for (double ly = y + 64; ly < y + room - 10; ly += 30) p.line(m, ly, m + w);
  }
  try {
    return makeNotebook(heading + (subject.isEmpty() ? QString() : " · " + subject), folderId, "#dc2626", {p}, "test");
  } catch (const store::StoreError& e) {
    emit failed(e.message());
    return {};
  }
}

QVariantMap DocKinds::classRegister(const QVariantMap& o, const QString& folderId) {
  const Refresh refresh;
  const Words t{o.value("lang").toString() != "fr"};
  const auto meta = daftarMeta(o);
  const QString subject = pick(o, "subject", meta, &daftar::Meta::subject);
  const QString year = pick(o, "year", meta, &daftar::Meta::year);
  const QString cls = o.value("cls").toString().trimmed();
  QStringList names = o.value("students").toStringList();
  names.erase(std::remove_if(names.begin(), names.end(), [](const QString& n) { return n.trimmed().isEmpty(); }), names.end());
  if (names.isEmpty())
    for (int i = 0; i < 30; ++i) names << QString();
  const int tests = std::clamp(o.value("tests").toInt(), 1, 8);
  const double W = A4H, H = A4W;  // landscape
  const double m = 30, w = W - 2 * m;
  const int per = 28;
  std::vector<Page> pages;
  const QString title = (cls.isEmpty() ? QString() : cls + " · ") + subject;
  auto heading = [&](Page& p, const QString& what) {
    p.text(m, 18, w, what + (title.trimmed().isEmpty() ? QString() : "  ·  " + title) + (year.isEmpty() ? QString() : "  ·  " + year), 16, true, t.ar ? "right" : "left");
  };
  auto rows = [&](int from, int count, int extra) {
    std::vector<QStringList> out;
    for (int i = from; i < from + count; ++i) {
      QStringList r{QString::number(i + 1), names.value(i)};
      for (int k = 0; k < extra; ++k) r << QString();
      out.push_back(r);
    }
    return out;
  };
  // Attendance: twelve sessions a page, dates written in the header.
  for (int from = 0; from < names.size(); from += per) {
    Page p(W, H);
    heading(p, t("Absences", "الغياب"));
    const int n = std::min<int>(per, int(names.size()) - from);
    std::vector<QStringList> cells{QStringList{QStringLiteral("N°"), t("Nom et prénom", "الاسم الكامل")}};
    for (int k = 0; k < 12; ++k) cells[0] << QString();
    for (const QStringList& r : rows(from, n, 12)) cells.push_back(r);
    std::vector<double> cols{0.45, 3.2};
    for (int k = 0; k < 12; ++k) cols.push_back(0.62);
    const double rowH = std::min(24.0, (H - 70 - m) / (n + 1));
    p.table(m, 52, w, rowH * (n + 1), rtl(cells, t.ar), rtl(cols, t.ar), even(n + 1), true, 11);
    pages.push_back(std::move(p));
  }
  // Marks, and each student's average (worked out as marks are typed).
  for (int from = 0; from < names.size(); from += per) {
    Page p(W, H);
    heading(p, t("Notes", "النقط"));
    const int n = std::min<int>(per, int(names.size()) - from);
    std::vector<QStringList> cells{QStringList{QStringLiteral("N°"), t("Nom et prénom", "الاسم الكامل")}};
    for (int k = 0; k < tests; ++k) cells[0] << t("Contrôle ", "فرض ") + QString::number(k + 1);
    cells[0] << t("Activités", "الأنشطة") << t("Moyenne", "المعدل");
    for (const QStringList& r : rows(from, n, tests + 2)) cells.push_back(r);
    std::vector<double> cols{0.45, 3.2};
    for (int k = 0; k < tests + 2; ++k) cols.push_back(1.0);
    const double rowH = std::min(24.0, (H - 70 - m) / (n + 1));
    ed::El& tbl = p.table(m, 52, w, rowH * (n + 1), rtl(cells, t.ar), rtl(cols, t.ar), even(n + 1), true, 11);
    // The average: of the columns after the name, up to the average.
    tbl.extra["average"] = QJsonObject{{"rtl", t.ar}};
    pages.push_back(std::move(p));
  }
  try {
    return makeNotebook(t("Registre", "سجل") + (title.trimmed().isEmpty() ? QString() : " · " + title), folderId, "#2563eb", std::move(pages), "register");
  } catch (const store::StoreError& e) {
    emit failed(e.message());
    return {};
  }
}
