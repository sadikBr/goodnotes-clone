//! Inkleaf's core, used by the Qt app (through qt/native): the notebook database, Google Drive sync, backups, PDF/PPTX/PNG
//! export, ink geometry and handwriting recognition.

pub mod backup;
pub mod db;
pub mod error;
pub mod export;
pub mod handwriting;
pub mod ink;
pub mod sync;
