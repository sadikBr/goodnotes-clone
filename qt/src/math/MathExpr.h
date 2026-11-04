// Maths for the page: reads an expression or equation as people write it
// (3x + 5 = 20, 2,5 × 4 =, √(16) + 2², sin(π/6), f(x) = x² − 2x, Arabic
// digits), works it out (a value, or the solutions of an equation in one
// unknown, exactly when it's a polynomial up to degree 2, numerically
// otherwise) and evaluates functions for graphs.
#pragma once

#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include <vector>

namespace mathx {

struct Node;
using NodeP = std::shared_ptr<const Node>;

/// Written maths in the form the parser reads: Arabic-Indic and Persian
/// digits, decimal commas, ×÷−·, superscripts, √, π and the like turned into
/// plain characters; spaces kept.
QString normalize(const QString& text);

struct Parsed {
  NodeP lhs, rhs;     ///< rhs: the other side of an equation (null: none, or an empty one: "2 + 2 =")
  bool equation = false;
  /// The unknown (x, y, t…): the one letter used, if any ("" when none).
  QString var;
  /// A function definition: "y = …" or "f(x) = …" (lhs is then the function's body).
  bool definition = false;
  QString error;
  bool ok() const { return error.isEmpty(); }
};
Parsed parse(const QString& text);

/// The value of `n` with the unknown at `x` (NaN when undefined).
double eval(const NodeP& n, double x = 0);

/// A polynomial's coefficients (lowest power first), when `n` is one in the
/// unknown (degree up to 6).
std::optional<std::vector<double>> polynomial(const NodeP& n);

struct Result {
  /// "value" (2 + 2 = 4), "check" (3 + 4 = 7: true or not), "solutions",
  /// "all" (true for every value), "none" (no solution), "error".
  QString kind;
  /// The answer as it's written after the expression: "4", "x = 5",
  /// "x = 1 ; x = 3", "x ≈ 0,52", "✓", "≠ (8)", "∅", "ℝ".
  QString answer;
  /// The expression, cleaned up for showing (3x + 5 = 20).
  QString pretty;
  /// The unknown, if any.
  QString var;
  /// Solutions (or the value).
  std::vector<double> values;
  QString error;
  bool ok() const { return error.isEmpty(); }
};
/// Works out what's written. `comma`: write decimals with a comma (-1: as the
/// input does, a comma when it doesn't say).
Result calculate(const QString& text, int comma = -1);

/// A number written as people do: integers plainly, simple fractions as
/// fractions (1/3), others with up to `digits` significant digits.
QString format(double v, bool comma, bool fractions = true, int digits = 8);
/// An expression written nicely: × for products, ², ³, √, π, − (for typesetting).
QString pretty(const QString& text);

/// A function of x for graphs ("x^2 - 2x", "y = sin x", "f(x) = 1/x").
struct Function {
  NodeP body;
  QString var = "x";
  QString error;
  bool ok() const { return body && error.isEmpty(); }
  double at(double x) const { return eval(body, x); }
};
Function function(const QString& text);

}  // namespace mathx
