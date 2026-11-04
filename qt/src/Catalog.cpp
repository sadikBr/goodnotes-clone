#include "Catalog.h"

#include "editor/Instrument.h"
#include "editor/TextFormat.h"

const std::vector<PaperTemplate>& paperTemplates() {
  static const std::vector<PaperTemplate> t = {
      {"blank", "Blank", "basic", false, false},
      {"ruled", "Ruled", "basic", false, false},
      {"narrow-ruled", "Narrow ruled", "basic", false, false},
      {"wide-ruled", "Wide ruled", "basic", false, false},
      {"grid", "Grid", "basic", false, false},
      {"dotted", "Dotted", "basic", false, false},
      {"cornell", "Cornell", "notes", true, false},
      {"legal", "Legal pad", "notes", false, false},
      {"two-column", "Two columns", "notes", true, false},
      {"checklist", "To-do list", "notes", false, false},
      {"graph", "Graph", "science", false, false},
      {"coordinate", "Coordinates", "science", true, false},
      {"isometric", "Isometric", "science", false, false},
      {"hexagon", "Hexagon", "science", false, false},
      {"handwriting", "Handwriting", "writing", false, false},
      {"calligraphy", "Calligraphy", "writing", false, false},
      {"storyboard", "Storyboard", "writing", true, false},
      {"planner-day", "Daily plan", "planner", true, false},
      {"planner-week", "Weekly plan", "planner", true, false},
      {"daftar", "Lesson log", "teacher", true, false},
      {"daftar-blue", "Lesson log (blue)", "teacher", true, true},
      {"daftar-green", "Lesson log (green)", "teacher", true, true},
      {"daftar-pink", "Lesson log (pink)", "teacher", true, true},
      {"daftar-navy", "Lesson log (navy)", "teacher", true, true},
      {"daftar-orange", "Lesson log (orange)", "teacher", true, true},
      {"daftar-timetable", "Timetable", "teacher", true, false},
      {"daftar-students", "Student list", "teacher", true, false},
      {"daftar-diagnostic", "Diagnostic report", "teacher", true, false},
      {"daftar-holidays", "School holidays", "teacher", true, false},
      {"daftar-memos", "Memos", "teacher", true, false},
      {"daftar-absence", "Absences", "teacher", true, false},
      {"daftar-title", "Daftar title page", "teacher", true, false},
      {"daftar-profile", "Teacher's card", "teacher", true, false},
      {"daftar-profile-2", "Teacher's card (2)", "teacher", true, false},
  };
  return t;
}

QVariantList Catalog::templates() const {
  QVariantList out;
  for (const PaperTemplate& p : paperTemplates()) {
    QVariantMap m{{"id", p.id}, {"label", p.label}, {"category", p.category}};
    if (p.pagePreview) m["preview"] = "page";
    if (p.hidden) m["hidden"] = true;
    out << m;
  }
  return out;
}

QString Catalog::currentList(const QString& text) const { return ed::currentList(text); }

double Catalog::snapAngle(double rad, bool coarse) const { return ed::snapAngle(rad, coarse); }
