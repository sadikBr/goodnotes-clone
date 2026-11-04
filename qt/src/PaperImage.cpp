#include "PaperImage.h"

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

void nibSample(Canvas& c, const QString& style, const QString& color, double size, bool dark) {
  const double w = 56, hgt = 22;
  std::vector<std::array<double, 3>> points;
  for (int i = 0; i <= 24; ++i) {
    const double t = i / 24.0;
    points.push_back({4 + t * (w - 8), hgt / 2 + std::sin(t * M_PI * 2) * 6, 0.35 + 0.5 * std::sin(t * M_PI)});
  }
  draw::drawStroke(c, {{"id", "sample"}, {"z", 0}, {"kind", "stroke"}, {"tool", "pen"}, {"style", style}, {"color", sampleInk(color, dark)}, {"width", std::min(6.5, std::max(1.4, size * 0.9))}, {"points", pts(points)}});
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

QImage InkSampleImage::requestImage(const QString& id, QSize* size, const QSize& requested) {
  if (QThread::currentThread() != qApp->thread()) {
    QImage out;
    QMetaObject::invokeMethod(qApp, [&] { out = requestImage(id, size, requested); }, Qt::BlockingQueuedConnection);
    return out;
  }
  const QStringList p = id.split('/');
  const QSize want = requested.isValid() && !requested.isEmpty() ? requested : QSize(112, 44);
  Canvas canvas(want.width(), want.height());
  canvas.setTransform(want.width() / 56.0, 0, 0, want.height() / 22.0, 0, 0);
  if (p.value(0) == "nib") nibSample(canvas, p.value(1), "#" + p.value(2), p.value(3).toDouble(), p.value(4) == "1");
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
