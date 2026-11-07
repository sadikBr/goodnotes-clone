// Was src/editor/geometry.test.ts.
#include "../editor/Geometry.h"
#include "../editor/Snap.h"
#include "SelfTest.h"

using namespace ed;

namespace {

El stroke(std::vector<P2> pts, double width = 4) {
  El e;
  e.id = "s";
  e.kind = Kind::Stroke;
  e.tool = "pen";
  e.color = "#000";
  e.width = width;
  for (const P2& p : pts) e.points.push_back({p[0], p[1], 0.5});
  return e;
}
El shapeEl(const QString& id, Shape s, std::optional<QString> fill = std::nullopt) {
  El e;
  e.id = id;
  e.kind = Kind::Shape;
  e.color = "#000";
  e.width = 2;
  e.shape = std::move(s);
  e.fill = fill;
  return e;
}
Shape poly(const QString& kind, std::vector<P2> pts) {
  Shape s;
  s.kind = kind;
  s.points = std::move(pts);
  return s;
}
Shape ellipse(double cx, double cy, double rx, double ry) {
  Shape s;
  s.kind = "ellipse";
  s.cx = cx;
  s.cy = cy;
  s.rx = rx;
  s.ry = ry;
  return s;
}
El box(Kind k, double x, double y, double w, double h) {
  El e;
  e.id = k == Kind::Text ? "t" : "i";
  e.kind = k;
  e.x = x;
  e.y = y;
  e.w = w;
  e.h = h;
  if (k == Kind::Text) {
    e.text = "hi";
    e.fontSize = 20;
    e.color = "#000";
  } else {
    e.src = "x";
  }
  return e;
}
bool sameRect(const Rect& a, const Rect& b) { return selftest::close(a.x, b.x, 9) && selftest::close(a.y, b.y, 9) && selftest::close(a.w, b.w, 9) && selftest::close(a.h, b.h, 9); }
using selftest::close;

}  // namespace

TEST("geometry", "computes stroke bounds including half the width") { CHECK(sameRect(bounds(stroke({{10, 10}, {30, 20}}, 4)), {8, 8, 24, 14})); }

TEST("geometry", "hit-tests strokes by distance to the polyline") {
  const El s = stroke({{0, 0}, {100, 0}}, 2);
  CHECK(hitTest(s, 50, 5, 5));
  CHECK(!hitTest(s, 50, 20, 5));
}

TEST("geometry", "hit-tests ellipse outlines but not their empty interior") {
  const El el = shapeEl("e", ellipse(0, 0, 50, 50));
  CHECK(hitTest(el, 50, 0, 3));
  CHECK(!hitTest(el, 0, 0, 3));
}

TEST("geometry", "detects points in polygons") {
  const std::vector<P2> sq = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
  CHECK(pointInPolygon(5, 5, sq));
  CHECK(!pointInPolygon(15, 5, sq));
}

TEST("geometry", "selects strokes mostly inside the lasso") {
  const Lasso lasso({{0, 0}, {100, 0}, {100, 100}, {0, 100}});
  CHECK(lasso.holds(stroke({{10, 10}, {50, 50}, {90, 90}})));
  CHECK(!lasso.holds(stroke({{10, 10}, {150, 150}, {190, 190}})));
}

TEST("geometry", "translates every element kind") {
  El text = box(Kind::Text, 1, 2, 100, 0);
  const El moved = translate(text, 5, 5);
  CHECK(moved.x == 6 && moved.y == 7);
  const El s = translate(stroke({{0, 0}}), 3, 4);
  CHECK(s.points[0] == (Pt{3, 4, 0.5}));
}

TEST("geometry", "sizes text boxes by their wrapped lines") {
  El text = box(Kind::Text, 0, 0, 100, 0);
  text.text = "one";
  CHECK_CLOSE(bounds(text).h, 27, 6);
  text.text = "one two three four five six seven";
  const double h = bounds(text).h;
  CHECK(h > 27 * 1.5);
  CHECK_CLOSE(std::fmod(h, 27), 0, 6);
}

TEST("geometry", "unions rectangles") {
  CHECK(!unionRects({}).has_value());
  CHECK(sameRect(*unionRects({{0, 0, 10, 10}, {5, -5, 20, 5}}), {0, -5, 25, 15}));
}

TEST("geometry", "detects a held pen at the end of a stroke") {
  const std::vector<Pt> pts = {{0, 0, 0.5}, {50, 0, 0.5}, {100, 0, 0.5}, {100.5, 0, 0.5}, {100.6, 0, 0.5}};
  CHECK(isHeld({0, 100, 200, 500, 900}, pts, 600, 2));
  CHECK(!isHeld({0, 100, 200, 250, 300}, pts, 600, 2));
}

TEST("buildShape", "normalises boxes dragged in any direction") {
  const auto r = buildShape("rectangle", {100, 100}, {20, 40});
  CHECK(r && r->kind == "rectangle" && r->points == (std::vector<P2>{{20, 40}, {100, 40}, {100, 100}, {20, 100}}));
}

TEST("buildShape", "constrains to squares, circles and 45° lines") {
  const auto c = buildShape("ellipse", {0, 0}, {100, 40}, true);
  CHECK(c && c->rx == 50 && c->ry == 50);
  const auto l = buildShape("line", {0, 0}, {100, 8}, true);
  CHECK(l && std::abs(l->points[1][1]) < 1e-9);
}

TEST("buildShape", "builds stars, diamonds, triangles and arrows") {
  CHECK(buildShape("star", {0, 0}, {100, 100})->kind == "polygon");
  CHECK(buildShape("star", {0, 0}, {100, 100})->points.size() == 10);
  CHECK(buildShape("diamond", {0, 0}, {100, 50})->points.size() == 4);
  CHECK(buildShape("triangle", {0, 0}, {100, 50})->kind == "triangle");
  CHECK(buildShape("arrow", {0, 0}, {50, 50})->kind == "arrow");
}

TEST("buildShape", "builds every closed shape to fill the box it was dragged") {
  for (const char* k : {"roundRect", "bubble", "rightTriangle", "parallelogram", "trapezoid", "pentagon", "hexagon", "octagon", "cross", "blockArrow", "lightning", "heart", "cloud", "moon"}) {
    const auto sh = buildShape(k, {10, 20}, {210, 120});
    CHECK(sh && sh->kind == "polygon" && sh->name == k && sh->points.size() >= 3);
    if (!sh) continue;
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (const P2& p : sh->points) x0 = std::min(x0, p[0]), y0 = std::min(y0, p[1]), x1 = std::max(x1, p[0]), y1 = std::max(y1, p[1]);
    CHECK(std::abs(x0 - 10) < 0.5 && std::abs(y0 - 20) < 0.5 && std::abs(x1 - 210) < 0.5 && std::abs(y1 - 120) < 0.5);
    CHECK(isClosedShape(*sh));
  }
}

TEST("buildShape", "a double arrow is a line with two heads") {
  const auto d = buildShape("doubleArrow", {0, 0}, {80, 0});
  CHECK(d && d->kind == "doubleArrow" && d->points.size() == 2 && !isClosedShape(*d));
  CHECK(convertShape(shapeEl("d", *d), "line").shape.kind == "line");
  CHECK(convertShape(shapeEl("d", *d), "hexagon").shape.kind == "doubleArrow");
}

TEST("buildShape", "ignores taps and auto mode") {
  CHECK(!buildShape("rectangle", {0, 0}, {1, 1}));
  CHECK(!buildShape("auto", {0, 0}, {100, 100}));
}

namespace {
El rectEl() { return shapeEl("r", poly("rectangle", {{0, 0}, {100, 0}, {100, 50}, {0, 50}})); }
El arrowEl() { return shapeEl("l", poly("arrow", {{0, 0}, {100, 0}})); }
}  // namespace

TEST("shape editing", "offers 8 box handles for shapes and 2 for lines") {
  CHECK(handlesFor(rectEl()).size() == 8);
  const auto hl = handlesFor(arrowEl());
  CHECK(hl.size() == 2 && hl[0].id == "p0" && hl[1].id == "p1");
  CHECK(handleAt(rectEl(), 101, 51, 4) == "se");
  CHECK(handleAt(rectEl(), 50, 25, 4).isEmpty());
}

TEST("shape editing", "resizes from a corner keeping the opposite corner fixed") {
  CHECK(sameRect(geomBox(resizeElement(rectEl(), "se", {200, 150})), {0, 0, 200, 150}));
  CHECK(sameRect(geomBox(resizeElement(rectEl(), "w", {50, 999})), {50, 0, 50, 50}));
}

TEST("shape editing", "keeps proportions with Shift and never inverts") {
  CHECK(sameRect(geomBox(resizeElement(rectEl(), "se", {300, 60}, true)), {0, 0, 300, 150}));
  CHECK(geomBox(resizeElement(rectEl(), "e", {-500, 0})).w == 4);
}

TEST("shape editing", "resizes ellipses, images and sticky notes") {
  const El e = resizeElement(shapeEl("e", ellipse(50, 50, 50, 25)), "e", {200, 0});
  CHECK(e.shape.cx == 100 && e.shape.rx == 100 && e.shape.ry == 25);
  const El img = resizeElement(box(Kind::Image, 10, 10, 100, 100), "nw", {0, 0});
  CHECK(img.x == 0 && img.y == 0 && img.w == 110 && img.h == 110);
}

TEST("shape editing", "moves line endpoints, snapping with Shift") {
  CHECK(resizeElement(arrowEl(), "p1", {80, 60}).shape.points == (std::vector<P2>{{0, 0}, {80, 60}}));
  CHECK(std::abs(resizeElement(arrowEl(), "p1", {100, 8}, true).shape.points[1][1]) < 1e-9);
}

TEST("shape editing", "selects closed shapes by tapping inside") {
  CHECK(hitForSelect(rectEl(), 50, 25, 2));
  CHECK(!hitForSelect(arrowEl(), 50, 25, 2));
}

TEST("shape editing", "converts shapes to other kinds in the same box") {
  const El e = convertShape(rectEl(), "ellipse");
  CHECK(e.shape.kind == "ellipse" && e.shape.cx == 50 && e.shape.cy == 25 && e.shape.rx == 50 && e.shape.ry == 25);
  const El l = convertShape(arrowEl(), "line");
  CHECK(l.shape.kind == "line" && l.shape.points == (std::vector<P2>{{0, 0}, {100, 0}}));
  CHECK(convertShape(arrowEl(), "star").shape.kind == "arrow");
}

TEST("model", "round-trips elements through storage and drops unknown kinds") {
  El s = stroke({{1, 2}});
  s.z = 3;
  const QJsonArray rows{toStored(s), QJsonObject{{"id", "x"}, {"kind", "hologram"}, {"z", 0}, {"data", QJsonObject()}}};
  const auto back = fromStored(rows);
  CHECK(back.size() == 1);
  CHECK(back[0].id == "s" && back[0].kind == Kind::Stroke && back[0].z == 3);
  CHECK(nextZ(back) == 4);
  CHECK(nextZ({}) == 0);
}

TEST("partial erasing", "cuts a stroke in two where the eraser touches it") {
  const auto pieces = eraseElement(stroke({{0, 0}, {100, 0}}, 2), 50, 0, 5);
  CHECK(pieces && pieces->size() == 2);
  if (!pieces || pieces->size() != 2) return;
  const El& left = (*pieces)[0];
  const El& right = (*pieces)[1];
  CHECK(left.id == "s" && right.id != "s");
  double lmax = -1e9, rmin = 1e9;
  for (const Pt& p : left.points) lmax = std::max(lmax, p[0]);
  for (const Pt& p : right.points) rmin = std::min(rmin, p[0]);
  CHECK(lmax < 45 && rmin > 55);
  CHECK(left.points[0] == (Pt{0, 0, 0.5}));
}

TEST("partial erasing", "trims an end, removes fully covered strokes, and ignores far-away ones") {
  const El s = stroke({{0, 0}, {100, 0}}, 2);
  const auto trimmed = eraseElement(s, 100, 0, 10);
  CHECK(trimmed && trimmed->size() == 1);
  if (trimmed && trimmed->size() == 1) {
    double m = -1e9;
    for (const Pt& p : (*trimmed)[0].points) m = std::max(m, p[0]);
    CHECK(m < 90);
  }
  const auto gone = eraseElement(stroke({{0, 0}, {3, 0}}, 2), 1, 0, 10);
  CHECK(gone && gone->empty());
  CHECK(!eraseElement(s, 50, 40, 5));
}

TEST("partial erasing", "opens up shape outlines and keeps an arrow's head on the tip piece") {
  const auto pieces = eraseElement(shapeEl("r", poly("rectangle", {{0, 0}, {100, 0}, {100, 50}, {0, 50}}), QString("#00000033")), 50, 0, 4);
  CHECK(pieces && !pieces->empty());
  if (pieces)
    for (const El& p : *pieces) CHECK(p.shape.kind == "path" && !p.fill);
  const auto cut = eraseElement(arrowEl(), 50, 0, 4);
  CHECK(cut && cut->size() == 2 && (*cut)[0].shape.kind == "path" && (*cut)[1].shape.kind == "arrow");
}

TEST("partial erasing", "leaves text alone") { CHECK(!eraseElement(box(Kind::Text, 0, 0, 100, 0), 5, 5, 10)); }

TEST("selection transforms", "scales from the opposite corner, keeping proportions on corners") {
  const auto t = groupTransform({0, 0, 100, 50}, "se", {100, 50}, {200, 60}, false);
  CHECK(t.sx == 2 && t.sy == 2);
  CHECK(applyAffine(t.m, {0, 0}) == (P2{0, 0}));
  CHECK(applyAffine(t.m, {100, 50}) == (P2{200, 100}));
}

TEST("selection transforms", "stretches along one axis from an edge handle") {
  const auto t = groupTransform({0, 0, 100, 50}, "e", {100, 25}, {150, 999}, false);
  CHECK(t.sx == 1.5 && t.sy == 1);
  const auto w = groupTransform({0, 0, 100, 50}, "w", {0, 25}, {-50, 25}, false);
  CHECK_CLOSE(applyAffine(w.m, {100, 0})[0], 100, 2);
}

TEST("selection transforms", "rotates around the centre, snapping to 15° with Shift") {
  const auto t = groupTransform({0, 0, 100, 50}, "rot", {50, 80}, {10, 25}, true);
  CHECK_CLOSE(t.angle, M_PI / 2, 2);
  const P2 c = applyAffine(t.m, {50, 25});
  CHECK_CLOSE(c[0], 50, 2);
  CHECK_CLOSE(c[1], 25, 2);
}

TEST("selection transforms", "transforms every element kind") {
  const El st = transformElement(stroke({{10, 0}}), rotateAbout(0, 0, M_PI / 2), 1, 1, M_PI / 2);
  CHECK_CLOSE(st.points[0][0], 0, 2);
  CHECK_CLOSE(st.points[0][1], 10, 2);
  CHECK(st.points[0][2] == 0.5);
  const El img = box(Kind::Image, 0, 0, 100, 50);
  const El scaled = transformElement(img, scaleAbout(0, 0, 2, 2), 2, 2, 0);
  CHECK(scaled.x == 0 && scaled.y == 0 && scaled.w == 200 && scaled.h == 100);
  const El turned = transformElement(img, rotateAbout(50, 25, M_PI / 2), 1, 1, M_PI / 2);
  CHECK_CLOSE(turned.rot(), M_PI / 2, 2);
  CHECK_CLOSE(turned.x, 0, 2);
  const Rect b = bounds(turned);
  CHECK_CLOSE(b.w, 50, 2);
  CHECK_CLOSE(b.h, 100, 2);
  const El big = transformElement(box(Kind::Text, 0, 0, 100, 0), scaleAbout(0, 0, 1.5, 1.5), 1.5, 1.5, 0);
  CHECK(big.fontSize == 30.0 && big.w == 150);
}

TEST("selection transforms", "hit-tests rotated boxes in their own frame") {
  El img = box(Kind::Image, 0, 40, 200, 20);
  img.rotation = M_PI / 2;
  CHECK(hitTest(img, 100, 50, 0));
  CHECK(hitTest(img, 100, 140, 1));
  CHECK(!hitTest(img, 190, 50, 1));
}

TEST("transformShape", "rotates and scales around a pivot") {
  const Shape t = transformShape(poly("line", {{0, 0}, {10, 0}}), {0, 0}, M_PI / 2, 2);
  CHECK(t.kind == "line");
  CHECK_CLOSE(t.points[1][0], 0, 2);
  CHECK_CLOSE(t.points[1][1], 20, 2);
}

TEST("transformShape", "keeps circles as circles and turns rotated ellipses into closed paths") {
  const Shape c = transformShape(ellipse(0, 0, 10, 10), {5, 0}, 1, 1.5);
  CHECK(c.kind == "ellipse" && c.rx == 15 && c.ry == 15);
  const Shape e = transformShape(ellipse(0, 0, 20, 10), {0, 0}, 0.6, 1);
  CHECK(e.kind == "path" && isClosedShape(e));
}

TEST("transformShape", "translates") {
  const Shape r = transformShape(poly("rectangle", {{0, 0}, {1, 0}, {1, 1}, {0, 1}}), {0, 0}, 0, 1, 5, 7);
  CHECK(r.points[0] == (P2{5, 7}));
}

TEST("drafting", "a line settles parallel, square to the line it starts on, or on a round angle") {
  const std::vector<DraftLine> lines{{{0, 0}, {100, 37}, "a"}};
  const double par = std::atan2(37, 100);
  AngleSnap s = snapDirection(par + 0.01, lines, std::nullopt);
  CHECK(s.why == "parallel" && std::abs(s.angle - par) < 1e-9);
  s = snapDirection(par + M_PI / 2 + 0.01, lines, lines[0]);
  CHECK(s.why == "square" && std::abs(s.angle - (par + M_PI / 2)) < 1e-9);
  s = snapDirection(M_PI / 4 + 0.015, {}, std::nullopt);
  CHECK(s.why == "round" && std::abs(s.angle - M_PI / 4) < 1e-9);
  s = snapDirection(0.3, {}, std::nullopt);
  CHECK(s.why.isEmpty() && s.angle == 0.3);
}

TEST("drafting", "ends join nearby corners; angles between lines read as drawn") {
  CHECK(nearestCorner({{10, 10}, {50, 50}}, {12, 11}, 5) == P2({10, 10}));
  CHECK(!nearestCorner({{10, 10}}, {30, 30}, 5));
  const DraftLine base{{0, 0}, {100, 0}, "b"};
  CHECK(std::abs(angleBetween(base, {0, 0}, {50, -50}) - 45) < 1e-9);
  CHECK(std::abs(angleBetween(base, {0, 0}, {-50, -50}) - 135) < 1e-9);
  CHECK(std::abs(angleBetween(base, {50, 0}, {50, -80}) - 90) < 1e-9);
}

TEST("drafting", "nearly square boxes and nearly round ellipses become exact") {
  auto r = buildShape("rectangle", {0, 0}, {100, 97});
  CHECK(r && squareUp(*r) && std::abs(shapeSize(*r).first - shapeSize(*r).second) < 1e-9);
  auto e = buildShape("ellipse", {0, 0}, {100, 60});
  CHECK(e && !squareUp(*e));
}

TEST("snapGaps", "a box settles midway between its neighbours, or at the gap they keep") {
  const std::vector<Rect> others{{0, 0, 50, 50}, {200, 0, 50, 50}};
  // Between them: 50 … 200, a 40-wide box is midway at x = 105.
  SnapResult s = snapGaps({100, 5, 40, 40}, others, 3, 0, 7, false, false);
  CHECK(std::abs(100 + s.dx - 105) < 1e-9 && s.dy == 0 && s.guides.size() == 2);
  // Too far from midway: left alone.
  s = snapGaps({100, 5, 40, 40}, others, -30, 0, 7, false, false);
  CHECK(s.dx == -30 && s.guides.empty());
  // A third to the right of the pair, at the same gap (150) as between them.
  s = snapGaps({400, 0, 50, 50}, others, 3, 0, 7, false, false);
  CHECK(std::abs(400 + s.dx - 400) < 1e-9);
  // An axis lined up by another guide stays as it is.
  s = snapGaps({100, 5, 40, 40}, others, 3, 0, 7, true, false);
  CHECK(s.dx == 3);
}
