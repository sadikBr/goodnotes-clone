//! The recognisers' tests (were src/editor/beautify.test.ts, symbols.test.ts
//! and symbols.samples.ts).

use std::f64::consts::PI;

use super::beautify::{beautify, brace_path, detect_brace, recognize_shape, smooth_path};
use super::symbols::{match_symbol, model, symbol_shape, NAMES};
use super::{Shape, P2};

// --- helpers -------------------------------------------------------------------------

/// Deterministic wobble so tests mimic a slightly shaky hand.
fn wobble(i: f64, amp: f64) -> f64 {
    ((((i * 12.9898).sin() * 43758.5453) % 1.0) + 1.0) % 1.0 * amp
}

fn polyline(vertices: &[P2], per_edge: usize, closed: bool, amp: f64) -> Vec<P2> {
    let mut out = Vec::new();
    let edges = if closed { vertices.len() } else { vertices.len() - 1 };
    let mut k = 0.0;
    for e in 0..edges {
        let a = vertices[e];
        let b = vertices[(e + 1) % vertices.len()];
        for s in 0..per_edge {
            let t = s as f64 / per_edge as f64;
            out.push([a[0] + t * (b[0] - a[0]) + wobble(k, amp), a[1] + t * (b[1] - a[1]) + wobble(k + 7.0, amp)]);
            k += 1.0;
        }
    }
    let end = if closed { vertices[0] } else { *vertices.last().unwrap() };
    out.push([end[0] + 2.0, end[1] - 1.5]);
    out
}

fn circle(cx: f64, cy: f64, rx: f64, ry: f64, n: usize, amp: f64) -> Vec<P2> {
    (0..=n)
        .map(|i| {
            let t = (i as f64 / n as f64) * PI * 2.0 * 0.97;
            [cx + rx * t.cos() + wobble(i as f64, amp), cy + ry * t.sin() + wobble(i as f64 + 3.0, amp)]
        })
        .collect()
}

/// Largest change of direction between consecutive segments, in degrees.
fn max_turn(pts: &[P2]) -> f64 {
    let mut max: f64 = 0.0;
    for i in 2..pts.len() {
        let a = (pts[i - 1][1] - pts[i - 2][1]).atan2(pts[i - 1][0] - pts[i - 2][0]);
        let b = (pts[i][1] - pts[i - 1][1]).atan2(pts[i][0] - pts[i - 1][0]);
        let mut d = (b - a).abs();
        if d > PI {
            d = 2.0 * PI - d;
        }
        max = max.max(d * 180.0 / PI);
    }
    max
}

fn kind(s: &Option<Shape>) -> &'static str {
    match s {
        None => "none",
        Some(Shape::Line { .. }) => "line",
        Some(Shape::Arrow { .. }) => "arrow",
        Some(Shape::Triangle { .. }) => "triangle",
        Some(Shape::Rectangle { .. }) => "rectangle",
        Some(Shape::Polygon { .. }) => "polygon",
        Some(Shape::Ellipse { .. }) => "ellipse",
        Some(Shape::Path { .. }) => "path",
    }
}

fn path_points(s: &Option<Shape>) -> Vec<P2> {
    match s {
        Some(Shape::Path { points, .. }) => points.clone(),
        _ => panic!("expected a path, got {}", kind(s)),
    }
}

fn close(a: f64, b: f64, digits: i32) -> bool {
    (a - b).abs() < 0.5 * 10f64.powi(-digits)
}

// --- recognize_shape -----------------------------------------------------------------

#[test]
fn recognises_lines_rectangles_triangles_and_circles() {
    assert_eq!(kind(&recognize_shape(&polyline(&[[0.0, 0.0], [200.0, 40.0]], 30, false, 2.0))), "line");
    let rect = recognize_shape(&polyline(&[[10.0, 10.0], [210.0, 12.0], [208.0, 130.0], [12.0, 128.0]], 25, true, 2.5));
    match &rect {
        Some(Shape::Rectangle { points }) => assert_eq!(points[0][1], points[1][1]),
        _ => panic!("expected a rectangle, got {}", kind(&rect)),
    }
    assert_eq!(kind(&recognize_shape(&polyline(&[[0.0, 200.0], [100.0, 0.0], [200.0, 200.0]], 30, true, 2.0))), "triangle");
    match recognize_shape(&circle(100.0, 100.0, 80.0, 76.0, 80, 2.0)) {
        Some(Shape::Ellipse { rx, ry, .. }) => assert_eq!(rx, ry),
        other => panic!("expected an ellipse, got {}", kind(&other)),
    }
}

#[test]
fn leaves_scribbles_and_tiny_marks_alone() {
    let scribble: Vec<P2> = (0..60).map(|i| [i as f64 * 3.0, if i % 2 == 1 { 40.0 } else { 0.0 }]).collect();
    assert!(recognize_shape(&scribble).is_none());
    assert!(recognize_shape(&[[0.0, 0.0], [2.0, 2.0], [3.0, 1.0], [4.0, 4.0]]).is_none());
}

// --- detect_brace -----------------------------------------------------------------------

/// A rough "{": ends on the right, sharp point on the left half-way down.
fn rough_brace() -> Vec<P2> {
    (0..=60)
        .map(|i| {
            let t = i as f64 / 60.0;
            let y = t * 200.0;
            let from_tip = (t - 0.5).abs();
            let x = if from_tip < 0.08 {
                10.0 + (from_tip / 0.08) * 12.0
            } else if t < 0.08 || t > 0.92 {
                22.0 + if t.min(1.0 - t) < 0.04 { 14.0 } else { 6.0 }
            } else {
                22.0
            };
            [x + wobble(i as f64, 1.5), y]
        })
        .collect()
}

#[test]
fn recognises_an_opening_brace_and_redraws_it_cleanly() {
    let b = detect_brace(&rough_brace());
    let closed = matches!(b, Some(Shape::Path { closed: true, .. }));
    assert!(!closed);
    let pts = path_points(&b);
    let xs: Vec<f64> = pts.iter().map(|p| p[0]).collect();
    let ys: Vec<f64> = pts.iter().map(|p| p[1]).collect();
    let min_x = xs.iter().cloned().fold(f64::INFINITY, f64::min);
    let max_x = xs.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    let tip = pts[xs.iter().position(|&x| x == min_x).unwrap()];
    assert!(tip[1] > 70.0 && tip[1] < 130.0);
    assert!(close(pts[0][0], max_x, 0));
    assert!(close(ys.iter().cloned().fold(f64::INFINITY, f64::min), 0.0, 0));
    assert!(close(ys.iter().cloned().fold(f64::NEG_INFINITY, f64::max), 200.0, 0));
}

#[test]
fn recognises_closing_and_sideways_braces() {
    let closing: Vec<P2> = rough_brace().iter().map(|p| [60.0 - p[0], p[1]]).collect();
    let c = path_points(&detect_brace(&closing));
    assert!(c.iter().map(|p| p[0]).fold(f64::NEG_INFINITY, f64::max) > c[0][0]);
    let under: Vec<P2> = rough_brace().iter().map(|p| [p[1], p[0]]).collect();
    assert_eq!(kind(&detect_brace(&under)), "path");
}

#[test]
fn does_not_mistake_a_parenthesis_or_a_straight_line_for_a_brace() {
    let paren: Vec<P2> = (0..50)
        .map(|i| {
            let t = i as f64 / 49.0;
            [30.0 - (t * PI).sin() * 20.0, t * 200.0]
        })
        .collect();
    assert!(detect_brace(&paren).is_none());
    assert!(detect_brace(&polyline(&[[0.0, 0.0], [0.0, 200.0]], 30, false, 1.0)).is_none());
}

#[test]
fn builds_a_symmetric_brace_template() {
    let pts = brace_path(0.0, 100.0, 50.0, 0.0, 20.0);
    assert_eq!(pts[0], [0.0, 20.0]);
    assert!(close(pts.last().unwrap()[0], 100.0, 2));
}

// --- smooth_path ------------------------------------------------------------------------

#[test]
fn turns_hand_drawn_graph_axes_into_two_straight_lines() {
    let axes = polyline(&[[0.0, 0.0], [3.0, 200.0], [260.0, 204.0]], 40, false, 2.5);
    let (points, _) = smooth_path(&axes);
    assert!(points.len() <= 4);
    assert!(close(points[0][0], axes[0][0], 0));
}

#[test]
fn smooths_a_jagged_curve() {
    let jagged: Vec<P2> = (0..120).map(|i| [i as f64 * 3.0, 100.0 - 60.0 * (i as f64 / 20.0).sin() + wobble(i as f64, 5.0)]).collect();
    let (points, _) = smooth_path(&jagged);
    assert!(max_turn(&points) < max_turn(&jagged) / 2.0);
    assert!(close(points[0][0], jagged[0][0], 0));
    assert!(close(points.last().unwrap()[0], jagged.last().unwrap()[0], 0));
}

#[test]
fn closes_loops_that_end_where_they_started() {
    let blob: Vec<P2> = (0..80)
        .map(|i| {
            let t = (i as f64 / 79.0) * PI * 2.0;
            [100.0 + t.cos() * (60.0 + 20.0 * (3.0 * t).sin()), 100.0 + t.sin() * 60.0]
        })
        .collect();
    assert!(smooth_path(&blob).1);
}

#[test]
fn beautify_prefers_basic_shapes_then_braces_then_smoothing() {
    assert_eq!(kind(&beautify(&circle(100.0, 100.0, 80.0, 76.0, 80, 2.0))), "ellipse");
    let wave: Vec<P2> = (0..80).map(|i| [i as f64 * 4.0, 50.0 + (i as f64 / 8.0).sin() * 30.0 + wobble(i as f64, 3.0)]).collect();
    assert_eq!(kind(&beautify(&wave)), "path");
    assert!(beautify(&[[0.0, 0.0], [1.0, 1.0]]).is_none());
}

#[test]
fn does_not_turn_hand_wobble_into_corners() {
    let shaky: Vec<P2> = (0..91)
        .map(|i| {
            let t = i as f64 / 90.0;
            let fi = i as f64;
            [fi * 4.2 + wobble(fi, 2.0), 300.0 - 160.0 * (0.45 * (t * 3.4).sin() + 0.7 * t * t) + wobble(fi + 9.0, 4.0) + if i % 7 == 0 { 4.0 } else { 0.0 }]
        })
        .collect();
    assert!(max_turn(&smooth_path(&shaky).0) < 25.0);
}

/// A hand-drawn "{": thin, slanted, wobbly, with an off-centre point.
fn hand_brace(tip_at: f64, slant_deg: f64, amp: f64, width: f64) -> Vec<P2> {
    let ideal = brace_path(0.0, 300.0, 300.0 * tip_at, 0.0, width);
    let pts = resample_ideal(&ideal, 70);
    let s = (slant_deg * PI / 180.0).sin();
    pts.iter().enumerate().map(|(i, p)| [100.0 + p[1] + p[0] * s + wobble(i as f64, amp), 50.0 + p[0] + wobble(i as f64 + 31.0, amp)]).collect()
}

fn resample_ideal(path: &[P2], n: usize) -> Vec<P2> {
    let seg: Vec<f64> = (1..path.len()).map(|i| (path[i][0] - path[i - 1][0]).hypot(path[i][1] - path[i - 1][1])).collect();
    let total: f64 = seg.iter().fold(0.0, |a, b| a + b);
    (0..n)
        .map(|k| {
            let mut d = (total * k as f64) / (n as f64 - 1.0);
            let mut i = 0;
            while i < seg.len() - 1 && d > seg[i] {
                d -= seg[i];
                i += 1;
            }
            let t = if seg[i] != 0.0 { (d / seg[i]).min(1.0) } else { 0.0 };
            [path[i][0] + (path[i + 1][0] - path[i][0]) * t, path[i][1] + (path[i + 1][1] - path[i][1]) * t]
        })
        .collect()
}

#[test]
fn recognises_thin_slanted_lopsided_braces_in_all_orientations() {
    for (tip, slant, amp, width) in [(0.45, 4.0, 2.5, 26.0), (0.38, 4.0, 2.5, 26.0), (0.6, -5.0, 2.5, 26.0), (0.45, 4.0, 1.5, 14.0), (0.45, 4.0, 3.5, 26.0)] {
        let brace = hand_brace(tip, slant, amp, width);
        assert_eq!(kind(&detect_brace(&brace)), "path", "{tip} {slant} {amp} {width}");
        assert_eq!(kind(&detect_brace(&brace.iter().map(|p| [400.0 - p[0], p[1]]).collect::<Vec<_>>())), "path");
        assert_eq!(kind(&detect_brace(&brace.iter().map(|p| [p[1], p[0]]).collect::<Vec<_>>())), "path");
        let mut rev = brace.clone();
        rev.reverse();
        assert_eq!(kind(&detect_brace(&rev)), "path");
    }
}

#[test]
fn does_not_call_an_s_curve_or_zigzag_a_brace() {
    let s: Vec<P2> = (0..60).map(|i| [100.0 + ((i as f64 / 59.0) * PI * 2.0).sin() * 15.0, i as f64 * 5.0]).collect();
    assert!(detect_brace(&s).is_none());
    let zig: Vec<P2> = (0..60).map(|i| [100.0 + (if i % 10 < 5 { i % 5 } else { 5 - (i % 5) }) as f64 * 4.0, i as f64 * 5.0]).collect();
    assert!(detect_brace(&zig).is_none());
}

#[test]
fn turns_a_parenthesis_into_a_clean_arc() {
    let paren: Vec<P2> = (0..50)
        .map(|i| {
            let t = -0.9 + (1.8 * i as f64) / 49.0;
            [200.0 - t.cos() * 120.0 + wobble(i as f64, 2.0), 200.0 + t.sin() * 120.0 + wobble(i as f64 + 4.0, 2.0)]
        })
        .collect();
    let pts = path_points(&beautify(&paren));
    let r: Vec<f64> = pts[1..pts.len() - 1].iter().map(|p| (p[0] - 200.0).hypot(p[1] - 200.0)).collect();
    let spread = r.iter().cloned().fold(f64::NEG_INFINITY, f64::max) - r.iter().cloned().fold(f64::INFINITY, f64::min);
    assert!(spread < 6.0);
}

#[test]
fn snaps_nearly_vertical_and_horizontal_graph_axes_exactly() {
    let axes = polyline(&[[100.0, 50.0], [104.0, 300.0], [400.0, 296.0]], 40, false, 2.0);
    let (points, _) = smooth_path(&axes);
    assert_eq!(points.len(), 3);
    assert!(close(points[1][0], points[0][0], 6));
    assert!(close(points[2][1], points[1][1], 6));
}

#[test]
fn keeps_a_smooth_curves_shape_instead_of_flattening_it() {
    let wave: Vec<P2> = (0..150).map(|i| [i as f64 * 3.0, 200.0 + (i as f64 / 15.0).sin() * 50.0 + wobble(i as f64, 3.0)]).collect();
    let (points, _) = smooth_path(&wave);
    let ys: Vec<f64> = points.iter().map(|p| p[1]).collect();
    assert!(ys.iter().cloned().fold(f64::NEG_INFINITY, f64::max) > 240.0);
    assert!(ys.iter().cloned().fold(f64::INFINITY, f64::min) < 160.0);
    assert!(max_turn(&points) < 20.0);
}

#[test]
fn draws_a_wobbly_s_curve_as_one_clean_curve() {
    let s: Vec<P2> = (0..61)
        .map(|i| {
            let fi = i as f64;
            [100.0 + ((fi / 60.0) * PI * 2.0).sin() * 30.0 + wobble(fi, 3.0) + (fi * 0.9).sin() * 2.5, 100.0 + fi * 4.0 + wobble(fi + 5.0, 2.0)]
        })
        .collect();
    let (points, _) = smooth_path(&s);
    assert!(max_turn(&points) < 12.0);
    let xs: Vec<f64> = points.iter().map(|p| p[0]).collect();
    assert!(xs.iter().cloned().fold(f64::NEG_INFINITY, f64::max) > 120.0);
    assert!(xs.iter().cloned().fold(f64::INFINITY, f64::min) < 80.0);
}

#[test]
fn does_not_mistake_an_l_or_a_v_for_a_brace() {
    let l = polyline(&[[100.0, 50.0], [104.0, 300.0], [400.0, 296.0]], 40, false, 2.5);
    assert!(detect_brace(&l).is_none());
    let v = polyline(&[[0.0, 0.0], [60.0, 150.0], [0.0, 300.0]], 40, false, 2.0);
    assert!(detect_brace(&v).is_none());
    assert_eq!(path_points(&beautify(&l)).len(), 3); // two straight axes
}

// --- symbols ----------------------------------------------------------------------------

/// The samples' random numbers (same sequence as the earlier tests').
struct Rng(u32);
impl Rng {
    fn next(&mut self) -> f64 {
        self.0 = self.0.wrapping_mul(1664525).wrapping_add(1013904223);
        self.0 as f64 / 4294967296.0
    }
}

/// Densify, then distort like handwriting: proportions, tilt, wobble, order and direction.
fn hand_drawn(strokes: &[Vec<P2>], r: &mut Rng, amount: f64) -> Vec<Vec<P2>> {
    let sx = 1.0 + (r.next() - 0.5) * 0.4 * amount;
    let sy = 1.0 + (r.next() - 0.5) * 0.4 * amount;
    let rot = (r.next() - 0.5) * 0.24 * amount;
    let mut size: f64 = 0.0;
    for st in strokes {
        for p in st {
            size = size.max(p[0].abs()).max(p[1].abs());
        }
    }
    let wob_sym = 0.02 * size * amount;
    let mut out: Vec<Vec<P2>> = Vec::new();
    for st in strokes {
        let mut dense = Vec::new();
        for i in 0..st.len() - 1 {
            for k in 0..8 {
                dense.push([st[i][0] + ((st[i + 1][0] - st[i][0]) * k as f64) / 8.0, st[i][1] + ((st[i + 1][1] - st[i][1]) * k as f64) / 8.0]);
            }
        }
        dense.push(*st.last().unwrap());
        let mut len = 0.0;
        for i in 1..st.len() {
            len += (st[i][0] - st[i - 1][0]).hypot(st[i][1] - st[i - 1][1]);
        }
        let wob = wob_sym.min(len * 0.035);
        let ph = r.next() * 6.0;
        let waves = 1.5 + r.next() * 2.0;
        let n = dense.len();
        let mut pts: Vec<P2> = Vec::with_capacity(n);
        for (i, p) in dense.iter().enumerate() {
            let u = (i as f64 / 1f64.max(n as f64 - 1.0)) * PI * 2.0 * waves;
            let wx = p[0] + (u + ph).sin() * wob + (r.next() - 0.5) * wob * 0.25;
            let wy = p[1] + (u * 1.3 + ph).cos() * wob + (r.next() - 0.5) * wob * 0.25;
            let (x, y) = (wx * sx, wy * sy);
            pts.push([x * rot.cos() - y * rot.sin() + 200.0, x * rot.sin() + y * rot.cos() + 300.0]);
        }
        if r.next() < 0.3 {
            pts.reverse();
        }
        out.push(pts);
    }
    for i in (1..out.len()).rev() {
        let j = (r.next() * (i as f64 + 1.0)).floor() as usize;
        out.swap(i, j);
    }
    out.into_iter().map(|st| st.into_iter().map(|p| [p[0] * 40.0, p[1] * 40.0]).collect()).collect()
}

fn curve(f: impl Fn(f64) -> P2, n: usize) -> Vec<P2> {
    (0..=n).map(|i| f(i as f64 / n as f64)).collect()
}

/// Things that are not maths symbols (letters, digits, scribbles).
fn not_symbols() -> Vec<(&'static str, Vec<Vec<P2>>)> {
    vec![
        ("S", vec![curve(|t| [0.5 + 0.45 * (PI * 0.2 + t * PI * 2.6).cos() * if t < 0.5 { 1.0 } else { -1.0 }, t], 30)]),
        ("Z", vec![vec![[0.0, 0.0], [1.0, 0.0], [0.0, 1.0], [1.0, 1.0]]]),
        ("two", vec![{
            let mut c = curve(|t| [0.5 + 0.45 * (PI * (1.0 - t)).cos(), 0.35 - 0.35 * (PI * t).sin()], 10);
            c.push([0.0, 1.0]);
            c.push([1.0, 1.0]);
            c
        }]),
        ("N", vec![vec![[0.0, 1.0], [0.0, 0.0], [1.0, 1.0], [1.0, 0.0]]]),
        ("W", vec![vec![[0.0, 0.0], [0.25, 1.0], [0.5, 0.3], [0.75, 1.0], [1.0, 0.0]]]),
        ("M", vec![vec![[0.0, 1.0], [0.2, 0.0], [0.5, 0.7], [0.8, 0.0], [1.0, 1.0]]]),
        ("c", vec![curve(|t| [0.5 + 0.5 * (PI * 0.25 + t * PI * 1.5).cos(), 0.5 + 0.5 * (PI * 0.25 + t * PI * 1.5).sin()], 30)]),
        ("u", vec![curve(|t| [t, 0.6 * (PI * t).sin()], 30)]),
        ("seven", vec![vec![[0.0, 0.0], [1.0, 0.0], [0.4, 1.0]]]),
        ("x", vec![vec![[0.0, 0.0], [1.0, 1.0]], vec![[1.0, 0.0], [0.0, 1.0]]]),
        ("plus", vec![vec![[0.5, 0.0], [0.5, 1.0]], vec![[0.0, 0.5], [1.0, 0.5]]]),
        ("A", vec![vec![[0.0, 1.0], [0.5, 0.0], [1.0, 1.0]], vec![[0.25, 0.55], [0.75, 0.55]]]),
        ("E", vec![vec![[1.0, 0.0], [0.0, 0.0], [0.0, 1.0], [1.0, 1.0]], vec![[0.0, 0.5], [0.7, 0.5]]]),
        ("H", vec![vec![[0.0, 0.0], [0.0, 1.0]], vec![[1.0, 0.0], [1.0, 1.0]], vec![[0.0, 0.5], [1.0, 0.5]]]),
        ("eleven", vec![vec![[0.0, 0.0], [0.0, 1.0]], vec![[0.4, 0.0], [0.4, 1.0]]]),
        ("T", vec![vec![[0.0, 0.0], [1.0, 0.0]], vec![[0.5, 0.0], [0.5, 1.0]]]),
        ("K", vec![vec![[0.0, 0.0], [0.0, 1.0]], vec![[0.8, 0.0], [0.0, 0.55], [0.8, 1.0]]]),
        ("scribble", vec![curve(|t| [t + 0.15 * (t * 40.0).sin(), 0.5 + 0.4 * (t * 17.0).sin()], 60)]),
        ("loop", vec![curve(|t| [0.5 + 0.5 * (t * PI * 2.0).cos(), 0.5 + 0.5 * (t * PI * 2.0).sin()], 30)]),
        ("parenLeft", vec![curve(|t| [0.4 - 0.4 * (PI * t).sin(), t], 30)]),
        ("parenRight", vec![curve(|t| [0.4 * (PI * t).sin(), t], 30)]),
    ]
}

fn bez(p0: P2, p1: P2, p2: P2, p3: P2) -> Vec<P2> {
    curve(
        |t| {
            let u = 1.0 - t;
            [
                u * u * u * p0[0] + 3.0 * u * u * t * p1[0] + 3.0 * u * t * t * p2[0] + t * t * t * p3[0],
                u * u * u * p0[1] + 3.0 * u * u * t * p1[1] + 3.0 * u * t * t * p2[1] + t * t * t * p3[1],
            ]
        },
        16,
    )
}

fn chain(parts: Vec<Vec<P2>>) -> Vec<P2> {
    let mut out = Vec::new();
    for (i, p) in parts.into_iter().enumerate() {
        out.extend(if i > 0 { p[1..].to_vec() } else { p });
    }
    out
}

/// How people actually draw ∫ { } (round hooks, a soft point), unlike the models.
fn handwritten() -> Vec<(&'static str, Vec<Vec<P2>>)> {
    vec![
        ("integral", vec![chain(vec![bez([0.55, 0.08], [0.5, -0.02], [0.35, 0.0], [0.33, 0.2]), bez([0.33, 0.2], [0.3, 0.45], [0.28, 0.7], [0.26, 0.9]), bez([0.26, 0.9], [0.25, 1.0], [0.12, 1.02], [0.08, 0.92])])]),
        (
            "leftBrace",
            vec![chain(vec![bez([0.5, 0.0], [0.2, 0.02], [0.3, 0.25], [0.25, 0.42]), bez([0.25, 0.42], [0.22, 0.5], [0.1, 0.52], [0.02, 0.53]), bez([0.02, 0.53], [0.15, 0.56], [0.22, 0.6], [0.22, 0.72]), bez([0.22, 0.72], [0.22, 0.92], [0.3, 1.0], [0.5, 0.98])])],
        ),
        (
            "rightBrace",
            vec![chain(vec![bez([0.0, 0.0], [0.3, 0.02], [0.2, 0.25], [0.25, 0.42]), bez([0.25, 0.42], [0.28, 0.5], [0.4, 0.52], [0.48, 0.53]), bez([0.48, 0.53], [0.35, 0.56], [0.28, 0.6], [0.28, 0.72]), bez([0.28, 0.72], [0.28, 0.92], [0.2, 1.0], [0.0, 0.98])])],
        ),
    ]
}

fn matched(strokes: &[Vec<P2>]) -> Option<&'static str> {
    match_symbol(strokes).map(|m| m.name)
}

#[test]
fn recognises_the_clean_models() {
    for name in NAMES {
        let scaled: Vec<Vec<P2>> = model(name).iter().map(|st| st.iter().map(|p| [p[0] * 40.0 + 100.0, p[1] * 40.0 + 100.0]).collect()).collect();
        assert_eq!(matched(&scaled), Some(name), "{name}");
    }
}

#[test]
fn recognises_hand_drawn_versions_in_any_stroke_order_and_direction() {
    let mut r = Rng(11);
    for name in NAMES {
        let n = 40;
        let right = (0..n).filter(|_| matched(&hand_drawn(&model(name), &mut r, 1.0)) == Some(name)).count();
        assert!(right as f64 / n as f64 >= 0.85, "{name}: {right}/{n}");
    }
}

#[test]
fn hardly_ever_mistakes_them_for_another_symbol() {
    let mut r = Rng(5);
    for name in NAMES {
        let wrong = (0..25).filter(|_| matches!(matched(&hand_drawn(&model(name), &mut r, 1.0)), Some(m) if m != name)).count();
        assert!(wrong <= 1, "{name}: {wrong}");
    }
}

#[test]
fn leaves_letters_digits_and_scribbles_alone() {
    let mut r = Rng(9);
    for (name, m) in not_symbols() {
        for _ in 0..25 {
            let got = matched(&hand_drawn(&m, &mut r, 1.0));
            assert!(got.is_none(), "{name} read as {got:?}");
        }
    }
}

#[test]
fn recognises_how_people_draw_integrals_and_braces_and_keeps_them_apart() {
    let mut r = Rng(23);
    for (name, strokes) in handwritten() {
        let right = (0..40).filter(|_| matched(&hand_drawn(&strokes, &mut r, 1.0)) == Some(name)).count();
        assert!(right >= 36, "{name}: {right}/40");
    }
}

#[test]
fn draws_the_clean_symbol_where_it_was_written() {
    let Shape::Path { points, breaks, .. } = symbol_shape("implies", 100.0, 200.0, 64.0, 40.0) else { panic!("expected a path") };
    assert_eq!(breaks, Some(vec![2, 4]));
    let xs: Vec<f64> = points.iter().map(|p| p[0]).collect();
    let ys: Vec<f64> = points.iter().map(|p| p[1]).collect();
    let mid = |v: &[f64]| (v.iter().cloned().fold(f64::INFINITY, f64::min) + v.iter().cloned().fold(f64::NEG_INFINITY, f64::max)) / 2.0;
    assert!(close(mid(&xs), 132.0, 0));
    assert!(close(mid(&ys), 220.0, 0));
}
