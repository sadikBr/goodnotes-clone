// Paper previews for QML, drawn like the editor's paper:
//   image://paper/<template>/<colour without #>/<corner|whole>
// "corner" is a close-up of the page's top-left (lines would be too faint at
// thumbnail size), "whole" the whole page. The Image's sourceSize sets the size.
#pragma once

#include <QQuickImageProvider>

class PaperImage : public QQuickImageProvider {
public:
  PaperImage() : QQuickImageProvider(QQuickImageProvider::Image) {}
  QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;
};

/// Ink previews for the tool drawer, drawn like strokes on the page (56 × 22 units):
///   image://ink/nib/<style>/<colour>/<size>/<dark>
///   image://ink/smooth/<streamline>/<colour>/<dark>
class InkSampleImage : public QQuickImageProvider {
public:
  InkSampleImage() : QQuickImageProvider(QQuickImageProvider::Image) {}
  QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;
};

/// Cover previews with the title (daftar/Cover coverPreview):
///   image://cover/<design>/<colour>/<landscape 0|1>/<title, URL-encoded>
class CoverImage : public QQuickImageProvider {
public:
  CoverImage() : QQuickImageProvider(QQuickImageProvider::Image) {}
  QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;
};

class Canvas;
/// The tool drawer's pen preview: a wave in `style` (in a 56 × 22 box).
void nibSample(Canvas& c, const QString& style, const QString& color, double size, bool dark);
/// The smoothing preview: a jittery line smoothed by `streamline`.
void smoothingSample(Canvas& c, double streamline, const QString& color, bool dark);
