// Were src/daftar.test.ts.
#include "../daftar/Daftar.h"
#include "../draw/DaftarPapers.h"
#include "SelfTest.h"

using namespace daftar;

namespace {

constexpr double W = 794, H = 1123;

Meta meta() {
  Meta m;
  m.year = "2026-2027";
  m.region = "الرباط سلا القنيطرة";
  m.directorate = "تمارة الصخيرات";
  m.school = "ماء العينين";
  m.teacher = "لبنى أرسلان";
  m.payroll = "2320866";
  m.subject = "اللغة العربية";
  m.classes = {"3/6", "3/7"};
  // 3/6: Monday 08:00–10:00 and 14:00–16:00; 3/7: Tuesday 10:00–12:00.
  m.timetable = {{1, "14:00", "16:00", "3/6"}, {1, "08:00", "10:00", "3/6"}, {2, "10:00", "12:00", "3/7"}};
  return m;
}

QDateTime monday(int hh, int mm = 0) { return QDateTime(QDate(2026, 9, 14), QTime(hh, mm)); }  // a Monday

QStringList texts(const std::vector<ed::El>& els) {
  QStringList out;
  for (const ed::El& e : els) out << e.text;
  return out;
}

}  // namespace

TEST("daftar", "today's lesson: the one under way, else the nearest that day") {
  CHECK_EQ(todaysLesson(meta(), "3/6", monday(9))->start, QString("08:00"));
  CHECK_EQ(todaysLesson(meta(), "3/6", monday(15, 30))->start, QString("14:00"));
  CHECK_EQ(todaysLesson(meta(), "3/6", monday(12, 30))->start, QString("14:00"));
  CHECK_EQ(todaysLesson(meta(), "3/6", monday(7))->start, QString("08:00"));
}

TEST("daftar", "today's lesson skips one already logged") {
  CHECK_EQ(todaysLesson(meta(), "3/6", monday(9), {"08:00"})->start, QString("14:00"));
  // Both logged: the nearest again.
  CHECK_EQ(todaysLesson(meta(), "3/6", monday(9), {"08:00", "14:00"})->start, QString("08:00"));
}

TEST("daftar", "no lesson today: none") {
  CHECK(!todaysLesson(meta(), "3/7", monday(10)));
  Meta m = meta();
  m.timetable.clear();
  CHECK(!todaysLesson(m, "3/6", monday(10)));
}

TEST("daftar", "lesson entries go on the first free row, one row after earlier writing") {
  const draw::LessonLayout L = draw::lessonLayout(W, H);
  CHECK_EQ(*nextFreeRow({}, W, H), 0);
  const double y = L.bodyTop + 4.2 * L.step;  // ink reaching into row 4
  CHECK_EQ(*nextFreeRow({{L.x0 + 50, L.bodyTop + 5, L.x0 + 200, y}}, W, H), 6);
  // Outside the table (header fields, notes): ignored.
  CHECK_EQ(*nextFreeRow({{L.x0, 10, L.x1, 60}}, W, H), 0);
  CHECK_EQ(*nextFreeRow({{L.x0, L.bodyBottom + 30, L.x1, L.bodyBottom + 50}}, W, H), 0);
  // A full page.
  CHECK(!nextFreeRow({{L.x0 + 50, L.bodyTop, L.x0 + 200, L.bodyBottom - L.step}}, W, H));
}

TEST("daftar", "a lesson entry writes the date, class and time in their columns") {
  const draw::LessonLayout L = draw::lessonLayout(W, H);
  const auto entry = lessonEntry(meta(), "3/6", 3, W, H, monday(9, 5));
  CHECK_EQ(texts(entry), (QStringList{"14/09/2026", "3/6", "08:00-10:00"}));
  CHECK(entry[0].x >= L.col("date").x0);
  CHECK(entry[1].x >= L.col("class").x0);
  CHECK(entry[2].x >= L.col("time").x0);
  for (const ed::El& e : entry) CHECK(selftest::close(e.y, L.bodyTop + 3 * L.step + 2, 5));
  // No timetable lesson today: date and class only.
  CHECK_EQ(texts(lessonEntry(meta(), "3/7", 0, W, H, monday(9))), (QStringList{"14/09/2026", "3/7"}));
}

TEST("daftar", "each class gets a lesson colour") {
  CHECK_EQ(draw::lessonTemplateFor(0), QString("daftar"));
  CHECK_EQ(draw::lessonTemplateFor(1), QString("daftar-blue"));
  CHECK_EQ(draw::lessonTemplateFor(6), QString("daftar"));
}

TEST("daftar", "pages are filled in with the details they ask for") {
  const QStringList title = texts(pageElements("daftar-title", meta(), {}, W, H));
  for (const char* v : {"الرباط سلا القنيطرة", "تمارة الصخيرات", "ماء العينين", "لبنى أرسلان", "3/6 - 3/7", "2320866", "2026-2027"}) CHECK(title.contains(QString::fromUtf8(v)));
  CHECK_EQ(texts(pageElements("daftar", meta(), "3/6", W, H)), (QStringList{"اللغة العربية", "3/6"}));
  CHECK_EQ(texts(pageElements("daftar-students", meta(), "3/7", W, H)), (QStringList{"3/7", "3/7"}));
  const QStringList profile = texts(pageElements("daftar-profile", meta(), {}, W, H));
  for (const char* v : {"2320866", "3/6 - 3/7", "2026-2027", "ماء العينين"}) CHECK(profile.contains(QString::fromUtf8(v)));
  // The timetable page carries a note per lesson.
  int notes = 0;
  for (const ed::El& e : pageElements("daftar-timetable", meta(), {}, W, H)) notes += e.kind == ed::Kind::Sticky;
  CHECK_EQ(notes, 3);
}

TEST("daftar", "every slot is on the page") {
  for (const char* t : {"daftar-title", "daftar-profile", "daftar-profile-2", "daftar-timetable", "daftar-holidays", "daftar-diagnostic", "daftar-students", "daftar-memos", "daftar-absence", "daftar", "daftar-blue", "daftar-green", "daftar-pink", "daftar-navy", "daftar-orange"})
    for (const auto& [key, s] : draw::daftarSlotList(t, W, H)) {
      CHECK(s.x >= 0);
      CHECK(s.x + s.w <= W);
      CHECK(s.w > 20);
      CHECK(s.y > 0 && s.y < H);
    }
}

TEST("daftar", "only daftar details are read") {
  CHECK(!parseDaftar(QJsonValue()));
  CHECK(!parseDaftar(QJsonValue("{oops")));
  CHECK(!parseDaftar(QJsonValue("{\"kind\":\"other\"}")));
  CHECK_EQ(parseDaftar(QJsonValue("{\"kind\":\"daftar\",\"classes\":[\"A\"]}"))->classes, QStringList{"A"});
}

TEST("daftar", "school years start in September") {
  CHECK_EQ(schoolYear(QDate(2026, 9, 1)), QString("2026-2027"));
  CHECK_EQ(schoolYear(QDate(2027, 3, 1)), QString("2026-2027"));
}

TEST("daftar", "student lists from Massar: code to the national number, surname and first name to the name") {
  const QString text = QStringList{"رقم التلميذ\tالنسب\tالاسم\tالنوع\tتاريخ الازدياد", "D130045678\tالعلوي\tمحمد\tذكر\t12/03/2012", "1\tR131122334\tBennani\tSalma\tF", "", "  "}.join("\n");
  const auto s = parseStudents(text);
  CHECK_EQ(int(s.size()), 2);
  CHECK(s[0].code == "D130045678" && s[0].name == QString::fromUtf8("العلوي محمد"));
  CHECK(s[1].code == "R131122334" && s[1].name == "Bennani Salma");
}

TEST("daftar", "student lists from plain lines, with or without a code, and CSV") {
  const auto s = parseStudents("1. Alaoui Mohamed\nD130000001 Idrissi Sara\nBennani;Omar");
  CHECK_EQ(int(s.size()), 3);
  CHECK(s[0].code.isEmpty() && s[0].name == "Alaoui Mohamed");
  CHECK(s[1].code == "D130000001" && s[1].name == "Idrissi Sara");
  CHECK(s[2].code.isEmpty() && s[2].name == "Bennani Omar");
}

TEST("daftar", "30 names per table and 60 per page") {
  std::vector<Student> st;
  for (int i = 0; i < 70; ++i) st.push_back({QString("D%1").arg(1000000 + i), QString("N%1").arg(i)});
  const auto first = studentElements(st, 0, W, H);
  int names = 0;
  const ed::El *n0 = nullptr, *n30 = nullptr;
  for (const ed::El& e : first) {
    if (e.text.startsWith("N")) ++names;
    if (e.text == "N0") n0 = &e;
    if (e.text == "N30") n30 = &e;
  }
  CHECK_EQ(names, 60);
  CHECK(n0 && n30 && n30->x < n0->x);  // the second table is to the left
  CHECK(n0 && n30 && std::abs(n30->y - n0->y) < 0.01);
  names = 0;
  for (const ed::El& e : studentElements(st, 60, W, H)) names += e.text.startsWith("N");
  CHECK_EQ(names, 10);
}

TEST("daftar", "holidays: both ends count; the holiday a day falls in") {
  const Holiday hd{"عطلة", "2026-12-20", "2026-12-28"};
  CHECK_EQ(holidayDays(hd), 9.0);
  Meta m = meta();
  m.holidays = {hd};
  CHECK_EQ(holidayOn(m, QDate(2026, 12, 24))->name, QString::fromUtf8("عطلة"));
  CHECK(!holidayOn(m, QDate(2026, 12, 29)));
}

TEST("daftar", "the fixed national holidays of a school year") {
  QStringList from;
  for (const Holiday& h : nationalHolidays("2026-2027")) from << h.from;
  CHECK(from.contains("2026-11-18"));
  CHECK(from.contains("2027-01-11"));
  CHECK(from.contains("2027-05-01"));
}

TEST("daftar", "the holiday list on the holidays page") {
  Meta m = meta();
  m.holidays = {{"عطلة", "2026-12-20", "2026-12-28"}};
  CHECK_EQ(texts(holidayElements(m, W, H)), (QStringList{"عطلة", "20/12/2026", "28/12/2026", "9"}));
  CHECK(texts(pageElements("daftar-holidays", m, {}, W, H)).contains("9"));
}

TEST("daftar", "an outline goes one line per grid row in the activities column") {
  const draw::LessonLayout L = draw::lessonLayout(W, H);
  const auto els = outlineElements({"النشاط 1", "النشاط 2"}, 3, W, H);
  const auto& act = L.col("activities");
  CHECK_EQ(int(els.size()), 2);
  CHECK(selftest::close(els[1].y - els[0].y, L.step));
  CHECK(els[0].x > act.x0);
  CHECK(els[0].x + els[0].w < act.x1);
}

TEST("daftar", "a class export keeps the front pages, its list and its lessons in the period") {
  const std::vector<PageInfo> pages = {{"daftar-title", "", {}}, {"daftar-students", "3/6", {}}, {"daftar", "3/6", {"2026-10-05 08:00"}}, {"daftar", "3/6", {"2027-03-01 08:00"}}, {"daftar-blue", "3/7", {}}, {"daftar-memos", "", {}}};
  CHECK(classExportPages(pages, "3/6", termRange(meta(), "year")) == (std::vector<int>{0, 1, 2, 3}));
  CHECK(classExportPages(pages, "3/6", termRange(meta(), "s1")) == (std::vector<int>{0, 1, 2}));
  CHECK(classExportPages(pages, "3/6", termRange(meta(), "s2")) == (std::vector<int>{0, 1, 3}));
}
