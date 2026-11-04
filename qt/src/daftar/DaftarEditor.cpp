#include "DaftarEditor.h"

#include <QtDebug>

#include "../draw/DaftarPapers.h"
#include "../editor/Geometry.h"
#include "Daftar.h"
#include "Store.h"

namespace daftar {

namespace {

bool isField(const ed::El& e) { return e.kind == ed::Kind::Text && e.role == QStringLiteral("daftar-field"); }

std::vector<InkRect> inkRects(const ed::Els& els) {
  std::vector<InkRect> out;
  for (const ed::ElPtr& e : els) {
    if (isField(*e)) continue;
    const ed::Rect& b = ed::EditorCore::boxOf(*e);
    out.push_back({b.x, b.y, b.x + b.w, b.y + b.h});
  }
  return out;
}

QString sectionOf(const ed::EditorCore& core, int i) { return core.pages[size_t(i)].meta["section"].toString(); }
QString templateOf(const ed::EditorCore& core, int i) { return core.pages[size_t(i)].meta["template"].toString(); }

/// The class of the page being edited (or the first class).
QString currentClass(const ed::EditorCore& core, const Meta& meta) {
  const QString s = core.page()["section"].toString();
  return !s.isEmpty() && meta.classes.contains(s) ? s : meta.classes.value(0);
}

/// A class's lesson page in use: the last with writing on it (else the first); -1 when none.
int classPage(ed::EditorCore& core, const QString& cls) {
  int current = -1;
  for (int i = 0; i < int(core.pages.size()); ++i) {
    if (sectionOf(core, i) != cls || !draw::isLessonTemplate(templateOf(core, i))) continue;
    if (current < 0) current = i;
    const ed::Els& els = core.load(i);
    if (std::any_of(els.begin(), els.end(), [](const ed::ElPtr& e) { return !isField(*e); })) current = i;
  }
  return current;
}

/// Adds elements to the active page as one undo step.
void addToActive(ed::EditorCore& core, std::vector<ed::El> els) {
  core.snapshot();
  double z = -1;
  for (const ed::ElPtr& e : core.elements()) z = std::max(z, e->z);
  z += 1;
  for (ed::El& e : els) {
    e.z = z++;
    core.elements().push_back(ed::makeEl(std::move(e)));
  }
  core.changed();
}

QVariantMap newLesson(ed::EditorCore& core, const QString& notebookId, const QString& cls) {
  const QJsonObject nb = store::notebook(notebookId);
  const auto d = parseDaftar(nb["meta"]);
  if (!d) return {};
  core.finishTextEdit();
  int target = classPage(core, cls);
  if (core.page()["section"].toString() == cls && draw::isLessonTemplate(core.page()["template"].toString())) target = core.active;
  std::optional<int> row;
  if (target >= 0) {
    const QJsonObject& p = core.pages[size_t(target)].meta;
    row = nextFreeRow(inkRects(core.load(target)), p["width"].toDouble(), p["height"].toDouble());
  }
  if (target < 0 || !row) {
    const QJsonObject page = addLessonPage(nb, *d, cls);
    return {{"reload", true}, {"again", true}, {"pageId", page["id"].toString()}};
  }
  const QJsonObject p = core.pages[size_t(target)].meta;
  core.activate(target);
  const QDateTime now = QDateTime::currentDateTime();
  const QString today = isoDate(now.date());
  QSet<QString> logged;
  for (const ed::ElPtr& e : core.elements())
    if (e->kind == ed::Kind::Text && e->role == QStringLiteral("daftar-session") && e->session && e->session->startsWith(today)) logged.insert(e->session->mid(11));
  const double w = p["width"].toDouble(), h = p["height"].toDouble();
  std::vector<ed::El> entry = lessonEntry(*d, cls, *row, w, h, now, logged);
  const QString date = entry[0].text;
  const QString time = entry.size() > 2 ? entry[2].text : QString();
  addToActive(core, std::move(entry));
  const draw::LessonLayout L = draw::lessonLayout(w, h);
  return {{"goTo", target}, {"y", L.bodyTop + *row * L.step}, {"toast", cls + " · " + days()[size_t(now.date().dayOfWeek() % 7)].ar + " " + date + (time.isEmpty() ? QString() : " · " + time)}};
}

QVariantMap insertOutline(ed::EditorCore& core, const Meta& meta, const QStringList& lines) {
  const QString cls = currentClass(core, meta);
  if (cls.isEmpty()) return {};
  core.finishTextEdit();
  if (!draw::isLessonTemplate(core.page()["template"].toString())) {
    const int i = classPage(core, cls);
    if (i < 0) return {};
    core.activate(i);
  }
  const QJsonObject p = core.page();
  const double w = p["width"].toDouble(), h = p["height"].toDouble();
  const draw::LessonLayout L = draw::lessonLayout(w, h);
  const auto& act = L.col("activities");
  const std::vector<InkRect> ink = inkRects(core.elements());
  std::vector<int> sessionRows;
  for (const ed::ElPtr& e : core.elements())
    if (e->kind == ed::Kind::Text && e->role == QStringLiteral("daftar-session")) sessionRows.push_back(int(std::floor((e->y - 2 - L.bodyTop) / L.step + 0.5)));
  std::optional<int> row;
  if (!sessionRows.empty()) {
    const int last = *std::max_element(sessionRows.begin(), sessionRows.end());
    const double top = L.bodyTop + last * L.step + 1;
    const bool used = std::any_of(ink.begin(), ink.end(), [&](const InkRect& r) { return r.x1 > act.x0 + 1 && r.x0 < act.x1 - 1 && r.y1 > top; });
    if (!used && last + lines.size() <= L.rows) row = last;
  }
  if (!row) row = nextFreeRow(ink, w, h, int(lines.size()));
  if (!row) return {{"error", "No room left for this outline on the page. Start a new lesson for a new page."}};
  addToActive(core, outlineElements(lines, *row, w, h));
  return {{"goTo", core.active}, {"y", L.bodyTop + *row * L.step}};
}

QVariantMap pasteStudents(const QString& notebookId, const QString& cls, const QString& text) {
  const QJsonObject nb = store::notebook(notebookId);
  const auto d = parseDaftar(nb["meta"]);
  if (!d) return {};
  const std::vector<Student> students = parseStudents(text);
  fillStudents(nb, *d, cls, students);
  QVariant first = QVariant::fromValue(nullptr);
  for (const QJsonValue& v : store::pages(nb["id"].toString()))
    if (v.toObject()["section"].toString() == cls && v.toObject()["template"].toString() == "daftar-students") {
      first = v.toObject()["id"].toString();
      break;
    }
  return {{"reloadAll", true}, {"pageId", first}, {"toast", QString("%1 students written for %2").arg(students.size()).arg(cls)}};
}

QVariant exportPages(ed::EditorCore& core, const Meta& meta, const QString& cls, const QString& term) {
  std::vector<PageInfo> pages;
  for (int i = 0; i < int(core.pages.size()); ++i) {
    PageInfo p{templateOf(core, i), sectionOf(core, i), {}};
    for (const ed::ElPtr& e : core.load(i))
      if (e->kind == ed::Kind::Text && e->role == QStringLiteral("daftar-session") && e->session && !e->session->isEmpty()) p.sessions << *e->session;
    pages.push_back(p);
  }
  const std::vector<int> picked = classExportPages(pages, cls, termRange(meta, term));
  if (std::none_of(picked.begin(), picked.end(), [&](int i) { return draw::isLessonTemplate(pages[size_t(i)].tmpl); })) return QVariant::fromValue(nullptr);
  QString period;
  if (term == "year") period = meta.year;
  else if (term == "s1") period = "S1";
  else if (term == "s2") period = "S2";
  else if (term == "month") period = isoDate(QDate::currentDate()).left(7);
  QString name = cls;
  name.replace('\\', '-').replace('/', '-');
  QVariantList idx;
  for (int i : picked) idx << i;
  return QVariantMap{{"pages", idx}, {"name", name + (period.isEmpty() ? QString() : " - " + period)}};
}

QVariantMap addClassPage(ed::EditorCore& core, const QString& notebookId) {
  const QJsonObject nb = store::notebook(notebookId);
  const auto d = parseDaftar(nb["meta"]);
  const QString s = core.page()["section"].toString();
  if (!d || s.isEmpty() || !d->classes.contains(s)) return {{"plain", true}};
  const QJsonObject page = addLessonPage(nb, *d, s);
  return {{"reload", true}, {"pageId", page["id"].toString()}};
}

std::optional<Meta> metaArg(const QVariant& v) {
  if (!v.isValid() || v.isNull()) return std::nullopt;
  return Meta::fromJson(QJsonObject::fromVariantMap(v.toMap()));
}

}  // namespace

QVariant runInEditor(ed::EditorCore& core, const QString& fn, const QVariantList& a) {
  try {
    if (fn == "newLesson") return newLesson(core, a.value(0).toString(), a.value(1).toString());
    if (fn == "goToClass") {
      const int i = classPage(core, a.value(0).toString());
      return QVariantMap{{"goTo", i >= 0 ? QVariant(i) : QVariant::fromValue(nullptr)}};
    }
    if (fn == "insertOutline") {
      const auto m = metaArg(a.value(0));
      return m ? insertOutline(core, *m, a.value(1).toStringList()) : QVariantMap();
    }
    if (fn == "pasteStudents") return pasteStudents(a.value(0).toString(), a.value(1).toString(), a.value(2).toString());
    if (fn == "exportPages") {
      const auto m = metaArg(a.value(0));
      return m ? exportPages(core, *m, a.value(1).toString(), a.value(2).toString()) : QVariant::fromValue(nullptr);
    }
    if (fn == "addClassPage") return addClassPage(core, a.value(0).toString());
    qWarning().noquote() << "daftar: no action" << fn;
    return QVariantMap();
  } catch (const store::StoreError& e) {
    return QVariantMap{{"error", e.message()}};
  }
}

}  // namespace daftar
