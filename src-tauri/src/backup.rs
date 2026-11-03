//! Backups: a copy of the whole library as one `.inkleaf` file (a SQLite
//! snapshot), written once a day into a folder the user picks (a USB key, or
//! a folder that Google Drive / Dropbox / OneDrive syncs) and on demand.
//! Only the newest backups are kept in that folder.

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use crate::db::{now_ms, Db};
use crate::error::{AppError, AppResult};

/// Backups in the folder are named `Inkleaf backup <local date and time>.inkleaf`.
pub const PREFIX: &str = "Inkleaf backup ";
pub const EXTENSION: &str = "inkleaf";

/// A daily backup is due this long after the last one (a little under a day,
/// so it happens at about the same time each day).
const DAILY_MS: i64 = 20 * 3600 * 1000;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase", default)]
pub struct BackupConfig {
    /// Folder the backups go to (None: not set up).
    pub folder: Option<String>,
    /// Back up once a day while the app is open.
    pub daily: bool,
    /// How many backups to keep in the folder.
    pub keep: u32,
    pub last_backup_at: Option<i64>,
    pub last_backup_file: Option<String>,
    /// Why the last attempt failed (e.g. the USB key isn't plugged in).
    pub last_error: Option<String>,
}

impl Default for BackupConfig {
    fn default() -> Self {
        BackupConfig { folder: None, daily: true, keep: 14, last_backup_at: None, last_backup_file: None, last_error: None }
    }
}

pub struct Backups {
    file: PathBuf,
    config: Mutex<BackupConfig>,
}

impl Backups {
    /// Loads the settings kept in `file` (defaults if it's missing or unreadable).
    pub fn load(file: PathBuf) -> Self {
        let config = std::fs::read_to_string(&file).ok().and_then(|s| serde_json::from_str(&s).ok()).unwrap_or_default();
        Backups { file, config: Mutex::new(config) }
    }

    fn lock(&self) -> std::sync::MutexGuard<'_, BackupConfig> {
        self.config.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn save(&self, c: &BackupConfig) {
        if let Ok(json) = serde_json::to_string_pretty(c) {
            let _ = std::fs::write(&self.file, json);
        }
    }

    pub fn status(&self) -> BackupConfig {
        self.lock().clone()
    }

    pub fn configure(&self, folder: Option<String>, daily: bool, keep: u32) -> AppResult<BackupConfig> {
        let folder = folder.map(|f| f.trim().to_string()).filter(|f| !f.is_empty());
        if let Some(f) = &folder {
            check_folder(Path::new(f))?;
        }
        let mut c = self.lock();
        if c.folder != folder {
            // A new place: the next daily backup happens right away.
            c.last_backup_at = None;
            c.last_backup_file = None;
        }
        c.folder = folder;
        c.daily = daily;
        c.keep = keep.clamp(1, 365);
        c.last_error = None;
        self.save(&c);
        Ok(c.clone())
    }

    /// Backs up into the folder now. `label` is the local date and time for
    /// the file name (e.g. "2026-11-03 0915"). Failures are remembered so the
    /// settings can show them, and returned.
    pub fn backup_now(&self, db: &Db, label: &str) -> AppResult<BackupConfig> {
        let mut c = self.lock();
        let folder = c.folder.clone().ok_or_else(|| AppError::Invalid("choose a backup folder first".into()))?;
        let result = (|| -> AppResult<PathBuf> {
            let dir = PathBuf::from(&folder);
            check_folder(&dir)?;
            let dest = dir.join(file_name(label));
            db.snapshot_to(&dest)?;
            prune(&dir, c.keep as usize, &dest);
            Ok(dest)
        })();
        match result {
            Ok(dest) => {
                c.last_backup_at = Some(now_ms());
                c.last_backup_file = Some(dest.to_string_lossy().into_owned());
                c.last_error = None;
                self.save(&c);
                Ok(c.clone())
            }
            Err(e) => {
                c.last_error = Some(e.to_string());
                self.save(&c);
                Err(e)
            }
        }
    }

    /// The daily backup, if it's on and due. Never fails: problems are kept
    /// in `last_error` and tried again on the next check.
    pub fn backup_if_due(&self, db: &Db, label: &str) -> BackupConfig {
        let due = {
            let c = self.lock();
            c.daily && c.folder.is_some() && c.last_backup_at.is_none_or(|t| now_ms() - t >= DAILY_MS)
        };
        if due {
            let _ = self.backup_now(db, label);
        }
        self.status()
    }
}

/// The folder exists and a file can be written in it.
fn check_folder(dir: &Path) -> AppResult<()> {
    if !dir.is_dir() {
        return Err(AppError::Invalid(format!("the backup folder isn't available: {} (is the drive connected?)", dir.display())));
    }
    let probe = dir.join(".inkleaf-write-test");
    std::fs::write(&probe, b"ok").map_err(|e| AppError::Invalid(format!("can't write in the backup folder {}: {e}", dir.display())))?;
    let _ = std::fs::remove_file(probe);
    Ok(())
}

/// `Inkleaf backup <label>.inkleaf`, with characters file systems reject removed.
pub fn file_name(label: &str) -> String {
    let clean: String = label.chars().map(|c| if c.is_alphanumeric() || c == ' ' || c == '-' || c == '_' { c } else { '-' }).collect();
    format!("{PREFIX}{}.{EXTENSION}", clean.trim())
}

/// Removes the oldest backups beyond `keep` (names sort by date). Only files
/// Inkleaf named itself are touched; `just_made` is always kept.
fn prune(dir: &Path, keep: usize, just_made: &Path) {
    let Ok(entries) = std::fs::read_dir(dir) else { return };
    let mut ours: Vec<PathBuf> = entries
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| {
            let name = p.file_name().and_then(|n| n.to_str()).unwrap_or("");
            name.starts_with(PREFIX) && name.ends_with(&format!(".{EXTENSION}"))
        })
        .collect();
    ours.sort();
    let extra = ours.len().saturating_sub(keep);
    for old in ours.into_iter().take(extra) {
        if old != just_made {
            let _ = std::fs::remove_file(old);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::db::NewNotebook;

    fn temp_dir(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("inkleaf-test-{name}-{}", crate::db::new_id()));
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn notebook(db: &Db, title: &str) {
        db.create_notebook(&NewNotebook { title: title.into(), folder_id: None, cover_color: None, template: None, paper_color: None, width: None, height: None, cover_page: false })
            .unwrap();
    }

    #[test]
    fn backs_up_and_restores_the_whole_library() {
        let dir = temp_dir("restore");
        let db = Db::open(&dir.join("inkleaf.db")).unwrap();
        notebook(&db, "Maths");
        let backup = dir.join("one.inkleaf");
        db.snapshot_to(&backup).unwrap();
        assert!(!dir.join("one.inkleaf.part").exists());
        notebook(&db, "Arabic");
        assert_eq!(db.library().unwrap().notebooks.len(), 2);

        let s = db.restore_from(&backup).unwrap();
        assert_eq!(s.notebooks, 1);
        let lib = db.library().unwrap();
        assert_eq!(lib.notebooks.len(), 1);
        assert_eq!(lib.notebooks[0].title, "Maths");
        // The library as it was before restoring is kept.
        assert_eq!(Db::inspect_backup(&dir.join("inkleaf-before-restore.db")).unwrap().notebooks, 2);
        // Still writable after the swap.
        notebook(&db, "Physics");
        assert_eq!(db.library().unwrap().notebooks.len(), 2);
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn rejects_files_that_are_not_backups() {
        let dir = temp_dir("reject");
        let f = dir.join("notes.inkleaf");
        std::fs::write(&f, b"hello").unwrap();
        assert!(Db::inspect_backup(&f).is_err());
        let db = Db::open(&dir.join("inkleaf.db")).unwrap();
        notebook(&db, "Kept");
        assert!(db.restore_from(&f).is_err());
        assert_eq!(db.library().unwrap().notebooks.len(), 1);
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn daily_backups_go_to_the_folder_and_only_the_newest_are_kept() {
        let dir = temp_dir("daily");
        let out = dir.join("Drive");
        std::fs::create_dir_all(&out).unwrap();
        std::fs::write(out.join("my notes.txt"), b"not ours").unwrap();
        let db = Db::open(&dir.join("inkleaf.db")).unwrap();
        let b = Backups::load(dir.join("backup.json"));
        assert!(b.configure(Some(dir.join("missing").to_string_lossy().into()), true, 2).is_err());
        b.configure(Some(out.to_string_lossy().into()), true, 2).unwrap();

        let c = b.backup_if_due(&db, "2026-11-01 0900");
        assert!(c.last_error.is_none());
        // Not due again the same day.
        b.backup_if_due(&db, "2026-11-01 1000");
        assert!(!out.join(file_name("2026-11-01 1000")).exists());
        b.backup_now(&db, "2026-11-02 0900").unwrap();
        b.backup_now(&db, "2026-11-03 0900").unwrap();
        let mut names: Vec<String> = std::fs::read_dir(&out).unwrap().map(|e| e.unwrap().file_name().to_string_lossy().into_owned()).collect();
        names.sort();
        assert_eq!(names, vec!["Inkleaf backup 2026-11-02 0900.inkleaf", "Inkleaf backup 2026-11-03 0900.inkleaf", "my notes.txt"]);
        // Settings survive a restart.
        assert_eq!(Backups::load(dir.join("backup.json")).status().keep, 2);
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn a_missing_folder_is_reported_not_fatal() {
        let dir = temp_dir("missing");
        let usb = dir.join("USB");
        std::fs::create_dir_all(&usb).unwrap();
        let db = Db::open(&dir.join("inkleaf.db")).unwrap();
        let b = Backups::load(dir.join("backup.json"));
        b.configure(Some(usb.to_string_lossy().into()), true, 7).unwrap();
        std::fs::remove_dir_all(&usb).unwrap(); // unplugged
        let c = b.backup_if_due(&db, "2026-11-01 0900");
        assert!(c.last_error.unwrap().contains("isn't available"));
        assert!(c.last_backup_at.is_none());
        std::fs::remove_dir_all(dir).ok();
    }
}
