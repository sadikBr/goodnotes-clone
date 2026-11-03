#include "ShadowImage.h"

#include <QPainter>
#include <QPainterPath>
#include <vector>

namespace {
/// One box-blur pass over the alpha channel (three passes ≈ a Gaussian).
void boxBlur(std::vector<float>& a, int w, int h, int r) {
  if (r < 1) return;
  std::vector<float> tmp(a.size());
  const float k = 1.0f / (2 * r + 1);
  for (int y = 0; y < h; ++y) {
    float sum = 0;
    for (int x = -r; x <= r; ++x) sum += a[y * w + std::clamp(x, 0, w - 1)];
    for (int x = 0; x < w; ++x) {
      tmp[y * w + x] = sum * k;
      sum += a[y * w + std::min(x + r + 1, w - 1)] - a[y * w + std::max(x - r, 0)];
    }
  }
  for (int x = 0; x < w; ++x) {
    float sum = 0;
    for (int y = -r; y <= r; ++y) sum += tmp[std::clamp(y, 0, h - 1) * w + x];
    for (int y = 0; y < h; ++y) {
      a[y * w + x] = sum * k;
      sum += tmp[std::min(y + r + 1, h - 1) * w + x] - tmp[std::max(y - r, 0) * w + x];
    }
  }
}
}  // namespace

QImage ShadowImage::requestImage(const QString& id, QSize* size, const QSize&) {
  const QStringList parts = id.split('/');
  const int radius = std::clamp(parts.value(0).toInt(), 0, 64);
  const int blur = std::clamp(parts.value(1).toInt(), 0, 64);
  // The box: its corners plus a middle that the nine-patch stretches.
  const int pad = blur * 2;
  const int core = radius * 2 + 4;
  const int w = core + pad * 2;
  QImage box(w, w, QImage::Format_ARGB32_Premultiplied);
  box.fill(Qt::transparent);
  {
    QPainter p(&box);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(pad, pad, core, core), radius, radius);
    p.fillPath(path, Qt::black);
  }
  std::vector<float> a(w * w);
  for (int y = 0; y < w; ++y)
    for (int x = 0; x < w; ++x) a[y * w + x] = qAlpha(box.pixel(x, y)) / 255.0f;
  // A Gaussian with sigma ≈ blur / 2 (like CSS box-shadow's blur radius).
  const int r = std::max(1, int(blur / 2.0 * 0.94 + 0.5));
  for (int i = 0; i < 3; ++i) boxBlur(a, w, w, r);
  QImage out(w, w, QImage::Format_ARGB32_Premultiplied);
  for (int y = 0; y < w; ++y) {
    auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
    for (int x = 0; x < w; ++x) line[x] = qRgba(0, 0, 0, std::clamp(int(a[y * w + x] * 255 + 0.5f), 0, 255));
  }
  if (size) *size = out.size();
  return out;
}
