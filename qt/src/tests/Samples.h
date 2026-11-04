// Sample elements of every kind (strokes in every pen, shapes, text in every
// font with lists and Arabic, sticky notes, pictures) for the drawing checks.
#pragma once

#include <QJsonObject>
#include <QString>
#include <utility>
#include <vector>

namespace samples {
std::vector<std::pair<QString, QJsonObject>> sampleElements();
}
