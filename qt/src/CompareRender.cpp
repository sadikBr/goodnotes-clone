#include "CompareRender.h"

#include <QDir>
#include <QImage>
#include <QJSValue>
#include <QtDebug>
#include <array>
#include <functional>
#include <vector>

#include "Js.h"
#include "draw/Covers.h"
#include "draw/DaftarPapers.h"
#include "draw/Elements.h"
#include "draw/Fonts.h"
#include "JsCanvas.h"
#include "Core.h"
#include <QJsonDocument>
#include <random>
#include <QJsonArray>
#include <QJsonObject>
#include <QBuffer>
#include <QPainter>
#include <cmath>

namespace {

struct Case {
  QString name;
  std::function<QImage()> script, native;
};

struct Diff {
  int pixels = 0;  ///< pixels where a channel differs by more than the tolerance
  int max = 0;     ///< the largest channel difference
};

Diff diff(const QImage& a0, const QImage& b0, QImage* map) {
  const QImage a = a0.convertToFormat(QImage::Format_ARGB32), b = b0.convertToFormat(QImage::Format_ARGB32);
  Diff d;
  if (a.size() != b.size()) {
    d.pixels = -1;
    return d;
  }
  if (map) {
    *map = QImage(a.size(), QImage::Format_ARGB32);
    map->fill(Qt::white);
  }
  for (int y = 0; y < a.height(); ++y) {
    const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
    const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
    for (int x = 0; x < a.width(); ++x) {
      const int m = std::max({std::abs(qRed(pa[x]) - qRed(pb[x])), std::abs(qGreen(pa[x]) - qGreen(pb[x])), std::abs(qBlue(pa[x]) - qBlue(pb[x])), std::abs(qAlpha(pa[x]) - qAlpha(pb[x]))});
      d.max = std::max(d.max, m);
      if (m > 2) {
        ++d.pixels;
        if (map) map->setPixel(x, y, qRgb(255, 0, 0));
      }
    }
  }
  return d;
}

std::vector<Case> cases() {
  std::vector<Case> out;
  Js& js = Js::instance();
  struct Size {
    const char* id;
    double w, h;
  };
  const Size sizes[] = {{"a4", 794, 1123}, {"a5", 559, 794}, {"letter", 816, 1056}, {"legal", 816, 1344}, {"square", 900, 900}};
  const char* colors[] = {"#ffffff", "#fffdf7", "#fdf3c8", "#eef1f4", "#22262d"};
  QJSValue list = js.fn("TEMPLATES");
  QStringList ids;
  for (int k = 0, n = list.property("length").toInt(); k < n; ++k) ids << list.property(k).property("id").toString();
  // Covers aren't in the paper catalogue.
  for (const draw::CoverDesign& d : draw::coverDesigns()) ids << d.tmpl;
  for (const QString& id : ids) {
    // Daftar pages: identical to the scripts' when ported (step 3), then
    // changed on purpose (Arabic only); their value positions still compare.
    if (id.startsWith("daftar")) continue;
    const bool special = false;
    if (id.startsWith("cover")) {
      // Every design in every cover colour; all sizes and orientations in two.
      const char* covers[] = {"#2563eb", "#0d9488", "#16a34a", "#ca8a04", "#ea580c", "#dc2626", "#db2777", "#7c3aed", "#475569", "#111827", "#f5f5f4"};
      for (const Size& s : sizes)
        for (int turn = 0; turn < 2; ++turn)
          for (const char* c : covers) {
            if ((std::string(s.id) != "a4" || turn) && std::string(c) != "#2563eb" && std::string(c) != "#ca8a04") continue;
            const double w = turn ? s.h : s.w, h = turn ? s.w : s.h;
            const QString color = c;
            out.push_back({QString("%1-%2%3-%4").arg(id, s.id, turn ? "-landscape" : "", color.mid(1)), [=, &js] { return js.paperScript(id, color, w, h, 1.0); },
                           [=, &js] { return js.paper(id, color, w, h, 1.0); }});
          }
      out.push_back({id + "-x2", [=, &js] { return js.paperScript(id, "#7c3aed", 794, 1123, 2.0); }, [=, &js] { return js.paper(id, "#7c3aed", 794, 1123, 2.0); }});
      continue;
    }
    for (const Size& s : sizes) {
      for (int turn = 0; turn < 2; ++turn) {
        const double w = turn ? s.h : s.w, h = turn ? s.w : s.h;
        for (const char* c : colors) {
          // Daftar pages: one size and colour (drawn by the scripts both
          // ways until their step).
          if (special && (std::string(s.id) != "a4" || turn || std::string(c) != "#ffffff")) continue;
          const QString color = c;
          const QString name = QString("paper-%1-%2%3-%4").arg(id, s.id, turn ? "-landscape" : "", color.mid(1));
          out.push_back({name, [=, &js] { return js.paperScript(id, color, w, h, 1.0); }, [=, &js] { return js.paper(id, color, w, h, 1.0); }});
        }
      }
    }
    // Sharper (HiDPI) and a zoomed-in part.
    out.push_back({"paper-" + id + "-x2", [=, &js] { return js.paperScript(id, "#ffffff", 794, 1123, 2.0); }, [=, &js] { return js.paper(id, "#ffffff", 794, 1123, 2.0); }});
    out.push_back({"paper-" + id + "-part", [=, &js] { return js.paperScript(id, "#fffdf7", 794, 1123, 3.0, QRectF(100, 200, 250, 180)); },
                   [=, &js] { return js.paper(id, "#fffdf7", 794, 1123, 3.0, QRectF(100, 200, 250, 180)); }});
  }
  return out;
}

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
  const QString png = pngDataUrl();
  out.push_back({"el-image", QJsonObject{{"kind", "image"}, {"id", "img"}, {"x", 60}, {"y", 60}, {"w", 160}, {"h", 120}, {"src", png}}});
  out.push_back({"el-image-rotated", QJsonObject{{"kind", "image"}, {"id", "img2"}, {"x", 60}, {"y", 60}, {"w", 160}, {"h", 120}, {"src", png}, {"rotation", 0.7}}});
  out.push_back({"el-image-missing", QJsonObject{{"kind", "image"}, {"id", "img3"}, {"x", 60}, {"y", 60}, {"w", 160}, {"h", 120}, {"src", "data:image/png;base64,AAAA"}}});
  return out;
}

QImage elementScript(const QJsonObject& el, double scale) { return Js::instance().elementsScript(QJsonArray{el}, scale, QRectF(0, 0, 320, 280)); }

QImage elementNative(const QJsonObject& el, double scale) {
  JsCanvas c(int(std::ceil(320 * scale)), int(std::ceil(280 * scale)));
  c.setTransform(scale, 0, 0, scale, 0, 0);
  draw::drawElement(c, el);
  return c.image();
}

void addElementCases(std::vector<Case>& out) {
  for (const auto& [name, el] : sampleElements()) {
    for (double scale : {1.0, 2.5}) {
      const QJsonObject e = el;
      out.push_back({QString("%1-x%2").arg(name).arg(scale), [=] { return elementScript(e, scale); }, [=] { return elementNative(e, scale); }});
    }
  }
  // Whole pages: paper and a mix of everything.
  QJsonArray all;
  // (Pictures are compared one by one: the scripts' page drawing only finds
  // pictures the editor has loaded.)
  for (const auto& [name, el] : sampleElements())
    if (el["kind"].toString() != "image") all.append(el);
  for (const char* t : {"ruled", "cover-zellige", "daftar"}) {
    const QString tmpl = t;
    out.push_back({QString("page-%1").arg(t),
                   [=] {
                     Js& js = Js::instance();
                     QJSValue els = js.toJs(all);
                     QJSValue canvas = js.call("renderPage", {tmpl, QString("#fffdf7"), 794.0, 1123.0, els, 1.5, js.engine().evaluate("new Map()")});
                     auto* c = qobject_cast<JsCanvas*>(canvas.toQObject());
                     return c ? c->image() : QImage();
                   },
                   [=] { return draw::renderPage(tmpl, "#fffdf7", 794, 1123, all, 1.5)->image(); }});
  }
}

}  // namespace

int compareRender(const QString& outDir) {
  QDir().mkpath(outDir);
  int failed = 0, total = 0;
  std::vector<Case> all = qEnvironmentVariableIsSet("INKLEAF_COMPARE_ELEMENTS_ONLY") ? std::vector<Case>{} : cases();
  addElementCases(all);
  for (const Case& c : all) {
    // The scripts' canvases go when the script engine collects them.
    if (++total % 40 == 0) Js::instance().engine().collectGarbage();
    const QImage a = c.script(), b = c.native();
    QImage map;
    const Diff d = diff(a, b, &map);
    if (d.pixels != 0) {
      ++failed;
      qWarning().noquote() << "DIFFERS" << c.name << (d.pixels < 0 ? QString("(size %1x%2 vs %3x%4)").arg(a.width()).arg(a.height()).arg(b.width()).arg(b.height()) : QString("%1 pixels, max %2").arg(d.pixels).arg(d.max));
      a.save(outDir + "/" + c.name + "-script.png");
      b.save(outDir + "/" + c.name + "-native.png");
      if (d.pixels > 0) map.save(outDir + "/" + c.name + "-diff.png");
    }
  }
  // Where daftar pages' filled-in values go.
  const char* pages[] = {"daftar", "daftar-blue", "daftar-title", "daftar-profile", "daftar-profile-2", "daftar-timetable", "daftar-holidays", "daftar-diagnostic", "daftar-students", "daftar-memos", "daftar-absence"};
  for (const char* t : pages) {
    for (const auto& [w, h] : {std::pair<double, double>{794, 1123}, {1123, 794}, {559, 794}}) {
      ++total;
      const QJSValue js = Js::instance().call("daftarPapers.daftarSlots", {QString(t), w, h});
      const QHash<QString, draw::Slot> native = draw::daftarSlots(t, w, h);
      QStringList bad;
      const QStringList keys = js.toVariant().toMap().keys();
      if (keys.size() != native.size()) bad << QString("%1 vs %2 slots").arg(keys.size()).arg(native.size());
      for (const QString& k : keys) {
        const QJSValue v = js.property(k);
        const draw::Slot n = native.value(k, {-1, -1, -1});
        if (std::abs(v.property("x").toNumber() - n.x) > 1e-6 || std::abs(v.property("y").toNumber() - n.y) > 1e-6 || std::abs(v.property("w").toNumber() - n.w) > 1e-6) bad << k;
      }
      if (!bad.isEmpty()) {
        ++failed;
        qWarning().noquote() << "DIFFERS slots" << t << w << h << bad.join(", ");
      }
    }
  }
  qInfo().noquote() << QString("compare-render: %1 of %2 cases differ").arg(failed).arg(total);
  return failed ? 1 : 0;
}
