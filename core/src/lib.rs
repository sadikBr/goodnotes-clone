//! Inkleaf's core, shared by the apps (the Qt desktop app and the Tauri
//! app): the notebook database, Google Drive sync, backups, PDF/PPTX/PNG
//! export, ink geometry and handwriting recognition.

pub mod backup;
pub mod db;
pub mod error;
pub mod export;
pub mod handwriting;
pub mod ink;
pub mod sync;
