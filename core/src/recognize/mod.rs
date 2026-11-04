//! Turning hand-drawn ink into clean shapes and symbols (was
//! src/editor/beautify.ts and symbols.ts): basic shapes, braces, smoothed
//! paths, and maths symbols. Numbers follow JavaScript's rules (Math.round,
//! Math.sign) so results match the earlier app exactly.

pub mod beautify;
pub mod symbols;

#[cfg(test)]
mod tests;

use serde::{Deserialize, Serialize};

pub type P2 = [f64; 2];

/// A recognised shape, as the editor stores it.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "lowercase")]
pub enum Shape {
    Line { points: Vec<P2> },
    Arrow { points: Vec<P2> },
    Triangle { points: Vec<P2> },
    Rectangle { points: Vec<P2> },
    Polygon { points: Vec<P2> },
    Ellipse { cx: f64, cy: f64, rx: f64, ry: f64 },
    /// A smoothed freehand path (graphs, curves, braces...); `breaks`: where
    /// a new stroke starts (multi-stroke symbols).
    Path {
        points: Vec<P2>,
        closed: bool,
        #[serde(skip_serializing_if = "Option::is_none", default)]
        breaks: Option<Vec<usize>>,
    },
}

/// Math.round.
pub(crate) fn js_round(v: f64) -> f64 {
    (v + 0.5).floor()
}

/// Math.sign (0 for 0).
pub(crate) fn js_sign(v: f64) -> f64 {
    if v > 0.0 {
        1.0
    } else if v < 0.0 {
        -1.0
    } else {
        v
    }
}

pub(crate) fn dist(a: P2, b: P2) -> f64 {
    (a[0] - b[0]).hypot(a[1] - b[1])
}
