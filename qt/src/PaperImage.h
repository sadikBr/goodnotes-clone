// Paper previews for QML, drawn by the web code like the editor's paper:
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

/// Ink previews for the tool drawer, drawn by the web code (56 × 22 units):
///   image://ink/nib/<style>/<colour>/<size>/<dark>
///   image://ink/smooth/<streamline>/<colour>/<dark>
class InkSampleImage : public QQuickImageProvider {
public:
  InkSampleImage() : QQuickImageProvider(QQuickImageProvider::Image) {}
  QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;
};
