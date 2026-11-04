// The pages of a daftar annossous (was src/editor/daftarPapers.ts), modelled
// on the printed books Moroccan teachers use: title page, the teacher's
// card, timetable, holidays, diagnostic report, student lists, memos,
// absences and the lesson pages. Arabic first, right to left. Pages are
// designed on A4 portrait and scaled to fit other sizes.
#pragma once

#include <QHash>
#include <QString>
#include <vector>

class JsCanvas;

namespace draw {

bool isLessonTemplate(const QString& t);
bool isDaftarTemplate(const QString& t);
/// The lesson page colour of the class at `classIndex`.
QString lessonTemplateFor(int classIndex);

/// Where a filled-in value goes: a text box (x, top y, width).
struct Slot {
  double x = 0, y = 0, w = 0;
};

/// Draws a daftar page (paper first), scaled from A4 to the page.
void drawDaftarPage(JsCanvas& c, const QString& tmpl, double w, double h, const QString& paper);
/// Where a daftar page's filled-in values go (keys like "school", "class", "year").
QHash<QString, Slot> daftarSlots(const QString& tmpl, double w, double h);

struct Column {
  double x0, x1;
};
/// A table's cells in page coordinates: columns right to left, rows from `top`.
struct TableLayout {
  std::vector<Column> cols;
  double top, rowH;
  int rows;
};
TableLayout holidaysLayout(double w, double h);
/// The student list's two tables (right one first).
std::vector<TableLayout> studentsLayout(double w, double h);

}  // namespace draw
