// A line-by-line C++ port of perfect-freehand 1.2.3 (getStroke and its
// helpers), so strokes outline exactly as the TypeScript app drew them.
//
// MIT License
//
// Copyright (c) 2021 Stephen Ruiz Ltd
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Freehand.h"

#include <cmath>

namespace draw {
namespace {

using V = std::pair<double, double>;
const double PI = M_PI;
const double FIXED_PI = PI + 0.0001;
const double RATE_OF_PRESSURE_CHANGE = 0.275;

V neg(V a) { return {-a.first, -a.second}; }
V add(V a, V b) { return {a.first + b.first, a.second + b.second}; }
V sub(V a, V b) { return {a.first - b.first, a.second - b.second}; }
V mul(V a, double n) { return {a.first * n, a.second * n}; }
V divv(V a, double n) { return {a.first / n, a.second / n}; }
V per(V a) { return {a.second, -a.first}; }
double dpr(V a, V b) { return a.first * b.first + a.second * b.second; }
bool isEqual(V a, V b) { return a.first == b.first && a.second == b.second; }
double len(V a) { return std::hypot(a.first, a.second); }
double dist2(V a, V b) {
  const double x = a.first - b.first, y = a.second - b.second;
  return x * x + y * y;
}
V uni(V a) { return divv(a, len(a)); }
double dist(V a, V b) { return std::hypot(a.second - b.second, a.first - b.first); }
V rotAround(V a, V c, double r) {
  const double s = std::sin(r), co = std::cos(r);
  const double px = a.first - c.first, py = a.second - c.second;
  const double nx = px * co - py * s, ny = px * s + py * co;
  return {nx + c.first, ny + c.second};
}
V lrp(V a, V b, double t) { return add(a, mul(sub(b, a), t)); }
V prj(V a, V b, double c) { return add(a, mul(b, c)); }

double getStrokeRadius(double size, double thinning, double pressure) { return size * (0.5 - thinning * (0.5 - pressure)); }

double simulatePressure(double prev, double distance, double size) {
  const double sp = std::min(1.0, distance / size);
  const double rp = std::min(1.0, 1 - sp);
  return std::min(1.0, prev + (rp - prev) * (sp * RATE_OF_PRESSURE_CHANGE));
}

struct SP {
  V point;
  double pressure;
  V vector;
  double distance, runningLength;
};

std::vector<SP> getStrokePoints(std::vector<FPoint> pts0, const FreehandOptions& o) {
  const double streamline = o.streamline, size = o.size;
  const bool isComplete = o.last;
  if (pts0.empty()) return {};
  const double t = 0.15 + (1 - streamline) * 0.85;
  std::vector<FPoint> pts = std::move(pts0);
  if (pts.size() == 2) {
    const FPoint last = pts[1];
    pts.pop_back();
    // Interpolated points carry no pressure (lrp keeps x and y only).
    for (int i = 1; i < 5; i++) {
      const V q = lrp({pts[0].x, pts[0].y}, {last.x, last.y}, i / 4.0);
      pts.push_back({q.first, q.second, -1});
    }
  }
  if (pts.size() == 1) pts.push_back({pts[0].x + 1, pts[0].y + 1, pts[0].p});
  auto has = [](double p) { return p >= 0; };
  std::vector<SP> out;
  out.push_back({{pts[0].x, pts[0].y}, has(pts[0].p) ? pts[0].p : 0.25, {1, 1}, 0, 0});
  bool hasReachedMinimumLength = false;
  double runningLength = 0;
  SP prev = out[0];
  const size_t max = pts.size() - 1;
  for (size_t i = 1; i < pts.size(); i++) {
    const V point = isComplete && i == max ? V{pts[i].x, pts[i].y} : lrp(prev.point, {pts[i].x, pts[i].y}, t);
    if (isEqual(prev.point, point)) continue;
    const double distance = dist(point, prev.point);
    runningLength += distance;
    if (i < max && !hasReachedMinimumLength) {
      if (runningLength < size) continue;
      hasReachedMinimumLength = true;
    }
    prev = {point, has(pts[i].p) ? pts[i].p : 0.5, uni(sub(prev.point, point)), distance, runningLength};
    out.push_back(prev);
  }
  out[0].vector = out.size() > 1 ? out[1].vector : V{0, 0};
  return out;
}

std::vector<V> getStrokeOutlinePoints(const std::vector<SP>& points, const FreehandOptions& o) {
  const double size = o.size, smoothing = o.smoothing, thinning = o.thinning;
  const bool simulate = o.simulatePressure, isComplete = o.last;
  const bool capStart = o.startCap, capEnd = o.endCap;
  auto taperStartEase = [](double t) { return t * (2 - t); };
  auto taperEndEase = [](double t) {
    --t;
    return t * t * t + 1;
  };
  if (points.empty() || size <= 0) return {};
  const double totalLength = points.back().runningLength;
  const double taperStart = o.startTaper < 0 ? std::max(size, totalLength) : o.startTaper;
  const double taperEnd = o.endTaper < 0 ? std::max(size, totalLength) : o.endTaper;
  const double minDistance = std::pow(size * smoothing, 2);
  std::vector<V> leftPts, rightPts;
  double prevPressure = points[0].pressure;
  for (size_t i = 0; i < std::min<size_t>(10, points.size()); ++i) {
    double pr = points[i].pressure;
    if (simulate) pr = simulatePressure(prevPressure, points[i].distance, size);
    prevPressure = (prevPressure + pr) / 2;
  }
  double radius = getStrokeRadius(size, thinning, points.back().pressure);
  bool haveFirstRadius = false;
  double firstRadius = 0;
  V prevVector = points[0].vector;
  V pl = points[0].point, pr = pl;
  V tl = pl, tr = pr;
  bool isPrevPointSharpCorner = false;
  for (size_t i = 0; i < points.size(); i++) {
    double pressure = points[i].pressure;
    const V point = points[i].point;
    const V vector = points[i].vector;
    const double distance = points[i].distance, runningLength = points[i].runningLength;
    const bool isLast = i == points.size() - 1;
    if (!isLast && totalLength - runningLength < 3) continue;
    if (thinning) {
      if (simulate) pressure = simulatePressure(prevPressure, distance, size);
      radius = getStrokeRadius(size, thinning, pressure);
    } else {
      radius = size / 2;
    }
    if (!haveFirstRadius) {
      firstRadius = radius;
      haveFirstRadius = true;
    }
    const double ts = runningLength < taperStart ? taperStartEase(runningLength / taperStart) : 1;
    const double te = totalLength - runningLength < taperEnd ? taperEndEase((totalLength - runningLength) / taperEnd) : 1;
    radius = std::max(0.01, radius * std::min(ts, te));
    const V nextVector = (isLast ? points[i] : points[i + 1]).vector;
    const double nextDpr = isLast ? 1.0 : dpr(vector, nextVector);
    const double prevDpr = dpr(vector, prevVector);
    const bool isPointSharpCorner = prevDpr < 0 && !isPrevPointSharpCorner;
    const bool isNextPointSharpCorner = nextDpr < 0;
    if (isPointSharpCorner || isNextPointSharpCorner) {
      const V offset = mul(per(prevVector), radius);
      for (double step = 0; step <= 1; step += 0.07692307692307693) {
        tl = rotAround(sub(point, offset), point, FIXED_PI * step);
        leftPts.push_back(tl);
        tr = rotAround(add(point, offset), point, FIXED_PI * -step);
        rightPts.push_back(tr);
      }
      pl = tl;
      pr = tr;
      if (isNextPointSharpCorner) isPrevPointSharpCorner = true;
      continue;
    }
    isPrevPointSharpCorner = false;
    if (isLast) {
      const V offset = mul(per(vector), radius);
      leftPts.push_back(sub(point, offset));
      rightPts.push_back(add(point, offset));
      continue;
    }
    const V offset = mul(per(lrp(nextVector, vector, nextDpr)), radius);
    tl = sub(point, offset);
    if (i <= 1 || dist2(pl, tl) > minDistance) {
      leftPts.push_back(tl);
      pl = tl;
    }
    tr = add(point, offset);
    if (i <= 1 || dist2(pr, tr) > minDistance) {
      rightPts.push_back(tr);
      pr = tr;
    }
    prevPressure = pressure;
    prevVector = vector;
  }
  const V firstPoint = points[0].point;
  const V lastPoint = points.size() > 1 ? points.back().point : add(points[0].point, {1, 1});
  std::vector<V> startCap, endCap;
  if (points.size() == 1) {
    if (!(taperStart || taperEnd) || isComplete) {
      // A dot.
      const V start = prj(firstPoint, uni(per(sub(firstPoint, add(firstPoint, {1, 1})))), -(haveFirstRadius ? (firstRadius ? firstRadius : radius) : radius));
      std::vector<V> dot;
      const double step = 1.0 / 13;
      for (double t = step; t <= 1; t += step) dot.push_back(rotAround(start, firstPoint, FIXED_PI * 2 * t));
      return dot;
    }
  } else {
    if (taperStart || (taperEnd && points.size() == 1)) {
      // Tapered start: no cap.
    } else if (capStart) {
      const double step = 1.0 / 13;
      for (double t = step; t <= 1; t += step) startCap.push_back(rotAround(rightPts[0], firstPoint, FIXED_PI * t));
    } else {
      const V cornersVector = sub(leftPts[0], rightPts[0]);
      const V offsetA = mul(cornersVector, 0.5), offsetB = mul(cornersVector, 0.51);
      startCap = {sub(firstPoint, offsetA), sub(firstPoint, offsetB), add(firstPoint, offsetB), add(firstPoint, offsetA)};
    }
    const V direction = per(neg(points.back().vector));
    if (taperEnd || (taperStart && points.size() == 1)) {
      endCap.push_back(lastPoint);
    } else if (capEnd) {
      const V start = prj(lastPoint, direction, radius);
      const double step = 1.0 / 29;
      for (double t = step; t < 1; t += step) endCap.push_back(rotAround(start, lastPoint, FIXED_PI * 3 * t));
    } else {
      endCap = {add(lastPoint, mul(direction, radius)), add(lastPoint, mul(direction, radius * 0.99)), sub(lastPoint, mul(direction, radius * 0.99)), sub(lastPoint, mul(direction, radius))};
    }
  }
  std::vector<V> out = leftPts;
  out.insert(out.end(), endCap.begin(), endCap.end());
  out.insert(out.end(), rightPts.rbegin(), rightPts.rend());
  out.insert(out.end(), startCap.begin(), startCap.end());
  return out;
}

}  // namespace

std::vector<std::pair<double, double>> getStroke(const std::vector<FPoint>& points, const FreehandOptions& o) { return getStrokeOutlinePoints(getStrokePoints(points, o), o); }

}  // namespace draw
