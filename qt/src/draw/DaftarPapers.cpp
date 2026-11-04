#include "DaftarPapers.h"

#include <QStringList>
#include <cmath>
#include <functional>

#include "../Canvas.h"
#include "Fonts.h"

namespace draw {
namespace {

double jsRound(double v) { return std::floor(v + 0.5); }

// --- colours ------------------------------------------------------------------

const QStringList LESSON_TEMPLATES = {"daftar", "daftar-blue", "daftar-green", "daftar-pink", "daftar-navy", "daftar-orange"};
const QStringList LESSON_ACCENTS = {"#5b2a5e", "#1f6fd1", "#1f7a4a", "#d1245f", "#2d2f72", "#e0612a"};
const QString PURPLE = "#5b2a5e", GREEN = "#1f7a4a", ORANGE = "#e0612a", INK = "#1f1d24", MUTED = "#6b6475", GRID = "rgba(70, 70, 90, 0.22)";
const QStringList DAFTAR_PAGES = {"daftar-title", "daftar-profile", "daftar-profile-2", "daftar-timetable", "daftar-holidays", "daftar-diagnostic", "daftar-students", "daftar-memos", "daftar-absence"};

QString accentOf(const QString& t) {
  if (const int i = LESSON_TEMPLATES.indexOf(t); i >= 0) return LESSON_ACCENTS[i];
  if (t == "daftar-students") return GREEN;
  if (t == "daftar-memos" || t == "daftar-absence") return ORANGE;
  return PURPLE;
}

/// Mixes a #rrggbb colour with white.
QString tint(const QString& hex, double k) {
  const int n = hex.mid(1).toInt(nullptr, 16);
  auto ch = [&](int v) { return int(jsRound(v + (255 - v) * k)); };
  return QString("rgb(%1, %2, %3)").arg(ch(n >> 16)).arg(ch((n >> 8) & 255)).arg(ch(n & 255));
}

// --- text ----------------------------------------------------------------------

/// Width of a label.
double labelWidth(const QString& s, double size, bool bold = false) {
  static Canvas* measurer = new Canvas(300, 150);
  measurer->setFont(fontCss("sans", size, bold, false, isRtl(s)));
  return measurer->textWidth(s);
}

struct TextOpts {
  double size;
  bool bold = false;
  QString color;
  /// Where x is: the text's right edge (default, for Arabic), left edge or centre.
  char anchor = 'r';
};

double text(Canvas& c, const QString& s, double x, double y, const TextOpts& o) {
  c.save();
  const bool rtl = isRtl(s);
  c.setFont(fontCss("sans", o.size, o.bold, false, rtl));
  c.setFill(o.color.isEmpty() ? INK : o.color);
  c.setTextBaseline("alphabetic");
  c.setTextAlign("left");
  c.setDirection(rtl ? "rtl" : "ltr");
  const double w = c.textWidth(s);
  c.fillText(s, o.anchor == 'r' ? x - w : o.anchor == 'c' ? x - w / 2 : x, y);
  c.restore();
  return w;
}
TextOpts T(double size, bool bold = false, const QString& color = {}, char anchor = 'r') { return {size, bold, color, anchor}; }

// --- shapes ----------------------------------------------------------------------

void roundRect(Canvas& c, double x, double y, double w, double h, double r) {
  c.beginPath();
  c.moveTo(x + r, y);
  c.lineTo(x + w - r, y);
  c.quadraticCurveTo(x + w, y, x + w, y + r);
  c.lineTo(x + w, y + h - r);
  c.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  c.lineTo(x + r, y + h);
  c.quadraticCurveTo(x, y + h, x, y + h - r);
  c.lineTo(x, y + r);
  c.quadraticCurveTo(x, y, x + r, y);
  c.closePath();
}

void line(Canvas& c, double x0, double y0, double x1, double y1, const QString& color, double width = 1) {
  c.setStroke(color);
  c.setLineWidth(width);
  c.beginPath();
  c.moveTo(x0, y0);
  c.lineTo(x1, y1);
  c.stroke();
}

void dotted(Canvas& c, double x0, double x1, double y, const QString& color = "rgba(60, 55, 70, 0.55)") {
  c.setFill(color);
  for (double x = std::max(x0, x1); x > std::min(x0, x1); x -= 4) c.fillRect(x - 1.1, y, 1.1, 1.1);
}

/// The strip of small diamonds along the top of the printed pages.
void topStrip(Canvas& c, double w, const QString& accent) {
  const double hgt = 24;
  c.setFill(tint(accent, 0.82));
  c.fillRect(0, 0, w, hgt);
  c.setFill(tint(accent, 0.62));
  c.beginPath();
  for (double x = 8; x < w; x += 16) {
    for (const auto& [cx, cy] : {std::pair<double, double>{x, 6}, {x + 8, 18}}) {
      c.moveTo(cx, cy - 5);
      c.lineTo(cx + 5, cy);
      c.lineTo(cx, cy + 5);
      c.lineTo(cx - 5, cy);
      c.closePath();
    }
  }
  c.fill();
}

/// A page heading: Arabic, large and bold (`fr` is not shown: Arabic only).
void heading(Canvas& c, const QString& ar, const QString& fr, double w, double y) {
  Q_UNUSED(fr);
  text(c, ar, w / 2, y + 8, T(26, true, {}, 'c'));
}

/// A rounded band across the page with a white section name.
void sectionBand(Canvas& c, const QString& label, const QString& fr, double x0, double x1, double y, const QString& accent) {
  c.setFill(accent);
  roundRect(c, x0, y, x1 - x0, 32, 14);
  c.fill();
  Q_UNUSED(fr);
  text(c, label, (x0 + x1) / 2, y + 22, T(15, true, "#ffffff", 'c'));
}

struct Col {
  QString ar, fr;
  /// Share of the table's width.
  double f;
};

/// Column edges, right to left (the first column is the rightmost).
std::vector<Column> columns(const std::vector<Col>& cols, double x0, double x1) {
  double total = 0;
  for (const Col& col : cols) total += col.f;
  double right = x1;
  std::vector<Column> out;
  for (const Col& col : cols) {
    const double w = ((x1 - x0) * col.f) / total;
    out.push_back({right - w, right});
    right -= w;
  }
  return out;
}

struct TableOpts {
  double headH = 44;
  bool stripes = false;
  /// Faint numbers in the first column, starting at this number (0: none).
  int numberFrom = 0;
};

/// A table with a coloured header (white labels) and ruled rows.
void table(Canvas& c, const std::vector<Col>& cols, double x0, double x1, double y0, double rowH, int rows, const QString& accent, const TableOpts& o = {}) {
  const double headH = o.headH;
  const std::vector<Column> edges = columns(cols, x0, x1);
  const double bottom = y0 + headH + rows * rowH;
  if (o.stripes) {
    c.setFill("rgba(40, 30, 60, 0.06)");
    for (int r = 1; r < rows; r += 2) c.fillRect(x0, y0 + headH + r * rowH, x1 - x0, rowH);
  }
  c.setFill(accent);
  c.fillRect(x0, y0, x1 - x0, headH);
  for (size_t i = 0; i < edges.size(); ++i) {
    const Col& col = cols[i];
    const Column& e = edges[i];
    const double cx = (e.x0 + e.x1) / 2;
    const bool two = false;  // Arabic only: no French line under the labels
    // Long Arabic labels wrap onto two lines, as in the printed headers.
    const QStringList words = col.ar.split(' ');
    const bool fits = labelWidth(col.ar, 12.5, true) < e.x1 - e.x0 - 10 || words.size() < 2;
    if (fits) {
      text(c, col.ar, cx, y0 + (two ? headH / 2 - 1 : headH / 2 + 5), T(12.5, true, "#ffffff", 'c'));
    } else {
      const int half = int(std::ceil(words.size() / 2.0));
      text(c, words.mid(0, half).join(' '), cx, y0 + headH / 2 - (two ? 11 : 3), T(11.5, true, "#ffffff", 'c'));
      text(c, words.mid(half).join(' '), cx, y0 + headH / 2 + (two ? 2 : 11), T(11.5, true, "#ffffff", 'c'));
    }
    if (i > 0) line(c, e.x1, y0 + headH, e.x1, bottom, accent, 1);
  }
  for (int r = 1; r < rows; r++) line(c, x0, y0 + headH + r * rowH, x1, y0 + headH + r * rowH, tint(accent, 0.45), 0.8);
  if (o.numberFrom > 0) {
    const Column& e = edges[0];
    for (int r = 0; r < rows; r++) text(c, QString::number(o.numberFrom + r), (e.x0 + e.x1) / 2, y0 + headH + r * rowH + rowH / 2 + 4, T(10, false, tint(INK, 0.55), 'c'));
  }
  c.setStroke(accent);
  c.setLineWidth(1.4);
  c.strokeRect(x0, y0, x1 - x0, bottom - y0);
}

/// "label :" then a dotted line to `xEnd`; returns where its value goes.
Slot field(Canvas& c, const QString& label, double xRight, double xEnd, double y, double size = 13.5) {
  const double lw = text(c, label + " :", xRight, y, T(size, true));
  dotted(c, xRight - lw - 6, xEnd, y + 2);
  return {xEnd, y - size - 3, std::max(30.0, xRight - lw - 10 - xEnd)};
}

/// Signature lines for the teacher, the head and the inspector.
void signatures(Canvas& c, double w, double y, double m) {
  text(c, "السيد(ة) الأستاذ(ة):", w - m, y, T(13, true));
  text(c, "السيد(ة) المدير(ة):", w / 2 + 50, y + 14, T(13, true));
  text(c, "السيد(ة) المفتش(ة):", m + 130, y + 28, T(13, true));
}

void star8(Canvas& c, double cx, double cy, double r, double turn) {
  c.beginPath();
  for (int i = 0; i < 16; i++) {
    const double a = turn + (i * M_PI) / 8;
    const double rr = i % 2 ? r * 0.7654 : r;
    const double x = cx + std::cos(a) * rr, y = cy + std::sin(a) * rr;
    if (i == 0) c.moveTo(x, y);
    else c.lineTo(x, y);
  }
  c.closePath();
}

// --- layouts ------------------------------------------------------------------------

constexpr double M = 48;
constexpr double A4W = 794, A4H = 1123;
struct Fit {
  double k, ox, oy;
};
Fit fit(double w, double h) {
  const double k = std::min(w / A4W, h / A4H);
  return {k, (w - A4W * k) / 2, (h - A4H * k) / 2};
}

struct LessonCol {
  QString key, ar, fr;
  double x0, x1;
};
struct LessonBase {
  double ix0, iy0, ix1, iy1;  // inset
  double x0, x1;
  std::vector<LessonCol> cols;
  double headTop, headH, bodyTop, bodyBottom, step;
  int rows;
  double fieldsY;
};
LessonBase lessonBase(double w, double h) {
  LessonBase L;
  L.ix0 = 20;
  L.iy0 = 20;
  L.ix1 = w - 20;
  L.iy1 = h - 20;
  L.x0 = L.ix0 + 14;
  L.x1 = L.ix1 - 14;
  const std::vector<Col> cols = {{"التاريخ", "Date", 0.13}, {"القسم", "Classe", 0.11}, {"التوقيت", "Horaire", 0.13}, {"طبيعة الأنشطة المنجزة", "Activités réalisées", 0.5}, {"التوقيع", "Signature", 0.13}};
  const char* keys[] = {"date", "class", "time", "activities", "sign"};
  const std::vector<Column> edges = columns(cols, L.x0, L.x1);
  for (size_t i = 0; i < cols.size(); ++i) L.cols.push_back({keys[i], cols[i].ar, cols[i].fr, edges[i].x0, edges[i].x1});
  L.headTop = L.iy0 + 64;
  L.headH = 46;
  L.bodyTop = L.headTop + L.headH;
  L.step = (5 / 25.4) * 96;  // 5 mm squares
  L.rows = int(std::floor((h - 210 - L.bodyTop) / L.step));
  L.bodyBottom = L.bodyTop + L.rows * L.step;
  L.fieldsY = L.iy0 + 40;
  return L;
}

const std::vector<Col>& holidayCols() {
  static const std::vector<Col> c = {{"الرقم الترتيبي", "N°", 0.1}, {"العطلة", "Vacances", 0.34}, {"من", "Du", 0.16}, {"إلى", "Au", 0.16}, {"المدة", "Durée", 0.1}, {"ملاحظات", "Remarques", 0.14}};
  return c;
}

struct HolidaysBase {
  double top;
  int rows;
  double headH, rowH;
};
HolidaysBase holidaysBase(double, double h) {
  const double top = 140, headH = 46;
  const int rows = 14;
  return {top, rows, headH, (h - 150 - top - headH) / rows};
}

struct StudentsBase {
  double top, tw;
  int rows;
  double rowH;
  std::vector<double> rights;
  double bodyTop;
};
StudentsBase studentsBase(double w, double h) {
  const double top = 140, gap = 18;
  const double tw = (w - 2 * M - gap) / 2;
  const int rows = 30;
  return {top, tw, rows, (h - 50 - top - 72) / rows, {w - M, w - M - tw - gap}, top + 28 + 44};
}

TableLayout scaleTable(const TableLayout& t, double w, double h) {
  const Fit f = fit(w, h);
  TableLayout out{{}, f.oy + t.top * f.k, t.rowH * f.k, t.rows};
  for (const Column& col : t.cols) out.cols.push_back({f.ox + col.x0 * f.k, f.ox + col.x1 * f.k});
  return out;
}

struct Day {
  QString ar, fr;
  double x0, x1;
};
struct TimetableBase {
  double m, top, bottom, timeX0, timeX1, headH, bodyTop, from, to;
  std::vector<Day> days;
  double yOf(int hh, int mm) const {
    const double t = std::min(to, std::max(from, hh + mm / 60.0));
    return bodyTop + ((t - from) / (to - from)) * (bottom - bodyTop);
  }
};
TimetableBase timetableBase(double w, double h) {
  TimetableBase T;
  T.m = M;
  T.top = 150;
  T.bottom = h - 130;
  const double timeW = 58;
  const double right = w - M - timeW;
  const std::pair<const char*, const char*> days[] = {{"الإثنين", "Lundi"}, {"الثلاثاء", "Mardi"}, {"الأربعاء", "Mercredi"}, {"الخميس", "Jeudi"}, {"الجمعة", "Vendredi"}, {"السبت", "Samedi"}};
  const double colW = (right - M) / 6;
  T.headH = 44;
  T.bodyTop = T.top + T.headH;
  T.from = 8;
  T.to = 18;
  T.timeX0 = right;
  T.timeX1 = w - M;
  for (int i = 0; i < 6; ++i) T.days.push_back({QString::fromUtf8(days[i].first), days[i].second, right - (i + 1) * colW, right - i * colW});
  return T;
}

struct ProfileSection {
  const char *ar, *fr;
  std::vector<std::pair<const char*, const char*>> rows;
};
const std::vector<ProfileSection>& profileSections() {
  static const std::vector<ProfileSection> s = {
      {"المعلومات الشخصية",
       "Informations personnelles",
       {{"الإسم الشخصي", "الإسم العائلي"},
        {"تاريخ الازدياد", "مكان الازدياد"},
        {"الإطار التربوي", "الوحدة المدرسية"},
        {"رقم التأجير", "رقم الضمان الاجتماعي"},
        {"أكاديمية التعيين", "الأقسام المسندة"},
        {"المديرية الإقليمية", "الموسم الدراسي"},
        {"رقم بطاقة التعريف الوطنية", ""}}},
      {"المعلومات الإدارية", "Informations administratives", {{"تاريخ التوظيف", "تاريخ الترسيم"}, {"الإطار", ""}, {"الدرجة", "تاريخها"}, {"الرتبة", "تاريخها"}}},
      {"المعلومات المهنية",
       "Informations professionnelles",
       {{"المؤسسة الحالية", "تاريخ الالتحاق بالمؤسسة"},
        {"تاريخ التعيين بالمديرية", "تاريخ التعيين بالأكاديمية"},
        {"نقطة آخر تفتيش وتاريخها", "ممنوحة من السيد(ة) المفتش(ة)"},
        {"لغة التكوين", "مركز التكوين"},
        {"لغة التدريس", ""}}},
  };
  return s;
}

/// Profile labels the app can fill in, and the setting each one shows.
const QHash<QString, QString>& profileSlots() {
  static const QHash<QString, QString> s = {{"رقم التأجير", "payroll"},       {"أكاديمية التعيين", "region"}, {"الأقسام المسندة", "classes"},
                                            {"المديرية الإقليمية", "directorate"}, {"الموسم الدراسي", "year"},      {"المؤسسة الحالية", "school"}};
  return s;
}

// --- drawing ------------------------------------------------------------------------

/// Slots by name, remembering the order they were found in.
struct SlotMap {
  QHash<QString, Slot> map;
  QStringList order;
  Slot& operator[](const QString& k) {
    if (!map.contains(k)) order << k;
    return map[k];
  }
};

/// Draws a daftar page and returns the slots where its values go.
SlotMap drawPage(Canvas& c, const QString& t, double w, double h) {
  const QString accent = accentOf(t);
  SlotMap out;
  const double mid = w / 2;
  if (t == "daftar-title") {
    c.setStroke(tint(accent, 0.35));
    c.setLineWidth(2);
    c.strokeRect(28, 28, w - 56, h - 56);
    c.setLineWidth(0.8);
    c.strokeRect(34, 34, w - 68, h - 68);
    text(c, "بسم الله الرحمن الرحيم", mid, 92, T(22, true, {}, 'c'));
    out["region"] = field(c, "الجهة", w - 70, mid + 30, 150, 14);
    out["directorate"] = field(c, "المديرية الإقليمية", w - 70, mid + 30, 186, 14);
    text(c, "وثائقي التربوية", 70, 170, T(18, true, {}, 'l'));
    star8(c, mid, 330, 26, M_PI / 8);
    c.setFill(tint(accent, 0.75));
    c.fill();
    star8(c, mid, 330, 13, 0);
    c.setFill(accent);
    c.fill();
    text(c, "دفتر النصوص", mid, 470, T(82, true, accent, 'c'));
    text(c, "خاص بالتعليم العمومي والخصوصي", mid, 600, T(20, true, {}, 'c'));
    out["school"] = field(c, "المؤسسة", w - 70, mid + 20, 860);
    out["teacher"] = field(c, "الأستاذ(ة)", mid - 20, 70, 860);
    out["classes"] = field(c, "القسم", w - 70, mid + 20, 912);
    out["payroll"] = field(c, "رقم التأجير", mid - 20, 70, 912);
    out["year"] = field(c, "الموسم الدراسي", mid + 150, mid - 150, 970);
  } else if (t == "daftar-profile") {
    heading(c, "بطاقة المعلومات الشخصية للأستاذ(ة)", "Fiche de renseignements de l'enseignant(e)", w, 66);
    c.setStroke(INK);
    c.setLineWidth(1.3);
    c.strokeRect(mid - 52, 110, 104, 124);
    text(c, "صورة", mid, 178, T(14, true, {}, 'c'));
    double y = 262;
    for (const ProfileSection& s : profileSections()) {
      sectionBand(c, s.ar, s.fr, M, w - M, y, accent);
      y += 66;
      for (const auto& [right, left] : s.rows) {
        const Slot a = field(c, right, w - M, mid + 14, y, 13);
        if (profileSlots().contains(right)) out[profileSlots()[right]] = a;
        if (*left) {
          const Slot b = field(c, left, mid - 14, M, y, 13);
          if (profileSlots().contains(left)) out[profileSlots()[left]] = b;
        }
        y += 34;
      }
      y += 10;
    }
  } else if (t == "daftar-profile-2") {
    heading(c, "بطاقة المعلومات الشخصية للأستاذ(ة)", "Fiche de renseignements (suite)", w, 66);
    sectionBand(c, "الشواهد الدراسية والمؤهلات المهنية", "Diplômes et qualifications", M, w - M, 112, accent);
    table(c, {{"الرقم الترتيبي", "N°", 0.12}, {"الشهادة", "Diplôme", 0.56}, {"تاريخها", "Date", 0.32}}, M, w - M, 158, 32, 9, accent, {44, true, 1});
    sectionBand(c, "الخدمات السابقة", "Services antérieurs", M, w - M, 528, accent);
    table(c,
          {{"الرقم الترتيبي", "N°", 0.1}, {"المؤسسات السابقة", "Établissements", 0.24}, {"المديرية", "Direction", 0.15}, {"الأكاديمية", "Académie", 0.16}, {"من ... إلى", "Du … au", 0.2},
           {"تعيين أو تكليف", "Affectation", 0.15}},
          M, w - M, 574, 32, 13, accent, {44, true, 1});
  } else if (t == "daftar-timetable") {
    topStrip(c, w, accent);
    heading(c, "استعمال الزمن", "Emploi du temps", w, 86);
    const TimetableBase L = timetableBase(w, h);
    c.setFill(accent);
    c.fillRect(L.m, L.top, w - 2 * L.m, L.headH);
    for (const Day& d : L.days) {
      const double cx = (d.x0 + d.x1) / 2;
      text(c, d.ar, cx, L.top + 27, T(13, true, "#fff", 'c'));
      if (d.x0 > L.m + 1) line(c, d.x0, L.bodyTop, d.x0, L.bottom, tint(accent, 0.45), 1);
    }
    for (int hr = int(L.from); hr <= int(L.to); hr++) {
      const double y = L.yOf(hr, 0);
      if (hr > L.from && hr < L.to) line(c, L.m, y, w - L.m, y, tint(accent, 0.55), 0.8);
      if (hr < L.to) line(c, L.m, L.yOf(hr, 30), L.timeX0, L.yOf(hr, 30), tint(accent, 0.85), 0.6);
      text(c, QString("%1:00").arg(hr, 2, 10, QChar('0')), (L.timeX0 + L.timeX1) / 2, std::min(L.bottom - 4, y + 14), T(11, false, MUTED, 'c'));
    }
    line(c, L.timeX0, L.top, L.timeX0, L.bottom, accent, 1.2);
    c.setStroke(accent);
    c.setLineWidth(1.4);
    c.strokeRect(L.m, L.top, w - 2 * L.m, L.bottom - L.top);
    signatures(c, w, h - 92, M);
  } else if (t == "daftar-holidays") {
    topStrip(c, w, accent);
    heading(c, "لائحة العطل المدرسية", "Vacances scolaires", w, 86);
    const HolidaysBase Hb = holidaysBase(w, h);
    table(c, holidayCols(), M, w - M, Hb.top, Hb.rowH, Hb.rows, accent, {Hb.headH, false, 1});
    signatures(c, w, h - 92, M);
  } else if (t == "daftar-diagnostic") {
    topStrip(c, w, accent);
    heading(c, "تقرير التقويم التشخيصي", "Rapport de l'évaluation diagnostique", w, 86);
    out["region"] = field(c, "الأكاديمية", w - M, mid + 20, 160, 13);
    out["directorate"] = field(c, "المديرية الإقليمية", w - M, mid + 20, 192, 13);
    out["school"] = field(c, "المؤسسة", w - M, mid + 20, 224, 13);
    out["teacher"] = field(c, "الأستاذ(ة)", mid - 20, M, 160, 13);
    out["year"] = field(c, "الموسم الدراسي", mid - 20, M, 192, 13);
    out["level"] = field(c, "المستوى", mid - 20, M, 224, 13);
    int i = 0;
    for (double y = 272; y < h - 120; y += 15, i++) line(c, M, y, w - M, y, i % 2 ? "rgba(40,40,60,0.18)" : "rgba(40,40,60,0.45)", i % 2 ? 0.6 : 0.9);
    signatures(c, w, h - 80, M);
  } else if (t == "daftar-students") {
    topStrip(c, w, accent);
    heading(c, "لائحة أسماء المتعلمين والمتعلمات", "Liste des élèves", w, 86);
    const StudentsBase S = studentsBase(w, h);
    for (size_t k = 0; k < S.rights.size(); ++k) {
      const double right = S.rights[k];
      const double x0 = right - S.tw;
      // "الفوج / المستوى" strip above the column headings.
      c.setFill(accent);
      c.fillRect(x0, S.top, S.tw, 28);
      // Label, then a white box for its value to the left of it.
      auto labelled = [&](const QString& label, double xr, double boxW, const QString& key) {
        const double lw = text(c, label, xr, S.top + 19, T(12, true, "#fff"));
        c.setFill("#ffffff");
        c.fillRect(xr - lw - 8 - boxW, S.top + 5, boxW, 18);
        out[key] = {xr - lw - 8 - boxW, S.top + 4, boxW};
      };
      labelled("الفوج", right - 10, 64, QString("group%1").arg(k));
      labelled("المستوى", right - S.tw * 0.5, 70, QString("level%1").arg(k));
      table(c, {{"الرقم الترتيبي", "", 0.16}, {"الرقم الوطني", "", 0.28}, {"أسماء المتعلمين والمتعلمات", "", 0.56}}, x0, right, S.top + 28, S.rowH, S.rows, accent, {44, false, 1 + int(k) * S.rows});
    }
  } else if (t == "daftar-memos") {
    topStrip(c, w, accent);
    const std::vector<Col> cols = {{"الرقم الترتيبي", "N°", 0.12}, {"رقم المذكرة", "Réf.", 0.13}, {"تاريخها", "Date", 0.17}, {"مصدرها", "Source", 0.19}, {"موضوعها", "Objet", 0.39}};
    heading(c, "المذكرات الوزارية", "Notes ministérielles", w, 78);
    table(c, cols, M, w - M, 112, 40, 9, accent, {48, false, 1});
    heading(c, "المذكرات الأكاديمية والإقليمية", "Notes académiques et provinciales", w, 586);
    table(c, cols, M, w - M, 620, 40, 9, accent, {48, false, 10});
  } else if (t == "daftar-absence") {
    topStrip(c, w, accent);
    heading(c, "غياب الأستاذ(ة): الرخص الطبية والشخصية", "Absences : congés de maladie et personnels", w, 86);
    table(c, {{"الرقم الترتيبي", "N°", 0.1}, {"نوع الغياب", "Absence", 0.14}, {"نوع الرخصة", "Congé", 0.22}, {"المدة", "Durée", 0.18}, {"من ......... إلى", "Du … au", 0.36}}, M, w - M, 140, 46, 11, accent,
          {48, false, 1});
    text(c, "ملاحظات", mid, 760, T(24, true, {}, 'c'));
    for (double y = 796; y < h - 50; y += 24) line(c, M, y, w - M, y, "rgba(40,40,60,0.3)", 0.8);
  } else if (isLessonTemplate(t)) {
    const LessonBase L = lessonBase(w, h);
    // Coloured frame around the page, like the printed books' edges.
    c.setFill(accent);
    c.fillRect(0, 0, w, h);
    c.setFill("#ffffff");
    roundRect(c, L.ix0, L.iy0, L.ix1 - L.ix0, L.iy1 - L.iy0, 10);
    c.fill();
    // المادة / القسم.
    out["subject"] = field(c, "المادة", L.x1, mid + 60, L.fieldsY, 13.5);
    out["class"] = field(c, "القسم", mid + 20, L.x0 + 10, L.fieldsY, 13.5);
    // Squared paper in every column but the signature.
    const LessonCol& sign = L.cols.back();
    c.setStroke(GRID);
    c.setLineWidth(0.6);
    c.beginPath();
    for (double y = L.bodyTop + L.step; y < L.bodyBottom - 0.5; y += L.step) {
      c.moveTo(sign.x1, y);
      c.lineTo(L.x1, y);
    }
    for (double x = L.x1 - L.step; x > sign.x1 + 0.5; x -= L.step) {
      c.moveTo(x, L.bodyTop);
      c.lineTo(x, L.bodyBottom);
    }
    c.stroke();
    // Header band and columns.
    c.setFill(accent);
    c.fillRect(L.x0, L.headTop, L.x1 - L.x0, L.headH);
    for (const LessonCol& col : L.cols) {
      const double cx = (col.x0 + col.x1) / 2;
      const bool big = col.key == "activities";
      text(c, col.ar, cx, L.headTop + (big ? 30 : 28), T(big ? 17 : 13, true, "#ffffff", 'c'));
      if (col.x0 > L.x0 + 1) line(c, col.x0, L.headTop, col.x0, L.bodyBottom, accent, 1.1);
    }
    c.setStroke(accent);
    c.setLineWidth(1.4);
    c.strokeRect(L.x0, L.headTop, L.x1 - L.x0, L.bodyBottom - L.headTop);
    // The notes strip: a slanted tab, then ruled lines.
    const double ty = L.bodyBottom + 20;
    c.setFill(accent);
    c.beginPath();
    c.moveTo(L.x1, ty);
    c.lineTo(L.x1 - 190, ty);
    c.lineTo(L.x1 - 206, ty + 28);
    c.lineTo(L.x1, ty + 28);
    c.closePath();
    c.fill();
    text(c, "ملاحظات", L.x1 - 24, ty + 20, T(14, true, "#ffffff"));
    line(c, L.x0, ty + 28, L.x1 - 206, ty + 28, accent, 1.2);
    for (double y = ty + 54; y < L.iy1 - 10; y += 22) line(c, L.x0, y, L.x1, y, "rgba(40,40,60,0.28)", 0.7);
  }
  return out;
}

}  // namespace

bool isLessonTemplate(const QString& t) { return LESSON_TEMPLATES.contains(t); }
bool isDaftarTemplate(const QString& t) { return isLessonTemplate(t) || DAFTAR_PAGES.contains(t); }
QString lessonTemplateFor(int i) { return LESSON_TEMPLATES[((i % 6) + 6) % 6]; }

void drawDaftarPage(Canvas& c, const QString& tmpl, double w, double h, const QString& paper) {
  c.setFill(paper);
  c.fillRect(0, 0, w, h);
  const Fit f = fit(w, h);
  c.save();
  c.translate(f.ox, f.oy);
  c.scale(f.k, f.k);
  drawPage(c, tmpl, A4W, A4H);
  c.restore();
}

QHash<QString, Slot> daftarSlots(const QString& tmpl, double w, double h) {
  // Drawn on a tiny canvas only for the positions.
  Canvas scratch(1, 1);
  const Fit f = fit(w, h);
  QHash<QString, Slot> out;
  const SlotMap found = drawPage(scratch, tmpl, A4W, A4H);
  for (auto it = found.map.begin(); it != found.map.end(); ++it) out[it.key()] = {f.ox + it->x * f.k, f.oy + it->y * f.k, it->w * f.k};
  return out;
}

std::vector<std::pair<QString, Slot>> daftarSlotList(const QString& tmpl, double w, double h) {
  Canvas scratch(1, 1);
  const Fit f = fit(w, h);
  const SlotMap found = drawPage(scratch, tmpl, A4W, A4H);
  std::vector<std::pair<QString, Slot>> out;
  for (const QString& k : found.order) {
    const Slot& s = found.map[k];
    out.push_back({k, {f.ox + s.x * f.k, f.oy + s.y * f.k, s.w * f.k}});
  }
  return out;
}

TableLayout holidaysLayout(double w, double h) {
  const HolidaysBase Hb = holidaysBase(A4W, A4H);
  return scaleTable({columns(holidayCols(), M, A4W - M), Hb.top + Hb.headH, Hb.rowH, Hb.rows}, w, h);
}

const LessonLayout::Col& LessonLayout::col(const QString& key) const {
  for (const Col& c : cols)
    if (c.key == key) return c;
  return cols.front();
}

LessonLayout lessonLayout(double w, double h) {
  const LessonBase L = lessonBase(A4W, A4H);
  const Fit f = fit(w, h);
  LessonLayout out;
  for (const LessonCol& c : L.cols) out.cols.push_back({c.key, f.ox + c.x0 * f.k, f.ox + c.x1 * f.k});
  out.x0 = f.ox + L.x0 * f.k;
  out.x1 = f.ox + L.x1 * f.k;
  out.bodyTop = f.oy + L.bodyTop * f.k;
  out.bodyBottom = f.oy + L.bodyBottom * f.k;
  out.step = L.step * f.k;
  out.rows = L.rows;
  return out;
}

double TimetableLayout::yOf(const QString& hhmm) const {
  // As the script read it: hours, then minutes (missing ones are 0).
  const QStringList p = hhmm.split(':');
  bool ok = true;
  const double hh = p.value(0).toDouble(&ok);
  const double mm = p.size() > 1 ? p[1].toDouble() : 0;
  const double t = std::min(to, std::max(from, (ok ? hh : 0) + mm / 60));
  return oy + (bodyTop + ((t - from) / (to - from)) * (bottom - bodyTop)) * k;
}

TimetableLayout timetableLayout(double w, double h) {
  const TimetableBase T = timetableBase(A4W, A4H);
  const Fit f = fit(w, h);
  TimetableLayout out;
  for (int i = 0; i < int(T.days.size()); ++i) out.days.push_back({i + 1, f.ox + T.days[i].x0 * f.k, f.ox + T.days[i].x1 * f.k});
  out.k = f.k;
  out.oy = f.oy;
  out.bodyTop = T.bodyTop;
  out.bottom = T.bottom;
  out.from = T.from;
  out.to = T.to;
  return out;
}

std::vector<TableLayout> studentsLayout(double w, double h) {
  const StudentsBase S = studentsBase(A4W, A4H);
  const std::vector<Col> f = {{"", "", 0.16}, {"", "", 0.28}, {"", "", 0.56}};
  std::vector<TableLayout> out;
  for (double right : S.rights) out.push_back(scaleTable({columns(f, right - S.tw, right), S.bodyTop, S.rowH, S.rows}, w, h));
  return out;
}

}  // namespace draw
