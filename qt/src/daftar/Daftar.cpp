// Daftar annossous (was src/daftar.ts); see Daftar.h.
#include "Daftar.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QtDebug>
#include <algorithm>
#include <cmath>

#include "../Core.h"
#include "../draw/Covers.h"
#include "../draw/DaftarPapers.h"
#include "Store.h"

namespace daftar {

namespace {

double jsRound(double v) { return std::floor(v + 0.5); }
QString pad(int n) { return QString::number(n).rightJustified(2, '0'); }
QString frDate(const QDate& d) { return pad(d.day()) + "/" + pad(d.month()) + "/" + QString::number(d.year()); }
QString frIso(const QString& s) { return s.isEmpty() ? QString() : s.mid(8, 2) + "/" + s.mid(5, 2) + "/" + s.left(4); }

const QString VALUE = "#1e3a8a";

ed::El text(double x, double y, double w, const QString& t, double size, const QString& color) {
  ed::El e;
  e.id = ed::newId();
  e.kind = ed::Kind::Text;
  e.x = x;
  e.y = y;
  e.w = w;
  e.text = t;
  e.fontSize = size;
  e.color = color;
  return e;
}

ed::El valueBox(const draw::Slot& slot, const QString& value, double size, double z) {
  ed::El e = text(slot.x, slot.y, slot.w, value, size, VALUE);
  e.z = z;
  e.bold = true;
  e.align = "center";
  e.font = "sans";
  e.role = "daftar-field";
  return e;
}

/// The values a page shows, by slot name.
QHash<QString, QString> values(const Meta& meta, const QString& cls) {
  return {{"region", meta.region},       {"directorate", meta.directorate}, {"school", meta.school}, {"teacher", meta.teacher}, {"payroll", meta.payroll},
          {"year", meta.year},           {"subject", meta.subject},         {"classes", meta.classes.join(" - ")},
          {"class", cls},                {"group0", cls},                   {"group1", cls}};
}

/// Pastel note colours for the timetable, one per class.
const char* const CLASS_COLORS[] = {"#f87171", "#facc15", "#4ade80", "#60a5fa", "#c084fc", "#fb923c", "#2dd4bf", "#f472b6"};

int mins(const QString& s) {
  const QStringList p = s.split(':');
  return p.value(0).toInt() * 60 + p.value(1).toInt();
}

/// JavaScript's String.prototype.trim (Unicode spaces and line ends).
QString jsTrim(const QString& s) {
  static const QRegularExpression re(QStringLiteral("^[\\s\\x{feff}]+|[\\s\\x{feff}]+$"), QRegularExpression::UseUnicodePropertiesOption);
  QString t = s;
  return t.remove(re);
}

QDate dateOf(const QString& iso) { return QDate::fromString(iso.left(10), "yyyy-MM-dd"); }

}  // namespace

namespace {
QDateTime g_fixedNow;
}

QDateTime now() { return g_fixedNow.isValid() ? g_fixedNow : QDateTime::currentDateTime(); }
void setNowForTests(const QDateTime& t) { g_fixedNow = t; }

// --- details -----------------------------------------------------------------------------

QJsonObject Meta::toJson() const {
  QJsonArray tt, hs;
  for (const Lesson& l : timetable) tt.append(QJsonObject{{"day", l.day}, {"start", l.start}, {"end", l.end}, {"cls", l.cls}});
  for (const Holiday& h : holidays) hs.append(h.toJson());
  QJsonObject o = extra;
  const QJsonObject own{{"kind", "daftar"},       {"year", year},       {"region", region},   {"directorate", directorate}, {"school", school},
                {"teacher", teacher},     {"payroll", payroll}, {"subject", subject}, {"classes", QJsonArray::fromStringList(classes)},
                {"timetable", tt},        {"holidays", hs}};
  for (auto it = own.begin(); it != own.end(); ++it) o[it.key()] = it.value();
  if (!classColors.isEmpty()) o["classColors"] = classColors;
  return o;
}

QString Meta::colorOf(const QString& cls) const {
  const QString c = classColors[cls].toString();
  if (!c.isEmpty()) return c;
  return CLASS_COLORS[std::max(0, int(classes.indexOf(cls))) % 8];
}

Meta Meta::fromJson(const QJsonObject& o) {
  Meta m;
  m.year = o["year"].toString();
  m.region = o["region"].toString();
  m.directorate = o["directorate"].toString();
  m.school = o["school"].toString();
  m.teacher = o["teacher"].toString();
  m.payroll = o["payroll"].toString();
  m.subject = o["subject"].toString();
  for (const QJsonValue& c : o["classes"].toArray()) m.classes << c.toString();
  for (const QJsonValue& v : o["timetable"].toArray()) {
    const QJsonObject l = v.toObject();
    m.timetable.push_back({l["day"].toInt(), l["start"].toString(), l["end"].toString(), l["cls"].toString()});
  }
  for (const QJsonValue& v : o["holidays"].toArray()) m.holidays.push_back(Holiday::fromJson(v.toObject()));
  m.classColors = o["classColors"].toObject();
  static const QStringList known{"kind", "year", "region", "directorate", "school", "teacher", "payroll", "subject", "classes", "timetable", "holidays", "classColors"};
  for (auto it = o.begin(); it != o.end(); ++it)
    if (!known.contains(it.key())) m.extra[it.key()] = it.value();
  return m;
}

const std::vector<DayName>& days() {
  static const std::vector<DayName> d = {{"الأحد", "Dimanche"}, {"الإثنين", "Lundi"}, {"الثلاثاء", "Mardi"}, {"الأربعاء", "Mercredi"},
                                         {"الخميس", "Jeudi"},   {"الجمعة", "Vendredi"}, {"السبت", "Samedi"}};
  return d;
}

std::optional<Meta> parseDaftar(const QJsonValue& notebookMeta) {
  if (!notebookMeta.isString() || notebookMeta.toString().isEmpty()) return std::nullopt;
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(notebookMeta.toString().toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
  const QJsonObject o = doc.object();
  if (o["kind"].toString() != "daftar" || !o["classes"].isArray()) return std::nullopt;
  return Meta::fromJson(o);
}

QString schoolYear(const QDate& d) {
  const int y = d.month() >= 8 ? d.year() : d.year() - 1;
  return QString("%1-%2").arg(y).arg(y + 1);
}

QString isoDate(const QDate& d) { return QString::number(d.year()) + "-" + pad(d.month()) + "-" + pad(d.day()); }

// --- filling pages in ---------------------------------------------------------------------

std::vector<ed::El> pageElements(const QString& tmpl, const Meta& meta, const QString& cls, double w, double h) {
  const QHash<QString, QString> v = values(meta, cls);
  std::vector<ed::El> out;
  for (const auto& [key, slot] : draw::daftarSlotList(tmpl, w, h)) {
    const QString value = v.value(key);
    if (value.isEmpty()) continue;
    // Long values shrink to fit their line.
    const double size = std::max(8.5, std::min(13.0, (slot.w / std::max(1, int(value.size()))) * 1.9));
    out.push_back(valueBox(slot, value, jsRound(size * 2) / 2, double(out.size())));
  }
  if (tmpl == "daftar-timetable")
    for (ed::El& e : timetableElements(meta, w, h)) out.push_back(std::move(e));
  if (tmpl == "daftar-holidays")
    for (ed::El& e : holidayElements(meta, w, h, int(out.size()))) out.push_back(std::move(e));
  return out;
}

std::vector<ed::El> timetableElements(const Meta& meta, double w, double h) {
  const draw::TimetableLayout L = draw::timetableLayout(w, h);
  std::vector<ed::El> out;
  for (size_t i = 0; i < meta.timetable.size(); ++i) {
    const Lesson& l = meta.timetable[i];
    auto col = std::find_if(L.days.begin(), L.days.end(), [&](const auto& d) { return d.day == l.day; });
    if (col == L.days.end()) continue;
    const double y0 = L.yOf(l.start);
    const double y1 = std::max(y0 + 30, L.yOf(l.end));
    ed::El e;
    e.id = ed::newId();
    e.z = 50 + double(i);
    e.kind = ed::Kind::Sticky;
    e.role = "daftar-field";
    e.x = col->x0 + 4;
    e.y = y0 + 3;
    e.w = col->x1 - col->x0 - 8;
    e.h = y1 - y0 - 6;
    e.text = l.cls + "\n" + l.start + QStringLiteral("–") + l.end;
    e.color = meta.colorOf(l.cls);
    e.fontSize = 12;
    e.bold = true;
    e.align = "center";
    out.push_back(std::move(e));
  }
  return out;
}

std::vector<ed::El> coverElements(const Meta& meta, double w, double h) {
  const draw::Box p = draw::zelligePanel(w, h);
  struct Row {
    QString ar, value;
  };
  std::vector<Row> rows;
  for (const Row& r : {Row{"المؤسسة", meta.school}, Row{"المادة", meta.subject}, Row{"الأستاذ(ة)", meta.teacher}, Row{"الأقسام", meta.classes.join("  ·  ")}})
    if (!r.value.isEmpty()) rows.push_back(r);
  const double x = p.x + 44;
  const double rw = p.w - 88;
  const double top = p.y + std::max(250.0, p.h * 0.36);
  const double step = std::min(78.0, (p.y + p.h - 100 - top) / std::max(1, int(rows.size())));
  std::vector<ed::El> out;
  double z = 10;
  for (size_t i = 0; i < rows.size(); ++i) {
    const double y = top + double(i) * step;
    ed::El label = text(x, y, rw, rows[i].ar, 13, "#6b7280");
    label.z = z++;
    label.role = "daftar-field";
    label.font = "sans";
    out.push_back(label);
    ed::El value = text(x, y + 22, rw, rows[i].value, 19, "#1f2937");
    value.z = z++;
    value.role = "daftar-field";
    value.font = "sans";
    value.bold = true;
    value.align = "center";
    out.push_back(value);
  }
  return out;
}

// --- lessons ------------------------------------------------------------------------------

std::optional<Lesson> todaysLesson(const Meta& meta, const QString& cls, const QDateTime& now, const QSet<QString>& logged) {
  const int day = now.date().dayOfWeek() % 7;  // Sunday = 0
  std::vector<Lesson> all, fresh;
  for (const Lesson& l : meta.timetable)
    if (l.cls == cls && l.day == day) all.push_back(l);
  for (const Lesson& l : all)
    if (!logged.contains(l.start)) fresh.push_back(l);
  std::vector<Lesson> today = fresh.empty() ? all : fresh;
  if (today.empty()) return std::nullopt;
  const int t = now.time().hour() * 60 + now.time().minute();
  // The lesson under way, else the nearest one.
  auto dist = [&](const Lesson& l) { return t >= mins(l.start) && t <= mins(l.end) ? -1 : std::min(std::abs(t - mins(l.start)), std::abs(t - mins(l.end))); };
  std::stable_sort(today.begin(), today.end(), [&](const Lesson& a, const Lesson& b) { return dist(a) < dist(b); });
  return today.front();
}

std::optional<int> nextFreeRow(const std::vector<InkRect>& ink, double w, double h, int need) {
  const draw::LessonLayout L = draw::lessonLayout(w, h);
  double bottom = -INFINITY;
  for (const InkRect& r : ink) {
    if (r.x1 < L.x0 || r.x0 > L.x1 || r.y1 < L.bodyTop + 1 || r.y0 > L.bodyBottom) continue;
    bottom = std::max(bottom, r.y1);
  }
  const int row = bottom == -INFINITY ? 0 : int(std::ceil((bottom - L.bodyTop) / L.step - 0.15)) + 1;
  if (row + need <= L.rows) return row;
  return std::nullopt;
}

std::vector<ed::El> lessonEntry(const Meta& meta, const QString& cls, int row, double w, double h, const QDateTime& now, const QSet<QString>& logged) {
  const draw::LessonLayout L = draw::lessonLayout(w, h);
  const auto lesson = todaysLesson(meta, cls, now, logged);
  const double y = L.bodyTop + row * L.step + 2;
  const QString session = jsTrim(isoDate(now.date()) + " " + (lesson ? lesson->start : QString()));
  auto cell = [&](const QString& k, const QString& s) {
    const auto& c = L.col(k);
    ed::El e = text(c.x0 + 2, y, c.x1 - c.x0 - 4, s, 11.5, VALUE);
    e.z = 0;
    e.bold = true;
    e.align = "center";
    e.font = "sans";
    e.role = "daftar-session";
    e.session = session;
    return e;
  };
  std::vector<ed::El> out = {cell("date", frDate(now.date())), cell("class", cls)};
  if (lesson) out.push_back(cell("time", lesson->start + "-" + lesson->end));
  return out;
}

// --- holidays -----------------------------------------------------------------------------

double holidayDays(const Holiday& hd) {
  // As the script worked it out: Date.UTC(+yyyy, +mm - 1, +dd) for each end.
  auto num = [](const QString& t) {
    const QString x = t.trimmed();
    if (x.isEmpty()) return 0.0;
    bool ok = false;
    const double v = x.toDouble(&ok);
    return ok ? v : NAN;
  };
  auto utcDays = [&](const QString& s) {
    double y = num(s.mid(0, 4)), m = num(s.mid(5, 2)) - 1, d = num(s.mid(8, 2));
    if (std::isnan(y) || std::isnan(m) || std::isnan(d)) return double(NAN);
    y = std::trunc(y);
    if (y >= 0 && y <= 99) y += 1900;
    y += std::floor(m / 12);
    m = m - 12 * std::floor(m / 12);
    return double(QDate(int(y), int(m) + 1, 1).toJulianDay()) + std::trunc(d) - 1;
  };
  const double days = std::floor((utcDays(hd.to) - utcDays(hd.from)) + 0.5) + 1;
  if (std::isnan(days)) return NAN;
  return std::max(1.0, days);
}

std::optional<Holiday> holidayOn(const Meta& meta, const QDate& d) {
  const QString day = isoDate(d);
  for (const Holiday& x : meta.holidays)
    if (x.from <= day && day <= (x.to.isEmpty() ? x.from : x.to)) return x;
  return std::nullopt;
}

std::vector<Holiday> nationalHolidays(const QString& year) {
  static const QRegularExpression re("^([0-9]{4})");
  const auto m = re.match(year);
  const int y = m.hasMatch() ? m.captured(1).toInt() : today().year();
  auto one = [](const QString& name, int yy, int mm, int dd) {
    const QString d = QString::number(yy) + "-" + pad(mm) + "-" + pad(dd);
    return Holiday{name, d, d};
  };
  return {one("ذكرى المسيرة الخضراء", y, 11, 6),    one("عيد الاستقلال", y, 11, 18),         one("رأس السنة الميلادية", y + 1, 1, 1),
          one("ذكرى تقديم وثيقة الاستقلال", y + 1, 1, 11), one("رأس السنة الأمازيغية", y + 1, 1, 14), one("عيد الشغل", y + 1, 5, 1)};
}

std::vector<ed::El> holidayElements(const Meta& meta, double w, double h, int z0) {
  const draw::TableLayout T = draw::holidaysLayout(w, h);
  std::vector<Holiday> list = meta.holidays;
  std::stable_sort(list.begin(), list.end(), [](const Holiday& a, const Holiday& b) { return a.from < b.from; });
  if (int(list.size()) > T.rows) list.resize(size_t(T.rows));
  std::vector<ed::El> out;
  for (size_t r = 0; r < list.size(); ++r) {
    const Holiday& hd = list[r];
    const double days = holidayDays(hd);
    const QString cells[] = {QString(), hd.name, frIso(hd.from), frIso(hd.to.isEmpty() ? hd.from : hd.to), std::isnan(days) ? QStringLiteral("NaN") : QString::number(days)};
    for (int c = 0; c < 5; ++c) {
      const QString& v = cells[c];
      if (v.isEmpty()) continue;
      const draw::Column& col = T.cols[size_t(c)];
      const double size = c == 1 ? std::max(8.0, std::min(11.5, ((col.x1 - col.x0) / double(v.size())) * 1.85)) : 11;
      ed::El e = text(col.x0 + 3, T.top + double(r) * T.rowH + (T.rowH - size * 1.3) / 2, col.x1 - col.x0 - 6, v, jsRound(size * 2) / 2, VALUE);
      e.z = z0 + double(out.size());
      e.bold = c == 1;
      e.align = "center";
      e.font = "sans";
      e.role = "daftar-field";
      out.push_back(std::move(e));
    }
  }
  return out;
}

// --- students -----------------------------------------------------------------------------

std::vector<Student> parseStudents(const QString& text) {
  using RE = QRegularExpression;
  const auto U = RE::UseUnicodePropertiesOption;
  static const RE MASSAR("^[A-Za-z][0-9]{6,12}$");
  static const RE HEADER("^(n°|no|nom|prénom|prenom|name|code|massar|élève|eleve|sexe|genre|الاسم|النسب|الإسم|رمز|رقم|الرقم|النوع|الجنس|تاريخ|مكان|القسم|اسم)", RE::CaseInsensitiveOption | U);
  static const RE NOT_NAME("^(ذكر|أنثى|انثى|m|f|h|g|masculin|féminin|feminin|garçon|fille)$", RE::CaseInsensitiveOption | U);
  static const RE DATE("[0-9]{1,4}[/.-][0-9]{1,2}[/.-][0-9]{1,4}");
  static const RE LETTERS2("\\p{L}{2}", U);
  static const RE CODE_INSIDE("(^|\\s)([A-Za-z][0-9]{6,12})(\\s|$)", U);
  static const RE NUMBERED("^\\s*[0-9]+[.)-]?\\s+", U);
  static const RE NUMBERED0("^[0-9]+[.)-]?\\s+", U);
  static const RE CELLS("\\t|;");
  static const RE LINES("\\r?\\n");
  auto cellsOf = [](const QStringList& parts) {
    QStringList out;
    for (const QString& c : parts)
      if (!jsTrim(c).isEmpty()) out << jsTrim(c);
    return out;
  };
  std::vector<Student> out;
  for (const QString& raw : text.split(LINES)) {
    const QString line = jsTrim(raw);
    if (line.isEmpty()) continue;
    QStringList cells = cellsOf(line.split(CELLS));
    if (cells.size() == 1 && line.contains(',')) cells = cellsOf(line.split(','));
    if (cells.size() == 1) {
      // "D130045678 Alaoui Mohamed": the code inside the line.
      const auto m = CODE_INSIDE.match(line);
      if (m.hasMatch()) {
        QString rest = line;
        rest.replace(rest.indexOf(m.captured(2)), m.captured(2).size(), QString());
        cells = {m.captured(2), jsTrim(rest.remove(NUMBERED))};
      } else {
        QString l = line;
        cells = {l.remove(NUMBERED0)};
      }
    }
    QString code;
    for (const QString& c : cells)
      if (MASSAR.match(c).hasMatch()) {
        code = c;
        break;
      }
    QStringList names;
    for (const QString& c : cells)
      if (c != code && LETTERS2.match(c).hasMatch() && !DATE.match(c).hasMatch() && !NOT_NAME.match(c).hasMatch()) names << c;
    if (names.isEmpty()) continue;
    if (code.isEmpty() && std::all_of(names.begin(), names.end(), [](const QString& c) { return HEADER.match(c).hasMatch(); })) continue;
    out.push_back({code, names.mid(0, 2).join(" ")});
  }
  if (out.size() > 300) out.resize(300);
  return out;
}

std::vector<ed::El> studentElements(const std::vector<Student>& students, int offset, double w, double h) {
  const std::vector<draw::TableLayout> tables = draw::studentsLayout(w, h);
  std::vector<ed::El> out;
  const int per = tables[0].rows;
  for (int i = 0; offset + i < int(students.size()) && i < STUDENTS_PER_PAGE; ++i) {
    const Student& st = students[size_t(offset + i)];
    const draw::TableLayout& T = tables[size_t(i / per)];
    const int r = i % per;
    auto cell = [&](int c, const QString& v, double size) {
      const draw::Column& col = T.cols[size_t(c)];
      const double fs = jsRound(std::max(7.5, std::min(size, ((col.x1 - col.x0 - 6) / std::max(1, int(v.size()))) * 1.9)) * 2) / 2;
      ed::El e = text(col.x0 + 3, T.top + r * T.rowH + (T.rowH - fs * 1.3) / 2 + 2, col.x1 - col.x0 - 6, v, fs, VALUE);
      e.z = 100 + double(out.size());
      e.align = "center";
      e.font = "sans";
      e.role = "daftar-student";
      out.push_back(std::move(e));
    };
    if (!st.code.isEmpty()) cell(1, st.code, 10.5);
    cell(2, st.name, 11.5);
  }
  return out;
}

// --- lesson outlines ----------------------------------------------------------------------

const std::vector<Outline>& outlines() {
  static const std::vector<Outline> o = {
      {"درس", {"التمهيد", "النشاط 1", "النشاط 2", "النشاط 3", "الخلاصة", "التقويم", "الواجب المنزلي"}},
      {"تمارين", {"تصحيح الواجب", "تمرين 1", "تمرين 2", "تمرين 3"}},
      {"فرض محروس", {"فرض محروس رقم …", "المدة: …", "المحاور: …"}},
      {"تصحيح الفرض", {"تصحيح الفرض المحروس رقم …", "الأخطاء الشائعة", "الدعم"}},
      {"دعم ومعالجة", {"التشخيص", "أنشطة الدعم", "التقويم"}},
  };
  return o;
}

std::vector<ed::El> outlineElements(const QStringList& lines, int row, double w, double h) {
  const draw::LessonLayout L = draw::lessonLayout(w, h);
  const auto& col = L.col("activities");
  std::vector<ed::El> out;
  for (int i = 0; i < lines.size() && i < std::max(0, L.rows - row); ++i) {
    ed::El e = text(col.x0 + 6, L.bodyTop + (row + i) * L.step + 2, col.x1 - col.x0 - 12, lines[i], 11.5, "#1f1d24");
    e.z = 0;
    e.font = "sans";
    out.push_back(std::move(e));
  }
  return out;
}

// --- exporting a class -------------------------------------------------------------------

std::optional<Range> termRange(const Meta& meta, const QString& term, const QDate& now) {
  if (term == "year") return std::nullopt;
  if (term == "month") return Range{isoDate(now).left(7) + "-01", isoDate(now).left(7) + "-31"};
  static const QRegularExpression re("^([0-9]{4})");
  const auto m = re.match(meta.year);
  const int y = m.hasMatch() ? m.captured(1).toInt() : schoolYear(now).left(4).toInt();
  // Semester 1: September to January; semester 2: February to July.
  if (term == "s1") return Range{QString::number(y) + "-08-15", QString::number(y + 1) + "-01-31"};
  return Range{QString::number(y + 1) + "-02-01", QString::number(y + 1) + "-07-31"};
}

std::vector<int> classExportPages(const std::vector<PageInfo>& pages, const QString& cls, const std::optional<Range>& range) {
  static const QStringList FRONT = {"daftar-title", "daftar-profile", "daftar-profile-2", "daftar-timetable"};
  std::vector<int> out;
  for (int i = 0; i < int(pages.size()); ++i) {
    const PageInfo& p = pages[size_t(i)];
    bool keep = false;
    if (FRONT.contains(p.tmpl) || p.tmpl.startsWith("cover")) keep = true;
    else if (p.section != cls) keep = false;
    else if (p.tmpl == "daftar-students") keep = true;
    else if (!draw::isLessonTemplate(p.tmpl)) keep = false;
    else if (!range) keep = true;
    else keep = std::any_of(p.sessions.begin(), p.sessions.end(), [&](const QString& d) { return d.left(10) >= range->from && d.left(10) <= range->to; });
    if (keep) out.push_back(i);
  }
  return out;
}

// --- creating -----------------------------------------------------------------------------

namespace {

/// Lesson pages each class starts with.
constexpr int LESSON_PAGES = 3;

QJsonObject addFilledPage(const QJsonObject& nb, int at, const QString& tmpl, const Meta& meta, const QString& cls) {
  QJsonObject page = store::addPage(nb["id"].toString(), at, tmpl);
  // New pages copy their neighbour's paper and section: set both.
  store::setPageTemplate(page["id"].toString(), tmpl, "#ffffff");
  store::setPageSection(page["id"].toString(), cls);
  store::saveElements(page["id"].toString(), pageElements(tmpl, meta, cls, page["width"].toDouble(), page["height"].toDouble()));
  return page;
}

}  // namespace

QJsonObject addLessonPage(const QJsonObject& nb, const Meta& meta, const QString& cls) {
  const QJsonArray pages = store::pages(nb["id"].toString());
  int at = pages.size();
  for (const QJsonValue& p : pages)
    if (p.toObject()["section"].toString() == cls) at = p.toObject()["position"].toInt() + 1;
  return addFilledPage(nb, at, draw::lessonTemplateFor(std::max(0, int(meta.classes.indexOf(cls)))), meta, cls);
}

QJsonObject createDaftar(const QString& title, const QString& folderId, const QString& coverColor, const Meta& meta, const QString& design) {
  const QJsonObject nb = store::createNotebook({{"title", title}, {"folderId", folderId.isEmpty() ? QJsonValue() : QJsonValue(folderId)}, {"coverColor", coverColor}, {"template", "daftar-title"}, {"paperColor", "#ffffff"}, {"coverPage", true}});
  const QString id = nb["id"].toString();
  const QJsonArray pages = store::pages(id);
  const QJsonObject cover = pages.at(0).toObject(), titlePage = pages.at(1).toObject();
  // The designs with a panel for the daftar's details (zellige, arabesque, berber).
  const QString tmpl = design == "arabesque" || design == "berber" ? "cover-" + design : QStringLiteral("cover-zellige");
  store::setPageTemplate(cover["id"].toString(), tmpl, coverColor);
  store::saveElements(titlePage["id"].toString(), pageElements("daftar-title", meta, QString(), titlePage["width"].toDouble(), titlePage["height"].toDouble()));
  int at = 2;
  auto add = [&](const QString& t, const QString& cls) { addFilledPage(nb, at++, t, meta, cls); };
  for (const char* t : {"daftar-profile", "daftar-profile-2", "daftar-timetable", "daftar-holidays", "daftar-diagnostic"}) add(t, QString());
  for (int i = 0; i < meta.classes.size(); ++i) {
    add("daftar-students", meta.classes[i]);
    for (int k = 0; k < LESSON_PAGES; ++k) add(draw::lessonTemplateFor(i), meta.classes[i]);
  }
  add("daftar-memos", QString());
  add("daftar-absence", QString());
  store::setNotebookMeta(id, QString::fromUtf8(QJsonDocument(meta.toJson()).toJson(QJsonDocument::Compact)));
  return store::notebook(id);
}

void finishDaftarCover(const QJsonObject& nb, const Meta& meta) {
  const QJsonArray pages = store::pages(nb["id"].toString());
  if (pages.isEmpty()) return;
  const QJsonObject cover = pages.at(0).toObject();
  std::vector<ed::El> els = store::elements(cover["id"].toString());
  for (ed::El& e : els)
    if (e.kind == ed::Kind::Text && e.role == QStringLiteral("cover-date") && !meta.year.isEmpty()) e.text = meta.year;
  for (ed::El& e : coverElements(meta, cover["width"].toDouble(), cover["height"].toDouble())) els.push_back(std::move(e));
  store::saveElements(cover["id"].toString(), els);
}

void updateDaftar(const QJsonObject& nb, const Meta& before, const Meta& meta) {
  const QJsonArray pages = store::pages(nb["id"].toString());
  QSet<QString> oldNote;
  for (const Lesson& l : before.timetable) oldNote.insert(l.cls + "\n" + l.start + QStringLiteral("–") + l.end);
  for (int i = 0; i < pages.size(); ++i) {
    const QJsonObject p = pages[i].toObject();
    const QString tmpl = p["template"].toString();
    const bool isCover = i == 0 && tmpl.startsWith("cover");
    if (!isCover && !tmpl.startsWith("daftar")) continue;
    std::vector<ed::El> keep;
    for (ed::El& e : store::elements(p["id"].toString())) {
      if (e.isTextish() && e.role == QStringLiteral("daftar-field")) continue;
      // Timetable notes made before they were marked.
      if (tmpl == "daftar-timetable" && e.kind == ed::Kind::Sticky && oldNote.contains(e.text)) continue;
      if (isCover && e.kind == ed::Kind::Text && e.role == QStringLiteral("cover-date") && !meta.year.isEmpty()) e.text = meta.year;
      keep.push_back(std::move(e));
    }
    std::vector<ed::El> fresh = isCover ? coverElements(meta, p["width"].toDouble(), p["height"].toDouble()) : pageElements(tmpl, meta, p["section"].toString(), p["width"].toDouble(), p["height"].toDouble());
    double minZ = 0;
    for (const ed::El& e : keep) minZ = std::min(minZ, e.z);
    const double z0 = minZ - double(fresh.size()) - 1;
    std::vector<ed::El> all;
    for (size_t k = 0; k < fresh.size(); ++k) {
      ed::El e = fresh[k];
      if (!(tmpl == "daftar-timetable" && e.kind == ed::Kind::Sticky)) e.z = z0 + double(k);
      all.push_back(std::move(e));
    }
    for (ed::El& e : keep) all.push_back(std::move(e));
    store::saveElements(p["id"].toString(), all);
  }
  QStringList added;
  for (const QString& c : meta.classes)
    if (!before.classes.contains(c)) added << c;
  if (!added.isEmpty()) {
    int at = pages.size();
    for (const QJsonValue& v : pages)
      if (v.toObject()["template"].toString() == "daftar-memos") {
        at = v.toObject()["position"].toInt();
        break;
      }
    for (const QString& cls : added) {
      addFilledPage(nb, at++, "daftar-students", meta, cls);
      for (int k = 0; k < LESSON_PAGES; ++k) addFilledPage(nb, at++, draw::lessonTemplateFor(int(meta.classes.indexOf(cls))), meta, cls);
    }
  }
  store::setNotebookMeta(nb["id"].toString(), QString::fromUtf8(QJsonDocument(meta.toJson()).toJson(QJsonDocument::Compact)));
}

void fillStudents(const QJsonObject& nb, const Meta& meta, const QString& cls, const std::vector<Student>& students) {
  const QString id = nb["id"].toString();
  auto lists = [&] {
    std::vector<QJsonObject> out;
    for (const QJsonValue& v : store::pages(id))
      if (v.toObject()["section"].toString() == cls && v.toObject()["template"].toString() == "daftar-students") out.push_back(v.toObject());
    return out;
  };
  std::vector<QJsonObject> pages = lists();
  const int need = std::max(1, int(std::ceil(double(students.size()) / STUDENTS_PER_PAGE)));
  if (int(pages.size()) < need) {
    const QJsonArray all = store::pages(id);
    int firstInClass = -1;
    for (const QJsonValue& v : all)
      if (v.toObject()["section"].toString() == cls) {
        firstInClass = v.toObject()["position"].toInt();
        break;
      }
    int at = !pages.empty() ? pages.back()["position"].toInt() + 1 : firstInClass >= 0 ? firstInClass : all.size();
    for (int k = int(pages.size()); k < need; ++k) addFilledPage(nb, at++, "daftar-students", meta, cls);
    pages = lists();
  }
  for (size_t k = 0; k < pages.size(); ++k) {
    const QJsonObject& p = pages[k];
    std::vector<ed::El> els;
    for (ed::El& e : store::elements(p["id"].toString()))
      if (!(e.kind == ed::Kind::Text && e.role == QStringLiteral("daftar-student"))) els.push_back(std::move(e));
    for (ed::El& e : studentElements(students, int(k) * STUDENTS_PER_PAGE, p["width"].toDouble(), p["height"].toDouble())) els.push_back(std::move(e));
    store::saveElements(p["id"].toString(), els);
  }
}

}  // namespace daftar
