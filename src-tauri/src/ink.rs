//! Ink geometry: stroke simplification and hand-drawn shape recognition.
//!
//! The recognizer turns a rough hand-drawn stroke into a clean geometric
//! shape (line, triangle, rectangle, polygon or ellipse) when the stroke is
//! close enough to one, and returns `None` otherwise so the stroke stays
//! freehand.

use serde::{Deserialize, Serialize};

pub type P2 = [f64; 2];

/// A sampled ink point: x, y and normalised pen pressure (0..=1).
pub type InkPoint = [f64; 3];

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "camelCase")]
pub enum Shape {
    Line { points: Vec<P2> },
    Triangle { points: Vec<P2> },
    Rectangle { points: Vec<P2> },
    Polygon { points: Vec<P2> },
    Ellipse { cx: f64, cy: f64, rx: f64, ry: f64 },
}

fn dist(a: P2, b: P2) -> f64 {
    ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2)).sqrt()
}

/// Perpendicular distance from `p` to the segment `a`-`b`.
fn seg_dist(p: P2, a: P2, b: P2) -> f64 {
    let (dx, dy) = (b[0] - a[0], b[1] - a[1]);
    let len2 = dx * dx + dy * dy;
    if len2 == 0.0 {
        return dist(p, a);
    }
    let t = (((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / len2).clamp(0.0, 1.0);
    dist(p, [a[0] + t * dx, a[1] + t * dy])
}

fn path_length(points: &[P2]) -> f64 {
    points.windows(2).map(|w| dist(w[0], w[1])).sum()
}

fn bbox(points: &[P2]) -> (f64, f64, f64, f64) {
    let mut b = (
        f64::INFINITY,
        f64::INFINITY,
        f64::NEG_INFINITY,
        f64::NEG_INFINITY,
    );
    for p in points {
        b.0 = b.0.min(p[0]);
        b.1 = b.1.min(p[1]);
        b.2 = b.2.max(p[0]);
        b.3 = b.3.max(p[1]);
    }
    b
}

/// Ramer–Douglas–Peucker: indices of the points kept.
fn rdp_indices(points: &[P2], epsilon: f64) -> Vec<usize> {
    let n = points.len();
    if n < 3 {
        return (0..n).collect();
    }
    let mut keep = vec![false; n];
    keep[0] = true;
    keep[n - 1] = true;
    let mut stack = vec![(0usize, n - 1)];
    while let Some((start, end)) = stack.pop() {
        let mut max_d = 0.0;
        let mut idx = start;
        for i in start + 1..end {
            let d = seg_dist(points[i], points[start], points[end]);
            if d > max_d {
                max_d = d;
                idx = i;
            }
        }
        if max_d > epsilon {
            keep[idx] = true;
            stack.push((start, idx));
            stack.push((idx, end));
        }
    }
    (0..n).filter(|&i| keep[i]).collect()
}

/// Simplifies an ink stroke, preserving pressure on the kept points.
pub fn simplify(points: &[InkPoint], epsilon: f64) -> Vec<InkPoint> {
    let xy: Vec<P2> = points.iter().map(|p| [p[0], p[1]]).collect();
    rdp_indices(&xy, epsilon.max(0.0))
        .into_iter()
        .map(|i| points[i])
        .collect()
}

/// Resamples a polyline to `n` points evenly spaced by arc length, which
/// removes the bias from uneven drawing speed.
fn resample(points: &[P2], n: usize) -> Vec<P2> {
    let total = path_length(points);
    if points.len() < 2 || total == 0.0 || n < 2 {
        return points.to_vec();
    }
    let step = total / (n - 1) as f64;
    let mut out = vec![points[0]];
    let mut acc = 0.0;
    let mut prev = points[0];
    let mut i = 1;
    while i < points.len() {
        let cur = points[i];
        let d = dist(prev, cur);
        if d > 0.0 && acc + d >= step {
            let t = (step - acc) / d;
            let q = [
                prev[0] + t * (cur[0] - prev[0]),
                prev[1] + t * (cur[1] - prev[1]),
            ];
            out.push(q);
            prev = q;
            acc = 0.0;
        } else {
            acc += d;
            prev = cur;
            i += 1;
        }
    }
    while out.len() < n {
        out.push(*points.last().unwrap());
    }
    out.truncate(n);
    out
}

/// Interior angle at `b` in degrees.
fn angle_at(a: P2, b: P2, c: P2) -> f64 {
    let v1 = [a[0] - b[0], a[1] - b[1]];
    let v2 = [c[0] - b[0], c[1] - b[1]];
    let dot = v1[0] * v2[0] + v1[1] * v2[1];
    let n = (v1[0].hypot(v1[1])) * (v2[0].hypot(v2[1]));
    if n == 0.0 {
        return 180.0;
    }
    (dot / n).clamp(-1.0, 1.0).acos().to_degrees()
}

/// Corners of a closed stroke, found by running RDP on the two halves split
/// at the point farthest from the start.
fn closed_corners(points: &[P2], epsilon: f64, min_gap: f64) -> Vec<P2> {
    let start = points[0];
    let far = (0..points.len())
        .max_by(|&a, &b| dist(points[a], start).total_cmp(&dist(points[b], start)))
        .unwrap_or(0);
    let mut first_half = points[..=far].to_vec();
    let mut second_half = points[far..].to_vec();
    second_half.push(start);
    let mut corners: Vec<P2> = rdp_indices(&first_half, epsilon)
        .into_iter()
        .map(|i| first_half[i])
        .collect();
    corners.pop(); // `far` is the first point of the second half
    corners.extend(
        rdp_indices(&second_half, epsilon)
            .into_iter()
            .map(|i| second_half[i]),
    );
    corners.pop(); // closing point duplicates the start
    first_half.clear();
    second_half.clear();

    // Merge corners that are too close to each other (including wrap-around).
    let mut merged: Vec<P2> = Vec::new();
    for c in corners {
        if merged.last().map_or(true, |&l| dist(l, c) >= min_gap) {
            merged.push(c);
        }
    }
    while merged.len() > 2 && dist(merged[0], *merged.last().unwrap()) < min_gap {
        merged.pop();
    }
    // Drop nearly straight vertices (angle close to 180°).
    loop {
        let n = merged.len();
        if n <= 3 {
            break;
        }
        let straight = (0..n)
            .find(|&i| angle_at(merged[(i + n - 1) % n], merged[i], merged[(i + 1) % n]) > 160.0);
        match straight {
            Some(i) => {
                merged.remove(i);
            }
            None => break,
        }
    }
    merged
}

/// Mean deviation of the points from the bounding-box ellipse, normalised
/// so that 0 is a perfect axis-aligned ellipse.
fn ellipse_error(points: &[P2]) -> (f64, f64, f64, f64, f64) {
    let (x0, y0, x1, y1) = bbox(points);
    let (cx, cy) = ((x0 + x1) / 2.0, (y0 + y1) / 2.0);
    let (rx, ry) = (((x1 - x0) / 2.0).max(1e-6), ((y1 - y0) / 2.0).max(1e-6));
    let err = points
        .iter()
        .map(|p| ((((p[0] - cx) / rx).powi(2) + ((p[1] - cy) / ry).powi(2)).sqrt() - 1.0).abs())
        .sum::<f64>()
        / points.len() as f64;
    (err, cx, cy, rx, ry)
}

/// Snaps a nearly axis-aligned rectangle to exact horizontal/vertical edges
/// and otherwise squares up the corners of a rotated one.
fn regularise_rectangle(c: &[P2]) -> Vec<P2> {
    let edge = [c[1][0] - c[0][0], c[1][1] - c[0][1]];
    let angle = edge[1].atan2(edge[0]).to_degrees().rem_euclid(90.0);
    let axis_aligned = !(8.0..=82.0).contains(&angle);
    if axis_aligned {
        let (x0, y0, x1, y1) = bbox(c);
        return vec![[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
    }
    // Rotated: build a true rectangle from the centre, the first edge's
    // direction and the average side lengths.
    let cx = c.iter().map(|p| p[0]).sum::<f64>() / 4.0;
    let cy = c.iter().map(|p| p[1]).sum::<f64>() / 4.0;
    let len = edge[0].hypot(edge[1]);
    let u = [edge[0] / len, edge[1] / len];
    let v = [-u[1], u[0]];
    let w = (dist(c[0], c[1]) + dist(c[2], c[3])) / 4.0;
    let h = (dist(c[1], c[2]) + dist(c[3], c[0])) / 4.0;
    // Keep the original winding direction.
    let cross =
        (c[1][0] - c[0][0]) * (c[2][1] - c[1][1]) - (c[1][1] - c[0][1]) * (c[2][0] - c[1][0]);
    let s = if cross >= 0.0 { 1.0 } else { -1.0 };
    let corner = |a: f64, b: f64| {
        [
            cx + a * w * u[0] + b * s * h * v[0],
            cy + a * w * u[1] + b * s * h * v[1],
        ]
    };
    vec![
        corner(-1.0, -1.0),
        corner(1.0, -1.0),
        corner(1.0, 1.0),
        corner(-1.0, 1.0),
    ]
}

/// Tries to recognise a hand-drawn stroke as a geometric shape.
pub fn recognize(raw: &[P2]) -> Option<Shape> {
    if raw.len() < 4 {
        return None;
    }
    let (x0, y0, x1, y1) = bbox(raw);
    let diag = (x1 - x0).hypot(y1 - y0);
    if diag < 12.0 {
        return None;
    }
    let length = path_length(raw);
    let first = raw[0];
    let last = *raw.last().unwrap();

    // Open strokes: only straight lines are recognised.
    if dist(first, last) / length > 0.94 {
        return Some(Shape::Line {
            points: vec![first, last],
        });
    }
    let closed = dist(first, last) < 0.22 * diag && length > 2.0 * diag * 0.9;
    if !closed {
        return None;
    }

    let pts = resample(raw, 96);
    let corners = closed_corners(&pts, 0.06 * diag, 0.12 * diag);
    let (e_err, cx, cy, rx, ry) = ellipse_error(&pts);

    match corners.len() {
        3 => Some(Shape::Triangle { points: corners }),
        4 => {
            let n = 4;
            let right = (0..n).all(|i| {
                let a = angle_at(corners[(i + n - 1) % n], corners[i], corners[(i + 1) % n]);
                (a - 90.0).abs() < 22.0
            });
            if right {
                Some(Shape::Rectangle {
                    points: regularise_rectangle(&corners),
                })
            } else if e_err < 0.06 {
                Some(Shape::Ellipse { cx, cy, rx, ry })
            } else {
                Some(Shape::Polygon { points: corners })
            }
        }
        _ if e_err < 0.12 => {
            // Snap near-circles to perfect circles.
            if (rx - ry).abs() / rx.max(ry) < 0.12 {
                let r = (rx + ry) / 2.0;
                Some(Shape::Ellipse {
                    cx,
                    cy,
                    rx: r,
                    ry: r,
                })
            } else {
                Some(Shape::Ellipse { cx, cy, rx, ry })
            }
        }
        5..=8 => Some(Shape::Polygon { points: corners }),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Deterministic wobble so tests mimic a slightly shaky hand.
    fn wobble(i: usize, amp: f64) -> f64 {
        ((i as f64 * 12.9898).sin() * 43758.5453).fract() * amp
    }

    fn polyline(vertices: &[P2], per_edge: usize, closed: bool, amp: f64) -> Vec<P2> {
        let mut out = Vec::new();
        let edges = if closed {
            vertices.len()
        } else {
            vertices.len() - 1
        };
        let mut k = 0;
        for e in 0..edges {
            let a = vertices[e];
            let b = vertices[(e + 1) % vertices.len()];
            for s in 0..per_edge {
                let t = s as f64 / per_edge as f64;
                out.push([
                    a[0] + t * (b[0] - a[0]) + wobble(k, amp),
                    a[1] + t * (b[1] - a[1]) + wobble(k + 7, amp),
                ]);
                k += 1;
            }
        }
        let end = if closed {
            vertices[0]
        } else {
            *vertices.last().unwrap()
        };
        out.push([end[0] + 2.0, end[1] - 1.5]);
        out
    }

    fn circle(cx: f64, cy: f64, rx: f64, ry: f64, n: usize, amp: f64) -> Vec<P2> {
        (0..=n)
            .map(|i| {
                let t = i as f64 / n as f64 * std::f64::consts::TAU * 0.97;
                [
                    cx + rx * t.cos() + wobble(i, amp),
                    cy + ry * t.sin() + wobble(i + 3, amp),
                ]
            })
            .collect()
    }

    #[test]
    fn recognises_straight_line() {
        let pts = polyline(&[[0.0, 0.0], [200.0, 40.0]], 30, false, 2.0);
        match recognize(&pts) {
            Some(Shape::Line { points }) => assert_eq!(points.len(), 2),
            other => panic!("expected line, got {other:?}"),
        }
    }

    #[test]
    fn recognises_axis_aligned_rectangle_and_snaps_it() {
        let pts = polyline(
            &[[10.0, 10.0], [210.0, 12.0], [208.0, 130.0], [12.0, 128.0]],
            25,
            true,
            2.5,
        );
        match recognize(&pts) {
            Some(Shape::Rectangle { points }) => {
                assert_eq!(points.len(), 4);
                assert_eq!(points[0][1], points[1][1], "top edge must be horizontal");
                assert_eq!(points[1][0], points[2][0], "right edge must be vertical");
            }
            other => panic!("expected rectangle, got {other:?}"),
        }
    }

    #[test]
    fn recognises_rotated_square() {
        let pts = polyline(
            &[[100.0, 0.0], [200.0, 100.0], [100.0, 200.0], [0.0, 100.0]],
            25,
            true,
            2.0,
        );
        match recognize(&pts) {
            Some(Shape::Rectangle { points }) => {
                let d01 = dist(points[0], points[1]);
                let d12 = dist(points[1], points[2]);
                assert!((d01 - d12).abs() < 3.0);
                assert!((angle_at(points[0], points[1], points[2]) - 90.0).abs() < 0.5);
            }
            other => panic!("expected rectangle, got {other:?}"),
        }
    }

    #[test]
    fn recognises_triangle() {
        let pts = polyline(&[[0.0, 200.0], [100.0, 0.0], [200.0, 200.0]], 30, true, 2.0);
        assert!(
            matches!(recognize(&pts), Some(Shape::Triangle { .. })),
            "{:?}",
            recognize(&pts)
        );
    }

    #[test]
    fn recognises_circle_and_ellipse() {
        match recognize(&circle(100.0, 100.0, 80.0, 76.0, 80, 2.0)) {
            Some(Shape::Ellipse { rx, ry, .. }) => {
                assert_eq!(rx, ry, "near-circle should snap to a circle")
            }
            other => panic!("expected circle, got {other:?}"),
        }
        match recognize(&circle(100.0, 100.0, 140.0, 60.0, 80, 1.5)) {
            Some(Shape::Ellipse { rx, ry, .. }) => assert!(rx > ry * 1.8),
            other => panic!("expected ellipse, got {other:?}"),
        }
    }

    #[test]
    fn leaves_scribbles_and_tiny_strokes_alone() {
        let scribble: Vec<P2> = (0..60)
            .map(|i| [i as f64 * 3.0, if i % 2 == 0 { 0.0 } else { 40.0 }])
            .collect();
        assert_eq!(recognize(&scribble), None);
        assert_eq!(
            recognize(&[[0.0, 0.0], [2.0, 2.0], [3.0, 1.0], [4.0, 4.0]]),
            None
        );
    }

    #[test]
    fn simplify_keeps_endpoints_and_pressure() {
        let pts: Vec<InkPoint> = (0..50).map(|i| [i as f64, 0.0, i as f64 / 50.0]).collect();
        let s = simplify(&pts, 0.5);
        assert_eq!(s.len(), 2);
        assert_eq!(s[0], pts[0]);
        assert_eq!(s[1], pts[49]);
        let corner: Vec<InkPoint> = vec![[0.0, 0.0, 0.5], [10.0, 0.0, 0.5], [10.0, 10.0, 0.7]];
        assert_eq!(simplify(&corner, 0.5).len(), 3);
    }
}
