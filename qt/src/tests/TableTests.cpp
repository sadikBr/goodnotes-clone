// Tables (qt/src/editor/Table): cells, sizes, changes, and grids drawn by hand.
#include "../editor/Table.h"
#include "../editor/Geometry.h"
#include "SelfTest.h"

using namespace ed;

namespace {
El table(int rows, int cols) {
  El el;
  el.id = "t";
  el.kind = Kind::Table;
  el.x = 100, el.y = 100, el.w = 300, el.h = rows * 40.0;
  el.fontSize = 16;
  TableData t;
  t.cols.assign(cols, 1);
  t.rows.assign(rows, 1);
  t.cells.assign(rows, QStringList(QVector<QString>(cols).toList()));
  setTableData(el, t);
  return el;
}
El stroke(const QString& id, std::vector<P2> pts) {
  El el;
  el.id = id;
  el.kind = Kind::Stroke;
  el.tool = "pen";
  el.color = "#111827";
  el.width = 3;
  // Many points along each piece, a little wobbly, as a hand draws.
  for (size_t i = 0; i + 1 < pts.size(); ++i)
    for (int k = 0; k < 12; ++k) {
      const double f = k / 12.0;
      el.points.push_back({pts[i][0] + (pts[i + 1][0] - pts[i][0]) * f + (k % 3 - 1) * 0.8, pts[i][1] + (pts[i + 1][1] - pts[i][1]) * f + (k % 2) * 0.8, 0.5});
    }
  el.points.push_back({pts.back()[0], pts.back()[1], 0.5});
  return el;
}
}  // namespace

TEST("tables", "cells are kept, with tabs and line breaks") {
  El el = table(2, 3);
  TableData t = tableData(el);
  CHECK_EQ(t.rowCount(), 2);
  CHECK_EQ(t.colCount(), 3);
  t.setCell(0, 0, "Name");
  t.setCell(1, 2, "two\nlines");
  t.setCell(1, 1, "a\tb");
  setTableData(el, t);
  const El back = El::fromJson(el.toJson());
  const TableData u = tableData(back);
  CHECK_EQ(u.cell(0, 0), QString("Name"));
  CHECK_EQ(u.cell(1, 2), QString("two\nlines"));
  CHECK_EQ(u.cell(1, 1), QString("a b"));
  // The text holds the cells (search and the AI read it).
  CHECK(back.text.contains("Name"));
}

TEST("tables", "a cell from a point, and its box") {
  const El el = table(2, 3);
  const auto c = tableCellAt(el, 100 + 250, 100 + 50);
  CHECK(c.has_value());
  CHECK_EQ(c->first, 1);
  CHECK_EQ(c->second, 2);
  CHECK(!tableCellAt(el, 50, 50).has_value());
  const Rect r = tableCellRect(el, 1, 2);
  CHECK_CLOSE(r.x, 300, 6);
  CHECK_CLOSE(r.y, 140, 6);
  CHECK_CLOSE(r.w, 100, 6);
}

TEST("tables", "rows and columns added and removed") {
  El el = table(2, 2);
  TableData t = tableData(el);
  t.setCell(1, 1, "keep");
  setTableData(el, t);
  CHECK(changeTable(el, "addRow", -1));
  CHECK_EQ(tableData(el).rowCount(), 3);
  CHECK_CLOSE(el.h, 120, 6);  // the table grows by a row; the others keep their height
  CHECK(changeTable(el, "addCol", 0));
  t = tableData(el);
  CHECK_EQ(t.colCount(), 3);
  CHECK_EQ(t.cell(1, 2), QString("keep"));  // the new column went after the first
  CHECK(changeTable(el, "removeRow", 0));
  CHECK_EQ(tableData(el).cell(0, 2), QString("keep"));
  CHECK(changeTable(el, "header", -1));
  CHECK(tableData(el).header);
  El one = table(1, 1);
  CHECK(!changeTable(one, "removeRow", -1));
  CHECK(!changeTable(one, "removeCol", -1));
}

TEST("tables", "rows grow to fit their text") {
  El el = table(2, 2);
  const double before = el.h;
  TableData t = tableData(el);
  t.setCell(0, 0, "a long sentence that needs several lines in a narrow cell");
  setTableData(el, t);
  fitTableRows(el);
  CHECK(el.h > before + 20);
  CHECK(tableData(el).rows[0] > tableData(el).rows[1]);
}

TEST("tables", "a grid drawn by hand is found") {
  // A box in one stroke, two lines down, one across: 2 rows × 3 columns.
  const El box = stroke("box", {{100, 100}, {400, 102}, {401, 250}, {99, 249}, {101, 103}});
  const El v1 = stroke("v1", {{200, 98}, {202, 252}});
  const El v2 = stroke("v2", {{300, 101}, {298, 248}});
  const El h1 = stroke("h1", {{98, 170}, {402, 172}});
  const El word = stroke("word", {{120, 200}, {130, 215}, {140, 195}});  // a tick in a cell
  const auto g = findDrawnGrid({&box, &v1, &v2, &h1, &word});
  CHECK(g.has_value());
  CHECK_EQ(int(g->xs.size()), 4);
  CHECK_EQ(int(g->ys.size()), 3);
  CHECK(g->lineIds.contains("box") && g->lineIds.contains("v1") && g->lineIds.contains("h1"));
  CHECK(!g->lineIds.contains("word"));
  CHECK(std::abs(g->frame.x - 100) < 5);
  CHECK(std::abs(g->frame.w - 300) < 10);
  // Just lines, no box ("#"): still a grid, 3 × 3.
  const El a = stroke("a", {{150, 100}, {152, 300}}), b = stroke("b", {{250, 100}, {248, 300}});
  const El c = stroke("c", {{100, 160}, {300, 162}}), d = stroke("d", {{100, 240}, {300, 238}});
  const auto hash = findDrawnGrid({&a, &b, &c, &d});
  CHECK(hash.has_value());
  CHECK_EQ(int(hash->xs.size()), 4);
  CHECK_EQ(int(hash->ys.size()), 4);
}

TEST("tables", "not a grid") {
  // A plus sign, a lone box, handwriting.
  const El p1 = stroke("p1", {{100, 150}, {200, 150}}), p2 = stroke("p2", {{150, 100}, {150, 200}});
  CHECK(!findDrawnGrid({&p1, &p2}).has_value());
  const El box = stroke("box", {{100, 100}, {300, 100}, {300, 200}, {100, 200}, {100, 100}});
  CHECK(!findDrawnGrid({&box}).has_value());
  const El w1 = stroke("w1", {{100, 100}, {120, 140}, {140, 100}, {160, 140}}), w2 = stroke("w2", {{170, 100}, {190, 140}, {210, 110}});
  CHECK(!findDrawnGrid({&w1, &w2}).has_value());
}
