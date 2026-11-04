//! Hand-drawn maths symbols (∫ Σ √ ∞ = ≡ ≠ ⇒ ⇔ → ↔ ≤ ≥ ± ∀ ∃, and { }),
//! recognised with the $P point-cloud recogniser (Vatavu, Anthony &
//! Wobbrock, 2012): the drawing's points are compared with a clean model of
//! each symbol whatever the stroke order or direction. The same models then
//! draw the clean symbol in the space the strokes took.

use std::f64::consts::PI;
use std::sync::OnceLock;

use serde::Serialize;

use super::{Shape, P2};

/// Symbol names, in the order the models are tried (ties go to the first).
pub const NAMES: [&str; 18] = [
    "leftBrace", "rightBrace", "integral", "sum", "sqrt", "infinity", "equals", "identical", "notEqual", "implies", "iff", "arrow", "leftRightArrow", "lessEqual", "greaterEqual", "plusMinus", "forAll", "exists",
];

pub fn glyph(name: &str) -> &'static str {
    match name {
        "integral" => "∫",
        "sum" => "Σ",
        "sqrt" => "√",
        "infinity" => "∞",
        "equals" => "=",
        "identical" => "≡",
        "notEqual" => "≠",
        "implies" => "⇒",
        "iff" => "⇔",
        "arrow" => "→",
        "leftRightArrow" => "↔",
        "lessEqual" => "≤",
        "greaterEqual" => "≥",
        "plusMinus" => "±",
        "forAll" => "∀",
        "exists" => "∃",
        "leftBrace" => "{",
        "rightBrace" => "}",
        _ => "",
    }
}

/// The $P distance under which a drawing counts, per symbol.
fn threshold(name: &str) -> f64 {
    match name {
        "integral" => 1.6,
        "sum" => 2.1,
        "sqrt" => 1.9,
        "infinity" => 1.4,
        "equals" => 1.5,
        "identical" => 1.65,
        "notEqual" => 1.6,
        "implies" => 1.6,
        "iff" => 1.6,
        "arrow" => 1.25,
        "leftRightArrow" => 1.5,
        "lessEqual" => 1.5,
        "greaterEqual" => 1.5,
        "plusMinus" => 1.7,
        "forAll" => 1.7,
        "exists" => 1.75,
        "leftBrace" | "rightBrace" => 1.6,
        _ => 0.0,
    }
}

// --- models -----------------------------------------------------------------------

fn cubic(p0: P2, p1: P2, p2: P2, p3: P2) -> Vec<P2> {
    let n = 12;
    (0..=n)
        .map(|i| {
            let t = i as f64 / n as f64;
            let u = 1.0 - t;
            [
                u * u * u * p0[0] + 3.0 * u * u * t * p1[0] + 3.0 * u * t * t * p2[0] + t * t * t * p3[0],
                u * u * u * p0[1] + 3.0 * u * u * t * p1[1] + 3.0 * u * t * t * p2[1] + t * t * t * p3[1],
            ]
        })
        .collect()
}

fn join(parts: Vec<Vec<P2>>) -> Vec<P2> {
    let mut out = Vec::new();
    for (i, p) in parts.into_iter().enumerate() {
        out.extend(if i > 0 { p[1..].to_vec() } else { p });
    }
    out
}

fn lemniscate() -> Vec<P2> {
    let n = 48;
    (0..=n)
        .map(|i| {
            let t = (i as f64 / n as f64) * PI * 2.0;
            let d = 1.0 + t.sin().powi(2);
            [1.0 + t.cos() / d, 0.4 + (t.sin() * t.cos()) / d]
        })
        .collect()
}

fn chevron(x0: f64, x1: f64, y0: f64, y1: f64) -> Vec<P2> {
    vec![[x0, y0], [x1, (y0 + y1) / 2.0], [x0, y1]]
}

fn left_brace() -> Vec<P2> {
    join(vec![
        cubic([0.6, 0.0], [0.32, 0.0], [0.32, 0.18], [0.31, 0.36]),
        cubic([0.31, 0.36], [0.3, 0.46], [0.18, 0.5], [0.04, 0.5]),
        cubic([0.04, 0.5], [0.18, 0.5], [0.3, 0.54], [0.31, 0.64]),
        cubic([0.31, 0.64], [0.32, 0.82], [0.32, 1.0], [0.6, 1.0]),
    ])
}

/// The clean model of a symbol: polylines in a box, y down.
pub fn model(name: &str) -> Vec<Vec<P2>> {
    let line = |a: P2, b: P2| vec![a, b];
    match name {
        "leftBrace" => vec![left_brace()],
        "rightBrace" => vec![left_brace().into_iter().map(|[x, y]| [0.64 - x, y]).collect()],
        "integral" => vec![join(vec![
            cubic([0.42, 0.1], [0.4, -0.02], [0.27, -0.02], [0.25, 0.14]),
            cubic([0.25, 0.14], [0.24, 0.4], [0.21, 0.6], [0.2, 0.86]),
            cubic([0.2, 0.86], [0.18, 1.02], [0.05, 1.02], [0.03, 0.9]),
        ])],
        "sum" => vec![vec![[0.82, 0.1], [0.82, 0.0], [0.0, 0.0], [0.45, 0.5], [0.0, 1.0], [0.85, 1.0], [0.85, 0.9]]],
        "sqrt" => vec![vec![[0.0, 0.6], [0.15, 0.52], [0.4, 1.0], [0.65, 0.0], [1.4, 0.0]]],
        "infinity" => vec![lemniscate()],
        "equals" => vec![line([0.0, 0.0], [1.2, 0.0]), line([0.0, 0.45], [1.2, 0.45])],
        "identical" => vec![line([0.0, 0.0], [1.2, 0.0]), line([0.0, 0.4], [1.2, 0.4]), line([0.0, 0.8], [1.2, 0.8])],
        "notEqual" => vec![line([0.0, 0.3], [1.2, 0.3]), line([0.0, 0.75], [1.2, 0.75]), line([0.85, 0.0], [0.35, 1.05])],
        "implies" => vec![line([0.0, 0.32], [1.35, 0.32]), line([0.0, 0.72], [1.35, 0.72]), chevron(1.15, 1.6, 0.0, 1.04)],
        "iff" => vec![line([0.2, 0.32], [1.8, 0.32]), line([0.2, 0.72], [1.8, 0.72]), chevron(1.6, 2.05, 0.0, 1.04), chevron(0.45, 0.0, 0.0, 1.04)],
        "arrow" => vec![line([0.0, 0.5], [1.6, 0.5]), chevron(1.25, 1.6, 0.12, 0.88)],
        "leftRightArrow" => vec![line([0.0, 0.5], [1.9, 0.5]), chevron(1.55, 1.9, 0.12, 0.88), chevron(0.35, 0.0, 0.12, 0.88)],
        "lessEqual" => vec![chevron(0.9, 0.0, 0.0, 0.75), line([0.0, 1.0], [0.9, 1.0])],
        "greaterEqual" => vec![chevron(0.0, 0.9, 0.0, 0.75), line([0.0, 1.0], [0.9, 1.0])],
        "plusMinus" => vec![line([0.45, 0.0], [0.45, 0.72]), line([0.0, 0.36], [0.9, 0.36]), line([0.0, 1.0], [0.9, 1.0])],
        "forAll" => vec![vec![[0.0, 0.0], [0.45, 1.0], [0.9, 0.0]], line([0.18, 0.42], [0.72, 0.42])],
        "exists" => vec![vec![[0.0, 0.0], [0.75, 0.0], [0.75, 1.0], [0.0, 1.0]], line([0.12, 0.5], [0.75, 0.5])],
        _ => vec![],
    }
}

// --- $P recogniser ------------------------------------------------------------------

const N: usize = 40;

#[derive(Clone, Copy)]
struct Cp {
    x: f64,
    y: f64,
}

fn path_len(stroke: &[P2]) -> f64 {
    let mut d = 0.0;
    for i in 1..stroke.len() {
        d += (stroke[i][0] - stroke[i - 1][0]).hypot(stroke[i][1] - stroke[i - 1][1]);
    }
    d
}

/// `n` points spread evenly along all strokes (no points across the gaps).
fn resample(strokes: &[Vec<P2>], n: usize) -> Vec<Cp> {
    let total: f64 = strokes.iter().fold(0.0, |s, st| s + path_len(st));
    if total == 0.0 {
        return vec![Cp { x: strokes[0][0][0], y: strokes[0][0][1] }; n];
    }
    let step = total / (n as f64 - 1.0);
    let mut out: Vec<Cp> = Vec::new();
    let mut acc = 0.0;
    for st in strokes {
        if st.is_empty() {
            continue;
        }
        let mut prev = st[0];
        if out.is_empty() {
            out.push(Cp { x: prev[0], y: prev[1] });
        }
        for &cur in &st[1..] {
            let mut d = (cur[0] - prev[0]).hypot(cur[1] - prev[1]);
            while acc + d >= step && d > 0.0 {
                let t = (step - acc) / d;
                let q = [prev[0] + t * (cur[0] - prev[0]), prev[1] + t * (cur[1] - prev[1])];
                out.push(Cp { x: q[0], y: q[1] });
                prev = q;
                d = (cur[0] - prev[0]).hypot(cur[1] - prev[1]);
                acc = 0.0;
            }
            acc += d;
            prev = cur;
        }
    }
    let last = strokes.last().unwrap();
    let lp = *last.last().unwrap();
    while out.len() < n {
        out.push(Cp { x: lp[0], y: lp[1] });
    }
    out.truncate(n);
    out
}

/// Scale (keeping proportions) to a unit box and centre on the centroid.
fn normalise(pts: &[Cp]) -> Vec<Cp> {
    let (mut x0, mut y0, mut x1, mut y1) = (f64::INFINITY, f64::INFINITY, f64::NEG_INFINITY, f64::NEG_INFINITY);
    for p in pts {
        x0 = x0.min(p.x);
        y0 = y0.min(p.y);
        x1 = x1.max(p.x);
        y1 = y1.max(p.y);
    }
    let s0 = (x1 - x0).max(y1 - y0);
    let s = if s0 == 0.0 || s0.is_nan() { 1.0 } else { s0 };
    let scaled: Vec<Cp> = pts.iter().map(|p| Cp { x: (p.x - x0) / s, y: (p.y - y0) / s }).collect();
    let cx = scaled.iter().fold(0.0, |a, p| a + p.x) / scaled.len() as f64;
    let cy = scaled.iter().fold(0.0, |a, p| a + p.y) / scaled.len() as f64;
    scaled.iter().map(|p| Cp { x: p.x - cx, y: p.y - cy }).collect()
}

fn cloud_distance(a: &[Cp], b: &[Cp], start: usize) -> f64 {
    let mut matched = vec![false; b.len()];
    let mut sum = 0.0;
    let mut i = start;
    loop {
        let mut best = f64::INFINITY;
        let mut index = usize::MAX;
        for j in 0..b.len() {
            if matched[j] {
                continue;
            }
            let d = (a[i].x - b[j].x).hypot(a[i].y - b[j].y);
            if d < best {
                best = d;
                index = j;
            }
        }
        if index != usize::MAX {
            matched[index] = true;
        }
        let weight = 1.0 - (((i + a.len() - start) % a.len()) as f64) / a.len() as f64;
        sum += weight * best;
        i = (i + 1) % a.len();
        if i == start {
            break;
        }
    }
    sum
}

fn greedy_match(a: &[Cp], b: &[Cp]) -> f64 {
    let step = (a.len() as f64).sqrt().floor() as usize;
    let mut min = f64::INFINITY;
    let mut i = 0;
    while i < a.len() {
        min = min.min(cloud_distance(a, b, i)).min(cloud_distance(b, a, i));
        i += step;
    }
    min
}

// --- shape features: corners and proportions ------------------------------------------

struct Box {
    x: f64,
    y: f64,
    w: f64,
    h: f64,
}

fn bbox_of(strokes: &[Vec<P2>]) -> Box {
    let (mut x0, mut y0, mut x1, mut y1) = (f64::INFINITY, f64::INFINITY, f64::NEG_INFINITY, f64::NEG_INFINITY);
    for st in strokes {
        for p in st {
            x0 = x0.min(p[0]);
            y0 = y0.min(p[1]);
            x1 = x1.max(p[0]);
            y1 = y1.max(p[1]);
        }
    }
    Box { x: x0, y: y0, w: x1 - x0, h: y1 - y0 }
}

/// Evenly spaced points along one stroke.
fn spaced(stroke: &[P2], step: f64) -> Vec<P2> {
    let mut out = vec![stroke[0]];
    let mut acc = 0.0;
    let mut prev = stroke[0];
    for &cur in &stroke[1..] {
        let mut d = (cur[0] - prev[0]).hypot(cur[1] - prev[1]);
        while acc + d >= step && d > 0.0 {
            let t = (step - acc) / d;
            prev = [prev[0] + t * (cur[0] - prev[0]), prev[1] + t * (cur[1] - prev[1])];
            out.push(prev);
            d = (cur[0] - prev[0]).hypot(cur[1] - prev[1]);
            acc = 0.0;
        }
        acc += d;
        prev = cur;
    }
    let l = *out.last().unwrap();
    if out.len() < 2 || (l[0] - prev[0]).hypot(l[1] - prev[1]) > step * 0.3 {
        out.push(prev);
    }
    out
}

/// Direction changes along a stroke (radians), measured over about an eighth of the symbol.
fn turns(stroke: &[P2], size: f64) -> Vec<f64> {
    let raw = spaced(stroke, (size * 0.04).max(1e-6));
    let pts: Vec<P2> = (0..raw.len())
        .map(|i| {
            let a = raw[i.saturating_sub(1)];
            let b = raw[(i + 1).min(raw.len() - 1)];
            let p = raw[i];
            [(a[0] + 2.0 * p[0] + b[0]) / 4.0, (a[1] + 2.0 * p[1] + b[1]) / 4.0]
        })
        .collect();
    let k = 3;
    let mut out = Vec::new();
    if pts.len() <= 2 * k {
        return out;
    }
    for i in k..pts.len() - k {
        let a = (pts[i][1] - pts[i - k][1]).atan2(pts[i][0] - pts[i - k][0]);
        let b = (pts[i + k][1] - pts[i][1]).atan2(pts[i + k][0] - pts[i][0]);
        let mut turn = (b - a).abs();
        if turn > PI {
            turn = 2.0 * PI - turn;
        }
        out.push(turn);
    }
    out
}

pub fn corner_count(stroke: &[P2], size: f64) -> usize {
    let t = turns(stroke, size);
    let mut corners = 0;
    let mut i = 0;
    while i < t.len() {
        if t[i] > (58.0 * PI) / 180.0 {
            corners += 1;
            i += 5; // one corner, not several around it
        }
        i += 1;
    }
    corners
}

/// The sharpest turn in a stroke (radians): near 0 for a straight line.
pub fn sharpest(stroke: &[P2], size: f64) -> f64 {
    turns(stroke, size).into_iter().fold(0.0, f64::max)
}

#[derive(Clone, Copy)]
struct Corners {
    corners: usize,
    sharpest: f64,
}

/// Each stroke's corner count and sharpest turn, ordered by corners.
fn corner_signature(strokes: &[Vec<P2>]) -> Vec<Corners> {
    let b = bbox_of(strokes);
    let size = b.w.max(b.h);
    let mut v: Vec<Corners> = strokes.iter().map(|st| Corners { corners: corner_count(st, size), sharpest: sharpest(st, size) }).collect();
    v.sort_by(|a, b| {
        let d = a.corners as f64 - b.corners as f64;
        let d = if d != 0.0 { d } else { a.sharpest - b.sharpest };
        d.partial_cmp(&0.0).unwrap_or(std::cmp::Ordering::Equal)
    });
    v
}

/// Symbols made of curves: their hooks and the brace's point aren't corners.
fn curvy(name: &str) -> bool {
    matches!(name, "integral" | "leftBrace" | "rightBrace")
}

struct Template {
    name: &'static str,
    strokes: usize,
    straight: usize,
    cloud: Vec<Cp>,
    corners: Vec<Corners>,
    aspect: f64,
}

fn templates() -> &'static Vec<Template> {
    static T: OnceLock<Vec<Template>> = OnceLock::new();
    T.get_or_init(|| {
        NAMES
            .iter()
            .map(|&name| {
                let m = model(name);
                let b = bbox_of(&m);
                Template { name, strokes: m.len(), straight: m.iter().filter(|st| st.len() == 2).count(), cloud: normalise(&resample(&m, N)), corners: corner_signature(&m), aspect: b.w / b.h.max(1e-6) }
            })
            .collect()
    })
}

#[derive(Debug, Clone, Serialize)]
pub struct SymbolMatch {
    pub name: &'static str,
    /// $P distance: lower is closer.
    pub distance: f64,
}

/// The symbol a drawing looks like, or None.
pub fn match_symbol(strokes: &[Vec<P2>]) -> Option<SymbolMatch> {
    let usable: Vec<Vec<P2>> = strokes.iter().filter(|s| s.len() > 1).cloned().collect();
    if usable.is_empty() {
        return None;
    }
    let cloud = normalise(&resample(&usable, N));
    let corners = corner_signature(&usable);
    let b = bbox_of(&usable);
    let aspect = b.w / b.h.max(1e-6);
    let mut best: Option<SymbolMatch> = None;
    for t in templates() {
        if !plausible(t, usable.len(), &corners, aspect) || !curvy_fits(t.name, &usable) {
            continue;
        }
        let d = greedy_match(&cloud, &t.cloud);
        if d >= threshold(t.name) {
            continue;
        }
        if best.as_ref().map_or(true, |bm| d / threshold(t.name) < bm.distance / threshold(bm.name)) {
            best = Some(SymbolMatch { name: t.name, distance: d });
        }
    }
    best
}

fn plausible(t: &Template, strokes: usize, corners: &[Corners], aspect: f64) -> bool {
    if strokes != t.strokes {
        return false;
    }
    if curvy(t.name) {
        if corners.iter().any(|c| c.corners > 3) {
            return false;
        }
        let ratio = aspect / t.aspect;
        return ratio > 0.45 && ratio < 2.2;
    }
    let bent = |c: &Corners| c.corners >= 1 || c.sharpest > (38.0 * PI) / 180.0;
    let need_pointed = t.corners.iter().filter(|c| c.corners >= 1).count();
    let pointed = corners.iter().filter(|c| bent(c)).count();
    if pointed < need_pointed {
        return false;
    }
    if pointed > t.strokes - t.straight {
        return false;
    }
    for i in 0..corners.len() {
        let want = t.corners[i].corners as i64;
        if (corners[i].corners as i64 - want).abs() > (if want >= 3 { 2 } else { 1 }) {
            return false;
        }
        if want == 1 && corners[i].corners > 1 {
            return false;
        }
    }
    let ratio = aspect / t.aspect;
    ratio > 0.45 && ratio < 2.2
}

/// What tells the curvy symbols apart (and from brackets).
fn curvy_fits(name: &str, strokes: &[Vec<P2>]) -> bool {
    if !curvy(name) || strokes.len() != 1 {
        return true;
    }
    let st = &strokes[0];
    let b = bbox_of(strokes);
    if b.w <= 0.0 || b.h <= 0.0 {
        return false;
    }
    let a = st[0];
    let z = *st.last().unwrap();
    let apart = (a[0] - z[0]).abs() / b.w;
    if name == "integral" {
        return apart > 0.3;
    }
    if apart > 0.45 {
        return false;
    }
    let left = name == "leftBrace";
    let mut tip = st[0];
    for &p in st {
        if if left { p[0] < tip[0] } else { p[0] > tip[0] } {
            tip = p;
        }
    }
    let at = (tip[1] - b.y) / b.h;
    if !(0.28..=0.72).contains(&at) {
        return false;
    }
    sharpest(st, b.w.max(b.h)) > (85.0 * PI) / 180.0
}

/// The plausible symbols' distances (best first), for tuning and tests.
pub fn rank_symbols(strokes: &[Vec<P2>]) -> Vec<SymbolMatch> {
    let cloud = normalise(&resample(strokes, N));
    let corners = corner_signature(strokes);
    let b = bbox_of(strokes);
    let aspect = b.w / b.h.max(1e-6);
    let mut v: Vec<SymbolMatch> = templates().iter().filter(|t| plausible(t, strokes.len(), &corners, aspect)).map(|t| SymbolMatch { name: t.name, distance: greedy_match(&cloud, &t.cloud) }).collect();
    v.sort_by(|a, b| a.distance.partial_cmp(&b.distance).unwrap_or(std::cmp::Ordering::Equal));
    v
}

// --- clean drawing ---------------------------------------------------------------------

/// The clean symbol in a box (page coordinates), as one path with breaks between its strokes.
pub fn symbol_shape(name: &str, x: f64, y: f64, w: f64, h: f64) -> Shape {
    let m = model(name);
    let b = bbox_of(&m);
    let mw = if b.w == 0.0 || b.w.is_nan() { 1.0 } else { b.w };
    let mh = if b.h == 0.0 || b.h.is_nan() { 1.0 } else { b.h };
    let scale = ((w / mw) * (h / mh)).sqrt();
    let ox = x + w / 2.0 - (mw * scale) / 2.0 - b.x * scale;
    let oy = y + h / 2.0 - (mh * scale) / 2.0 - b.y * scale;
    let mut points = Vec::new();
    let mut breaks = Vec::new();
    for st in &m {
        if !points.is_empty() {
            breaks.push(points.len());
        }
        for p in st {
            points.push([ox + p[0] * scale, oy + p[1] * scale]);
        }
    }
    Shape::Path { points, closed: false, breaks: if breaks.is_empty() { None } else { Some(breaks) } }
}
