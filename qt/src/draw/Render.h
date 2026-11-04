// Pictures the app draws in C++ (was in the script engine class): a page's
// paper (with a small cache: pages alike share one), elements, decoded
// pictures for image elements, and the app's clock.
#pragma once

#include <QImage>
#include <QJsonArray>
#include <QRectF>
#include <QString>

namespace draw {

/// The one clock for pen samples and gesture timing (ms).
double clockMs();
/// A page's paper (template, colour) at `scale` pixels per page unit: the
/// `region` part of the page (page units), or all of it.
QImage paperImage(const QString& tmpl, const QString& color, double w, double h, double scale, QRectF region = {});
/// Elements (their JSON) drawn in order: `region` of the page at `scale`, on a transparent picture.
QImage elementsImage(const QJsonArray& els, double scale, QRectF region);
/// A decoded picture for an image element (data: URL or stored asset id); null when it can't be read.
const QImage* imageFor(const QString& src);

}  // namespace draw
