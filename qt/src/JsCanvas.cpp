#include "JsCanvas.h"

#include <QBuffer>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QJSEngine>
#include <QGlyphRun>
#include <QRawFont>
#include <QPainterPathStroker>
#include <QRegularExpression>
#include <QTextLayout>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kDpm = 96 / 0.0254;  // 96 dpi: CSS px = 0.75 pt

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

/// Three box blurs ≈ a Gaussian of `sigma` over an alpha image.
void blurAlpha(QImage& img, double sigma) {
  const int r = std::max(1, int(sigma * 0.94 + 0.5));
  const int w = img.width(), h = img.height();
  std::vector<float> a(size_t(w) * h), t(a.size());
  for (int y = 0; y < h; ++y) {
    const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
    for (int x = 0; x < w; ++x) a[size_t(y) * w + x] = qAlpha(line[x]);
  }
  const float k = 1.0f / (2 * r + 1);
  for (int pass = 0; pass < 3; ++pass) {
    for (int y = 0; y < h; ++y) {
      float s = 0;
      for (int x = -r; x <= r; ++x) s += a[size_t(y) * w + std::clamp(x, 0, w - 1)];
      for (int x = 0; x < w; ++x) {
        t[size_t(y) * w + x] = s * k;
        s += a[size_t(y) * w + std::min(x + r + 1, w - 1)] - a[size_t(y) * w + std::max(x - r, 0)];
      }
    }
    for (int x = 0; x < w; ++x) {
      float s = 0;
      for (int y = -r; y <= r; ++y) s += t[size_t(std::clamp(y, 0, h - 1)) * w + x];
      for (int y = 0; y < h; ++y) {
        a[size_t(y) * w + x] = s * k;
        s += t[size_t(std::min(y + r + 1, h - 1)) * w + x] - t[size_t(std::max(y - r, 0)) * w + x];
      }
    }
  }
  for (int y = 0; y < h; ++y) {
    QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
    for (int x = 0; x < w; ++x) {
      const QRgb c = line[x];
      const int na = std::clamp(int(a[size_t(y) * w + x] + 0.5f), 0, 255);
      const int oa = qAlpha(c);
      // Premultiplied: scale the colour with the new alpha.
      if (oa == 0) line[x] = qRgba(0, 0, 0, 0);
      else line[x] = qRgba(qRed(c) * na / oa, qGreen(c) * na / oa, qBlue(c) * na / oa, na);
    }
  }
}

}  // namespace

// --- gradients -------------------------------------------------------------------

void JsGradient::addColorStop(double offset, const QString& color) { m_g->setColorAt(clamp01(offset), JsCanvas::parseColor(color)); }

// --- colours and fonts -------------------------------------------------------------

QColor JsCanvas::parseColor(const QString& input) {
  const QString css = input.trimmed().toLower();
  if (css.isEmpty() || css == "transparent") return QColor(0, 0, 0, 0);
  if (css.startsWith('#')) {
    const QString hex = css.mid(1);
    bool ok = false;
    if (hex.size() == 8 || hex.size() == 4) {
      // CSS puts alpha last (#rrggbbaa); Qt would read it first.
      const QString full = hex.size() == 4 ? QString("%1%1%2%2%3%3%4%4").arg(hex[0]).arg(hex[1]).arg(hex[2]).arg(hex[3]) : hex;
      const uint v = full.toUInt(&ok, 16);
      if (ok) return QColor((v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255);
    }
    QColor c(css);
    return c.isValid() ? c : QColor(Qt::black);
  }
  static const QRegularExpression fn(R"(^(rgba?|hsla?)\((.*)\)$)");
  const auto m = fn.match(css);
  if (m.hasMatch()) {
    QString body = m.captured(2);
    body.replace('/', ' ').replace(',', ' ');
    const QStringList parts = body.split(' ', Qt::SkipEmptyParts);
    auto num = [&](int i, double scale) -> double {
      if (i >= parts.size()) return i == 3 ? 1.0 : 0.0;
      QString p = parts[i];
      if (p.endsWith('%')) return p.chopped(1).toDouble() / 100.0 * scale;
      if (p.endsWith("deg")) p.chop(3);
      return p.toDouble();
    };
    double alpha = num(3, 1);
    if (parts.size() > 3 && !parts[3].endsWith('%') && alpha > 1) alpha = 1;
    // Alpha in 8 bits, as browsers keep it (faint layers add up the same).
    alpha = std::round(clamp01(alpha) * 255) / 255;
    if (m.captured(1).startsWith("rgb")) {
      return QColor::fromRgbF(float(clamp01(num(0, 255) / 255)), float(clamp01(num(1, 255) / 255)), float(clamp01(num(2, 255) / 255)), float(clamp01(alpha)));
    }
    double hue = std::fmod(num(0, 360), 360.0);
    if (hue < 0) hue += 360;
    return QColor::fromHslF(float(hue / 360.0), float(clamp01(num(1, 1))), float(clamp01(num(2, 1))), float(clamp01(alpha)));
  }
  QColor c = QColor::fromString(css);
  return c.isValid() ? c : QColor(Qt::black);
}

QFont JsCanvas::parseFont(const QString& css) {
  static QHash<QString, QFont> cache;
  if (auto it = cache.constFind(css); it != cache.constEnd()) return *it;
  QFont f;
  f.setHintingPreference(QFont::PreferNoHinting);
  f.setStyleStrategy(QFont::PreferAntialias);
  // [style] [variant] [weight] size[/line-height] family, family…
  static const QRegularExpression sizeRe(R"((\d*\.?\d+)(px|pt|em|rem)(/\S+)?)");
  const auto m = sizeRe.match(css);
  double px = 10;
  QString families = QStringLiteral("sans-serif");
  if (m.hasMatch()) {
    px = m.captured(1).toDouble();
    if (m.captured(2) == "pt") px = px * 96 / 72;
    else if (m.captured(2) != "px") px *= 16;
    const QString before = css.left(m.capturedStart());
    families = css.mid(m.capturedEnd()).trimmed();
    for (const QString& tok : before.split(' ', Qt::SkipEmptyParts)) {
      const QString t = tok.toLower();
      if (t == "italic" || t == "oblique") f.setItalic(true);
      else if (t == "bold" || t == "bolder") f.setWeight(QFont::Bold);
      else if (t == "lighter") f.setWeight(QFont::Light);
      else if (t == "small-caps") f.setCapitalization(QFont::SmallCaps);
      else {
        bool ok = false;
        const int w = t.toInt(&ok);
        if (ok && w >= 1 && w <= 1000) f.setWeight(QFont::Weight(std::clamp(w, 100, 900)));
      }
    }
  }
  f.setPointSizeF(std::max(0.5, px * 0.75));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  // Kerning for every script (browsers kern Arabic too).
  f.setFeature(QFont::Tag("kern"), 1);
#endif
  QStringList list;
  for (QString fam : families.split(',', Qt::SkipEmptyParts)) {
    fam = fam.trimmed();
    if (fam.startsWith('"') || fam.startsWith('\'')) fam = fam.mid(1, fam.size() - 2);
    if (fam == "sans-serif" || fam == "system-ui" || fam == "-apple-system") fam = "Inter";
    else if (fam == "serif") fam = "Lora";
    else if (fam == "cursive") fam = "Playpen Sans";
    else if (fam == "monospace") fam = "monospace";
    if (!fam.isEmpty() && !list.contains(fam)) {
      list << fam;
      // The face's extended-Latin letters (a separate file, see scripts/qt-fonts.py).
      if (QFontDatabase::hasFamily(fam + " Ext")) list << fam + " Ext";
    }
  }
  if (!list.contains("Inter")) list << "Inter";
  f.setFamilies(list);
  cache.insert(css, f);
  return f;
}

// --- the canvas ---------------------------------------------------------------------

JsCanvas::JsCanvas(int w, int h, QObject* parent) : QObject(parent) {
  m_states.emplace_back();
  m_states.back().font = parseFont(m_states.back().fontCss);
  reset(w, h);
}

JsCanvas::~JsCanvas() {
  if (m_painter)
    for (size_t i = 1; i < m_states.size(); ++i) m_painter->restore();
  m_painter.reset();
}

void JsCanvas::reset(int w, int h) {
  if (m_painter)
    for (size_t i = 1; i < m_states.size(); ++i) m_painter->restore();
  m_painter.reset();
  m_img = QImage(std::clamp(w, 1, 16384), std::clamp(h, 1, 16384), QImage::Format_ARGB32_Premultiplied);
  m_img.fill(Qt::transparent);
  m_img.setDotsPerMeterX(int(kDpm));
  m_img.setDotsPerMeterY(int(kDpm));
  m_states.clear();
  m_states.emplace_back();
  m_states.back().font = parseFont(m_states.back().fontCss);
  beginPath();
  m_hasPoint = false;
}

QImage JsCanvas::image() {
  // Ends painting (it starts again on the next call), balancing saved states.
  if (m_painter)
    for (size_t i = 1; i < m_states.size(); ++i) m_painter->restore();
  m_painter.reset();
  return m_img;
}

QPainter& JsCanvas::painter() {
  if (!m_painter) {
    m_painter = std::make_unique<QPainter>(&m_img);
    m_painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    // Reapply the clips of saved states (painting restarted after image()).
    int depth = 0;
    for (const State& s : m_states) {
      Q_UNUSED(s);
      if (depth++) m_painter->save();
    }
  }
  return *m_painter;
}

void JsCanvas::save() {
  QPainter& p = painter();  // create first: a new painter replays the saved states
  m_states.push_back(m_states.back());
  p.save();
}

void JsCanvas::restore() {
  if (m_states.size() <= 1) return;
  QPainter& p = painter();
  m_states.pop_back();
  p.restore();
}

void JsCanvas::translate(double x, double y) { st().ctm = QTransform::fromTranslate(x, y) * st().ctm; }
void JsCanvas::rotate(double a) {
  QTransform r;
  r.rotateRadians(a);
  st().ctm = r * st().ctm;
}
void JsCanvas::scale(double x, double y) { st().ctm = QTransform::fromScale(x, y) * st().ctm; }
void JsCanvas::transform(double a, double b, double c, double d, double e, double f) { st().ctm = QTransform(a, b, c, d, e, f) * st().ctm; }
void JsCanvas::setTransform(double a, double b, double c, double d, double e, double f) { st().ctm = QTransform(a, b, c, d, e, f); }
QVariantList JsCanvas::getTransformList() const {
  const QTransform& t = st().ctm;
  return {t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()};
}

// --- paths ---------------------------------------------------------------------------

void JsCanvas::moveTo(double x, double y) {
  m_path.moveTo(st().ctm.map(QPointF(x, y)));
  m_hasPoint = true;
}
void JsCanvas::ensurePoint(double x, double y) {
  if (!m_hasPoint) moveTo(x, y);
}
void JsCanvas::lineTo(double x, double y) {
  if (!m_hasPoint) return moveTo(x, y);
  m_path.lineTo(st().ctm.map(QPointF(x, y)));
}
void JsCanvas::closePath() {
  if (m_hasPoint) m_path.closeSubpath();
}
void JsCanvas::quadraticCurveTo(double cx, double cy, double x, double y) {
  ensurePoint(cx, cy);
  m_path.quadTo(st().ctm.map(QPointF(cx, cy)), st().ctm.map(QPointF(x, y)));
}
void JsCanvas::bezierCurveTo(double c1x, double c1y, double c2x, double c2y, double x, double y) {
  ensurePoint(c1x, c1y);
  const QTransform& t = st().ctm;
  m_path.cubicTo(t.map(QPointF(c1x, c1y)), t.map(QPointF(c2x, c2y)), t.map(QPointF(x, y)));
}

void JsCanvas::arc(double x, double y, double r, double a0, double a1, bool ccw) { ellipse(x, y, r, r, 0, a0, a1, ccw); }

void JsCanvas::ellipse(double x, double y, double rx, double ry, double rot, double a0, double a1, bool ccw) {
  if (rx < 0 || ry < 0 || !std::isfinite(rx + ry + a0 + a1)) return;
  // The sweep, as the canvas spec defines it.
  const double tau = 2 * M_PI;
  double sweep;
  if (!ccw) {
    sweep = a1 - a0;
    if (sweep >= tau) sweep = tau;
    else {
      sweep = std::fmod(sweep, tau);
      if (sweep < 0) sweep += tau;
    }
  } else {
    sweep = a0 - a1;
    if (sweep >= tau) sweep = tau;
    else {
      sweep = std::fmod(sweep, tau);
      if (sweep < 0) sweep += tau;
    }
    sweep = -sweep;
  }
  // The arc in its own space (centre, rotation), then through the transform.
  QTransform local;
  local.translate(x, y);
  local.rotateRadians(rot);
  const QTransform t = local * st().ctm;
  const QPointF start = t.map(QPointF(rx * std::cos(a0), ry * std::sin(a0)));
  if (m_hasPoint) m_path.lineTo(start);
  else m_path.moveTo(start);
  m_hasPoint = true;
  // Short line pieces are exact under any transform (and cheap at page sizes).
  const double len = std::max(rx, ry) * std::abs(sweep) * std::sqrt(std::abs(t.determinant()));
  const int n = std::clamp(int(len / 2.0), 8, 720);
  for (int i = 1; i <= n; ++i) {
    const double a = a0 + sweep * i / n;
    m_path.lineTo(t.map(QPointF(rx * std::cos(a), ry * std::sin(a))));
  }
}

void JsCanvas::arcTo(double x1, double y1, double x2, double y2, double r) {
  // Device space is fine for the tangent points when the transform keeps
  // circles round (the drawing code only uses uniform scales).
  if (!m_hasPoint) return moveTo(x1, y1);
  const QTransform inv = st().ctm.inverted();
  const QPointF p0 = inv.map(m_path.currentPosition());
  const QPointF p1(x1, y1), p2(x2, y2);
  const QPointF v0 = p0 - p1, v2 = p2 - p1;
  const double l0 = std::hypot(v0.x(), v0.y()), l2 = std::hypot(v2.x(), v2.y());
  if (r <= 0 || l0 < 1e-9 || l2 < 1e-9) return lineTo(x1, y1);
  const double cosA = (v0.x() * v2.x() + v0.y() * v2.y()) / (l0 * l2);
  const double angle = std::acos(std::clamp(cosA, -1.0, 1.0));
  if (angle < 1e-6 || std::abs(angle - M_PI) < 1e-6) return lineTo(x1, y1);
  const double d = r / std::tan(angle / 2);
  const QPointF t0 = p1 + v0 / l0 * d, t2 = p1 + v2 / l2 * d;
  const QPointF bis = (v0 / l0 + v2 / l2);
  const double bl = std::hypot(bis.x(), bis.y());
  const QPointF c = p1 + bis / bl * (r / std::sin(angle / 2));
  const double a0 = std::atan2(t0.y() - c.y(), t0.x() - c.x());
  const double a2 = std::atan2(t2.y() - c.y(), t2.x() - c.x());
  const double cross = v0.x() * v2.y() - v0.y() * v2.x();
  lineTo(t0.x(), t0.y());
  arc(c.x(), c.y(), r, a0, a2, cross > 0);
}

void JsCanvas::rect(double x, double y, double w, double h) {
  const QTransform& t = st().ctm;
  m_path.moveTo(t.map(QPointF(x, y)));
  m_path.lineTo(t.map(QPointF(x + w, y)));
  m_path.lineTo(t.map(QPointF(x + w, y + h)));
  m_path.lineTo(t.map(QPointF(x, y + h)));
  m_path.closeSubpath();
  m_path.moveTo(t.map(QPointF(x, y)));
  m_hasPoint = true;
}

void JsCanvas::roundRect(double x, double y, double w, double h, const QJSValue& radii) {
  double r = 0;
  if (radii.isNumber()) r = radii.toNumber();
  else if (radii.isArray() && radii.property("length").toInt() > 0) r = radii.property(0).toNumber();
  r = std::clamp(r, 0.0, std::min(std::abs(w), std::abs(h)) / 2);
  if (r <= 0) return rect(x, y, w, h);
  moveTo(x + r, y);
  lineTo(x + w - r, y);
  arc(x + w - r, y + r, r, -M_PI / 2, 0);
  lineTo(x + w, y + h - r);
  arc(x + w - r, y + h - r, r, 0, M_PI / 2);
  lineTo(x + r, y + h);
  arc(x + r, y + h - r, r, M_PI / 2, M_PI);
  lineTo(x, y + r);
  arc(x + r, y + r, r, M_PI, M_PI * 1.5);
  closePath();
  moveTo(x, y);
}

// --- painting -----------------------------------------------------------------------

QBrush JsCanvas::brushFor(const QJSValue& v, bool* userSpace) {
  *userSpace = false;
  if (v.isQObject()) {
    QObject* o = v.toQObject();
    if (auto* g = qobject_cast<JsGradient*>(o)) {
      *userSpace = true;
      return QBrush(g->gradient());
    }
    if (auto* p = qobject_cast<JsPattern*>(o)) {
      *userSpace = true;
      return QBrush(p->image());
    }
  }
  return QBrush(parseColor(v.toString()));
}

void JsCanvas::setFillStyle(const QJSValue& v) {
  if (!v.isString() && !v.isQObject()) return;
  st().fillJs = v;
  st().fill = brushFor(v, &st().fillUser);
}
void JsCanvas::setStrokeStyle(const QJSValue& v) {
  if (!v.isString() && !v.isQObject()) return;
  st().strokeJs = v;
  st().strokeBrush = brushFor(v, &st().strokeUser);
}

QString JsCanvas::lineCap() const { return st().cap == Qt::RoundCap ? "round" : st().cap == Qt::SquareCap ? "square" : "butt"; }
void JsCanvas::setLineCap(const QString& c) { st().cap = c == "round" ? Qt::RoundCap : c == "square" ? Qt::SquareCap : Qt::FlatCap; }
QString JsCanvas::lineJoin() const { return st().join == Qt::RoundJoin ? "round" : st().join == Qt::BevelJoin ? "bevel" : "miter"; }
void JsCanvas::setLineJoin(const QString& j) { st().join = j == "round" ? Qt::RoundJoin : j == "bevel" ? Qt::BevelJoin : Qt::SvgMiterJoin; }

void JsCanvas::setFont(const QString& css) {
  st().fontCss = css;
  st().font = parseFont(css);
  if (st().letterSpacing) st().font.setLetterSpacing(QFont::AbsoluteSpacing, st().letterSpacing);
}

void JsCanvas::setLetterSpacing(const QString& s) {
  st().letterSpacingCss = s;
  QString v = s.trimmed();
  if (v.endsWith("px")) v.chop(2);
  st().letterSpacing = v.toDouble();
  st().font.setLetterSpacing(QFont::AbsoluteSpacing, st().letterSpacing);
}

void JsCanvas::setComposite(const QString& c) {
  static const QHash<QString, QPainter::CompositionMode> modes = {
      {"source-over", QPainter::CompositionMode_SourceOver},
      {"source-in", QPainter::CompositionMode_SourceIn},
      {"source-out", QPainter::CompositionMode_SourceOut},
      {"source-atop", QPainter::CompositionMode_SourceAtop},
      {"destination-over", QPainter::CompositionMode_DestinationOver},
      {"destination-in", QPainter::CompositionMode_DestinationIn},
      {"destination-out", QPainter::CompositionMode_DestinationOut},
      {"destination-atop", QPainter::CompositionMode_DestinationAtop},
      {"copy", QPainter::CompositionMode_Source},
      {"xor", QPainter::CompositionMode_Xor},
      {"lighter", QPainter::CompositionMode_Plus},
      {"multiply", QPainter::CompositionMode_Multiply},
      {"screen", QPainter::CompositionMode_Screen},
      {"overlay", QPainter::CompositionMode_Overlay},
      {"darken", QPainter::CompositionMode_Darken},
      {"lighten", QPainter::CompositionMode_Lighten},
      {"color-dodge", QPainter::CompositionMode_ColorDodge},
      {"color-burn", QPainter::CompositionMode_ColorBurn},
      {"hard-light", QPainter::CompositionMode_HardLight},
      {"soft-light", QPainter::CompositionMode_SoftLight},
      {"difference", QPainter::CompositionMode_Difference},
      {"exclusion", QPainter::CompositionMode_Exclusion},
  };
  if (auto it = modes.constFind(c); it != modes.constEnd()) {
    st().compositeName = c;
    st().composite = *it;
  }
}

void JsCanvas::setShadowColor(const QString& c) { st().shadowColor = parseColor(c); }

void JsCanvas::setLineDash(const QVariantList& dashes) {
  QVector<qreal> d;
  for (const QVariant& v : dashes) {
    const double x = v.toDouble();
    if (!std::isfinite(x) || x < 0) return;
    d << x;
  }
  if (d.size() % 2) d += d;
  bool allZero = std::all_of(d.begin(), d.end(), [](qreal x) { return x == 0; });
  st().dash = allZero ? QVector<qreal>() : d;
}
QVariantList JsCanvas::getLineDash() const {
  QVariantList out;
  for (qreal d : st().dash) out << d;
  return out;
}

QPen JsCanvas::strokePen() const {
  const State& s = st();
  QPen pen(s.strokeBrush, s.lineWidth, Qt::SolidLine, s.cap, s.join);
  pen.setMiterLimit(s.miter / 2);  // Qt measures it in half widths
  if (!s.dash.isEmpty()) {
    // Qt's dash lengths are in line widths.
    QVector<qreal> d;
    for (qreal x : s.dash) d << std::max(1e-3, x / s.lineWidth);
    pen.setDashPattern(d);
    pen.setDashOffset(s.dashOffset / s.lineWidth);
  }
  return pen;
}

void JsCanvas::drawShadow(const QPainterPath& path) {
  const State& s = st();
  if (s.shadowColor.alpha() == 0 || (s.shadowBlur <= 0 && s.shadowOffset.isNull())) return;
  const double sigma = s.shadowBlur / 2;
  const int pad = int(std::ceil(sigma * 3)) + 2;
  const QRect box = path.boundingRect().toAlignedRect().adjusted(-pad, -pad, pad, pad);
  if (box.width() <= 0 || box.height() <= 0 || box.width() > 8192 || box.height() > 8192) return;
  QImage img(box.size(), QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  {
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(-box.topLeft());
    p.fillPath(path, s.shadowColor);
  }
  if (sigma > 0.3) blurAlpha(img, sigma);
  QPainter& p = painter();
  p.setTransform(QTransform());
  p.setOpacity(s.alpha);
  p.setCompositionMode(s.composite);
  p.drawImage(box.topLeft() + s.shadowOffset, img);
}

void JsCanvas::fillDevice(const QPainterPath& path, const QBrush& b, bool userSpace) {
  QPainter& p = painter();
  drawShadow(path);
  QBrush brush = b;
  if (userSpace) brush.setTransform(st().ctm);
  p.setTransform(QTransform());
  p.setOpacity(st().alpha);
  p.setCompositionMode(st().composite);
  p.fillPath(path, brush);
}

void JsCanvas::fill(const QString& rule) {
  QPainterPath path = m_path;
  path.setFillRule(rule == "evenodd" ? Qt::OddEvenFill : Qt::WindingFill);
  fillDevice(path, st().fill, st().fillUser);
}

void JsCanvas::stroke() {
  // Stroked in user space (so the line width follows the transform), then drawn.
  const QTransform& t = st().ctm;
  if (!t.isInvertible()) return;
  const QPainterPath user = t.inverted().map(m_path);
  QPainterPathStroker stroker(strokePen());
  QPainterPath outline = t.map(stroker.createStroke(user));
  outline.setFillRule(Qt::WindingFill);
  fillDevice(outline, st().strokeBrush, st().strokeUser);
}

void JsCanvas::clip(const QString& rule) {
  QPainterPath path = m_path;
  path.setFillRule(rule == "evenodd" ? Qt::OddEvenFill : Qt::WindingFill);
  QPainter& p = painter();
  p.setTransform(QTransform());
  p.setClipPath(path, p.hasClipping() ? Qt::IntersectClip : Qt::ReplaceClip);
}

void JsCanvas::fillRect(double x, double y, double w, double h) {
  QPainterPath path;
  path.addPolygon(st().ctm.map(QPolygonF(QRectF(x, y, w, h))));
  path.closeSubpath();
  fillDevice(path, st().fill, st().fillUser);
}

void JsCanvas::strokeRect(double x, double y, double w, double h) {
  const QPainterPath saved = m_path;
  const bool had = m_hasPoint;
  beginPath();
  rect(x, y, w, h);
  stroke();
  m_path = saved;
  m_hasPoint = had;
}

void JsCanvas::clearRect(double x, double y, double w, double h) {
  QPainter& p = painter();
  p.setTransform(st().ctm);
  p.setOpacity(1);
  p.setCompositionMode(QPainter::CompositionMode_Clear);
  p.fillRect(QRectF(x, y, w, h), Qt::transparent);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

// --- text -----------------------------------------------------------------------------

namespace {
struct Laid {
  std::unique_ptr<QTextLayout> layout;
  double width = 0, ascent = 0, descent = 0;
};
Laid layOut(const QString& text, const QFont& font, bool rtl) {
  Laid l;
  l.layout = std::make_unique<QTextLayout>(text, font);
  QTextOption opt;
  opt.setWrapMode(QTextOption::NoWrap);
  opt.setTextDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
  opt.setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);
  l.layout->setTextOption(opt);
  l.layout->beginLayout();
  QTextLine line = l.layout->createLine();
  if (line.isValid()) {
    line.setLineWidth(1e7);
    line.setPosition(QPointF(0, 0));
    l.width = line.naturalTextWidth();
    l.ascent = line.ascent();
    l.descent = line.descent();
  }
  l.layout->endLayout();
  return l;
}
}  // namespace

void JsCanvas::drawText(const QString& text, double x, double y, bool strokeIt, const QJSValue& maxWidth) {
  if (text.isEmpty()) return;
  const State& s = st();
  const bool rtl = s.direction == "rtl";
  Laid l = layOut(text, s.font, rtl);
  const QFontMetricsF fm(s.font, &m_img);
  const double em = fm.ascent() + fm.descent();
  const double px = s.font.pointSizeF() / 0.75;
  // Horizontal anchor.
  QString align = s.align;
  if (align == "start") align = rtl ? "right" : "left";
  else if (align == "end") align = rtl ? "left" : "right";
  double w = l.width;
  double sx = 1;
  if (maxWidth.isNumber() && maxWidth.toNumber() > 0 && w > maxWidth.toNumber()) {
    sx = maxWidth.toNumber() / w;
    w = maxWidth.toNumber();
  }
  double left = align == "right" ? x - w : align == "center" ? x - w / 2 : x;
  // Vertical anchor: where the baseline goes.
  const double a = fm.ascent(), d = fm.descent();
  double base = y;
  const QString& b = s.baseline;
  if (b == "top") base = y + px * a / em;
  else if (b == "hanging") base = y + px * a / em * 0.8;
  else if (b == "middle") base = y + px * a / em - px / 2;
  else if (b == "bottom" || b == "ideographic") base = y - px * d / em;
  QPainter& p = painter();
  QTransform t = st().ctm;
  t = QTransform::fromScale(sx, 1) * QTransform::fromTranslate(left, base - l.ascent) * t;
  if (s.shadowColor.alpha() && (s.shadowBlur > 0 || !s.shadowOffset.isNull())) {
    QPainterPath glyphs;
    QTextLine line = l.layout->lineAt(0);
    for (const QGlyphRun& run : line.glyphRuns()) {
      const QRawFont raw = run.rawFont();
      const auto idx = run.glyphIndexes();
      const auto pos = run.positions();
      for (int i = 0; i < idx.size(); ++i) glyphs.addPath(raw.pathForGlyph(idx[i]).translated(pos[i]));
    }
    drawShadow(t.map(glyphs));
  }
  p.setTransform(t);
  p.setOpacity(s.alpha);
  p.setCompositionMode(s.composite);
  if (strokeIt) {
    QPainterPath glyphs;
    QTextLine line = l.layout->lineAt(0);
    for (const QGlyphRun& run : line.glyphRuns()) {
      const QRawFont raw = run.rawFont();
      const auto idx = run.glyphIndexes();
      const auto pos = run.positions();
      for (int i = 0; i < idx.size(); ++i) glyphs.addPath(raw.pathForGlyph(idx[i]).translated(pos[i]));
    }
    p.strokePath(glyphs, strokePen());
    return;
  }
  QBrush brush = s.fill;
  if (s.fillUser) brush.setTransform(t.inverted() * st().ctm);
  p.setPen(QPen(brush, 0));
  l.layout->draw(&p, QPointF(0, 0));
}

void JsCanvas::fillText(const QString& text, double x, double y, const QJSValue& maxWidth) { drawText(text, x, y, false, maxWidth); }
void JsCanvas::strokeText(const QString& text, double x, double y, const QJSValue& maxWidth) { drawText(text, x, y, true, maxWidth); }

QVariantMap JsCanvas::measureText(const QString& text) {
  const State& s = st();
  Laid l = layOut(text, s.font, s.direction == "rtl");
  const QFontMetricsF fm(s.font, &m_img);
  const QRectF tight = fm.tightBoundingRect(text);
  const double px = s.font.pointSizeF() / 0.75;
  const double em = fm.ascent() + fm.descent();
  // Distances from the current baseline setting, like the browser.
  double shift = 0;
  if (s.baseline == "top") shift = px * fm.ascent() / em;
  else if (s.baseline == "middle") shift = px * fm.ascent() / em - px / 2;
  else if (s.baseline == "bottom" || s.baseline == "ideographic") shift = -px * fm.descent() / em;
  return {
      {"width", l.width},
      {"actualBoundingBoxAscent", -tight.top() - shift},
      {"actualBoundingBoxDescent", tight.bottom() + shift},
      {"actualBoundingBoxLeft", -tight.left()},
      {"actualBoundingBoxRight", tight.right()},
      {"fontBoundingBoxAscent", px * fm.ascent() / em - shift},
      {"fontBoundingBoxDescent", px * fm.descent() / em + shift},
  };
}

// --- images -----------------------------------------------------------------------------

void JsCanvas::drawImage(QObject* source, double a, double b, const QJSValue& c, const QJSValue& d, const QJSValue& e, const QJSValue& f, const QJSValue& g,
                         const QJSValue& h) {
  QImage img;
  if (auto* cv = qobject_cast<JsCanvas*>(source)) img = cv == this ? m_img.copy() : cv->image();
  else if (auto* im = qobject_cast<JsImage*>(source)) img = im->image();
  if (img.isNull()) return;
  QRectF src(0, 0, img.width(), img.height()), dst;
  if (!c.isUndefined() && !g.isUndefined()) {
    src = QRectF(a, b, c.toNumber(), d.toNumber());
    dst = QRectF(e.toNumber(), f.toNumber(), g.toNumber(), h.toNumber());
  } else if (!c.isUndefined()) {
    dst = QRectF(a, b, c.toNumber(), d.toNumber());
  } else {
    dst = QRectF(a, b, img.width(), img.height());
  }
  QPainter& p = painter();
  if (st().shadowColor.alpha()) {
    QPainterPath box;
    box.addPolygon(st().ctm.map(QPolygonF(dst)));
    drawShadow(box);
  }
  p.setTransform(st().ctm);
  p.setOpacity(st().alpha);
  p.setCompositionMode(st().composite);
  p.setRenderHint(QPainter::SmoothPixmapTransform, st().smooth);
  p.drawImage(dst, img, src);
}

QJSValue JsCanvas::getImageData(int x, int y, int w, int h) {
  QJSEngine* engine = qjsEngine(this);
  if (!engine) return {};
  return engine->globalObject().property("__imageData").call({engine->toScriptValue(pixels(x, y, w, h)), w, h});
}

void JsCanvas::putImageData(const QJSValue& data, int x, int y) {
  const int w = data.property("width").toInt(), h = data.property("height").toInt();
  const QByteArray bytes = data.property("data").property("buffer").toVariant().toByteArray();
  putPixels(bytes, x, y, w, h);
}

QString JsCanvas::toDataURL(const QString& type, double quality) {
  QByteArray bytes;
  QBuffer buf(&bytes);
  buf.open(QIODevice::WriteOnly);
  const bool jpeg = type == "image/jpeg";
  QImage img = image();
  if (jpeg) {
    // JPEG has no alpha: on white, as browsers do.
    QImage flat(img.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    QPainter p(&flat);
    p.drawImage(0, 0, img);
    p.end();
    flat.save(&buf, "JPEG", int(std::clamp(quality, 0.0, 1.0) * 100));
  } else {
    img.save(&buf, "PNG");
  }
  return QStringLiteral("data:") + (jpeg ? "image/jpeg" : "image/png") + ";base64," + QString::fromLatin1(bytes.toBase64());
}

QByteArray JsCanvas::pixels(int x, int y, int w, int h) {
  const QImage part = image().copy(x, y, w, h).convertToFormat(QImage::Format_RGBA8888);
  QByteArray out;
  out.reserve(qsizetype(w) * h * 4);
  for (int row = 0; row < part.height(); ++row) out.append(reinterpret_cast<const char*>(part.constScanLine(row)), part.width() * 4);
  return out;
}

void JsCanvas::putPixels(const QByteArray& rgba, int x, int y, int w, int h) {
  if (rgba.size() < qsizetype(w) * h * 4) return;
  QImage part(reinterpret_cast<const uchar*>(rgba.constData()), w, h, w * 4, QImage::Format_RGBA8888);
  QPainter& p = painter();
  p.setTransform(QTransform());
  p.setOpacity(1);
  p.setCompositionMode(QPainter::CompositionMode_Source);
  p.drawImage(QPoint(x, y), part.convertToFormat(QImage::Format_ARGB32_Premultiplied));
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

QObject* JsCanvas::createLinearGradient(double x0, double y0, double x1, double y1) {
  auto* g = new QLinearGradient(x0, y0, x1, y1);
  g->setSpread(QGradient::PadSpread);
  return new JsGradient(g);
}

QObject* JsCanvas::createRadialGradient(double x0, double y0, double r0, double x1, double y1, double r1) {
  auto* g = new QRadialGradient(QPointF(x1, y1), r1, QPointF(x0, y0), r0);
  g->setSpread(QGradient::PadSpread);
  return new JsGradient(g);
}

QObject* JsCanvas::createPattern(QObject* source, const QString&) {
  QImage img;
  if (auto* cv = qobject_cast<JsCanvas*>(source)) img = cv->image();
  else if (auto* im = qobject_cast<JsImage*>(source)) img = im->image();
  if (img.isNull()) return nullptr;
  return new JsPattern(img);
}
