#include "MathExpr.h"

#include <QRegularExpression>
#include <algorithm>
#include <functional>
#include <cmath>
#include <set>

namespace mathx {

struct Node {
  /// 'n' number, 'v' the unknown, '+', '-', '*', '/', '^', '~' minus,
  /// 'f' function (name), '!' factorial, '%' percent, '|' absolute value, 'd' degrees.
  char op = 'n';
  double v = 0;
  QString name;
  NodeP a, b;
};

namespace {

NodeP num(double v) {
  auto n = std::make_shared<Node>();
  n->op = 'n';
  n->v = v;
  return n;
}
NodeP mk(char op, NodeP a, NodeP b = nullptr, QString name = {}) {
  auto n = std::make_shared<Node>();
  n->op = op;
  n->a = std::move(a);
  n->b = std::move(b);
  n->name = std::move(name);
  return n;
}

const QStringList& functionNames() {
  static const QStringList names = {"sin", "cos", "tan", "asin", "acos", "atan", "arcsin", "arccos", "arctan", "sqrt", "cbrt", "racine", "ln", "log", "exp",
                                    "abs", "floor", "ceil", "round", "sinh", "cosh", "tanh"};
  return names;
}

struct Token {
  /// 'n' number, 'i' identifier (a function, pi, e or one letter), or the character itself.
  char kind = 0;
  double v = 0;
  QString text;
};

std::vector<Token> tokenize(const QString& s, QString* error) {
  std::vector<Token> out;
  int i = 0;
  const int n = s.size();
  while (i < n) {
    const QChar c = s[i];
    if (c.isSpace()) {
      ++i;
      continue;
    }
    if (c.isDigit() || (c == '.' && i + 1 < n && s[i + 1].isDigit())) {
      int j = i;
      while (j < n && (s[j].isDigit() || s[j] == '.')) ++j;
      bool ok = false;
      const double v = s.mid(i, j - i).toDouble(&ok);
      if (!ok) {
        *error = "can't read the number " + s.mid(i, j - i);
        return {};
      }
      out.push_back({'n', v, s.mid(i, j - i)});
      i = j;
      continue;
    }
    if (c.isLetter()) {
      int j = i;
      while (j < n && s[j].isLetter() && s[j].unicode() < 128) ++j;
      if (j == i) {
        *error = QStringLiteral("can't read “%1”").arg(c);
        return {};
      }
      const QString word = s.mid(i, j - i).toLower();
      // Known words; anything else is one letter after another (2xy = 2·x·y).
      if (functionNames().contains(word) || word == "pi") {
        out.push_back({'i', 0, word});
      } else {
        // A known function at the start of a longer word ("sinx").
        bool split = false;
        for (const QString& f : functionNames())
          if (word.startsWith(f) && word.size() > f.size()) {
            out.push_back({'i', 0, f});
            for (const QChar& l : word.mid(f.size())) out.push_back({'i', 0, QString(l)});
            split = true;
            break;
          }
        if (!split)
          for (const QChar& l : word) out.push_back({'i', 0, QString(l)});
      }
      i = j;
      continue;
    }
    if (QStringLiteral("+-*/^()|!%=°").contains(c)) {
      out.push_back({char(c.unicode()), 0, QString(c)});
      ++i;
      continue;
    }
    *error = QStringLiteral("can't read “%1”").arg(c);
    return {};
  }
  return out;
}

struct Parser {
  std::vector<Token> t;
  size_t p = 0;
  QString error;
  std::set<QString> vars;

  bool at(char k) const { return p < t.size() && t[p].kind == k; }
  bool startsOperand() const {
    if (p >= t.size()) return false;
    const char k = t[p].kind;
    return k == 'n' || k == 'i' || k == '(' || k == '|';
  }
  NodeP fail(const QString& why) {
    if (error.isEmpty()) error = why;
    return num(NAN);
  }

  NodeP expr() {
    NodeP left = product();
    while (error.isEmpty() && (at('+') || at('-'))) {
      const char op = t[p++].kind;
      left = mk(op, left, product());
    }
    return left;
  }
  NodeP product() {
    NodeP left = unary();
    while (error.isEmpty()) {
      if (at('*') || at('/')) {
        const char op = t[p++].kind;
        left = mk(op, left, unary());
      } else if (startsOperand() && !(at('|') && absDepth > 0)) {
        // Written next to each other: 2x, 3(x + 1), x(x − 1).
        left = mk('*', left, power());
      } else {
        break;
      }
    }
    return left;
  }
  NodeP unary() {
    if (at('-')) {
      ++p;
      return mk('~', unary());
    }
    if (at('+')) {
      ++p;
      return unary();
    }
    return power();
  }
  NodeP power() {
    NodeP base = postfix();
    if (at('^')) {
      ++p;
      return mk('^', base, unary());
    }
    return base;
  }
  NodeP postfix() {
    NodeP v = primary();
    while (at('!') || at('%') || at(char(0xB0))) {
      const char k = t[p++].kind;
      v = mk(k == char(0xB0) ? 'd' : k, v);
    }
    return v;
  }
  int absDepth = 0;
  NodeP primary() {
    if (p >= t.size()) return fail("the expression ends too soon");
    const Token tok = t[p];
    if (tok.kind == 'n') {
      ++p;
      return num(tok.v);
    }
    if (tok.kind == '(') {
      ++p;
      NodeP inside = expr();
      if (!at(')')) return fail("a bracket isn't closed");
      ++p;
      return inside;
    }
    if (tok.kind == '|') {
      ++p;
      ++absDepth;
      NodeP inside = expr();
      --absDepth;
      if (!at('|')) return fail("an absolute value isn't closed");
      ++p;
      return mk('|', inside);
    }
    if (tok.kind == 'i') {
      ++p;
      if (tok.text == "pi") return num(M_PI);
      if (tok.text == "e") return num(M_E);
      if (functionNames().contains(tok.text)) {
        NodeP arg;
        if (at('(')) {
          ++p;
          arg = expr();
          if (!at(')')) return fail("a bracket isn't closed");
          ++p;
        } else {
          arg = power();  // sin x, √2
        }
        return mk('f', arg, nullptr, tok.text);
      }
      vars.insert(tok.text);
      auto v = std::make_shared<Node>();
      v->op = 'v';
      v->name = tok.text;
      return v;
    }
    return fail(QStringLiteral("unexpected “%1”").arg(tok.text));
  }
};

double factorial(double v) {
  if (v < 0 || v > 170 || std::abs(v - std::round(v)) > 1e-9) return std::tgamma(v + 1);
  double r = 1;
  for (int i = 2; i <= int(std::round(v)); ++i) r *= i;
  return r;
}

/// Parses an expression on its own; its letters go to `vars`.
NodeP parseExpr(const QString& normalized, QString* error, std::set<QString>* vars) {
  Parser ps;
  ps.t = tokenize(normalized, error);
  if (!error->isEmpty()) return nullptr;
  if (ps.t.empty()) {
    *error = "nothing to work out";
    return nullptr;
  }
  NodeP n = ps.expr();
  if (ps.error.isEmpty() && ps.p < ps.t.size()) ps.error = QStringLiteral("unexpected “%1”").arg(ps.t[ps.p].text);
  if (!ps.error.isEmpty()) {
    *error = ps.error;
    return nullptr;
  }
  vars->insert(ps.vars.begin(), ps.vars.end());
  return n;
}

bool nearInt(double v) { return std::abs(v - std::round(v)) < 1e-9 * std::max(1.0, std::abs(v)); }

}  // namespace

QString normalize(const QString& in) {
  QString s;
  s.reserve(in.size() + 8);
  static const QString sup = QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹");
  for (int i = 0; i < in.size(); ++i) {
    const QChar c = in[i];
    const ushort u = c.unicode();
    if (u >= 0x0660 && u <= 0x0669) s += QChar(char16_t(u'0' + (u - 0x0660)));
    else if (u >= 0x06F0 && u <= 0x06F9) s += QChar(char16_t(u'0' + (u - 0x06F0)));
    else if (u == 0x066B) s += '.';                                     // Arabic decimal separator
    else if (u == 0x066C || u == 0x060C) s += ',';                      // Arabic thousands separator, comma
    else if (QStringLiteral("×·⋅✕∙").contains(c)) s += '*';
    else if (QStringLiteral("÷:∕").contains(c)) s += '/';
    else if (QStringLiteral("−–—‐").contains(c)) s += '-';
    else if (c == QChar(0xFF1D)) s += '=';
    else if (c == QChar(0x221A)) s += QStringLiteral(" sqrt ");       // √
    else if (c == QChar(0x221B)) s += QStringLiteral(" cbrt ");       // ∛
    else if (c == QChar(0x03C0)) s += QStringLiteral(" pi ");         // π
    else if (u == 0x0633) s += 'x';                                     // س (Arabic school maths' x)
    else if (u == 0x0635) s += 'y';                                     // ص
    else if (sup.contains(c) || c == QChar(0x207B)) {
      // A run of superscripts is one exponent: x² → x^(2), 10⁻³ → 10^(-3).
      QString e;
      while (i < in.size() && (sup.contains(in[i]) || in[i] == QChar(0x207B))) {
        e += in[i] == QChar(0x207B) ? QChar('-') : QChar(char16_t(u'0' + sup.indexOf(in[i])));
        ++i;
      }
      --i;
      s += "^(" + e + ")";
    } else if (c == QChar(0x00BD)) s += QStringLiteral("(1/2)");
    else if (c == QChar(0x00BC)) s += QStringLiteral("(1/4)");
    else if (c == QChar(0x00BE)) s += QStringLiteral("(3/4)");
    else s += c;
  }
  // Decimal commas: 2,5 → 2.5 (a comma between digits).
  static const QRegularExpression decimalComma(QStringLiteral("(\\d),(\\d)"));
  s.replace(decimalComma, QStringLiteral("\\1.\\2"));
  return s;
}

namespace {

struct Definition {
  QString body, var;
  bool found = false;
};
/// "y = …", "f(x) = …".
Definition definitionOf(const QString& s) {
  static const QRegularExpression fx(QStringLiteral("^\\s*([a-zA-Z])\\s*\\(\\s*([a-zA-Z])\\s*\\)\\s*=(.*)$"));
  static const QRegularExpression y(QStringLiteral("^\\s*y\\s*=(.*)$"));
  if (auto m = fx.match(s); m.hasMatch()) return {m.captured(3), m.captured(2).toLower(), true};
  if (auto m = y.match(s); m.hasMatch()) return {m.captured(1), "x", true};
  return {};
}

}  // namespace

Parsed parse(const QString& text) {
  Parsed out;
  const QString s = normalize(text);
  std::set<QString> vars;
  const Definition def = definitionOf(s);
  if (def.found) {
    out.definition = true;
    out.lhs = parseExpr(def.body, &out.error, &vars);
    vars.erase(def.var);
    if (out.ok() && !vars.empty()) out.error = QStringLiteral("a function of %1 can only use %1").arg(def.var);
    out.var = def.var;
    return out;
  }
  const QStringList sides = s.split('=');
  if (sides.size() > 2) {
    out.error = "more than one “=”";
    return out;
  }
  out.lhs = parseExpr(sides[0], &out.error, &vars);
  if (!out.ok()) return out;
  if (sides.size() == 2) {
    out.equation = true;
    if (!sides[1].trimmed().isEmpty() && sides[1].trimmed() != "?") {
      out.rhs = parseExpr(sides[1], &out.error, &vars);
      if (!out.ok()) return out;
    }
  }
  if (vars.size() > 1) {
    QStringList names(vars.begin(), vars.end());
    out.error = "more than one unknown (" + names.join(", ") + ")";
    return out;
  }
  if (!vars.empty()) out.var = *vars.begin();
  return out;
}

double eval(const NodeP& n, double x) {
  if (!n) return NAN;
  switch (n->op) {
    case 'n': return n->v;
    case 'v': return x;
    case '+': return eval(n->a, x) + eval(n->b, x);
    case '-': return eval(n->a, x) - eval(n->b, x);
    case '*': return eval(n->a, x) * eval(n->b, x);
    case '/': {
      const double d = eval(n->b, x);
      return d == 0 ? NAN : eval(n->a, x) / d;
    }
    case '^': {
      const double b = eval(n->a, x), e = eval(n->b, x);
      // Odd roots of negatives: (−8)^(1/3) = −2.
      if (b < 0 && !nearInt(e)) {
        const double inv = 1 / e;
        if (nearInt(inv) && int(std::round(inv)) % 2) return -std::pow(-b, e);
      }
      return std::pow(b, e);
    }
    case '~': return -eval(n->a, x);
    case '!': return factorial(eval(n->a, x));
    case '%': return eval(n->a, x) / 100;
    case 'd': return eval(n->a, x) * M_PI / 180;
    case '|': return std::abs(eval(n->a, x));
    case 'f': {
      const double a = eval(n->a, x);
      const QString& f = n->name;
      if (f == "sin") return std::sin(a);
      if (f == "cos") return std::cos(a);
      if (f == "tan") return std::tan(a);
      if (f == "asin" || f == "arcsin") return std::asin(a);
      if (f == "acos" || f == "arccos") return std::acos(a);
      if (f == "atan" || f == "arctan") return std::atan(a);
      if (f == "sinh") return std::sinh(a);
      if (f == "cosh") return std::cosh(a);
      if (f == "tanh") return std::tanh(a);
      if (f == "sqrt" || f == "racine") return a < 0 ? NAN : std::sqrt(a);
      if (f == "cbrt") return std::cbrt(a);
      if (f == "ln") return a <= 0 ? NAN : std::log(a);
      if (f == "log") return a <= 0 ? NAN : std::log10(a);
      if (f == "exp") return std::exp(a);
      if (f == "abs") return std::abs(a);
      if (f == "floor") return std::floor(a);
      if (f == "ceil") return std::ceil(a);
      if (f == "round") return std::round(a);
      return NAN;
    }
  }
  return NAN;
}

namespace {

using Poly = std::vector<double>;
constexpr size_t kMaxDegree = 6;

Poly addP(const Poly& a, const Poly& b, double sign) {
  Poly r(std::max(a.size(), b.size()), 0.0);
  for (size_t i = 0; i < a.size(); ++i) r[i] += a[i];
  for (size_t i = 0; i < b.size(); ++i) r[i] += sign * b[i];
  return r;
}
std::optional<Poly> mulP(const Poly& a, const Poly& b) {
  if (a.size() + b.size() - 1 > kMaxDegree + 1) return std::nullopt;
  Poly r(a.size() + b.size() - 1, 0.0);
  for (size_t i = 0; i < a.size(); ++i)
    for (size_t j = 0; j < b.size(); ++j) r[i + j] += a[i] * b[j];
  return r;
}
bool constant(const Poly& p) {
  for (size_t i = 1; i < p.size(); ++i)
    if (std::abs(p[i]) > 1e-12) return false;
  return true;
}
/// Whether the unknown appears in `n`.
bool hasVar(const NodeP& n) { return n && (n->op == 'v' || hasVar(n->a) || hasVar(n->b)); }

}  // namespace

std::optional<std::vector<double>> polynomial(const NodeP& n) {
  if (!n) return std::nullopt;
  if (!hasVar(n)) {
    const double v = eval(n, 0);
    if (!std::isfinite(v)) return std::nullopt;
    return Poly{v};
  }
  switch (n->op) {
    case 'v': return Poly{0, 1};
    case '~': {
      auto a = polynomial(n->a);
      if (!a) return std::nullopt;
      for (double& c : *a) c = -c;
      return a;
    }
    case '+':
    case '-': {
      auto a = polynomial(n->a), b = polynomial(n->b);
      if (!a || !b) return std::nullopt;
      return addP(*a, *b, n->op == '+' ? 1 : -1);
    }
    case '*': {
      auto a = polynomial(n->a), b = polynomial(n->b);
      if (!a || !b) return std::nullopt;
      return mulP(*a, *b);
    }
    case '/': {
      auto a = polynomial(n->a), b = polynomial(n->b);
      if (!a || !b || !constant(*b) || (*b)[0] == 0) return std::nullopt;
      for (double& c : *a) c /= (*b)[0];
      return a;
    }
    case '^': {
      if (hasVar(n->b)) return std::nullopt;
      const double e = eval(n->b, 0);
      if (!nearInt(e) || e < 0 || e > double(kMaxDegree)) return std::nullopt;
      auto a = polynomial(n->a);
      if (!a) return std::nullopt;
      Poly r{1};
      for (int i = 0; i < int(std::round(e)); ++i) {
        auto m = mulP(r, *a);
        if (!m) return std::nullopt;
        r = *m;
      }
      return r;
    }
    default: return std::nullopt;
  }
}

QString format(double v, bool comma, bool fractions, int digits) {
  if (!std::isfinite(v)) return QStringLiteral("?");
  const QString minus = QStringLiteral("−");
  if (std::abs(v) < 1e-12) return QStringLiteral("0");
  const QString sign = v < 0 ? minus : QString();
  const double a = std::abs(v);
  if (nearInt(a) && a < 1e15) return sign + QString::number(qint64(std::round(a)));
  // A short decimal stays a decimal (2,5); otherwise a simple fraction (1/3).
  const double scaled = a * 10000;
  if (!(nearInt(scaled) && a >= 1e-4) && fractions) {
    for (int d = 2; d <= 64; ++d) {
      const double n = a * d;
      if (nearInt(n) && n < 1e9) return sign + QString::number(qint64(std::round(n))) + "/" + QString::number(d);
    }
  }
  QString s;
  if (a >= 1e-6 && a < 1e12) {
    const int whole = int(std::floor(std::log10(a))) + 1;
    const int decimals = std::clamp(digits - whole, 0, 12);
    s = QString::number(a, 'f', decimals);
    if (s.contains('.')) {
      while (s.endsWith('0')) s.chop(1);
      if (s.endsWith('.')) s.chop(1);
    }
  } else {
    s = QString::number(a, 'g', digits);
  }
  if (comma) s.replace('.', ',');
  return sign + s;
}

QString pretty(const QString& text) {
  const bool comma = QRegularExpression(QStringLiteral("\\d,\\d")).match(text).hasMatch() || !QRegularExpression(QStringLiteral("\\d\\.\\d")).match(text).hasMatch();
  QString err;
  const std::vector<Token> toks = tokenize(normalize(text), &err);
  if (!err.isEmpty()) return text.trimmed();
  static const QString supDigits = QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹");
  QString out;
  for (size_t i = 0; i < toks.size(); ++i) {
    const Token& t = toks[i];
    auto prevKind = [&]() { return i > 0 ? toks[i - 1].kind : char(0); };
    switch (t.kind) {
      case 'n': {
        QString s = t.text;
        if (comma) s.replace('.', ',');
        out += s;
        break;
      }
      case 'i':
        if (t.text == "pi") out += QChar(0x03C0);
        else if (t.text == "sqrt" || t.text == "racine") out += QChar(0x221A);
        else if (t.text == "cbrt") out += QChar(0x221B);
        else if (functionNames().contains(t.text)) out += t.text + (i + 1 < toks.size() && toks[i + 1].kind != '(' ? " " : "");
        else out += t.text;
        break;
      case '^': {
        // A whole-number exponent goes up: x² , 10⁻³ (with or without brackets).
        size_t j = i + 1;
        const bool bracket = j < toks.size() && toks[j].kind == '(';
        if (bracket) ++j;
        QString sup;
        if (j < toks.size() && toks[j].kind == '-') sup += QChar(0x207B), ++j;
        if (j < toks.size() && toks[j].kind == 'n' && !toks[j].text.contains('.')) {
          for (const QChar& d : toks[j].text) sup += supDigits[d.digitValue()];
          ++j;
          if (!bracket || (j < toks.size() && toks[j].kind == ')')) {
            out += sup;
            i = bracket ? j : j - 1;
            break;
          }
        }
        out += '^';
        break;
      }
      case '*': out += QStringLiteral(" × "); break;
      case '/': out += QStringLiteral(" / "); break;
      case '+': out += QStringLiteral(" + "); break;
      case '-': {
        // A sign (at the start, after an operator or a bracket) stays close.
        const char pk = prevKind();
        if (pk == 0 || QStringLiteral("+-*/^(=").contains(QChar(pk))) out += QChar(0x2212);
        else out += QStringLiteral(" − ");
        break;
      }
      case '=': out += QStringLiteral(" = "); break;
      default: out += t.text;
    }
  }
  return out.simplified();
}

Function function(const QString& text) {
  Function f;
  const Parsed p = parse(text);
  if (!p.ok()) {
    f.error = p.error;
    return f;
  }
  if (p.equation && !p.definition) {
    f.error = "write it as y = … or f(x) = …";
    return f;
  }
  f.body = p.lhs;
  if (!p.var.isEmpty()) f.var = p.var;
  return f;
}

namespace {

/// Roots of f in [lo, hi], numerically (sign changes, then bisection).
std::vector<double> numericRoots(const std::function<double(double)>& f, double lo, double hi) {
  std::vector<double> roots;
  const int steps = 20000;
  const double dx = (hi - lo) / steps;
  double x0 = lo, y0 = f(lo);
  for (int i = 1; i <= steps && roots.size() < 8; ++i) {
    const double x1 = lo + i * dx, y1 = f(x1);
    if (std::isfinite(y0) && std::isfinite(y1)) {
      if (y0 == 0) {
        roots.push_back(x0);
      } else if ((y0 < 0) != (y1 < 0)) {
        double a = x0, b = x1, fa = y0;
        for (int k = 0; k < 80; ++k) {
          const double m = (a + b) / 2, fm = f(m);
          if (!std::isfinite(fm)) break;
          if ((fm < 0) == (fa < 0)) a = m, fa = fm;
          else b = m;
        }
        const double r = (a + b) / 2;
        // A jump (1/x at 0, tan) isn't a root.
        if (std::abs(f(r)) < 1e-6 * std::max(1.0, std::abs(y0) + std::abs(y1))) roots.push_back(r);
      }
    }
    x0 = x1;
    y0 = y1;
  }
  // The same root found twice (a touch at y0 == 0 then a crossing).
  std::sort(roots.begin(), roots.end());
  roots.erase(std::unique(roots.begin(), roots.end(), [](double a, double b) { return std::abs(a - b) < 1e-7; }), roots.end());
  return roots;
}

bool exactish(double v) {
  // Whole numbers and simple fractions are shown exactly.
  for (int d = 1; d <= 64; ++d)
    if (nearInt(v * d)) return true;
  return false;
}

}  // namespace

Result calculate(const QString& text, int comma) {
  Result r;
  QString s = text.trimmed();
  bool useComma = true;
  if (comma >= 0) useComma = comma == 1;
  else if (QRegularExpression(QStringLiteral("\\d\\.\\d")).match(s).hasMatch() && !QRegularExpression(QStringLiteral("\\d,\\d")).match(s).hasMatch()) useComma = false;
  const Parsed p = parse(s);
  r.pretty = pretty(s);
  if (!p.ok()) {
    r.kind = "error";
    r.error = p.error;
    return r;
  }
  r.var = p.var;
  auto num = [&](double v) { return format(v, useComma); };
  // A fraction also as a decimal: 1/3 (≈ 0,3333).
  auto withDecimal = [&](double v) {
    const QString exact = format(v, useComma);
    return exact.contains('/') ? exact + " (≈ " + format(v, useComma, false, 4) + ")" : exact;
  };
  if (p.definition) {
    r.kind = "function";
    return r;
  }
  if (!p.rhs) {
    if (!p.var.isEmpty()) {
      // An expression in x: expanded when it's a polynomial.
      if (auto poly = polynomial(p.lhs)) {
        QStringList terms;
        for (int i = int(poly->size()) - 1; i >= 0; --i) {
          const double c = (*poly)[i];
          if (std::abs(c) < 1e-12) continue;
          QString coef = format(std::abs(c), useComma);
          QString term = i == 0 ? coef : (std::abs(std::abs(c) - 1) < 1e-12 ? QString() : coef) + p.var + (i > 1 ? pretty("^" + QString::number(i)) : QString());
          if (terms.isEmpty()) terms << (c < 0 ? QStringLiteral("−") : QString()) + term;
          else terms << (c < 0 ? QStringLiteral("− ") : QStringLiteral("+ ")) + term;
        }
        r.kind = "expanded";
        r.answer = terms.isEmpty() ? QStringLiteral("0") : terms.join(' ');
        return r;
      }
      r.kind = "error";
      r.error = "it has an unknown: write an equation (… = …) to solve it";
      return r;
    }
    const double v = eval(p.lhs);
    if (!std::isfinite(v)) {
      r.kind = "error";
      r.error = "it isn't defined (a division by zero, or the root of a negative number…)";
      return r;
    }
    r.kind = "value";
    r.values = {v};
    r.answer = withDecimal(v);
    return r;
  }
  if (p.var.isEmpty()) {
    // Both sides are numbers: true or not.
    const double a = eval(p.lhs), b = eval(p.rhs);
    if (!std::isfinite(a) || !std::isfinite(b)) {
      r.kind = "error";
      r.error = "it isn't defined";
      return r;
    }
    r.kind = "check";
    r.values = {a};
    r.answer = std::abs(a - b) <= 1e-9 * std::max(1.0, std::max(std::abs(a), std::abs(b))) ? QStringLiteral("✓") : QStringLiteral("≠ (") + num(a) + ")";
    return r;
  }
  // An equation in one unknown.
  const NodeP diff = mk('-', p.lhs, p.rhs);
  std::vector<double> sols;
  bool approx = false;
  if (auto poly = polynomial(diff)) {
    Poly c = *poly;
    while (c.size() > 1 && std::abs(c.back()) < 1e-12) c.pop_back();
    if (c.size() == 1) {
      r.kind = std::abs(c[0]) < 1e-12 ? "all" : "none";
      r.answer = r.kind == "all" ? QStringLiteral("ℝ") : QStringLiteral("∅");
      return r;
    }
    if (c.size() == 2) {
      sols = {-c[0] / c[1]};
    } else if (c.size() == 3) {
      const double a = c[2], b = c[1], k = c[0];
      const double disc = b * b - 4 * a * k;
      if (disc < -1e-12) {
        r.kind = "none";
        r.answer = QStringLiteral("∅");
        return r;
      }
      if (std::abs(disc) <= 1e-12) sols = {-b / (2 * a)};
      else sols = {(-b - std::sqrt(disc)) / (2 * a), (-b + std::sqrt(disc)) / (2 * a)};
    } else {
      const NodeP d = diff;
      sols = numericRoots([d](double x) { return eval(d, x); }, -100, 100);
    }
  } else {
    const NodeP d = diff;
    sols = numericRoots([d](double x) { return eval(d, x); }, -100, 100);
  }
  std::sort(sols.begin(), sols.end());
  if (sols.empty()) {
    r.kind = "none";
    r.answer = QStringLiteral("∅");
    return r;
  }
  QStringList parts;
  for (double v : sols) {
    const bool exact = exactish(v);
    approx = approx || !exact;
    parts << p.var + (exact ? QStringLiteral(" = ") + format(v, useComma) : QStringLiteral(" ≈ ") + format(v, useComma, false, 4));
  }
  r.kind = "solutions";
  r.values = sols;
  r.answer = parts.join(QStringLiteral(" ; "));
  return r;
}

}  // namespace mathx
