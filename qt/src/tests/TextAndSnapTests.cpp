// Were src/editor/textFormat.test.ts and snap.test.ts.
#include "../editor/Snap.h"
#include "../editor/TextFormat.h"
#include "SelfTest.h"

using namespace ed;

TEST("lists", "turns bullets on and off, skipping blank lines") {
  const QString on = toggleList("Cells\n\nTissues", "bullet");
  CHECK_EQ(on, QString("• Cells\n\n• Tissues"));
  CHECK_EQ(currentList(on), QString("bullet"));
  CHECK_EQ(toggleList(on, "bullet"), QString("Cells\n\nTissues"));
}

TEST("lists", "numbers items in order and switches between list kinds") {
  const QString numbered = toggleList("• a\n• b\n• c", "number");
  CHECK_EQ(numbered, QString("1. a\n2. b\n3. c"));
  CHECK_EQ(currentList(numbered), QString("number"));
  CHECK(currentList("1. a\nplain").isEmpty());
}

TEST("lists", "starts a list in an empty box with the first marker") {
  CHECK_EQ(toggleList("", "bullet"), QString("• "));
  CHECK_EQ(toggleList("", "number"), QString("1. "));
}

TEST("lists", "continues bullets and numbers on Enter") {
  const auto a = continueList("• first", 7);
  CHECK(a && a->value == "• first\n• " && a->caret == 10);
  const auto b = continueList("1. one", 6);
  CHECK(b && b->value == "1. one\n2. " && b->caret == 10);
}

TEST("lists", "ends the list on an empty item and ignores plain text") {
  const auto a = continueList("• a\n• ", 6);
  CHECK(a && a->value == "• a\n" && a->caret == 4);
  CHECK(!continueList("plain", 5));
}

namespace {
std::vector<Anchor> textAt(double y, double left) {
  return {{'y', "baseline", y + 15, left, left + 100}, {'y', "mid", y + 10, left, left + 100}, {'x', "left", left, y, y + 20}, {'x', "right", left + 100, y, y + 20}, {'x', "center", left + 50, y, y + 20}};
}
}  // namespace

TEST("snapMove", "snaps a nearby baseline onto another text's baseline") {
  const auto r = snapMove(textAt(100, 300), textAt(200, 50), 0, 103, 6);
  CHECK(r.dy == 100);
  bool found = false;
  for (const Guide& g : r.guides) found = found || (g.label == "Baseline" && g.value == 215);
  CHECK(found);
}

TEST("snapMove", "aligns left edges and leaves far-away moves alone") {
  const auto r = snapMove(textAt(400, 53), textAt(200, 50), 0, 0, 6);
  CHECK(r.dx == -3);
  QString xLabel;
  for (const Guide& g : r.guides)
    if (g.axis == 'x') {
      xLabel = g.label;
      break;
    }
  CHECK_EQ(xLabel, QString("Left edge"));
  const auto far = snapMove(textAt(400, 200), textAt(200, 50), 1, 1, 6);
  CHECK(far.dx == 1 && far.dy == 1 && far.guides.empty());
}

TEST("snapMove", "puts text on ruled lines, centres it between them, and on the page centre") {
  const auto paper = paperTargets({110, 144}, 0, 794, true, 90, 794, 1123);
  CHECK(snapMove(textAt(92, 300), paper, 0, 0, 6).dy == 3);
  const auto centred = snapMove(textAt(114, 300), paper, 0, 0, 6);
  CHECK(centred.dy == 3);
  CHECK(!centred.guides.empty() && centred.guides[0].label == "Centred in line");
  CHECK(snapMove(textAt(600, 345), paper, 0, 0, 6).dx == 2);
}

// Was src/editor/ruler.test.ts.
#include "../editor/Instrument.h"

namespace {
double degOf(double r) { return std::round(r * 180 / M_PI * 100) / 100; }
}  // namespace

TEST("ruler", "snaps to common angles when close, else whole degrees") {
  CHECK(degOf(snapAngle(44 * M_PI / 180, false)) == 45);
  CHECK(degOf(snapAngle(-1.5 * M_PI / 180, false)) == 0);
  CHECK(degOf(snapAngle(37.4 * M_PI / 180, false)) == 37);
  CHECK(degOf(snapAngle(52 * M_PI / 180, true)) == 45);
}

TEST("ruler", "projects onto an edge, offset so the ink sits against it") {
  EdgeGuide e;
  e.a = {0, 100};
  e.dir = {1, 0};
  e.normal = {0, -1};
  CHECK(projectOnto(e, {50, 87}, 2) == (P2{50, 98}));
  const double d = M_SQRT1_2;
  EdgeGuide diag;
  diag.a = {0, 0};
  diag.dir = {d, d};
  diag.normal = {d, -d};
  const P2 p = projectOnto(diag, {10, 0}, 0);
  CHECK_CLOSE(p[0], 5, 2);
  CHECK_CLOSE(p[1], 5, 2);
}

TEST("ruler", "projects onto the protractor's arc, outside it") {
  EdgeGuide g;
  g.arc = true;
  g.c = {0, 0};
  g.r = 100;
  const P2 p = projectOnto(g, {0, -50}, 3);
  CHECK_CLOSE(p[0], 0, 2);
  CHECK_CLOSE(p[1], -103, 2);
}

TEST("ruler", "locks strokes to edges and knows its body") {
  Instrument r;
  r.show(400, 400);
  CHECK(r.contains(400, 400));
  CHECK(!r.contains(400, 500));
  const auto edge = r.edgeNear({400, 400 + 15 * MM / 2 + 3}, 12);
  CHECK(edge && !edge->arc);
  CHECK(!r.edgeNear({400, 600}, 12));
  r.setKind("protractor");
  r.show(400, 400);
  CHECK(r.describe(1, 1000)["svg"].toString().contains("rotate(0)"));
}
