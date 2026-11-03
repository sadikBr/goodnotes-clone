//! Ink geometry on the Rust side: stroke simplification.
//!
//! Shape recognition and beautification run in the editor (TypeScript) so
//! they can be previewed live while the pen is still down.

pub type P2 = [f64; 2];

/// A sampled ink point: x, y and normalised pen pressure (0..=1).
pub type InkPoint = [f64; 3];

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
        for (i, p) in points.iter().enumerate().take(end).skip(start + 1) {
            let d = seg_dist(*p, points[start], points[end]);
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

#[cfg(test)]
mod tests {
    use super::*;

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

    #[test]
    fn simplify_handles_tiny_inputs() {
        assert!(simplify(&[], 1.0).is_empty());
        assert_eq!(simplify(&[[1.0, 2.0, 0.5]], 1.0).len(), 1);
    }
}
