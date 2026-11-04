//! Ink beautification: rough hand-drawn strokes into clean geometry.
//!
//!  1. recognize_shape: line, triangle, rectangle, polygon, ellipse/circle
//!  2. detect_brace:    curly braces in any of the four orientations
//!  3. smooth_path:     everything else; straight runs become straight lines,
//!     sharp turns stay sharp corners, curved runs become smooth splines

use super::{dist, js_round, js_sign, Shape, P2};

fn seg_dist(p: P2, a: P2, b: P2) -> f64 {
    let dx = b[0] - a[0];
    let dy = b[1] - a[1];
    let len2 = dx * dx + dy * dy;
    if len2 == 0.0 {
        return dist(p, a);
    }
    let t = (((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / len2).min(1.0).max(0.0);
    dist(p, [a[0] + t * dx, a[1] + t * dy])
}

pub fn path_length(pts: &[P2]) -> f64 {
    let mut total = 0.0;
    for i in 1..pts.len() {
        total += dist(pts[i - 1], pts[i]);
    }
    total
}

pub(crate) struct BBox {
    pub x0: f64,
    pub y0: f64,
    pub x1: f64,
    pub y1: f64,
    pub w: f64,
    pub h: f64,
    pub diag: f64,
}

pub(crate) fn bbox(pts: &[P2]) -> BBox {
    let (mut x0, mut y0, mut x1, mut y1) = (f64::INFINITY, f64::INFINITY, f64::NEG_INFINITY, f64::NEG_INFINITY);
    for p in pts {
        x0 = x0.min(p[0]);
        y0 = y0.min(p[1]);
        x1 = x1.max(p[0]);
        y1 = y1.max(p[1]);
    }
    BBox { x0, y0, x1, y1, w: x1 - x0, h: y1 - y0, diag: (x1 - x0).hypot(y1 - y0) }
}

/// Ramer–Douglas–Peucker; the indices of kept points.
fn rdp_indices(pts: &[P2], eps: f64) -> Vec<usize> {
    let n = pts.len();
    if n < 3 {
        return (0..n).collect();
    }
    let mut keep = vec![false; n];
    keep[0] = true;
    keep[n - 1] = true;
    let mut stack = vec![(0usize, n - 1)];
    while let Some((s, e)) = stack.pop() {
        let mut max_d = 0.0;
        let mut idx = s;
        for i in s + 1..e {
            let d = seg_dist(pts[i], pts[s], pts[e]);
            if d > max_d {
                max_d = d;
                idx = i;
            }
        }
        if max_d > eps {
            keep[idx] = true;
            stack.push((s, idx));
            stack.push((idx, e));
        }
    }
    (0..n).filter(|&i| keep[i]).collect()
}

/// Resamples to `n` points evenly spaced along the path.
pub fn resample_count(pts: &[P2], n: usize) -> Vec<P2> {
    let total = path_length(pts);
    if pts.len() < 2 || total == 0.0 || n < 2 {
        return pts.to_vec();
    }
    let mut out = resample_spacing(pts, total / (n as f64 - 1.0));
    out.truncate(n);
    out
}

/// Resamples to points `step` apart along the path (endpoints kept).
pub fn resample_spacing(pts: &[P2], step: f64) -> Vec<P2> {
    if pts.len() < 2 || step <= 0.0 {
        return pts.to_vec();
    }
    let mut out = vec![pts[0]];
    let mut acc = 0.0;
    let mut prev = pts[0];
    let mut i = 1;
    while i < pts.len() {
        let cur = pts[i];
        let d = dist(prev, cur);
        if d > 0.0 && acc + d >= step {
            let t = (step - acc) / d;
            let q = [prev[0] + t * (cur[0] - prev[0]), prev[1] + t * (cur[1] - prev[1])];
            out.push(q);
            prev = q;
            acc = 0.0;
        } else {
            acc += d;
            prev = cur;
            i += 1;
        }
    }
    let last = *pts.last().unwrap();
    if dist(*out.last().unwrap(), last) > step * 0.25 {
        out.push(last);
    } else {
        *out.last_mut().unwrap() = last;
    }
    out
}

/// Interior angle at b, in degrees (180 = straight).
fn angle_at(a: P2, b: P2, c: P2) -> f64 {
    let (v1x, v1y) = (a[0] - b[0], a[1] - b[1]);
    let (v2x, v2y) = (c[0] - b[0], c[1] - b[1]);
    let n = v1x.hypot(v1y) * v2x.hypot(v2y);
    if n == 0.0 {
        return 180.0;
    }
    ((v1x * v2x + v1y * v2y) / n).min(1.0).max(-1.0).acos() * 180.0 / std::f64::consts::PI
}

// ------------------------------------------------------- basic shapes

fn closed_corners(pts: &[P2], eps: f64, min_gap: f64) -> Vec<P2> {
    let start = pts[0];
    let mut far = 0;
    for (i, p) in pts.iter().enumerate() {
        if dist(*p, start) > dist(pts[far], start) {
            far = i;
        }
    }
    let first: Vec<P2> = pts[..=far].to_vec();
    let mut second: Vec<P2> = pts[far..].to_vec();
    second.push(start);
    let mut corners: Vec<P2> = rdp_indices(&first, eps).into_iter().map(|i| first[i]).collect();
    corners.pop();
    corners.extend(rdp_indices(&second, eps).into_iter().map(|i| second[i]));
    corners.pop();
    let mut merged: Vec<P2> = Vec::new();
    for c in corners {
        if merged.is_empty() || dist(*merged.last().unwrap(), c) >= min_gap {
            merged.push(c);
        }
    }
    while merged.len() > 2 && dist(merged[0], *merged.last().unwrap()) < min_gap {
        merged.pop();
    }
    loop {
        let n = merged.len();
        if n <= 3 {
            break;
        }
        let found = (0..n).find(|&k| angle_at(merged[(k + n - 1) % n], merged[k], merged[(k + 1) % n]) > 160.0);
        match found {
            Some(i) => {
                merged.remove(i);
            }
            None => break,
        }
    }
    merged
}

struct EllipseFit {
    err: f64,
    cx: f64,
    cy: f64,
    rx: f64,
    ry: f64,
}

fn ellipse_fit(pts: &[P2]) -> EllipseFit {
    let b = bbox(pts);
    let cx = (b.x0 + b.x1) / 2.0;
    let cy = (b.y0 + b.y1) / 2.0;
    let rx = (b.w / 2.0).max(1e-6);
    let ry = (b.h / 2.0).max(1e-6);
    let mut s = 0.0;
    for p in pts {
        s += (((p[0] - cx) / rx).hypot((p[1] - cy) / ry) - 1.0).abs();
    }
    EllipseFit { err: s / pts.len() as f64, cx, cy, rx, ry }
}

fn regularise_rectangle(c: &[P2]) -> Vec<P2> {
    let edge = [c[1][0] - c[0][0], c[1][1] - c[0][1]];
    let ang = ((edge[1].atan2(edge[0]) * 180.0 / std::f64::consts::PI) % 90.0 + 90.0) % 90.0;
    if !(8.0..=82.0).contains(&ang) {
        let b = bbox(c);
        return vec![[b.x0, b.y0], [b.x1, b.y0], [b.x1, b.y1], [b.x0, b.y1]];
    }
    let cx = c.iter().fold(0.0, |s, p| s + p[0]) / 4.0;
    let cy = c.iter().fold(0.0, |s, p| s + p[1]) / 4.0;
    let len = edge[0].hypot(edge[1]);
    let u = [edge[0] / len, edge[1] / len];
    let v = [-u[1], u[0]];
    let w = (dist(c[0], c[1]) + dist(c[2], c[3])) / 4.0;
    let h = (dist(c[1], c[2]) + dist(c[3], c[0])) / 4.0;
    let cross = (c[1][0] - c[0][0]) * (c[2][1] - c[1][1]) - (c[1][1] - c[0][1]) * (c[2][0] - c[1][0]);
    let s = if cross >= 0.0 { 1.0 } else { -1.0 };
    let corner = |a: f64, b: f64| [cx + a * w * u[0] + b * s * h * v[0], cy + a * w * u[1] + b * s * h * v[1]];
    vec![corner(-1.0, -1.0), corner(1.0, -1.0), corner(1.0, 1.0), corner(-1.0, 1.0)]
}

/// Recognises a rough stroke as a basic geometric shape, or None.
pub fn recognize_shape(raw: &[P2]) -> Option<Shape> {
    if raw.len() < 4 {
        return None;
    }
    let b = bbox(raw);
    if b.diag < 12.0 {
        return None;
    }
    let length = path_length(raw);
    let first = raw[0];
    let last = *raw.last().unwrap();
    if dist(first, last) / length > 0.94 {
        return Some(Shape::Line { points: vec![first, snap_to_axis(first, last, 4.0)] });
    }
    let closed = dist(first, last) < 0.22 * b.diag && length > 2.0 * b.diag * 0.9;
    if !closed {
        return None;
    }
    let pts = resample_count(raw, 96);
    let corners = closed_corners(&pts, 0.06 * b.diag, 0.12 * b.diag);
    let e = ellipse_fit(&pts);
    if corners.len() == 3 {
        return Some(Shape::Triangle { points: corners });
    }
    if corners.len() == 4 {
        let right = (0..4).all(|i| (angle_at(corners[(i + 3) % 4], corners[i], corners[(i + 1) % 4]) - 90.0).abs() < 22.0);
        if right {
            return Some(Shape::Rectangle { points: regularise_rectangle(&corners) });
        }
        if e.err < 0.06 {
            return Some(Shape::Ellipse { cx: e.cx, cy: e.cy, rx: e.rx, ry: e.ry });
        }
        return Some(Shape::Polygon { points: corners });
    }
    if e.err < 0.12 {
        if (e.rx - e.ry).abs() / e.rx.max(e.ry) < 0.12 {
            let r = (e.rx + e.ry) / 2.0;
            return Some(Shape::Ellipse { cx: e.cx, cy: e.cy, rx: r, ry: r });
        }
        return Some(Shape::Ellipse { cx: e.cx, cy: e.cy, rx: e.rx, ry: e.ry });
    }
    if (5..=8).contains(&corners.len()) {
        return Some(Shape::Polygon { points: corners });
    }
    None
}

// ------------------------------------------------------- helpers

/// Snaps the direction a→b to exactly horizontal/vertical when within `tolerance_deg`.
pub fn snap_to_axis(a: P2, b: P2, tolerance_deg: f64) -> P2 {
    let dx = b[0] - a[0];
    let dy = b[1] - a[1];
    let ang = (dy.atan2(dx) * 180.0 / std::f64::consts::PI).abs();
    let off_h = ang.min(180.0 - ang);
    let off_v = (90.0 - ang).abs();
    if off_h <= tolerance_deg {
        return [b[0], a[1]];
    }
    if off_v <= tolerance_deg {
        return [a[0], b[1]];
    }
    b
}

/// Taubin smoothing (alternating shrink / inflate Laplacian steps); the first
/// and last points stay fixed.
pub fn taubin(pts: &[P2], iterations: usize) -> Vec<P2> {
    let mut p: Vec<P2> = pts.to_vec();
    let n = p.len();
    if n < 3 {
        return p;
    }
    for _ in 0..iterations {
        for f in [0.5, -0.53] {
            let mut q = p.clone();
            for i in 1..n - 1 {
                q[i][0] = p[i][0] + f * ((p[i - 1][0] + p[i + 1][0]) / 2.0 - p[i][0]);
                q[i][1] = p[i][1] + f * ((p[i - 1][1] + p[i + 1][1]) / 2.0 - p[i][1]);
            }
            p = q;
        }
    }
    p
}

fn max_deviation(seg: &[P2]) -> f64 {
    let a = seg[0];
    let z = *seg.last().unwrap();
    seg.iter().fold(0.0, |m: f64, p| m.max(seg_dist(*p, a, z)))
}

/// Least-squares circle (Kåsa fit).
fn fit_circle(pts: &[P2]) -> Option<(f64, f64, f64)> {
    let n = pts.len();
    if n < 5 {
        return None;
    }
    let nf = n as f64;
    let mx = pts.iter().fold(0.0, |s, p| s + p[0]) / nf;
    let my = pts.iter().fold(0.0, |s, p| s + p[1]) / nf;
    let (mut sxx, mut syy, mut sxy, mut sxz, mut syz, mut sz) = (0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    for p in pts {
        let x = p[0] - mx;
        let y = p[1] - my;
        let z = x * x + y * y;
        sxx += x * x;
        syy += y * y;
        sxy += x * y;
        sxz += x * z;
        syz += y * z;
        sz += z;
    }
    let det = sxx * syy - sxy * sxy;
    if det.abs() < 1e-9 {
        return None;
    }
    let a = (sxz * syy - syz * sxy) / det;
    let b = (syz * sxx - sxz * sxy) / det;
    let cx = a / 2.0;
    let cy = b / 2.0;
    let r = (cx * cx + cy * cy + sz / nf).sqrt();
    if !r.is_finite() || r <= 0.0 {
        return None;
    }
    Some((cx + mx, cy + my, r))
}

/// If the run is close to a circular arc, that arc (endpoints kept).
pub fn fit_arc(seg: &[P2]) -> Option<Vec<P2>> {
    let (cx, cy, r) = fit_circle(seg)?;
    let length = path_length(seg);
    if r > length * 2.5 {
        return None; // too flat to call an arc
    }
    let resid = seg.iter().fold(0.0, |s, p| s + ((p[0] - cx).hypot(p[1] - cy) - r).abs()) / seg.len() as f64;
    if resid > 1.2f64.max(0.035 * r) {
        return None;
    }
    let pi = std::f64::consts::PI;
    let mut total = 0.0;
    let mut prev = (seg[0][1] - cy).atan2(seg[0][0] - cx);
    let a0 = prev;
    let mut back = 0usize;
    for p in &seg[1..] {
        let a = (p[1] - cy).atan2(p[0] - cx);
        let mut d = a - prev;
        if d > pi {
            d -= 2.0 * pi;
        }
        if d < -pi {
            d += 2.0 * pi;
        }
        if total != 0.0 && js_sign(d) != js_sign(total) && d.abs() > 0.02 {
            back += 1;
        }
        total += d;
        prev = a;
    }
    let sweep = total.abs() * (180.0 / pi);
    if !(35.0..=330.0).contains(&sweep) || back as f64 > seg.len() as f64 * 0.1 {
        return None;
    }
    let steps = 8f64.max((sweep / 4.0).ceil()) as usize;
    let mut out = Vec::with_capacity(steps + 1);
    for i in 0..=steps {
        let t = a0 + (total * i as f64) / steps as f64;
        out.push([cx + t.cos() * r, cy + t.sin() * r]);
    }
    out[0] = seg[0];
    *out.last_mut().unwrap() = *seg.last().unwrap();
    Some(out)
}

/// Cubic B-spline basis values for clamped uniform knots (Cox–de Boor).
fn bspline_basis(t: f64, m: usize) -> Vec<f64> {
    let k = 3usize;
    let nk = m + k + 1;
    let knots: Vec<f64> = (0..nk).map(|i| ((i as f64 - k as f64) / (m as f64 - k as f64)).max(0.0).min(1.0)).collect();
    let tt = t.min(1.0 - 1e-9);
    let mut n: Vec<f64> = (0..nk - 1).map(|i| if knots[i] <= tt && tt < knots[i + 1] { 1.0 } else { 0.0 }).collect();
    for d in 1..=k {
        let mut next = vec![0.0; nk - 1 - d];
        for i in 0..next.len() {
            let a = knots[i + d] - knots[i];
            let b = knots[i + d + 1] - knots[i + 1];
            next[i] = (if a > 0.0 { ((tt - knots[i]) / a) * n[i] } else { 0.0 }) + (if b > 0.0 { ((knots[i + d + 1] - tt) / b) * n[i + 1] } else { 0.0 });
        }
        n = next;
    }
    n
}

/// Solves a small dense linear system (Gaussian elimination, partial pivoting).
fn solve(a: &[Vec<f64>], b: &[f64]) -> Option<Vec<f64>> {
    let n = b.len();
    let mut m: Vec<Vec<f64>> = a.iter().enumerate().map(|(i, row)| {
        let mut r = row.clone();
        r.push(b[i]);
        r
    }).collect();
    for c in 0..n {
        let mut piv = c;
        for r in c + 1..n {
            if m[r][c].abs() > m[piv][c].abs() {
                piv = r;
            }
        }
        if m[piv][c].abs() < 1e-12 {
            return None;
        }
        m.swap(c, piv);
        for r in c + 1..n {
            let f = m[r][c] / m[c][c];
            if f != 0.0 {
                for j in c..=n {
                    m[r][j] -= f * m[c][j];
                }
            }
        }
    }
    let mut x = vec![0.0; n];
    for r in (0..n).rev() {
        let mut acc = m[r][n];
        for j in r + 1..n {
            acc -= m[r][j] * x[j];
        }
        x[r] = acc / m[r][r];
    }
    Some(x)
}

/// Fits a smooth cubic B-spline through a run by least squares (ends pinned,
/// with a light bending penalty), sampled densely.
pub fn fit_smooth_curve(seg: &[P2], spacing: f64) -> Vec<P2> {
    let l = path_length(seg);
    let m = 4f64.max(48f64.min(js_round(l / spacing) + 3.0)) as usize;
    let n = seg.len();
    if n < m + 2 {
        return seg.to_vec();
    }
    let mut t = vec![0.0];
    for i in 1..n {
        let prev = t[i - 1];
        t.push(prev + dist(seg[i - 1], seg[i]));
    }
    let basis: Vec<Vec<f64>> = t.iter().map(|v| bspline_basis(v / l, m)).collect();
    let free = m - 2;
    let mut ata = vec![vec![0.0; free]; free];
    let mut rx = vec![0.0; free];
    let mut ry = vec![0.0; free];
    let p0 = seg[0];
    let pn = seg[n - 1];
    for s in 0..n {
        let row = &basis[s];
        let fx = seg[s][0] - row[0] * p0[0] - row[m - 1] * pn[0];
        let fy = seg[s][1] - row[0] * p0[1] - row[m - 1] * pn[1];
        for i in 1..m - 1 {
            if row[i] == 0.0 {
                continue;
            }
            rx[i - 1] += row[i] * fx;
            ry[i - 1] += row[i] * fy;
            for j in 1..m - 1 {
                if row[j] != 0.0 {
                    ata[i - 1][j - 1] += row[i] * row[j];
                }
            }
        }
    }
    // Bending penalty on second differences of the control polygon.
    let lambda = 0.02 * (n as f64 / m as f64);
    for i in 0..m - 2 {
        let idx = [i as i64, i as i64 + 1, i as i64 + 2];
        let w = [1.0, -2.0, 1.0];
        for a in 0..3 {
            for b in 0..3 {
                let ia = idx[a] - 1;
                let ib = idx[b] - 1;
                let v = lambda * w[a] * w[b];
                let free_i = free as i64;
                if ia >= 0 && ia < free_i && ib >= 0 && ib < free_i {
                    ata[ia as usize][ib as usize] += v;
                } else if ia >= 0 && ia < free_i {
                    let fixed = if idx[b] == 0 { p0 } else { pn };
                    rx[ia as usize] -= v * fixed[0];
                    ry[ia as usize] -= v * fixed[1];
                }
            }
        }
    }
    let (Some(cx), Some(cy)) = (solve(&ata, &rx), solve(&ata, &ry)) else {
        return seg.to_vec();
    };
    let mut ctrl = vec![p0];
    for i in 0..cx.len() {
        ctrl.push([cx[i], cy[i]]);
    }
    ctrl.push(pn);
    let samples = 16f64.max(400f64.min(js_round(l / 2.5))) as usize;
    let mut out = Vec::with_capacity(samples + 1);
    for i in 0..=samples {
        let b = bspline_basis(i as f64 / samples as f64, m);
        let (mut x, mut y) = (0.0, 0.0);
        for j in 0..m {
            x += b[j] * ctrl[j][0];
            y += b[j] * ctrl[j][1];
        }
        out.push([x, y]);
    }
    out[0] = p0;
    *out.last_mut().unwrap() = pn;
    out
}

/// Turning angle (degrees, 180 = straight) at each point, over ±w points.
fn turning(pts: &[P2], w: usize) -> Vec<f64> {
    (0..pts.len()).map(|i| if i < w || i + w >= pts.len() { 180.0 } else { angle_at(pts[i - w], pts[i], pts[i + w]) }).collect()
}

/// Indices of sharp corners: clear local minima of the turning angle.
fn find_corners(pts: &[P2], w: usize, max_angle: f64) -> Vec<usize> {
    let ang = turning(pts, w);
    let mut out = Vec::new();
    if pts.len() < 2 * w {
        return out;
    }
    for i in w..pts.len() - w {
        if ang[i] >= max_angle {
            continue;
        }
        let mut is_min = true;
        for j in i.saturating_sub(w)..=(i + w).min(pts.len() - 1) {
            if ang[j] < ang[i] || (ang[j] == ang[i] && j < i) {
                is_min = false;
                break;
            }
        }
        if is_min {
            out.push(i);
        }
    }
    out
}

// ------------------------------------------------------------- braces

fn quad(a: P2, c: P2, b: P2, steps: usize, out: &mut Vec<P2>) {
    for i in 1..=steps {
        let t = i as f64 / steps as f64;
        let m = 1.0 - t;
        out.push([m * m * a[0] + 2.0 * m * t * c[0] + t * t * b[0], m * m * a[1] + 2.0 * m * t * c[1] + t * t * b[1]]);
    }
}

/// A clean brace path in (u, v): u runs along the brace, v across it.
pub fn brace_path(u0: f64, u1: f64, tip_u: f64, tip_v: f64, end_v: f64) -> Vec<P2> {
    let spine = (tip_v + end_v) / 2.0;
    let half = (end_v - spine).abs();
    let q = 1f64.max((half * 1.4).min((tip_u - u0) * 0.4).min((u1 - tip_u) * 0.4));
    let mut out = vec![[u0, end_v]];
    quad([u0, end_v], [u0, spine], [u0 + q, spine], 12, &mut out);
    out.push([tip_u - q, spine]);
    quad([tip_u - q, spine], [tip_u, spine], [tip_u, tip_v], 12, &mut out);
    quad([tip_u, tip_v], [tip_u, spine], [tip_u + q, spine], 12, &mut out);
    out.push([u1 - q, spine]);
    quad([u1 - q, spine], [u1, spine], [u1, end_v], 12, &mut out);
    out
}

fn dist_to_polyline(p: P2, line: &[P2]) -> f64 {
    let mut best = f64::INFINITY;
    for i in 1..line.len() {
        best = best.min(seg_dist(p, line[i - 1], line[i]));
    }
    best
}

/// Detects a curly brace ({, }, or the same turned sideways) by fitting clean
/// brace templates to the stroke.
pub fn detect_brace(raw: &[P2]) -> Option<Shape> {
    if raw.len() < 6 {
        return None;
    }
    let first = raw[0];
    let last = *raw.last().unwrap();
    let axis_len = dist(first, last);
    if axis_len < 30.0 {
        return None;
    }
    let ang = (last[1] - first[1]).atan2(last[0] - first[0]);
    let (cos, sin) = (ang.cos(), ang.sin());
    let to_uv = |p: &P2| [(p[0] - first[0]) * cos + (p[1] - first[1]) * sin, -(p[0] - first[0]) * sin + (p[1] - first[1]) * cos];
    let from_uv = |p: &P2| [first[0] + p[0] * cos - p[1] * sin, first[1] + p[0] * sin + p[1] * cos];
    let uv: Vec<P2> = raw.iter().map(to_uv).collect();
    let pts = taubin(&resample_count(&uv, 90), 4);
    let lb = bbox(&pts);
    let l = lb.w;
    let w = lb.h;
    if w < 4.0 || l < 1.8 * w {
        return None;
    }
    let travel = pts.last().unwrap()[0] - pts[0][0];
    if travel.abs() < 0.75 * l {
        return None;
    }
    let dir = js_sign(travel);
    let mut back = 0;
    for i in 1..pts.len() {
        if (pts[i][0] - pts[i - 1][0]) * dir < -0.25 {
            back += 1;
        }
    }
    if back as f64 > pts.len() as f64 * 0.15 {
        return None;
    }
    let mut best: Option<(f64, Vec<P2>)> = None;
    for tip_low in [true, false] {
        let tip_v = if tip_low { lb.y0 } else { lb.y1 };
        let end_v = if tip_low { lb.y1 } else { lb.y0 };
        let mut tip: i64 = -1;
        for (i, p) in pts.iter().enumerate() {
            let pos = (p[0] - lb.x0) / l;
            if !(0.2..=0.8).contains(&pos) {
                continue;
            }
            if tip < 0 || (if tip_low { p[1] < pts[tip as usize][1] } else { p[1] > pts[tip as usize][1] }) {
                tip = i as i64;
            }
        }
        if tip < 0 {
            continue;
        }
        let tp = pts[tip as usize];
        let reach = (tp[1] - end_v).abs();
        if reach < 0.75 * w {
            continue;
        }
        let ends = [pts[0], *pts.last().unwrap()];
        if ends.iter().any(|e| (e[1] - end_v).abs() > 0.55 * w) {
            continue;
        }
        let path = brace_path(lb.x0, lb.x1, tp[0], tip_v, end_v);
        let d1 = pts.iter().fold(0.0, |s, p| s + dist_to_polyline(*p, &path)) / pts.len() as f64;
        let dense = resample_count(&path, 90);
        let d2 = dense.iter().fold(0.0, |s, p| s + dist_to_polyline(*p, &pts)) / dense.len() as f64;
        let err = (d1 + d2) / 2.0 / w;
        // A plain "V" must not count: a brace fits its template clearly better than two lines.
        let vee = [pts[0], tp, *pts.last().unwrap()];
        let v1 = pts.iter().fold(0.0, |acc, p| acc + dist_to_polyline(*p, &vee)) / pts.len() as f64;
        let vee_dense = resample_count(&vee, 60);
        let v2 = vee_dense.iter().fold(0.0, |acc, p| acc + dist_to_polyline(*p, &pts)) / vee_dense.len() as f64;
        let vee_err = (v1 + v2) / 2.0 / w;
        if err > vee_err * 0.75 {
            continue;
        }
        if best.as_ref().map_or(true, |(e, _)| err < *e) {
            best = Some((err, path));
        }
    }
    let (err, path) = best?;
    if err > 0.17 {
        return None;
    }
    Some(Shape::Path { points: path.iter().map(from_uv).collect(), closed: false, breaks: None })
}

// ---------------------------------------------------------- smoothing

/// Smooths a freehand stroke while keeping its intent (straight runs
/// straight, sharp turns sharp, near-circular runs true arcs).
pub fn smooth_path(raw: &[P2]) -> (Vec<P2>, bool) {
    let b = bbox(raw);
    if raw.len() < 3 || b.diag < 6.0 {
        return (raw.to_vec(), false);
    }
    let step = 1f64.max(3f64.min(b.diag / 150.0));
    let mut pts = resample_spacing(raw, step);
    if pts.len() < 4 {
        return (raw.to_vec(), false);
    }
    let total = path_length(&pts);
    let closed = dist(pts[0], *pts.last().unwrap()) < 6f64.max(0.08 * b.diag) && total > 2.2 * b.w.max(b.h);
    if closed {
        pts.pop();
        pts.push(pts[0]);
    }
    let w = 3f64.max(js_round(12f64.max(0.035 * b.diag) / step)) as usize;
    let mut corners = vec![0usize];
    corners.extend(find_corners(&taubin(&pts, 6), w, 120.0));
    corners.push(pts.len() - 1);
    let scale = 2.5f64.max(12f64.min(0.02 * b.diag));
    let spacing = 14f64.max(45f64.min(0.09 * b.diag));
    let iterations = 6f64.max(150f64.min(js_round(2.0 * (scale / step).powi(2)))) as usize;
    let mut runs: Vec<(bool, Vec<P2>)> = Vec::new();
    for s in 1..corners.len() {
        let seg: Vec<P2> = pts[corners[s - 1]..=corners[s]].to_vec();
        if seg.len() < 2 {
            continue;
        }
        let a = seg[0];
        let z = *seg.last().unwrap();
        let chord = dist(a, z);
        if seg.len() <= 3 || max_deviation(&seg) < 2.5f64.max(0.035 * chord) {
            runs.push((true, vec![a, z]));
            continue;
        }
        let sm = taubin(&seg, iterations);
        if max_deviation(&sm) < 2f64.max(0.04 * chord) {
            runs.push((true, vec![a, z]));
            continue;
        }
        if let Some(arc) = fit_arc(&sm) {
            runs.push((false, arc));
            continue;
        }
        runs.push((false, fit_smooth_curve(&sm, spacing)));
    }
    let mut out = vec![if let Some(r) = runs.first() { r.1[0] } else { pts[0] }];
    let mut off = [0.0, 0.0];
    for (straight, points) in &runs {
        let shifted: Vec<P2> = points.iter().map(|p| [p[0] + off[0], p[1] + off[1]]).collect();
        if *straight && !closed {
            let start = *out.last().unwrap();
            let end = snap_to_axis(start, shifted[1], 4.0);
            off = [off[0] + end[0] - shifted[1][0], off[1] + end[1] - shifted[1][1]];
            out.push(end);
        } else {
            out.extend_from_slice(&shifted[1..]);
        }
    }
    if closed {
        let first = out[0];
        *out.last_mut().unwrap() = first;
    }
    (out, closed)
}

/// Full beautification: a basic shape, a brace, or a smoothed path. None for
/// taps and tiny marks.
pub fn beautify(raw: &[P2]) -> Option<Shape> {
    if raw.len() < 2 {
        return None;
    }
    if let Some(s) = recognize_shape(raw) {
        return Some(s);
    }
    if let Some(b) = detect_brace(raw) {
        return Some(b);
    }
    if bbox(raw).diag < 6.0 {
        return None;
    }
    let (points, closed) = smooth_path(raw);
    Some(Shape::Path { points, closed, breaks: None })
}
