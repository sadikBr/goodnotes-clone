#include "Samples.h"

#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QPainter>
#include <array>
#include <cmath>

#include "../draw/Fonts.h"

namespace samples {

namespace {

QJsonArray pts(const std::vector<std::array<double, 3>>& p) {
  QJsonArray a;
  for (const auto& q : p) a.append(QJsonArray{q[0], q[1], q[2]});
  return a;
}

/// Handwriting-like points: a wavy line with loops, pressure varying.
std::vector<std::array<double, 3>> scribble(double x0, double y0, int n, double seed) {
  std::vector<std::array<double, 3>> p;
  for (int i = 0; i < n; ++i) {
    const double t = i / double(n - 1);
    p.push_back({x0 + t * 260 + 18 * std::sin(t * 19 + seed), y0 + 22 * std::sin(t * 11 + seed * 2) + 10 * std::cos(t * 37), 0.25 + 0.6 * (0.5 + 0.5 * std::sin(t * 7 + seed))});
  }
  return p;
}

QString pngDataUrl() {
  QImage img(40, 30, QImage::Format_ARGB32);
  img.fill(QColor(30, 120, 200));
  QPainter p(&img);
  p.fillRect(5, 5, 15, 10, QColor(250, 200, 40));
  p.end();
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  img.save(&buf, "PNG");
  return "data:image/png;base64," + QString::fromLatin1(bytes.toBase64());
}

}  // namespace

std::vector<std::pair<QString, QJsonObject>> sampleElements() {
  std::vector<std::pair<QString, QJsonObject>> out;
  auto stroke = [&](const QString& name, const QString& tool, const QString& style, const QString& color, double width, const std::vector<std::array<double, 3>>& p, QJsonValue smooth = QJsonValue()) {
    QJsonObject o{{"kind", "stroke"}, {"id", name}, {"tool", tool}, {"color", color}, {"width", width}, {"points", pts(p)}};
    if (!style.isEmpty()) o["style"] = style;
    if (!smooth.isNull()) o["smooth"] = smooth;
    out.push_back({"el-" + name, o});
  };
  const std::vector<std::array<double, 3>> vee = {{100, 100, 0.5}, {120, 140, 0.6}, {140, 180, 0.7}, {150, 200, 0.7}, {160, 180, 0.6}, {180, 140, 0.5}, {200, 100, 0.4}, {205, 95, 0.4}, {210, 90, 0.35}};
  for (const char* style : {"fountain", "ballpoint", "pencil", "calligraphy", "brush"}) {
    stroke(QString("%1-scribble").arg(style), "pen", style, "#111827", 3, scribble(60, 120, 90, 1.3));
    stroke(QString("%1-thick").arg(style), "pen", style, "#2563eb", 9, scribble(70, 200, 60, 2.1));
    stroke(QString("%1-vee").arg(style), "pen", style, "#dc2626", 4, vee);
    stroke(QString("%1-tap").arg(style), "pen", style, "#16a34a", 5, {{150, 150, 0.6}, {150.5, 150.4, 0.6}});
    stroke(QString("%1-one").arg(style), "pen", style, "#9333ea", 4, {{150, 150, 0.5}});
    stroke(QString("%1-two").arg(style), "pen", style, "#ea580c", 4, {{100, 100, 0.5}, {220, 160, 0.5}});
  }
  stroke("default-style", "pen", "", "#111827", 3, scribble(60, 120, 80, 0.4));
  stroke("smooth-off", "pen", "fountain", "#111827", 3, scribble(60, 120, 80, 0.7), 0);
  stroke("smooth-strong", "pen", "fountain", "#111827", 3, scribble(60, 120, 80, 0.9), 0.62);
  stroke("highlighter", "highlighter", "", "#facc15", 16, scribble(60, 140, 70, 0.2));
  stroke("highlighter-dab", "highlighter", "", "#facc15", 16, {{150, 150, 0.5}, {150.2, 150.1, 0.5}, {150.3, 150.2, 0.5}});
  stroke("highlighter-curl", "highlighter", "", "#22c55e", 20, {{100, 100, 0.5}, {160, 102, 0.5}, {170, 120, 0.5}, {150, 135, 0.5}, {110, 130, 0.5}});

  auto shape = [&](const QString& name, QJsonObject s, const QString& fill = {}) {
    QJsonObject o{{"kind", "shape"}, {"id", name}, {"color", "#111827"}, {"width", 3}, {"shape", s}};
    if (!fill.isEmpty()) o["fill"] = fill;
    out.push_back({"el-shape-" + name, o});
  };
  auto P = [](std::initializer_list<std::pair<double, double>> l) {
    QJsonArray a;
    for (auto [x, y] : l) a.append(QJsonArray{x, y});
    return a;
  };
  shape("line", {{"kind", "line"}, {"points", P({{60, 60}, {260, 180}})}});
  shape("arrow", {{"kind", "arrow"}, {"points", P({{60, 180}, {260, 60}})}});
  shape("rect", {{"kind", "rectangle"}, {"points", P({{60, 60}, {260, 60}, {260, 200}, {60, 200}})}});
  shape("rect-filled", {{"kind", "rectangle"}, {"points", P({{60, 60}, {260, 60}, {260, 200}, {60, 200}})}}, "rgba(37, 99, 235, 0.25)");
  shape("triangle", {{"kind", "triangle"}, {"points", P({{160, 50}, {270, 220}, {50, 220}})}}, "#fde68a");
  shape("ellipse", {{"kind", "ellipse"}, {"cx", 160}, {"cy", 140}, {"rx", 100}, {"ry", 60}}, "#bbf7d0");
  shape("circle-tiny", {{"kind", "ellipse"}, {"cx", 160}, {"cy", 140}, {"rx", 0.2}, {"ry", 0.1}});
  shape("polygon", {{"kind", "polygon"}, {"points", P({{160, 40}, {190, 110}, {270, 120}, {210, 170}, {230, 250}, {160, 205}, {90, 250}, {110, 170}, {50, 120}, {130, 110}})}});
  shape("path-open", {{"kind", "path"}, {"closed", false}, {"points", P({{50, 200}, {90, 80}, {140, 160}, {190, 60}, {250, 190}})}});
  shape("path-closed", {{"kind", "path"}, {"closed", true}, {"points", P({{50, 200}, {90, 80}, {140, 160}, {190, 60}, {250, 190}})}}, "#fecaca");
  shape("path-breaks", {{"kind", "path"}, {"closed", false}, {"breaks", QJsonArray{3}}, {"points", P({{60, 100}, {150, 100}, {240, 100}, {60, 160}, {150, 160}, {240, 160}})}});

  auto text = [&](const QString& name, QJsonObject o) {
    o["kind"] = "text";
    o["id"] = name;
    if (!o.contains("x")) o["x"] = 40;
    if (!o.contains("y")) o["y"] = 40;
    if (!o.contains("w")) o["w"] = 240;
    if (!o.contains("fontSize")) o["fontSize"] = 22;
    if (!o.contains("color")) o["color"] = "#111827";
    out.push_back({"el-text-" + name, o});
  };
  const QString para = "The quick brown fox jumps over the lazy dog, and then naps in the sun.";
  for (const draw::FontPreset& f : draw::fontPresets()) text("font-" + f.id, {{"text", para}, {"font", f.id}});
  text("bold", {{"text", para}, {"bold", true}});
  text("italic", {{"text", para}, {"italic", true}, {"font", "book"}});
  text("bold-italic", {{"text", para}, {"bold", true}, {"italic", true}});
  text("center", {{"text", para}, {"align", "center"}});
  text("right", {{"text", para}, {"align", "right"}, {"font", "playpen"}});
  text("bullets", {{"text", "• First item that is long enough to wrap onto the next line\n• Second\n◦ Sub item\n- Dash item\n* Star"}, {"fontSize", 18}});
  text("numbers", {{"text", "1. One thing\n2) Another thing that wraps over more than one line here\n10. Ten\n123. Big"}, {"fontSize", 18}});
  text("blank-lines", {{"text", "Above\n\n  indented   spaces  \nBelow"}, {"fontSize", 20}});
  text("long-word", {{"text", "Supercalifragilisticexpialidocious-and-more-and-more"}, {"w", 120}});
  text("arabic", {{"text", "بسم الله الرحمن الرحيم، هذا نص عربي طويل بما يكفي ليلتف على عدة أسطر في المربع."}, {"fontSize", 22}});
  text("arabic-center", {{"text", "دفتر النصوص\nالموسم الدراسي 2026-2027"}, {"align", "center"}});
  text("arabic-left", {{"text", "الأستاذ(ة): سادك"}, {"align", "left"}, {"bold", true}});
  text("mixed", {{"text", "Français et العربية mélangés, avec des chiffres 123 et ² ½ ×."}, {"font", "school"}});
  text("arabic-list", {{"text", "• البند الأول\n• البند الثاني الذي يكون طويلا بما يكفي ليلتف"}, {"fontSize", 18}});
  text("small", {{"text", para}, {"fontSize", 9}, {"color", "#dc2626"}});
  text("huge", {{"text", "Big"}, {"fontSize", 120}, {"w", 300}});
  text("rotated", {{"text", para}, {"rotation", 0.4}});
  text("empty", {{"text", ""}});

  auto sticky = [&](const QString& name, QJsonObject o) {
    o["kind"] = "sticky";
    o["id"] = name;
    if (!o.contains("x")) o["x"] = 50;
    if (!o.contains("y")) o["y"] = 50;
    if (!o.contains("w")) o["w"] = 200;
    if (!o.contains("h")) o["h"] = 160;
    if (!o.contains("color")) o["color"] = "#fef08a";
    out.push_back({"el-sticky-" + name, o});
  };
  sticky("plain", {{"text", "Buy milk\nCall mum"}});
  sticky("overflow", {{"text", para + " " + para + " " + para}, {"fontSize", 20}});
  sticky("font", {{"text", para}, {"font", "marker"}, {"color", "#bbf7d0"}, {"fontSize", 16}});
  sticky("arabic", {{"text", "ملاحظة مهمة للحصة القادمة"}, {"color", "#fbcfe8"}});
  sticky("rotated", {{"text", "Tilted"}, {"rotation", -0.3}});
  // Tables: rows on lines, cells split by tabs.
  auto table = [&](const QString& name, QJsonObject o) {
    o["kind"] = "table";
    o["id"] = name;
    if (!o.contains("x")) o["x"] = 30;
    if (!o.contains("y")) o["y"] = 40;
    if (!o.contains("w")) o["w"] = 340;
    if (!o.contains("h")) o["h"] = 150;
    if (!o.contains("color")) o["color"] = "#374151";
    if (!o.contains("fontSize")) o["fontSize"] = 14;
    out.push_back({"el-table-" + name, o});
  };
  table("plain", {{"text", "\t\t\n\t\t\n\t\t"}, {"cols", QJsonArray{1, 1, 1}}, {"rows", QJsonArray{1, 1, 1}}});
  table("header", {{"text", "Element\tSymbol\tMass\nHydrogen\tH\t1,008\nHelium, a noble gas that is light\tHe\t4,003"}, {"header", true},
                   {"cols", QJsonArray{0.5, 0.2, 0.3}}, {"rows", QJsonArray{0.25, 0.25, 0.5}}});
  table("arabic", {{"text", "اليوم\tالحصة\nالاثنين\tالرياضيات\nالثلاثاء\tالفيزياء"}, {"header", true}, {"font", "school"}, {"color", "#2563eb"}, {"width", 2}});
  const QString png = pngDataUrl();
  out.push_back({"el-image", QJsonObject{{"kind", "image"}, {"id", "img"}, {"x", 60}, {"y", 60}, {"w", 160}, {"h", 120}, {"src", png}}});
  out.push_back({"el-image-rotated", QJsonObject{{"kind", "image"}, {"id", "img2"}, {"x", 60}, {"y", 60}, {"w", 160}, {"h", 120}, {"src", png}, {"rotation", 0.7}}});
  out.push_back({"el-image-missing", QJsonObject{{"kind", "image"}, {"id", "img3"}, {"x", 60}, {"y", 60}, {"w", 160}, {"h", 120}, {"src", "data:image/png;base64,AAAA"}}});
  return out;
}


}  // namespace samples
