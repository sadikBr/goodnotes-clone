//! Handwriting recognition for "convert ink to text".
//!
//! On Windows this uses the operating system's offline handwriting
//! recognizer (Windows Ink, `Windows.UI.Input.Inking`), which follows the
//! languages installed in Windows settings. Other platforms report the
//! feature as unavailable and the app lets the user type the text instead.

use serde::Serialize;

/// One stroke as a list of `[x, y]` points in page coordinates.
pub type InkStroke = Vec<[f64; 2]>;

#[derive(Debug, Clone, Serialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Recognition {
    /// Whether a recognizer exists on this system.
    pub available: bool,
    /// Best guess first; empty when nothing was recognised.
    pub candidates: Vec<String>,
}

/// Removes empty strokes and caps the input size so a huge selection cannot
/// stall the recognizer.
pub fn sanitize(strokes: &[InkStroke]) -> Vec<InkStroke> {
    strokes
        .iter()
        .filter(|s| !s.is_empty())
        .take(2000)
        .map(|s| {
            s.iter()
                .copied()
                .filter(|p| p[0].is_finite() && p[1].is_finite())
                .take(5000)
                .collect()
        })
        .filter(|s: &InkStroke| !s.is_empty())
        .collect()
}

#[cfg(windows)]
pub fn recognize(strokes: &[InkStroke]) -> Result<Recognition, String> {
    use windows::Foundation::Point;
    use windows::UI::Input::Inking::{
        InkRecognitionTarget, InkRecognizerContainer, InkStrokeBuilder, InkStrokeContainer,
    };
    use windows_collections::IIterable;

    let strokes = sanitize(strokes);
    if strokes.is_empty() {
        return Ok(Recognition {
            available: true,
            candidates: Vec::new(),
        });
    }
    let run = move || -> windows_core::Result<Recognition> {
        // This runs on a dedicated worker thread; the multithreaded
        // apartment lets us block on the async recognizer safely.
        unsafe {
            let _ = windows::Win32::System::WinRT::RoInitialize(
                windows::Win32::System::WinRT::RO_INIT_MULTITHREADED,
            );
        }
        let recognizer = InkRecognizerContainer::new()?;
        if recognizer.GetRecognizers()?.Size()? == 0 {
            return Ok(Recognition {
                available: false,
                candidates: Vec::new(),
            });
        }
        let builder = InkStrokeBuilder::new()?;
        let container = InkStrokeContainer::new()?;
        for stroke in &strokes {
            let points: Vec<Point> = stroke
                .iter()
                .map(|p| Point {
                    X: p[0] as f32,
                    Y: p[1] as f32,
                })
                .collect();
            let iterable: IIterable<Point> = points.into();
            container.AddStroke(&builder.CreateStroke(&iterable)?)?;
        }
        let results = recognizer
            .RecognizeAsync(&container, InkRecognitionTarget::All)?
            .join()?;
        // Each result is one recognised word; join the best candidate of each
        // for the top line, and offer the per-word alternatives as variants.
        let mut words: Vec<Vec<String>> = Vec::new();
        for i in 0..results.Size()? {
            let candidates = results.GetAt(i)?.GetTextCandidates()?;
            let mut list = Vec::new();
            for j in 0..candidates.Size()?.min(5) {
                list.push(candidates.GetAt(j)?.to_string());
            }
            if !list.is_empty() {
                words.push(list);
            }
        }
        Ok(Recognition {
            available: true,
            candidates: combine(&words),
        })
    };
    std::thread::spawn(run)
        .join()
        .map_err(|_| "handwriting recognition crashed".to_string())?
        .map_err(|e| format!("handwriting recognition failed: {e}"))
}

#[cfg(not(windows))]
pub fn recognize(_strokes: &[InkStroke]) -> Result<Recognition, String> {
    Ok(Recognition {
        available: false,
        candidates: Vec::new(),
    })
}

/// Builds full-text candidates from per-word alternatives: the best guess,
/// then variants that swap one word for its next alternative.
#[cfg_attr(not(windows), allow(dead_code))]
fn combine(words: &[Vec<String>]) -> Vec<String> {
    if words.is_empty() {
        return Vec::new();
    }
    let best: Vec<&str> = words.iter().map(|w| w[0].as_str()).collect();
    let mut out = vec![best.join(" ")];
    for (i, alts) in words.iter().enumerate() {
        for alt in alts.iter().skip(1).take(2) {
            let mut v = best.clone();
            v[i] = alt;
            let s = v.join(" ");
            if !out.contains(&s) {
                out.push(s);
            }
        }
        if out.len() >= 6 {
            break;
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn combines_word_alternatives() {
        let words = vec![
            vec!["Biology".to_string(), "Bio logy".to_string()],
            vec!["notes".to_string(), "rotes".to_string()],
        ];
        assert_eq!(
            combine(&words),
            vec!["Biology notes", "Bio logy notes", "Biology rotes"]
        );
        assert!(combine(&[]).is_empty());
    }

    #[test]
    fn sanitize_drops_empty_and_invalid_points() {
        let s = sanitize(&[
            vec![],
            vec![[1.0, 2.0], [f64::NAN, 3.0]],
            vec![[f64::INFINITY, 0.0]],
        ]);
        assert_eq!(s, vec![vec![[1.0, 2.0]]]);
    }

    #[cfg(not(windows))]
    #[test]
    fn reports_unavailable_off_windows() {
        let r = recognize(&[vec![[0.0, 0.0], [1.0, 1.0]]]).unwrap();
        assert!(!r.available);
    }
}
