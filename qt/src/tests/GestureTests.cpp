// Pen gestures (qt/src/editor/Gestures): what is and isn't a scratch-out.
// Ordinary writing must never be taken for one, so most cases here are
// things that must stay ink.
#include <cmath>

#include "../editor/Gestures.h"
#include "../editor/Geometry.h"
#include "SelfTest.h"

using namespace ed;

namespace {
/// A pen path through `pts`, a point every ~1.5 units (as a pen samples it).
std::vector<Pt> path(const std::vector<P2>& pts) {
  std::vector<Pt> out;
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    const double len = std::hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]);
    const int n = std::max(1, int(len / 1.5));
    for (int k = 0; k < n; ++k) out.push_back({pts[i][0] + (pts[i + 1][0] - pts[i][0]) * k / n, pts[i][1] + (pts[i + 1][1] - pts[i][1]) * k / n, 0.5});
  }
  out.push_back({pts.back()[0], pts.back()[1], 0.5});
  return out;
}
/// A quick scribble over (x, y, w, h): `legs` legs across, slowly going down.
std::vector<Pt> scribble(double x, double y, double w, double h, int legs, double wobble = 0) {
  std::vector<P2> pts;
  for (int i = 0; i <= legs; ++i) pts.push_back({x + (i % 2 ? w : 0) + std::sin(i * 1.7) * wobble, y + h * i / legs + std::cos(i * 2.3) * wobble});
  return path(pts);
}
El strokeEl(const QString& id, const std::vector<Pt>& pts) {
  El el;
  el.id = id;
  el.kind = Kind::Stroke;
  el.tool = "pen";
  el.width = 3;
  el.points = pts;
  return el;
}
/// Handwriting-like strokes: a word along (x, y), `n` letters.
std::vector<Pt> word(const QString& kind, double x, double y, int n) {
  std::vector<P2> pts;
  const double h = 20;
  for (int i = 0; i < n; ++i) {
    const double x0 = x + i * 14;
    if (kind == "m") {  // humps: up and down, moving on
      pts.push_back({x0, y + h}), pts.push_back({x0 + 3, y}), pts.push_back({x0 + 7, y + h}), pts.push_back({x0 + 10, y}), pts.push_back({x0 + 14, y + h});
    } else if (kind == "w") {
      pts.push_back({x0, y}), pts.push_back({x0 + 4, y + h}), pts.push_back({x0 + 7, y + 6}), pts.push_back({x0 + 10, y + h}), pts.push_back({x0 + 14, y});
    } else if (kind == "e") {  // loops
      for (int k = 0; k <= 12; ++k) pts.push_back({x0 + 7 + 7 * std::cos(k * M_PI / 6), y + 10 + 9 * std::sin(k * M_PI / 6)});
    }
  }
  return path(pts);
}
}  // namespace

TEST("gestures", "a scribble is a scratch-out") {
  CHECK(asScribble(scribble(100, 100, 120, 24, 6)).has_value());
  CHECK(asScribble(scribble(100, 100, 120, 30, 9, 4)).has_value());          // wobbly, more legs
  CHECK(asScribble(scribble(100, 100, 40, 14, 7, 1.5)).has_value());         // a short word
  // At an angle.
  std::vector<Pt> tilted;
  for (const Pt& p : scribble(0, 0, 120, 24, 7)) tilted.push_back({200 + p[0] * 0.87 - p[1] * 0.5, 200 + p[0] * 0.5 + p[1] * 0.87, 0.5});
  CHECK(asScribble(tilted).has_value());
}

TEST("gestures", "writing and drawing are not scratch-outs") {
  CHECK(!asScribble(scribble(100, 100, 120, 24, 4)).has_value());  // only twice back and forth
  CHECK(!asScribble(word("m", 100, 100, 6)).has_value());          // mmmmmm
  CHECK(!asScribble(word("w", 100, 100, 6)).has_value());          // wwwwww
  CHECK(!asScribble(word("e", 100, 100, 6)).has_value());          // eeeeee (loops)
  // A zigzag line, moving on.
  std::vector<P2> zig;
  for (int i = 0; i <= 12; ++i) zig.push_back({100.0 + i * 15, i % 2 ? 130.0 : 100.0});
  CHECK(!asScribble(path(zig)).has_value());
  // A word circled twice, for emphasis.
  std::vector<P2> circle;
  for (int k = 0; k <= 48; ++k) circle.push_back({200 + 70 * std::cos(k * M_PI / 12), 200 + 25 * std::sin(k * M_PI / 12)});
  CHECK(!asScribble(path(circle)).has_value());
  // A line gone over again and again to thicken it.
  std::vector<P2> retrace;
  for (int i = 0; i <= 8; ++i) retrace.push_back({i % 2 ? 300.0 : 100.0, 100.0 + (i % 3) * 0.6});
  CHECK(!asScribble(path(retrace)).has_value());
  // A straight line, a dot, a tick.
  CHECK(!asScribble(path({{100, 100}, {300, 102}})).has_value());
  CHECK(!asScribble(path({{100, 100}, {101, 101}})).has_value());
  CHECK(!asScribble(path({{100, 100}, {110, 115}, {130, 85}})).has_value());
  // A spiral.
  std::vector<P2> spiral;
  for (int k = 0; k <= 80; ++k) spiral.push_back({200 + k * 0.8 * std::cos(k * 0.3), 200 + k * 0.8 * std::sin(k * 0.3)});
  CHECK(!asScribble(path(spiral)).has_value());
}

TEST("gestures", "only what the scribble covers goes") {
  const auto s = asScribble(scribble(95, 92, 130, 34, 8));
  CHECK(s.has_value());
  const El under = strokeEl("under", word("m", 100, 100, 6));            // the word scratched out
  const El half = strokeEl("half", word("m", 180, 100, 8));              // sticks out a long way
  const El far = strokeEl("far", word("m", 400, 400, 3));                // elsewhere
  std::vector<P2> ring;
  for (int k = 0; k <= 48; ++k) ring.push_back({160 + 150 * std::cos(k * M_PI / 24), 110 + 120 * std::sin(k * M_PI / 24)});
  const El around = strokeEl("around", path(ring));                       // a big circle around it all
  El typed;
  typed.id = "typed";
  typed.kind = Kind::Text;
  typed.x = 105, typed.y = 98, typed.w = 100, typed.fontSize = 16, typed.text = "gone";
  El pic;
  pic.id = "pic";
  pic.kind = Kind::Image;
  pic.x = 110, pic.y = 100, pic.w = 40, pic.h = 20;
  const QSet<QString> gone = scratchedOut(*s, {&under, &half, &far, &around, &typed, &pic});
  CHECK(gone.contains("under"));
  CHECK(gone.contains("typed"));
  CHECK(!gone.contains("half"));
  CHECK(!gone.contains("far"));
  CHECK(!gone.contains("around"));  // shading inside a drawing leaves the drawing
  CHECK(!gone.contains("pic"));     // pictures, graphs, tables: never
}
