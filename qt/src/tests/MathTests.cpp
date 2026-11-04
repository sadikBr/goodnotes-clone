// The maths toolkit's reading and working out (qt/src/math/MathExpr).
#include "../math/MathExpr.h"
#include "SelfTest.h"

using namespace mathx;

namespace {
QString answer(const QString& s) { return calculate(s).answer; }
}  // namespace

TEST("maths", "arithmetic as people write it") {
  CHECK_EQ(answer("2 + 2 ="), QString("4"));
  CHECK_EQ(answer("2,5 × 4 ="), QString("10"));
  CHECK_EQ(answer("7 ÷ 2"), QString("3,5"));
  CHECK_EQ(answer("7.5 / 2 ="), QString("3.75"));
  CHECK_EQ(answer("1/3"), QString("1/3 (≈ 0,3333)"));
  CHECK_EQ(answer("√16 + 2²"), QString("8"));
  CHECK_EQ(answer("√(9) × 3²"), QString("27"));
  CHECK_EQ(answer("-3 - 4"), QString("−7"));
  CHECK_EQ(answer("2(3 + 4)"), QString("14"));
  CHECK_EQ(answer("5!"), QString("120"));
  CHECK_EQ(answer("20% × 150"), QString("30"));
  CHECK_EQ(answer("12 : 4"), QString("3"));
  CHECK_EQ(answer("10⁻²"), QString("0,01"));
  CHECK_EQ(answer("|−5| + 1"), QString("6"));
  CHECK_EQ(answer("sin(30°)"), QString("0,5"));
  CHECK_EQ(answer("cos(π)"), QString("−1"));
  CHECK_EQ(answer("2^10"), QString("1024"));
  // Arabic digits and decimal separator.
  CHECK_EQ(answer("٣ + ٤ ="), QString("7"));
  CHECK_EQ(answer("٢٫٥ × ٢"), QString("5"));
  CHECK_EQ(calculate("1 / 0").kind, QString("error"));
  CHECK_EQ(calculate("√(−4)").kind, QString("error"));
}

TEST("maths", "true or not") {
  CHECK_EQ(answer("3 + 4 = 7"), QString("✓"));
  CHECK_EQ(answer("3 + 4 = 8"), QString("≠ (7)"));
  CHECK_EQ(answer("0,1 + 0,2 = 0,3"), QString("✓"));
}

TEST("maths", "equations in one unknown") {
  CHECK_EQ(answer("3x + 5 = 20"), QString("x = 5"));
  CHECK_EQ(answer("2(x − 1) = x + 4"), QString("x = 6"));
  CHECK_EQ(answer("x² − 4x + 3 = 0"), QString("x = 1 ; x = 3"));
  CHECK_EQ(answer("x^2 = 2"), QString("x ≈ −1,414 ; x ≈ 1,414"));
  CHECK_EQ(answer("x² + 1 = 0"), QString("∅"));
  CHECK_EQ(answer("x + 1 = x + 1"), QString("ℝ"));
  CHECK_EQ(answer("x + 1 = x + 2"), QString("∅"));
  CHECK_EQ(answer("2t = 7"), QString("t = 3,5"));
  CHECK_EQ(answer("3x = 1"), QString("x = 1/3"));
  CHECK_EQ(answer("x/4 = 1,5"), QString("x = 6"));
  CHECK_EQ(answer("x³ = 8"), QString("x = 2"));
  CHECK_EQ(answer("2^x = 8"), QString("x = 3"));
  CHECK_EQ(answer("س + ٣ = ٥"), QString("x = 2"));
  CHECK_EQ(calculate("x + y = 3").kind, QString("error"));
}

TEST("maths", "expressions with an unknown are expanded") {
  CHECK_EQ(answer("(x + 1)²"), QString("x² + 2x + 1"));
  CHECK_EQ(answer("(x − 2)(x + 3)"), QString("x² + x − 6"));
  CHECK_EQ(calculate("f(x) = x² − 2x").kind, QString("function"));
}

TEST("maths", "functions for graphs") {
  const Function f = function("f(x) = x² − 2x");
  CHECK(f.ok());
  CHECK_CLOSE(f.at(3), 3, 9);
  const Function g = function("y = sin x");
  CHECK(g.ok());
  CHECK_CLOSE(g.at(M_PI / 2), 1, 9);
  CHECK(function("1/x").ok());
  CHECK(std::isnan(function("1/x").at(0)));
  CHECK(function("2x + 1").ok());
  CHECK_CLOSE(function("2x + 1").at(2), 5, 9);
  CHECK_CLOSE(function("g(t) = t^2").at(3), 9, 9);
  CHECK(!function("x + 1 = 3").ok());
  CHECK(!function("x + ").ok());
}

TEST("maths", "typeset") {
  CHECK_EQ(pretty("3*x^2-2x+1=0"), QString("3 × x² − 2x + 1 = 0"));
  CHECK_EQ(pretty("sqrt(16)+pi"), QString("√(16) + π"));
  CHECK_EQ(pretty("2,5*4"), QString("2,5 × 4"));
  CHECK_EQ(pretty("10^(-3)"), QString("10⁻³"));
  CHECK_EQ(pretty("-x+1"), QString("−x + 1"));
}

TEST("maths", "numbers are written as people do") {
  CHECK_EQ(format(2.5, true), QString("2,5"));
  CHECK_EQ(format(2.5, false), QString("2.5"));
  CHECK_EQ(format(-3, true), QString("−3"));
  CHECK_EQ(format(1.0 / 3, true), QString("1/3"));
  CHECK_EQ(format(M_PI, true), QString("3,1415927"));
  CHECK_EQ(format(1e20, true), QString("1e+20"));
}
