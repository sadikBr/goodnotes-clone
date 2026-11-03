// Soft shadows for QML without a blur effect: "image://shadow/<radius>/<blur>"
// is a blurred rounded box, drawn stretched as a nine-patch (BorderImage)
// under cards, menus and dialogs. Each size is made once and cached.
#pragma once

#include <QQuickImageProvider>

class ShadowImage : public QQuickImageProvider {
public:
  ShadowImage() : QQuickImageProvider(QQuickImageProvider::Image) {}
  QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;
};
