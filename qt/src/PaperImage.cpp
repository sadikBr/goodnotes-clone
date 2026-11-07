#include "PaperImage.h"
#include "Ink.h"

#include "daftar/Cover.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <QUrl>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <array>
#include <cmath>
#include <vector>

#include "draw/Strokes.h"

#include "draw/Render.h"
#include "draw/Elements.h"
#include "editor/Geometry.h"
#include "Canvas.h"

QImage PaperImage::requestImage(const QString& id, QSize* size, const QSize& requested) {
  // Drawn on the GUI thread (the fonts and caches live there).
  if (QThread::currentThread() != qApp->thread()) {
    QImage out;
    QMetaObject::invokeMethod(qApp, [&] { out = requestImage(id, size, requested); }, Qt::BlockingQueuedConnection);
    return out;
  }
  const QStringList parts = id.split('/');
  const QString tmpl = parts.value(0, "blank");
  const QString color = "#" + parts.value(1, "fffdf7");
  const bool whole = parts.value(2) == "whole";
  const QSize want = requested.isValid() && !requested.isEmpty() ? requested : QSize(176, 220);
  QImage img;
  if (whole) {
    img = draw::paperImage(tmpl, color, 794, 1123, want.width() / 794.0);
    img = img.copy(0, 0, want.width(), want.height());
  } else {
    // The page's top-left, a little above the first ruled line (300 units across).
    const double scale = want.width() / 300.0;
    img = draw::paperImage(tmpl, color, 794, 1123, scale, QRectF(40, 70, 300, want.height() / scale));
  }
  if (size) *size = img.size();
  return img;
}

// Small previews for the tool drawer's menus (were qt/js/samples.ts), drawn in a 56 × 22 box.
namespace {

bool isLightColor(const QString& hex) {
  static const QRegularExpression re("^#?([0-9a-f]{6})$", QRegularExpression::CaseInsensitiveOption);
  const auto m = re.match(hex.trimmed());
  if (!m.hasMatch()) return false;
  const int n = m.captured(1).toInt(nullptr, 16);
  auto lin = [](double v) {
    v /= 255;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * lin(n >> 16) + 0.7152 * lin((n >> 8) & 255) + 0.0722 * lin(n & 255) > 0.45;
}

/// An ink colour that shows on the drawer (very dark ink on a dark drawer, white on light).
QString sampleInk(const QString& color, bool dark) {
  static const QRegularExpression white("^#f{6}$", QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression veryDark("^#[0-3][0-9a-f][0-3][0-9a-f][0-3][0-9a-f]$", QRegularExpression::CaseInsensitiveOption);
  if (!dark && isLightColor(color) && white.match(color).hasMatch()) return "#111827";
  if (dark && !isLightColor(color) && veryDark.match(color).hasMatch()) return "#e7e5e4";
  return color;
}

QJsonArray pts(const std::vector<std::array<double, 3>>& p) {
  QJsonArray a;
  for (const auto& q : p) a.append(QJsonArray{q[0], q[1], q[2]});
  return a;
}

}  // namespace

void nibSample(Canvas& c, const QString& style, const QString& color, double size, bool dark, const QString& dash, double gap, double relative) {
  const double w = 56, hgt = 22;
  std::vector<std::array<double, 3>> points;
  if (style == "tape") {
    points = {{8, hgt / 2 + 2, 0.5}, {w - 10, hgt / 2 - 2, 0.5}};
  } else {
    // (The wide and glowing pens keep further from the edges.)
    const bool wide = style == "neon" || style == "watercolor" || style == "marker" || style == "airbrush" || style == "flat" || style == "inkbrush" || style == "crayon" || style == "chalk";
    const double amp = wide ? 4.5 : 6, inset = wide ? 7 : 4;
    for (int i = 0; i <= 24; ++i) {
      const double t = i / 24.0;
      points.push_back({inset + t * (w - inset * 2), hgt / 2 + std::sin(t * M_PI * 2) * amp, 0.35 + 0.5 * std::sin(t * M_PI)});
    }
  }
  double width = std::min(6.5, std::max(1.4, size * 0.9));
  const double cap = style == "tape" ? 11 : style == "marker" ? 2.6 : style == "watercolor" ? 2.0 : style == "neon" ? 3.0
                     : style == "airbrush" ? 2.2 : style == "flat" ? 2.4 : style == "inkbrush" ? 2.8 : style == "crayon" || style == "chalk" ? 3.6 : 6.5;
  if (style == "tape") width = 11;
  else if (style == "airbrush") width = std::min(2.2, width);
  else if (style == "flat") width = std::min(2.4, width);
  else if (style == "inkbrush") width = std::min(2.8, width);
  else if (style == "crayon" || style == "chalk") width = std::min(3.6, width);
  else if (style == "marker") width = std::min(2.6, width);
  else if (style == "watercolor") width = std::min(2.0, width);
  else if (style == "neon") width = std::min(3.0, std::max(1.8, width));
  // A size among a pen's sizes (0..1 of the largest): thicker for the larger ones.
  if (relative > 0) width = cap * 1.25 * (0.15 + 0.85 * std::min(1.0, relative));
  QJsonObject el{{"id", "sample"}, {"z", 0}, {"kind", "stroke"}, {"tool", "pen"}, {"style", style}, {"color", sampleInk(color, dark)}, {"width", width}, {"points", pts(points)}};
  // Construction lines are always dashed; other even pens when their line style is.
  if (style == "construction" || (ink::dashable(style) && (dash == "dash" || dash == "dot"))) {
    el["dash"] = dash == "dot" ? "dot" : "dash";
    if (gap > 0) el["gap"] = gap;
  }
  draw::drawStroke(c, el);
}

void smoothingSample(Canvas& c, double streamline, const QString& color, bool dark) {
  const double w = 56, hgt = 22;
  std::vector<std::array<double, 3>> points;
  for (int i = 0; i <= 40; ++i) {
    const double t = i / 40.0;
    const double jitter = std::sin(i * 12.9898) * 43758.5453;
    points.push_back({4 + t * (w - 8), hgt / 2 + std::sin(t * M_PI * 2) * 5 + (jitter - std::floor(jitter) - 0.5) * 3.2, 0.5});
  }
  draw::drawStroke(c, {{"id", "sample"}, {"z", 0}, {"kind", "stroke"}, {"tool", "pen"}, {"style", "ballpoint"}, {"color", sampleInk(color, dark)}, {"width", 1.6}, {"points", pts(points)}, {"smooth", streamline}});
}

QHash<QString, QString>& pictureSources() {
  static QHash<QString, QString> sources;
  return sources;
}

QImage InkSampleImage::requestImage(const QString& id, QSize* size, const QSize& requested) {
  if (QThread::currentThread() != qApp->thread()) {
    QImage out;
    QMetaObject::invokeMethod(qApp, [&] { out = requestImage(id, size, requested); }, Qt::BlockingQueuedConnection);
    return out;
  }
  const QStringList p = id.split('/');
  // A picture on a page, whole (for cropping it): image://ink/picture/<key>.
  if (p.value(0) == "picture") {
    const QImage* im = draw::imageFor(pictureSources().value(p.value(1)));
    QImage img = im ? *im : QImage();
    if (!img.isNull() && requested.isValid() && !requested.isEmpty() && (img.width() > requested.width() || img.height() > requested.height()))
      img = img.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size) *size = img.size();
    return img;
  }
  // A shape as the shape tool draws it, for the shape picker: image://ink/shape/<kind>/<hex>.
  if (p.value(0) == "shape") {
    const QSize want = requested.isValid() && !requested.isEmpty() ? requested : QSize(48, 48);
    Canvas canvas(want.width(), want.height());
    const double k = want.width() / 24.0, m = 4 * k;
    const bool segment = ed::isSegmentKind(p.value(1));
    // Lines go corner to corner; the rest fill the tile, a little wider than tall.
    const auto sh = segment ? ed::buildShape(p.value(1), {m, want.height() - m}, {want.width() - m, m})
                            : ed::buildShape(p.value(1), {m * 0.75, m * 1.25}, {want.width() - m * 0.75, want.height() - m * 1.25});
    if (sh) draw::drawElement(canvas, QJsonObject{{"kind", "shape"}, {"shape", sh->toJson()}, {"color", "#" + p.value(2, "111827")}, {"width", 1.6 * k}});
    QImage img = canvas.image();
    if (size) *size = img.size();
    return img;
  }
  const QSize want = requested.isValid() && !requested.isEmpty() ? requested : QSize(112, 44);
  Canvas canvas(want.width(), want.height());
  canvas.setTransform(want.width() / 56.0, 0, 0, want.height() / 22.0, 0, 0);
  if (p.value(0) == "nib") nibSample(canvas, p.value(1), "#" + p.value(2), p.value(3).toDouble(), p.value(4) == "1", p.value(5), p.value(6).toDouble(), p.value(7).toDouble());
  else smoothingSample(canvas, p.value(1).toDouble(), "#" + p.value(2), p.value(3) == "1");
  QImage img = canvas.image();
  if (size) *size = img.size();
  return img;
}

namespace {

/// One cover preview, drawn on the pool.
class CoverResponse : public QQuickImageResponse, public QRunnable {
public:
  CoverResponse(const QString& id, const QSize& requested) : m_id(id), m_requested(requested) { setAutoDelete(false); }
  void run() override {
    const QStringList p = m_id.split('/');
    const QString design = p.value(0, "classic");
    const QString color = "#" + p.value(1, "2563eb");
    const bool landscape = p.value(2) == "1";
    const QString title = QUrl::fromPercentEncoding(p.mid(3).join('/').toUtf8());
    const int w = m_requested.isValid() && m_requested.width() > 0 ? m_requested.width() : 240;
    m_image = cover::coverPreview(design, color, title, w, landscape);
    emit finished();
  }
  QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }

private:
  QString m_id;
  QSize m_requested;
  QImage m_image;
};

}  // namespace

CoverImage::CoverImage() : QQuickAsyncImageProvider() { m_pool.setMaxThreadCount(2); }

QQuickImageResponse* CoverImage::requestImageResponse(const QString& id, const QSize& requested) {
  auto* r = new CoverResponse(id, requested);
  m_pool.start(r);
  return r;
}
