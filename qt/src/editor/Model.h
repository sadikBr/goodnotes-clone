// The editor's elements (was src/editor/model.ts): strokes, shapes, text
// boxes, sticky notes and pictures, as typed C++ values. They convert to and
// from the JSON the library stores (unknown keys are kept as they were).
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace ed {

using P2 = std::array<double, 2>;
using Pt = std::array<double, 3>;  ///< x, y, pressure (0..1)

struct Rect {
  double x = 0, y = 0, w = 0, h = 0;
};

/// A recognised or drawn shape (line, arrow, triangle, rectangle, polygon, ellipse, path).
struct Shape {
  QString kind;
  std::vector<P2> points;
  double cx = 0, cy = 0, rx = 0, ry = 0;  ///< ellipse
  bool closed = false;                    ///< path
  std::vector<int> breaks;                ///< path: where a new stroke starts (multi-stroke symbols)
  static Shape fromJson(const QJsonObject& o);
  QJsonObject toJson() const;
};

enum class Kind { Stroke, Shape, Text, Sticky, Image };

struct El {
  QString id;
  double z = 0;
  Kind kind = Kind::Stroke;
  QString color;
  double width = 0;
  // stroke
  QString tool;                  ///< pen, highlighter
  std::optional<QString> style;  ///< pen nib (none: fountain)
  std::vector<Pt> points;
  std::optional<double> smooth;
  // shape
  Shape shape;
  std::optional<QString> fill;  ///< null: unfilled
  // text, sticky, image
  double x = 0, y = 0, w = 0, h = 0;
  QString text, src;
  std::optional<double> fontSize;  ///< text: always; sticky: none means 18
  std::optional<QString> font, align, role, session;
  std::optional<bool> bold, italic;
  std::optional<double> rotation;
  /// Keys this model doesn't know, kept for saving.
  QJsonObject extra;
  /// Changes whenever the element does (each new or changed element gets a
  /// new one): the canvas redraws what changed. Not saved.
  quint64 rev = 0;
  /// Bounds, worked out once (elements aren't changed in place, except the
  /// one being typed into, which clears it).
  mutable std::optional<Rect> cachedBounds;

  bool isTextish() const { return kind == Kind::Text || kind == Kind::Sticky; }
  bool isBox() const { return kind == Kind::Text || kind == Kind::Sticky || kind == Kind::Image; }
  double rot() const { return rotation.value_or(0); }
  /// The editor's JSON for it (what drawing and saving take); a stroke's
  /// points can be left out (saving writes them itself).
  QJsonObject toJson(bool withPoints = true) const;
  static El fromJson(const QJsonObject& o);
};

/// Elements are shared between the page and its undo history; a change
/// makes a new one (see EditorCore).
using ElPtr = std::shared_ptr<El>;
/// A new element (with a new rev).
ElPtr makeEl(El e);

QString kindName(Kind k);
std::optional<Kind> kindOf(const QString& name);
QString newId();
/// One above the highest z.
double nextZ(const std::vector<El>& els);
/// Elements from stored rows ({id, kind, z, data}), sorted by z; unknown or corrupt rows skipped.
std::vector<El> fromStored(const QJsonArray& rows);
QJsonObject toStored(const El& el);

}  // namespace ed
