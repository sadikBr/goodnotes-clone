// Stroke outlines: a C++ port of perfect-freehand 1.2.3 (getStroke).
// Copyright (c) 2021 Stephen Ruiz Ltd, MIT License (see Freehand.cpp).
#pragma once

#include <functional>
#include <vector>

namespace draw {

struct FPoint {
  double x, y;
  /// Pressure (0..1), or a negative number for none.
  double p = -1;
};

struct FreehandOptions {
  double size = 16, thinning = 0.5, smoothing = 0.5, streamline = 0.5;
  bool simulatePressure = true, last = false;
  /// Taper length at each end (0: none, < 0: the whole stroke); caps when not tapered.
  double startTaper = 0, endTaper = 0;
  bool startCap = true, endCap = true;
};

/// The stroke's outline polygon.
std::vector<std::pair<double, double>> getStroke(const std::vector<FPoint>& points, const FreehandOptions& o);

}  // namespace draw
