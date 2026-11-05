// Daftar annossous (دفتر النصوص, a teacher's lesson log), was src/daftar.ts:
// its details, the text boxes its pages are filled in with, lessons and
// the timetable, holidays, student lists, lesson outlines, exporting a class,
// and creating and updating a daftar notebook. The pages themselves are
// drawn by draw/DaftarPapers; this file only says what goes where.
#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

#include "../editor/Model.h"

namespace daftar {

/// One weekly lesson: day (1 = Monday … 6 = Saturday, 0 = Sunday), times "HH:MM", class.
struct Lesson {
  int day = 1;
  QString start, end, cls;
};

/// School holidays and vacations (dates YYYY-MM-DD, inclusive).
struct Holiday {
  QString name, from, to;
  QJsonObject toJson() const { return {{"name", name}, {"from", from}, {"to", to}}; }
  static Holiday fromJson(const QJsonObject& o) { return {o["name"].toString(), o["from"].toString(), o["to"].toString()}; }
};

struct Meta {
  QString year, region, directorate, school, teacher, payroll, subject;
  QStringList classes;
  std::vector<Lesson> timetable;
  std::vector<Holiday> holidays;
  /// Colours chosen for classes ({ class: "#rrggbb" }); the others take the palette's.
  QJsonObject classColors;
  /// The notebook's other settings (its contents, cards…), kept as they are.
  QJsonObject extra;
  QString colorOf(const QString& cls) const;
  QJsonObject toJson() const;
  static Meta fromJson(const QJsonObject& o);
};

/// Now: the clock, or the moment the reference checks fix (--golden).
QDateTime now();
inline QDate today() { return now().date(); }
void setNowForTests(const QDateTime& t);

/// The day names, Sunday first (index = day number).
struct DayName {
  QString ar, fr;
};
const std::vector<DayName>& days();

/// A notebook's daftar details (its `meta` JSON), or none when it isn't a daftar.
std::optional<Meta> parseDaftar(const QJsonValue& notebookMeta);
/// The school year a date falls in, e.g. "2026-2027" (years start in September).
QString schoolYear(const QDate& d = today());
QString isoDate(const QDate& d);

// --- filling pages in
/// Text boxes for a page's filled-in values (school, class, year…), with the timetable's notes and the holiday list on theirs.
std::vector<ed::El> pageElements(const QString& tmpl, const Meta& meta, const QString& cls, double w, double h);
/// A sticky note per lesson on the timetable page, coloured by class.
std::vector<ed::El> timetableElements(const Meta& meta, double w, double h);
/// The cover's details under its title: school, subject, teacher and classes.
std::vector<ed::El> coverElements(const Meta& meta, double w, double h);

// --- lessons
/// The class's lesson today closest to `now`, skipping lessons already logged ("HH:MM" starts).
std::optional<Lesson> todaysLesson(const Meta& meta, const QString& cls, const QDateTime& now, const QSet<QString>& logged = {});
struct InkRect {
  double x0, y0, x1, y1;
};
/// The first free grid row on a lesson page below what's written there (one row left empty), or none when fewer than `need` rows remain.
std::optional<int> nextFreeRow(const std::vector<InkRect>& ink, double w, double h, int need = 3);
/// A new lesson entry's text boxes on row `row`: date, class and (from the timetable) time.
std::vector<ed::El> lessonEntry(const Meta& meta, const QString& cls, int row, double w, double h, const QDateTime& now, const QSet<QString>& logged = {});

// --- holidays
/// Days from `from` to `to`, both included.
double holidayDays(const Holiday& hd);
std::optional<Holiday> holidayOn(const Meta& meta, const QDate& d);
/// Morocco's fixed-date public holidays in a school year ("2026-2027").
std::vector<Holiday> nationalHolidays(const QString& year);
std::vector<ed::El> holidayElements(const Meta& meta, double w, double h, int z0 = 0);

// --- students
struct Student {
  QString code;  ///< رقم مسار (e.g. D130045678), or empty
  QString name;
};
/// A class list pasted from Massar or a spreadsheet, one student per line.
std::vector<Student> parseStudents(const QString& text);
constexpr int STUDENTS_PER_PAGE = 60;
/// The names on one student list page: students[offset …] in its two tables.
std::vector<ed::El> studentElements(const std::vector<Student>& students, int offset, double w, double h);

// --- lesson outlines
struct Outline {
  QString name;
  QStringList lines;
};
const std::vector<Outline>& outlines();
/// An outline written down the activities column from row `row`, one line per row.
std::vector<ed::El> outlineElements(const QStringList& lines, int row, double w, double h);

// --- exporting a class
struct Range {
  QString from, to;
};
/// The dates a term ("year", "s1", "s2", "month") covers, or none for everything.
std::optional<Range> termRange(const Meta& meta, const QString& term, const QDate& now = today());
struct PageInfo {
  QString tmpl, section;
  QStringList sessions;  ///< "YYYY-MM-DD HH:MM" of its lessons
};
/// Which pages to export for a class: front pages, its student list, its lessons (in the range).
std::vector<int> classExportPages(const std::vector<PageInfo>& pages, const QString& cls, const std::optional<Range>& range);

// --- creating and updating (the library)
/// Creates a daftar laid out like the printed books; returns the notebook.
QJsonObject createDaftar(const QString& title, const QString& folderId, const QString& coverColor, const Meta& meta, const QString& design = QStringLiteral("zellige"));
/// Adds a lesson page at the end of a class's section; returns it.
QJsonObject addLessonPage(const QJsonObject& nb, const Meta& meta, const QString& cls);
/// Fills in the cover once its title and date are placed.
void finishDaftarCover(const QJsonObject& nb, const Meta& meta);
/// Applies changed details: filled-in values written again, new classes get their sections.
void updateDaftar(const QJsonObject& nb, const Meta& before, const Meta& meta);
/// Writes a class's student list onto its list pages (adding pages when needed).
void fillStudents(const QJsonObject& nb, const Meta& meta, const QString& cls, const std::vector<Student>& students);

}  // namespace daftar
