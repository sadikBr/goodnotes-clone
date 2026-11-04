// Tables (Kind::Table): their cells and sizes, changes to them, and turning
// a grid drawn by hand (lines, boxes) into a table.
#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

#include "Model.h"

namespace ed {

/// A table's contents: cells (rows × cols), and column / row sizes as shares.
struct TableData {
  std::vector<double> cols, rows;
  std::vector<QStringList> cells;  ///< [row][col], line breaks as '\n'
  bool header = false;
  int rowCount() const { return int(rows.size()); }
  int colCount() const { return int(cols.size()); }
  QString cell(int r, int c) const;
  void setCell(int r, int c, const QString& text);
};
TableData tableData(const El& el);
/// Writes `t` into the table element (its text and sizes).
void setTableData(El& el, const TableData& t);
/// The cell at a page point (an unrotated table), as {row, col}.
std::optional<std::pair<int, int>> tableCellAt(const El& el, double x, double y);
/// A cell's box on the page.
Rect tableCellRect(const El& el, int row, int col);
/// Makes rows tall enough for their text (the table grows downwards).
void fitTableRows(El& el);
/// Adds or removes a row or column ("addRow", "addCol", "removeRow",
/// "removeCol") at `index` (-1: the end; new ones go after it), or
/// toggles the header row ("header"). False when nothing changed.
bool changeTable(El& el, const QString& op, int index);

/// A grid drawn by hand among `els`: its frame, its column and row edges,
/// and which elements are its lines.
struct DrawnGrid {
  Rect frame;
  std::vector<double> xs, ys;
  QSet<QString> lineIds;
  QString color;
  double width = 1.5;
};
std::optional<DrawnGrid> findDrawnGrid(const std::vector<const El*>& els);

}  // namespace ed
