//! SQLite persistence for the library (folders, notebooks), pages and page
//! elements (strokes, shapes, text, sticky notes, images).
//!
//! Every page element is stored as its own row holding a JSON payload, which
//! keeps the schema stable while the editor's element types evolve. Deletions
//! of folders, notebooks and pages are soft ("trashed") so they can be
//! recovered, mirroring the trash/recover flow of mainstream note apps.

use rusqlite::{params, Connection, OpenFlags, OptionalExtension};
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::{SystemTime, UNIX_EPOCH};

use crate::error::{AppError, AppResult};
use crate::lock::{self, ContentKey, LockInfo};
use std::collections::HashMap;

const SCHEMA_VERSION: i64 = 7;

/// Ids of the folders in the trash and every folder inside them, at any
/// depth: what's in a trashed folder is in the trash with it.
const TRASHED_FOLDERS: &str = "(WITH RECURSIVE gone(id) AS (
        SELECT id FROM folders WHERE trashed_at IS NOT NULL
        UNION SELECT c.id FROM folders c JOIN gone ON c.parent_id = gone.id
    ) SELECT id FROM gone)";

pub fn now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

pub fn new_id() -> String {
    uuid::Uuid::new_v4().to_string()
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Folder {
    pub id: String,
    pub name: String,
    pub parent_id: Option<String>,
    pub color: String,
    pub favourite: bool,
    pub created_at: i64,
    pub updated_at: i64,
    pub trashed_at: Option<i64>,
    /// Password protected (with everything inside), and whether it's unlocked now.
    #[serde(default)]
    pub locked: bool,
    #[serde(default)]
    pub unlocked: bool,
    /// Which lock (shared by a folder and what's inside it): tells a lock of
    /// its own from one that comes with its folder.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub lock_id: Option<String>,
    #[serde(skip)]
    pub lock: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Notebook {
    pub id: String,
    pub title: String,
    pub folder_id: Option<String>,
    pub cover_color: String,
    pub default_template: String,
    pub favourite: bool,
    pub page_count: i64,
    pub thumbnail: Option<String>,
    pub created_at: i64,
    pub updated_at: i64,
    pub trashed_at: Option<i64>,
    /// Whether the first page is a cover (the library then shows it full-size).
    pub has_cover: bool,
    /// App-defined settings as JSON (e.g. a daftar's classes and timetable).
    pub meta: Option<String>,
    /// Password protected (its content is encrypted), and whether it's unlocked now
    /// (when locked, its thumbnail and settings aren't shown).
    #[serde(default)]
    pub locked: bool,
    #[serde(default)]
    pub unlocked: bool,
    /// Which lock (shared by a folder and what's inside it): tells a lock of
    /// its own from one that comes with its folder.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub lock_id: Option<String>,
    #[serde(skip)]
    pub lock: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Page {
    pub id: String,
    pub notebook_id: String,
    pub position: i64,
    pub template: String,
    pub paper_color: String,
    pub width: f64,
    pub height: f64,
    pub favourite: bool,
    pub thumbnail: Option<String>,
    pub created_at: i64,
    pub updated_at: i64,
    /// Imported PDF this page shows as its background, and which page of it.
    pub background_asset: Option<String>,
    pub background_page: Option<i64>,
    /// Which section of the notebook the page belongs to (e.g. a class in a daftar).
    pub section: Option<String>,
}

/// One element on a page. `data` is an opaque JSON payload owned by the
/// frontend editor (points, colors, text, ...).
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Element {
    pub id: String,
    pub kind: String,
    pub z: i64,
    pub data: serde_json::Value,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Library {
    pub folders: Vec<Folder>,
    pub notebooks: Vec<Notebook>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TrashContents {
    pub folders: Vec<Folder>,
    pub notebooks: Vec<Notebook>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct SearchHit {
    pub notebook_id: String,
    pub notebook_title: String,
    pub page_id: Option<String>,
    pub page_number: Option<i64>,
    pub snippet: String,
}

/// A saved selection to put on any page: its elements (JSON, from the top
/// left of their box), a picture, and the box's size.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Snippet {
    pub id: String,
    pub name: String,
    pub data: String,
    pub preview: Option<String>,
    pub width: f64,
    pub height: f64,
    pub created_at: i64,
    pub used_at: Option<i64>,
}

/// A handwritten page for the AI to read.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PageToRead {
    pub page_id: String,
    pub notebook_id: String,
    pub width: f64,
    pub height: f64,
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct NewNotebook {
    pub title: String,
    pub folder_id: Option<String>,
    pub cover_color: Option<String>,
    pub template: Option<String>,
    pub paper_color: Option<String>,
    pub width: Option<f64>,
    pub height: Option<f64>,
    /// Start with a cover page (in the cover colour) before the first paper page.
    #[serde(default)]
    pub cover_page: bool,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PdfPageSize {
    pub width: f64,
    pub height: f64,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct NewPdfNotebook {
    pub title: String,
    pub folder_id: Option<String>,
    pub cover_color: Option<String>,
    pub asset_id: String,
    pub pages: Vec<PdfPageSize>,
}

pub const MAX_PDF_BYTES: usize = 300 * 1024 * 1024;

/// Default page size: A4 at 96 DPI-ish logical units.
pub const DEFAULT_PAGE_WIDTH: f64 = 794.0;
pub const DEFAULT_PAGE_HEIGHT: f64 = 1123.0;

pub const TEMPLATES: &[&str] = &[
    "blank",
    "ruled",
    "narrow-ruled",
    "wide-ruled",
    "grid",
    "dotted",
    "cornell",
    "legal",
    "two-column",
    "checklist",
    "graph",
    "coordinate",
    "isometric",
    "hexagon",
    "handwriting",
    "calligraphy",
    "storyboard",
    "planner-day",
    "planner-week",
    "daftar",
    "daftar-timetable",
    "daftar-blue",
    "daftar-green",
    "daftar-pink",
    "daftar-navy",
    "daftar-orange",
    "daftar-title",
    "daftar-profile",
    "daftar-profile-2",
    "daftar-holidays",
    "daftar-diagnostic",
    "daftar-students",
    "daftar-memos",
    "daftar-absence",
    "cover",
    "cover-minimal",
    "cover-geometric",
    "cover-waves",
    "cover-marble",
    "cover-leather",
    "cover-kraft",
    "cover-dots",
    "cover-zellige",
];

fn validate_template(t: &str) -> AppResult<()> {
    if TEMPLATES.contains(&t) {
        Ok(())
    } else {
        Err(AppError::Invalid(format!("unknown page template '{t}'")))
    }
}

/// Accepts `#rrggbb` colours only.
fn validate_color(color: &str) -> AppResult<()> {
    let ok = color.len() == 7
        && color.starts_with('#')
        && color[1..].chars().all(|c| c.is_ascii_hexdigit());
    if ok {
        Ok(())
    } else {
        Err(AppError::Invalid(format!("invalid colour '{color}'")))
    }
}

fn validate_name(name: &str) -> AppResult<String> {
    let trimmed = name.trim();
    if trimmed.is_empty() {
        return Err(AppError::Invalid("name must not be empty".into()));
    }
    if trimmed.chars().count() > 200 {
        return Err(AppError::Invalid(
            "name is too long (max 200 characters)".into(),
        ));
    }
    Ok(trimmed.to_string())
}

pub struct Db {
    conn: Mutex<Connection>,
    /// Content keys of the locks unlocked this session, by lock id (never stored).
    keys: Mutex<HashMap<String, ContentKey>>,
    /// The database file (None in memory); needed to restore a backup.
    path: Option<PathBuf>,
}

/// What a backup file holds, shown before restoring it.
#[derive(Debug, Clone, Serialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct BackupSummary {
    pub notebooks: i64,
    pub folders: i64,
    pub pages: i64,
}

impl Db {
    pub fn open(path: &Path) -> AppResult<Self> {
        let conn = Connection::open(path)?;
        Self::init(conn, Some(path.to_path_buf()))
    }

    pub fn open_in_memory() -> AppResult<Self> {
        Self::init(Connection::open_in_memory()?, None)
    }

    fn configure(conn: &Connection) -> AppResult<()> {
        conn.execute_batch(
            "PRAGMA journal_mode = WAL;
             PRAGMA foreign_keys = ON;
             PRAGMA synchronous = NORMAL;",
        )?;
        Ok(())
    }

    fn init(conn: Connection, path: Option<PathBuf>) -> AppResult<Self> {
        Self::configure(&conn)?;
        Self::migrate(&conn)?;
        Ok(Db {
            conn: Mutex::new(conn),
            keys: Mutex::new(HashMap::new()),
            path,
        })
    }

    /// Runs `f` with the database connection (for sync, which works on raw rows).
    pub fn with_conn<T>(&self, f: impl FnOnce(&mut Connection) -> AppResult<T>) -> AppResult<T> {
        let mut conn = self.conn();
        f(&mut conn)
    }

    /// Writes a consistent copy of the whole library (notebooks, pages, ink,
    /// imported PDFs) to `dest` as one SQLite file. It is written under a
    /// temporary name first, so a synced folder never uploads half a file.
    pub fn snapshot_to(&self, dest: &Path) -> AppResult<()> {
        let mut part = dest.as_os_str().to_owned();
        part.push(".part");
        let part = PathBuf::from(part);
        let _ = std::fs::remove_file(&part);
        let target = part.to_string_lossy().into_owned();
        self.conn().execute("VACUUM INTO ?1", [target])?;
        std::fs::rename(&part, dest).inspect_err(|_| {
            let _ = std::fs::remove_file(&part);
        })?;
        Ok(())
    }

    /// Checks that `path` is an Inkleaf backup this version can open, and
    /// counts what's in it.
    pub fn inspect_backup(path: &Path) -> AppResult<BackupSummary> {
        let bad = || AppError::Invalid("that file isn't an Inkleaf backup".into());
        let conn = Connection::open_with_flags(path, OpenFlags::SQLITE_OPEN_READ_ONLY | OpenFlags::SQLITE_OPEN_NO_MUTEX).map_err(|_| bad())?;
        let version: i64 = conn.query_row("PRAGMA user_version", [], |r| r.get(0)).map_err(|_| bad())?;
        if version < 1 {
            return Err(bad());
        }
        if version > SCHEMA_VERSION {
            return Err(AppError::Invalid("that backup was made by a newer Inkleaf: update the app to restore it".into()));
        }
        let check: String = conn.query_row("PRAGMA quick_check", [], |r| r.get(0)).map_err(|_| bad())?;
        if check != "ok" {
            return Err(AppError::Invalid("that backup file is damaged".into()));
        }
        let count = |table: &str| -> AppResult<i64> {
            conn.query_row(&format!("SELECT COUNT(*) FROM {table} WHERE trashed_at IS NULL"), [], |r| r.get(0)).map_err(|_| bad())
        };
        let pages: i64 = conn
            .query_row("SELECT COUNT(*) FROM pages p JOIN notebooks n ON n.id = p.notebook_id WHERE n.trashed_at IS NULL", [], |r| r.get(0))
            .map_err(|_| bad())?;
        Ok(BackupSummary { notebooks: count("notebooks")?, folders: count("folders")?, pages })
    }

    /// Replaces the whole library with a backup. The current library is
    /// first saved next to the database as `inkleaf-before-restore.db`; if
    /// anything fails, the current library is reopened unchanged.
    pub fn restore_from(&self, src: &Path) -> AppResult<BackupSummary> {
        let summary = Self::inspect_backup(src)?;
        let path = self.path.clone().ok_or_else(|| AppError::Invalid("restoring needs a database file".into()))?;
        let safety = path.with_file_name("inkleaf-before-restore.db");
        // Read the backup before touching anything (it may sit in the same folder).
        let bytes = std::fs::read(src)?;
        let mut conn = self.conn();
        let _ = std::fs::remove_file(&safety);
        conn.execute("VACUUM INTO ?1", [safety.to_string_lossy().into_owned()])?;
        // Close the database so its files can be replaced.
        drop(std::mem::replace(&mut *conn, Connection::open_in_memory()?));
        let swap = (|| -> AppResult<Connection> {
            for ext in ["-wal", "-shm"] {
                let mut side = path.as_os_str().to_owned();
                side.push(ext);
                let _ = std::fs::remove_file(PathBuf::from(side));
            }
            std::fs::write(&path, &bytes)?;
            let next = Connection::open(&path)?;
            Self::configure(&next)?;
            Self::migrate(&next)?;
            Ok(next)
        })();
        match swap {
            Ok(next) => {
                // The restored library replaces what's in the cloud too: send
                // all of it on the next sync.
                let _ = next.execute_batch(
                    "DELETE FROM sync_state;
                     DELETE FROM sync_tombstones;
                     INSERT OR IGNORE INTO sync_changes SELECT id FROM notebooks;
                     INSERT OR IGNORE INTO sync_changes VALUES ('__folders__');",
                );
                *conn = next;
                Ok(summary)
            }
            Err(e) => {
                // Put the library back as it was.
                let _ = std::fs::copy(&safety, &path);
                if let Ok(old) = Connection::open(&path) {
                    let _ = Self::configure(&old);
                    *conn = old;
                }
                Err(e)
            }
        }
    }

    fn conn(&self) -> std::sync::MutexGuard<'_, Connection> {
        // A poisoned lock only means another command panicked mid-call; the
        // SQLite connection itself is still usable.
        self.conn.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn migrate(conn: &Connection) -> AppResult<()> {
        let version: i64 = conn.query_row("PRAGMA user_version", [], |r| r.get(0))?;
        if version < 1 {
            conn.execute_batch(
                "CREATE TABLE IF NOT EXISTS folders (
                    id TEXT PRIMARY KEY,
                    name TEXT NOT NULL,
                    parent_id TEXT REFERENCES folders(id) ON DELETE CASCADE,
                    color TEXT NOT NULL DEFAULT '#6b7280',
                    favourite INTEGER NOT NULL DEFAULT 0,
                    created_at INTEGER NOT NULL,
                    updated_at INTEGER NOT NULL,
                    trashed_at INTEGER
                );
                CREATE TABLE IF NOT EXISTS notebooks (
                    id TEXT PRIMARY KEY,
                    title TEXT NOT NULL,
                    folder_id TEXT REFERENCES folders(id) ON DELETE CASCADE,
                    cover_color TEXT NOT NULL DEFAULT '#3b82f6',
                    default_template TEXT NOT NULL DEFAULT 'ruled',
                    favourite INTEGER NOT NULL DEFAULT 0,
                    created_at INTEGER NOT NULL,
                    updated_at INTEGER NOT NULL,
                    trashed_at INTEGER
                );
                CREATE TABLE IF NOT EXISTS pages (
                    id TEXT PRIMARY KEY,
                    notebook_id TEXT NOT NULL REFERENCES notebooks(id) ON DELETE CASCADE,
                    position INTEGER NOT NULL,
                    template TEXT NOT NULL,
                    paper_color TEXT NOT NULL DEFAULT '#fffdf7',
                    width REAL NOT NULL,
                    height REAL NOT NULL,
                    favourite INTEGER NOT NULL DEFAULT 0,
                    thumbnail TEXT,
                    created_at INTEGER NOT NULL,
                    updated_at INTEGER NOT NULL
                );
                CREATE INDEX IF NOT EXISTS pages_by_notebook ON pages(notebook_id, position);
                CREATE TABLE IF NOT EXISTS elements (
                    id TEXT PRIMARY KEY,
                    page_id TEXT NOT NULL REFERENCES pages(id) ON DELETE CASCADE,
                    kind TEXT NOT NULL,
                    z INTEGER NOT NULL,
                    data TEXT NOT NULL,
                    text_content TEXT
                );
                CREATE INDEX IF NOT EXISTS elements_by_page ON elements(page_id, z);",
            )?;
            conn.pragma_update(None, "user_version", 1)?;
        }
        if version < 2 {
            // v2: imported PDFs (stored once per notebook) as page backgrounds.
            conn.execute_batch(
                "CREATE TABLE IF NOT EXISTS assets (
                    id TEXT PRIMARY KEY,
                    notebook_id TEXT REFERENCES notebooks(id) ON DELETE CASCADE,
                    kind TEXT NOT NULL,
                    data BLOB NOT NULL,
                    created_at INTEGER NOT NULL
                );
                CREATE INDEX IF NOT EXISTS assets_by_notebook ON assets(notebook_id);
                ALTER TABLE pages ADD COLUMN background_asset TEXT;
                ALTER TABLE pages ADD COLUMN background_page INTEGER;",
            )?;
            conn.pragma_update(None, "user_version", 2)?;
        }
        if version < 3 {
            // v3: notebook settings (JSON) and page sections.
            conn.execute_batch(
                "ALTER TABLE notebooks ADD COLUMN meta TEXT;
                ALTER TABLE pages ADD COLUMN section TEXT;",
            )?;
            conn.pragma_update(None, "user_version", 3)?;
        }
        if version < 4 {
            // v4: sync bookkeeping. Triggers note which notebooks changed (any
            // real edit; not thumbnails, which every device redraws itself) and
            // what was deleted for good, so sync knows what to send.
            conn.execute_batch(
                "CREATE TABLE IF NOT EXISTS sync_changes (notebook_id TEXT PRIMARY KEY);
                CREATE TABLE IF NOT EXISTS sync_tombstones (
                    kind TEXT NOT NULL,
                    id TEXT NOT NULL,
                    deleted_at INTEGER NOT NULL,
                    PRIMARY KEY (kind, id)
                );
                CREATE TABLE IF NOT EXISTS sync_state (
                    item TEXT PRIMARY KEY,
                    file_id TEXT NOT NULL,
                    version TEXT NOT NULL
                );
                CREATE TRIGGER IF NOT EXISTS sync_nb_ins AFTER INSERT ON notebooks BEGIN
                    INSERT OR IGNORE INTO sync_changes VALUES (NEW.id);
                END;
                CREATE TRIGGER IF NOT EXISTS sync_nb_upd AFTER UPDATE ON notebooks BEGIN
                    INSERT OR IGNORE INTO sync_changes VALUES (NEW.id);
                END;
                CREATE TRIGGER IF NOT EXISTS sync_nb_del AFTER DELETE ON notebooks BEGIN
                    INSERT OR REPLACE INTO sync_tombstones VALUES ('notebook', OLD.id, CAST(strftime('%s', 'now') AS INTEGER) * 1000);
                    DELETE FROM sync_changes WHERE notebook_id = OLD.id;
                END;
                CREATE TRIGGER IF NOT EXISTS sync_pg_ins AFTER INSERT ON pages BEGIN
                    INSERT OR IGNORE INTO sync_changes VALUES (NEW.notebook_id);
                END;
                CREATE TRIGGER IF NOT EXISTS sync_pg_upd AFTER UPDATE OF notebook_id, position, template, paper_color, width, height, favourite, background_asset, background_page, section ON pages BEGIN
                    INSERT OR IGNORE INTO sync_changes VALUES (NEW.notebook_id);
                END;
                CREATE TRIGGER IF NOT EXISTS sync_pg_del AFTER DELETE ON pages BEGIN
                    INSERT OR IGNORE INTO sync_changes SELECT OLD.notebook_id WHERE EXISTS (SELECT 1 FROM notebooks WHERE id = OLD.notebook_id);
                END;
                CREATE TRIGGER IF NOT EXISTS sync_el_ins AFTER INSERT ON elements BEGIN
                    INSERT OR IGNORE INTO sync_changes SELECT notebook_id FROM pages WHERE id = NEW.page_id;
                END;
                CREATE TRIGGER IF NOT EXISTS sync_el_upd AFTER UPDATE ON elements BEGIN
                    INSERT OR IGNORE INTO sync_changes SELECT notebook_id FROM pages WHERE id = NEW.page_id;
                END;
                CREATE TRIGGER IF NOT EXISTS sync_el_del AFTER DELETE ON elements BEGIN
                    INSERT OR IGNORE INTO sync_changes SELECT notebook_id FROM pages WHERE id = OLD.page_id;
                END;
                CREATE TRIGGER IF NOT EXISTS sync_f_ins AFTER INSERT ON folders BEGIN
                    INSERT OR IGNORE INTO sync_changes VALUES ('__folders__');
                END;
                CREATE TRIGGER IF NOT EXISTS sync_f_upd AFTER UPDATE ON folders BEGIN
                    INSERT OR IGNORE INTO sync_changes VALUES ('__folders__');
                END;
                CREATE TRIGGER IF NOT EXISTS sync_f_del AFTER DELETE ON folders BEGIN
                    INSERT OR REPLACE INTO sync_tombstones VALUES ('folder', OLD.id, CAST(strftime('%s', 'now') AS INTEGER) * 1000);
                    INSERT OR IGNORE INTO sync_changes VALUES ('__folders__');
                END;",
            )?;
            conn.pragma_update(None, "user_version", 4)?;
        }
        if version < 5 {
            // v5: password protection (the lock's description; content is encrypted).
            for table in ["notebooks", "folders"] {
                let has: bool = conn
                    .prepare(&format!("PRAGMA table_info({table})"))?
                    .query_map([], |r| r.get::<_, String>(1))?
                    .any(|c| c.is_ok_and(|c| c == "lock"));
                if !has {
                    conn.execute_batch(&format!("ALTER TABLE {table} ADD COLUMN lock TEXT;"))?;
                }
            }
            conn.pragma_update(None, "user_version", 5)?;
        }
        if version < 6 {
            // v6: handwriting read by the AI (for search and questions), and when.
            let cols: Vec<String> = conn.prepare("PRAGMA table_info(pages)")?.query_map([], |r| r.get::<_, String>(1))?.collect::<Result<_, _>>()?;
            if !cols.iter().any(|c| c == "ink_text") {
                conn.execute_batch("ALTER TABLE pages ADD COLUMN ink_text TEXT; ALTER TABLE pages ADD COLUMN ink_text_at INTEGER;")?;
            }
            conn.pragma_update(None, "user_version", 6)?;
        }
        if version < 7 {
            // v7: reusable pieces (a selection saved to put on any page).
            conn.execute_batch(
                "CREATE TABLE IF NOT EXISTS snippets (
                    id TEXT PRIMARY KEY,
                    name TEXT NOT NULL,
                    data TEXT NOT NULL,
                    preview TEXT,
                    width REAL NOT NULL,
                    height REAL NOT NULL,
                    created_at INTEGER NOT NULL,
                    used_at INTEGER
                );",
            )?;
            conn.pragma_update(None, "user_version", SCHEMA_VERSION)?;
        }
        // PDFs stored for an import that never finished.
        conn.execute(
            "DELETE FROM assets WHERE notebook_id IS NULL AND created_at < ?1",
            [now_ms() - 3_600_000],
        )?;
        Ok(())
    }

    // ---------------------------------------------------------------- locks

    fn keys(&self) -> std::sync::MutexGuard<'_, HashMap<String, ContentKey>> {
        self.keys.lock().unwrap_or_else(|e| e.into_inner())
    }

    /// The content key of a lock column's lock, if it's unlocked.
    fn key_of(&self, lock: &Option<String>) -> Option<ContentKey> {
        let id = lock::lock_id(lock.as_deref()?)?;
        self.keys().get(&id).copied()
    }

    /// No lock: None; locked and unlocked: its key; locked: Err(Locked).
    fn need_key(&self, lock: &Option<String>) -> AppResult<Option<ContentKey>> {
        match lock {
            None => Ok(None),
            Some(_) => self.key_of(lock).map(Some).ok_or(AppError::Locked),
        }
    }

    fn reveal_notebook(&self, mut n: Notebook) -> Notebook {
        if n.lock.is_none() {
            return n;
        }
        n.locked = true;
        n.lock_id = n.lock.as_deref().and_then(lock::lock_id);
        match self.key_of(&n.lock) {
            Some(k) => {
                n.unlocked = true;
                n.thumbnail = n.thumbnail.and_then(|t| lock::decrypt_text(&k, &t).ok());
                n.meta = n.meta.and_then(|m| lock::decrypt_text(&k, &m).ok());
            }
            None => {
                n.thumbnail = None;
                n.meta = None;
            }
        }
        n
    }

    fn reveal_folder(&self, mut f: Folder) -> Folder {
        f.locked = f.lock.is_some();
        f.unlocked = f.locked && self.key_of(&f.lock).is_some();
        f.lock_id = f.lock.as_deref().and_then(lock::lock_id);
        f
    }

    fn reveal_library(&self, lib: Library) -> Library {
        Library {
            folders: lib.folders.into_iter().map(|f| self.reveal_folder(f)).collect(),
            notebooks: lib.notebooks.into_iter().map(|n| self.reveal_notebook(n)).collect(),
        }
    }

    fn reveal_page(&self, mut p: Page, lock: &Option<String>) -> Page {
        if lock.is_some() {
            p.thumbnail = match self.key_of(lock) {
                Some(k) => p.thumbnail.and_then(|t| lock::decrypt_text(&k, &t).ok()),
                None => None,
            };
        }
        p
    }

    /// Locks a notebook with a password: its pages, ink, text, pictures,
    /// imported PDF and settings are encrypted. It stays unlocked this session.
    pub fn lock_notebook(&self, id: &str, password: &str) -> AppResult<Notebook> {
        let mut conn = self.conn();
        if notebook_lock(&conn, id)?.is_some() {
            return Err(AppError::Invalid("It already has a password: change it instead.".into()));
        }
        let (info, key) = lock::new_lock(password)?;
        let tx = conn.transaction()?;
        encrypt_notebook(&tx, id, &key)?;
        tx.execute("UPDATE notebooks SET lock = ?2, updated_at = ?3 WHERE id = ?1", params![id, info.to_json(), now_ms()])?;
        tx.commit()?;
        self.keys().insert(info.id.clone(), key);
        Ok(self.reveal_notebook(get_notebook(&conn, id)?))
    }

    /// Locks a folder with a password: everything in it (subfolders, their
    /// notebooks, and anything added later) is encrypted and opens with it.
    /// Notebooks with a password of their own keep it.
    pub fn lock_folder(&self, id: &str, password: &str) -> AppResult<()> {
        let mut conn = self.conn();
        if folder_lock(&conn, id)?.is_some() {
            return Err(AppError::Invalid("It already has a password: change it instead.".into()));
        }
        let (info, key) = lock::new_lock(password)?;
        let tx = conn.transaction()?;
        apply_lock_to_tree(&tx, id, &info.to_json(), &key)?;
        tx.commit()?;
        self.keys().insert(info.id, key);
        Ok(())
    }

    /// Unlocks a notebook or folder ("notebook" | "folder") for this session.
    pub fn unlock(&self, kind: &str, id: &str, password: &str) -> AppResult<()> {
        let lock = self.lock_of(kind, id)?.ok_or_else(|| AppError::Invalid("It has no password.".into()))?;
        let info = LockInfo::parse(&lock)?;
        let key = lock::unlock(&info, password)?;
        self.keys().insert(info.id, key);
        Ok(())
    }

    /// Locks everything again (forgets every key).
    pub fn lock_all(&self) {
        self.keys().clear();
    }

    /// Whether anything is unlocked this session.
    pub fn any_unlocked(&self) -> bool {
        !self.keys().is_empty()
    }

    fn lock_of(&self, kind: &str, id: &str) -> AppResult<Option<String>> {
        let conn = self.conn();
        match kind {
            "notebook" => notebook_lock(&conn, id),
            "folder" => folder_lock(&conn, id),
            _ => Err(AppError::Invalid(format!("unknown kind '{kind}'"))),
        }
    }

    /// A new password for an unlocked notebook or folder (and everything
    /// sharing its lock). The content isn't re-encrypted: only its key is.
    pub fn change_password(&self, kind: &str, id: &str, password: &str) -> AppResult<()> {
        let lock = self.lock_of(kind, id)?.ok_or_else(|| AppError::Invalid("It has no password.".into()))?;
        let key = self.need_key(&Some(lock.clone()))?.expect("locked");
        let info = LockInfo::parse(&lock)?;
        let next = lock::rewrap(&info, &key, password)?.to_json();
        let mut conn = self.conn();
        let tx = conn.transaction()?;
        let now = now_ms();
        for table in ["folders", "notebooks"] {
            let rows: Vec<(String, String)> = {
                let mut stmt = tx.prepare(&format!("SELECT id, lock FROM {table} WHERE lock IS NOT NULL"))?;
                let rows = stmt.query_map([], |r| Ok((r.get(0)?, r.get(1)?)))?;
                rows.collect::<Result<_, _>>()?
            };
            for (row, l) in rows {
                if lock::lock_id(&l).as_deref() == Some(info.id.as_str()) {
                    tx.execute(&format!("UPDATE {table} SET lock = ?2, updated_at = ?3 WHERE id = ?1"), params![row, next, now])?;
                }
            }
        }
        tx.commit()?;
        Ok(())
    }

    /// Removes the password from an unlocked notebook, or from a folder and
    /// what's inside it that shares its lock: decrypted again.
    pub fn remove_lock(&self, kind: &str, id: &str) -> AppResult<()> {
        let lock = self.lock_of(kind, id)?.ok_or_else(|| AppError::Invalid("It has no password.".into()))?;
        let key = self.need_key(&Some(lock.clone()))?.expect("locked");
        let lock_id = lock::lock_id(&lock);
        let mut conn = self.conn();
        let tx = conn.transaction()?;
        let now = now_ms();
        let notebooks: Vec<String> = if kind == "notebook" {
            vec![id.to_string()]
        } else {
            let folders = folder_tree(&tx, id)?;
            for f in &folders {
                let l: Option<String> = tx.query_row("SELECT lock FROM folders WHERE id = ?1", [f], |r| r.get(0))?;
                if l.as_deref().and_then(lock::lock_id) == lock_id {
                    tx.execute("UPDATE folders SET lock = NULL, updated_at = ?2 WHERE id = ?1", params![f, now])?;
                }
            }
            notebooks_in(&tx, &folders)?
        };
        for n in notebooks {
            let l = notebook_lock(&tx, &n)?;
            if l.as_deref().and_then(lock::lock_id) == lock_id {
                decrypt_notebook(&tx, &n, &key)?;
                tx.execute("UPDATE notebooks SET lock = NULL, updated_at = ?2 WHERE id = ?1", params![n, now])?;
            }
        }
        tx.commit()?;
        Ok(())
    }

    // ---------------------------------------------------------------- library

    pub fn library(&self) -> AppResult<Library> {
        let conn = self.conn();
        Ok(self.reveal_library(Library {
            folders: query_folders(
                &conn,
                &format!("WHERE id NOT IN {TRASHED_FOLDERS} ORDER BY name COLLATE NOCASE"),
            )?,
            notebooks: query_notebooks(
                &conn,
                &format!(
                    "WHERE n.trashed_at IS NULL AND (n.folder_id IS NULL OR n.folder_id NOT IN {TRASHED_FOLDERS})
                     ORDER BY n.updated_at DESC"
                ),
            )?,
        }))
    }

    pub fn trash(&self) -> AppResult<TrashContents> {
        let conn = self.conn();
        let lib = self.reveal_library(Library {
            folders: query_folders(
                &conn,
                "WHERE trashed_at IS NOT NULL ORDER BY trashed_at DESC",
            )?,
            notebooks: query_notebooks(
                &conn,
                "WHERE n.trashed_at IS NOT NULL ORDER BY n.trashed_at DESC",
            )?,
        });
        Ok(TrashContents { folders: lib.folders, notebooks: lib.notebooks })
    }

    /// Deletes for good everything in the trash: trashed notebooks, and
    /// trashed folders with all they contain (subfolders and notebooks).
    pub fn empty_trash(&self) -> AppResult<()> {
        let mut conn = self.conn();
        let tx = conn.transaction()?;
        tx.execute(&format!("DELETE FROM notebooks WHERE trashed_at IS NOT NULL OR folder_id IN {TRASHED_FOLDERS}"), [])?;
        tx.execute(&format!("DELETE FROM folders WHERE id IN {TRASHED_FOLDERS}"), [])?;
        tx.commit()?;
        Ok(())
    }

    // ---------------------------------------------------------------- folders

    pub fn create_folder(
        &self,
        name: &str,
        parent_id: Option<&str>,
        color: Option<&str>,
    ) -> AppResult<Folder> {
        let name = validate_name(name)?;
        let conn = self.conn();
        if let Some(parent) = parent_id {
            ensure_exists(&conn, "folders", parent)?;
        }
        // Inside a locked folder: locked with it.
        let lock = match parent_id {
            Some(parent) => folder_lock(&conn, parent)?,
            None => None,
        };
        let now = now_ms();
        let folder = Folder {
            id: new_id(),
            name,
            parent_id: parent_id.map(str::to_string),
            color: color.unwrap_or("#6b7280").to_string(),
            favourite: false,
            created_at: now,
            updated_at: now,
            trashed_at: None,
            locked: lock.is_some(),
            unlocked: false,
            lock_id: None,
            lock: lock.clone(),
        };
        conn.execute(
            "INSERT INTO folders (id, name, parent_id, color, favourite, created_at, updated_at, lock)
             VALUES (?1, ?2, ?3, ?4, 0, ?5, ?5, ?6)",
            params![folder.id, folder.name, folder.parent_id, folder.color, now, lock],
        )?;
        Ok(self.reveal_folder(folder))
    }

    pub fn rename_folder(&self, id: &str, name: &str) -> AppResult<()> {
        let name = validate_name(name)?;
        let conn = self.conn();
        touch_row(
            &conn,
            "UPDATE folders SET name = ?2, updated_at = ?3 WHERE id = ?1",
            id,
            &name,
        )
    }

    pub fn move_folder(&self, id: &str, parent_id: Option<&str>) -> AppResult<()> {
        let conn = self.conn();
        if let Some(parent) = parent_id {
            ensure_exists(&conn, "folders", parent)?;
            // Refuse cycles: walk up from the new parent.
            let mut cursor = Some(parent.to_string());
            while let Some(current) = cursor {
                if current == id {
                    return Err(AppError::Invalid(
                        "a folder cannot be moved inside itself".into(),
                    ));
                }
                cursor = conn
                    .query_row(
                        "SELECT parent_id FROM folders WHERE id = ?1",
                        [&current],
                        |r| r.get(0),
                    )
                    .optional()?
                    .flatten();
            }
        }
        ensure_exists(&conn, "folders", id)?;
        // Into a locked folder: everything inside is locked with it.
        let lock = match parent_id {
            Some(parent) => folder_lock(&conn, parent)?,
            None => None,
        };
        let key = self.need_key(&lock)?;
        drop(conn);
        let mut conn = self.conn();
        let tx = conn.transaction()?;
        tx.execute(
            "UPDATE folders SET parent_id = ?2, updated_at = ?3 WHERE id = ?1",
            params![id, parent_id, now_ms()],
        )?;
        if let (Some(lock), Some(key)) = (&lock, &key) {
            apply_lock_to_tree(&tx, id, lock, key)?;
        }
        tx.commit()?;
        Ok(())
    }

    pub fn set_folder_trashed(&self, id: &str, trashed: bool) -> AppResult<()> {
        let conn = self.conn();
        let value = trashed.then(now_ms);
        let n = conn.execute(
            "UPDATE folders SET trashed_at = ?2, updated_at = ?3 WHERE id = ?1",
            params![id, value, now_ms()],
        )?;
        expect_one(n, id)
    }

    /// Deletes a folder for good, with its subfolders and every notebook in them.
    pub fn delete_folder(&self, id: &str) -> AppResult<()> {
        let mut conn = self.conn();
        let tree = "(WITH RECURSIVE t(id) AS (
                SELECT id FROM folders WHERE id = ?1
                UNION SELECT c.id FROM folders c JOIN t ON c.parent_id = t.id
            ) SELECT id FROM t)";
        let tx = conn.transaction()?;
        tx.execute(&format!("DELETE FROM notebooks WHERE folder_id IN {tree}"), [id])?;
        let n = tx.execute(&format!("DELETE FROM folders WHERE id IN {tree}"), [id])?;
        if n == 0 {
            return Err(AppError::NotFound(id.to_string()));
        }
        tx.commit()?;
        Ok(())
    }

    pub fn set_folder_favourite(&self, id: &str, favourite: bool) -> AppResult<()> {
        let conn = self.conn();
        let n = conn.execute(
            "UPDATE folders SET favourite = ?2, updated_at = ?3 WHERE id = ?1",
            params![id, favourite, now_ms()],
        )?;
        expect_one(n, id)
    }

    pub fn set_folder_color(&self, id: &str, color: &str) -> AppResult<()> {
        validate_color(color)?;
        let conn = self.conn();
        let n = conn.execute(
            "UPDATE folders SET color = ?2, updated_at = ?3 WHERE id = ?1",
            params![id, color, now_ms()],
        )?;
        expect_one(n, id)
    }

    // -------------------------------------------------------------- notebooks

    pub fn create_notebook(&self, input: &NewNotebook) -> AppResult<Notebook> {
        let title = validate_name(&input.title)?;
        let template = input.template.clone().unwrap_or_else(|| "ruled".into());
        validate_template(&template)?;
        if let Some(color) = &input.paper_color {
            validate_color(color)?;
        }
        for size in [input.width, input.height].into_iter().flatten() {
            if !(100.0..=5000.0).contains(&size) {
                return Err(AppError::Invalid(
                    "page size must be between 100 and 5000 units".into(),
                ));
            }
        }
        let mut conn = self.conn();
        if let Some(folder) = &input.folder_id {
            ensure_exists(&conn, "folders", folder)?;
        }
        // In a locked folder: locked with it (which needs the folder unlocked).
        let lock = match &input.folder_id {
            Some(folder) => folder_lock(&conn, folder)?,
            None => None,
        };
        self.need_key(&lock)?;
        let cover_color = input.cover_color.clone().unwrap_or_else(|| "#3b82f6".into());
        validate_color(&cover_color)?;
        let now = now_ms();
        let id = new_id();
        let tx = conn.transaction()?;
        tx.execute(
            "INSERT INTO notebooks (id, title, folder_id, cover_color, default_template, favourite, created_at, updated_at, lock)
             VALUES (?1, ?2, ?3, ?4, ?5, 0, ?6, ?6, ?7)",
            params![
                id,
                title,
                input.folder_id,
                cover_color,
                template,
                now,
                lock
            ],
        )?;
        let width = input.width.unwrap_or(DEFAULT_PAGE_WIDTH);
        let height = input.height.unwrap_or(DEFAULT_PAGE_HEIGHT);
        let mut pages = Vec::new();
        if input.cover_page {
            pages.push(("cover".to_string(), cover_color.clone()));
        }
        pages.push((template.clone(), input.paper_color.clone().unwrap_or_else(|| "#fffdf7".into())));
        for (position, (page_template, paper)) in pages.into_iter().enumerate() {
            tx.execute(
                "INSERT INTO pages (id, notebook_id, position, template, paper_color, width, height, created_at, updated_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?8)",
                params![new_id(), id, position as i64, page_template, paper, width, height, now],
            )?;
        }
        tx.commit()?;
        Ok(self.reveal_notebook(get_notebook(&conn, &id)?))
    }

    pub fn notebook(&self, id: &str) -> AppResult<Notebook> {
        Ok(self.reveal_notebook(get_notebook(&self.conn(), id)?))
    }

    pub fn rename_notebook(&self, id: &str, title: &str) -> AppResult<()> {
        let title = validate_name(title)?;
        let conn = self.conn();
        touch_row(
            &conn,
            "UPDATE notebooks SET title = ?2, updated_at = ?3 WHERE id = ?1",
            id,
            &title,
        )
    }

    pub fn move_notebook(&self, id: &str, folder_id: Option<&str>) -> AppResult<()> {
        let mut conn = self.conn();
        if let Some(folder) = folder_id {
            ensure_exists(&conn, "folders", folder)?;
        }
        // Into a locked folder, an unlocked notebook is locked with it. (A
        // locked notebook keeps its own lock wherever it goes.)
        let own = notebook_lock(&conn, id)?;
        let dest = match folder_id {
            Some(folder) => folder_lock(&conn, folder)?,
            None => None,
        };
        let key = if own.is_none() { self.need_key(&dest)? } else { None };
        let tx = conn.transaction()?;
        let n = tx.execute(
            "UPDATE notebooks SET folder_id = ?2, updated_at = ?3 WHERE id = ?1",
            params![id, folder_id, now_ms()],
        )?;
        expect_one(n, id)?;
        if let (Some(lock), Some(key)) = (&dest, &key) {
            encrypt_notebook(&tx, id, key)?;
            tx.execute("UPDATE notebooks SET lock = ?2 WHERE id = ?1", params![id, lock])?;
        }
        tx.commit()?;
        Ok(())
    }

    pub fn set_notebook_cover(&self, id: &str, color: &str) -> AppResult<()> {
        validate_color(color)?;
        let conn = self.conn();
        touch_row(
            &conn,
            "UPDATE notebooks SET cover_color = ?2, updated_at = ?3 WHERE id = ?1",
            id,
            color,
        )?;
        // A cover page takes the new colour too (its thumbnail is redrawn by the app).
        conn.execute(
            "UPDATE pages SET paper_color = ?2, thumbnail = NULL, updated_at = ?3
             WHERE notebook_id = ?1 AND template LIKE 'cover%' AND position = 0",
            params![id, color, now_ms()],
        )?;
        Ok(())
    }

    /// Stores the notebook's app-defined settings (JSON), or clears them.
    pub fn set_notebook_meta(&self, id: &str, meta: Option<&str>) -> AppResult<()> {
        if let Some(m) = meta {
            if m.len() > 256 * 1024 {
                return Err(AppError::Invalid("notebook settings are too large".into()));
            }
            serde_json::from_str::<serde_json::Value>(m)
                .map_err(|e| AppError::Invalid(format!("notebook settings are not valid JSON: {e}")))?;
        }
        let conn = self.conn();
        let key = self.need_key(&notebook_lock(&conn, id)?)?;
        let stored = match (&key, meta) {
            (Some(k), Some(m)) => Some(lock::encrypt_text(k, m)),
            _ => meta.map(str::to_string),
        };
        let n = conn.execute(
            "UPDATE notebooks SET meta = ?2, updated_at = ?3 WHERE id = ?1",
            params![id, stored, now_ms()],
        )?;
        if n == 0 {
            return Err(AppError::NotFound(id.to_string()));
        }
        Ok(())
    }

    /// Puts a page in a section of its notebook (or none).
    pub fn set_page_section(&self, page_id: &str, section: Option<&str>) -> AppResult<()> {
        if section.is_some_and(|s| s.len() > 200) {
            return Err(AppError::Invalid("section name is too long".into()));
        }
        let conn = self.conn();
        let n = conn.execute(
            "UPDATE pages SET section = ?2, updated_at = ?3 WHERE id = ?1",
            params![page_id, section, now_ms()],
        )?;
        if n == 0 {
            return Err(AppError::NotFound(page_id.to_string()));
        }
        Ok(())
    }

    pub fn set_notebook_trashed(&self, id: &str, trashed: bool) -> AppResult<()> {
        let conn = self.conn();
        let value = trashed.then(now_ms);
        // Recovering a notebook whose folder is trashed moves it to the root
        // so it becomes visible again.
        if !trashed {
            conn.execute(
                "UPDATE notebooks SET folder_id = NULL WHERE id = ?1 AND folder_id IN
                   (SELECT id FROM folders WHERE trashed_at IS NOT NULL)",
                [id],
            )?;
        }
        let n = conn.execute(
            "UPDATE notebooks SET trashed_at = ?2, updated_at = ?3 WHERE id = ?1",
            params![id, value, now_ms()],
        )?;
        expect_one(n, id)
    }

    pub fn delete_notebook(&self, id: &str) -> AppResult<()> {
        let conn = self.conn();
        expect_one(
            conn.execute("DELETE FROM notebooks WHERE id = ?1", [id])?,
            id,
        )
    }

    pub fn set_notebook_favourite(&self, id: &str, favourite: bool) -> AppResult<()> {
        let conn = self.conn();
        let n = conn.execute(
            "UPDATE notebooks SET favourite = ?2 WHERE id = ?1",
            params![id, favourite],
        )?;
        expect_one(n, id)
    }

    /// Deep-copies a notebook with all of its pages and elements.
    pub fn duplicate_notebook(&self, id: &str) -> AppResult<Notebook> {
        let mut conn = self.conn();
        let source = get_notebook(&conn, id)?;
        let now = now_ms();
        let new_nb = new_id();
        let tx = conn.transaction()?;
        tx.execute(
            "INSERT INTO notebooks (id, title, folder_id, cover_color, default_template, favourite, created_at, updated_at, meta, lock)
             SELECT ?1, ?2, folder_id, cover_color, default_template, 0, ?3, ?3, meta, lock FROM notebooks WHERE id = ?4",
            params![new_nb, format!("{} copy", source.title), now, id],
        )?;
        let page_ids: Vec<String> = {
            let mut stmt =
                tx.prepare("SELECT id FROM pages WHERE notebook_id = ?1 ORDER BY position")?;
            let rows = stmt.query_map([id], |r| r.get(0))?;
            rows.collect::<Result<_, _>>()?
        };
        for old_page in page_ids {
            let new_page = new_id();
            tx.execute(
                "INSERT INTO pages (id, notebook_id, position, template, paper_color, width, height, favourite, thumbnail, created_at, updated_at, background_asset, background_page, section)
                 SELECT ?1, ?2, position, template, paper_color, width, height, favourite, thumbnail, ?3, ?3, background_asset, background_page, section FROM pages WHERE id = ?4",
                params![new_page, new_nb, now, old_page],
            )?;
            copy_elements(&tx, &old_page, &new_page)?;
        }
        // Give the copy its own copies of any imported PDFs.
        let assets: Vec<String> = {
            let mut stmt = tx.prepare("SELECT id FROM assets WHERE notebook_id = ?1")?;
            let rows = stmt.query_map([id], |r| r.get(0))?;
            rows.collect::<Result<_, _>>()?
        };
        for old in assets {
            let new = new_id();
            tx.execute(
                "INSERT INTO assets (id, notebook_id, kind, data, created_at) SELECT ?1, ?2, kind, data, ?3 FROM assets WHERE id = ?4",
                params![new, new_nb, now, old],
            )?;
            tx.execute(
                "UPDATE pages SET background_asset = ?1 WHERE notebook_id = ?2 AND background_asset = ?3",
                params![new, new_nb, old],
            )?;
        }
        tx.commit()?;
        Ok(self.reveal_notebook(get_notebook(&conn, &new_nb)?))
    }

    // ------------------------------------------------------------ imported PDFs

    /// Stores PDF bytes, not yet attached to a notebook. Returns the asset id.
    pub fn store_pdf_asset(&self, bytes: &[u8]) -> AppResult<String> {
        if !bytes.starts_with(b"%PDF") {
            return Err(AppError::Invalid("that file isn't a PDF".into()));
        }
        if bytes.len() > MAX_PDF_BYTES {
            return Err(AppError::Invalid(
                "PDFs larger than 300 MB aren't supported".into(),
            ));
        }
        let id = new_id();
        self.conn().execute(
            "INSERT INTO assets (id, notebook_id, kind, data, created_at) VALUES (?1, NULL, 'pdf', ?2, ?3)",
            params![id, bytes, now_ms()],
        )?;
        Ok(id)
    }

    /// Creates a notebook whose pages show the pages of a stored PDF.
    pub fn create_pdf_notebook(&self, input: &NewPdfNotebook) -> AppResult<Notebook> {
        let title = validate_name(&input.title)?;
        if input.pages.is_empty() || input.pages.len() > 5000 {
            return Err(AppError::Invalid(
                "a PDF needs between 1 and 5000 pages".into(),
            ));
        }
        if input
            .pages
            .iter()
            .any(|p| !(20.0..=20000.0).contains(&p.width) || !(20.0..=20000.0).contains(&p.height))
        {
            return Err(AppError::Invalid("unsupported PDF page size".into()));
        }
        let mut conn = self.conn();
        if let Some(folder) = &input.folder_id {
            ensure_exists(&conn, "folders", folder)?;
        }
        // In a locked folder: locked with it, the PDF encrypted.
        let lock = match &input.folder_id {
            Some(folder) => folder_lock(&conn, folder)?,
            None => None,
        };
        let key = self.need_key(&lock)?;
        let now = now_ms();
        let id = new_id();
        let tx = conn.transaction()?;
        tx.execute(
            "INSERT INTO notebooks (id, title, folder_id, cover_color, default_template, favourite, created_at, updated_at, lock)
             VALUES (?1, ?2, ?3, ?4, 'blank', 0, ?5, ?5, ?6)",
            params![id, title, input.folder_id, input.cover_color.clone().unwrap_or_else(|| "#dc2626".into()), now, lock],
        )?;
        // Claim the stored PDF now that the notebook exists (foreign key).
        if tx.execute(
            "UPDATE assets SET notebook_id = ?1 WHERE id = ?2 AND notebook_id IS NULL",
            params![id, input.asset_id],
        )? != 1
        {
            return Err(AppError::NotFound(input.asset_id.clone()));
        }
        for (i, p) in input.pages.iter().enumerate() {
            tx.execute(
                "INSERT INTO pages (id, notebook_id, position, template, paper_color, width, height, created_at, updated_at, background_asset, background_page)
                 VALUES (?1, ?2, ?3, 'blank', '#ffffff', ?4, ?5, ?6, ?6, ?7, ?3)",
                params![new_id(), id, i as i64, p.width, p.height, now, input.asset_id],
            )?;
        }
        if let Some(key) = &key {
            encrypt_notebook(&tx, &id, key)?;
        }
        tx.commit()?;
        Ok(self.reveal_notebook(get_notebook(&conn, &id)?))
    }

    pub fn asset_bytes(&self, id: &str) -> AppResult<Vec<u8>> {
        let conn = self.conn();
        let (data, lock): (Vec<u8>, Option<String>) = conn
            .query_row(
                "SELECT a.data, n.lock FROM assets a LEFT JOIN notebooks n ON n.id = a.notebook_id WHERE a.id = ?1",
                [id],
                |r| Ok((r.get(0)?, r.get(1)?)),
            )
            .optional()?
            .ok_or_else(|| AppError::NotFound(id.to_string()))?;
        match self.need_key(&lock)? {
            Some(key) => lock::decrypt_blob(&key, &data),
            None => Ok(data),
        }
    }

    // ------------------------------------------------------------------ pages

    pub fn pages(&self, notebook_id: &str) -> AppResult<Vec<Page>> {
        let conn = self.conn();
        ensure_exists(&conn, "notebooks", notebook_id)?;
        let mut stmt = conn.prepare(
            "SELECT id, notebook_id, position, template, paper_color, width, height, favourite, thumbnail, created_at, updated_at, background_asset, background_page, section
             FROM pages WHERE notebook_id = ?1 ORDER BY position",
        )?;
        let rows = stmt.query_map([notebook_id], page_from_row)?;
        let pages: Vec<Page> = rows.collect::<Result<_, _>>()?;
        let lock = notebook_lock(&conn, notebook_id)?;
        Ok(pages.into_iter().map(|p| self.reveal_page(p, &lock)).collect())
    }

    /// Inserts a page at `index` (clamped; `None` appends). When no template
    /// is given the neighbouring page's template and size are reused.
    pub fn add_page(
        &self,
        notebook_id: &str,
        index: Option<i64>,
        template: Option<&str>,
    ) -> AppResult<Page> {
        if let Some(t) = template {
            validate_template(t)?;
        }
        let mut conn = self.conn();
        let notebook = get_notebook(&conn, notebook_id)?;
        let count = notebook.page_count;
        let index = index.unwrap_or(count).clamp(0, count);
        let tx = conn.transaction()?;
        let neighbour: Option<(String, String, f64, f64, Option<String>)> = tx
            .query_row(
                "SELECT template, paper_color, width, height, section FROM pages WHERE notebook_id = ?1
                 ORDER BY ABS(position - ?2) LIMIT 1",
                params![notebook_id, index - 1],
                |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?, r.get(3)?, r.get(4)?)),
            )
            .optional()?;
        let (n_template, paper, width, height, section) = neighbour.unwrap_or((
            notebook.default_template.clone(),
            "#fffdf7".into(),
            DEFAULT_PAGE_WIDTH,
            DEFAULT_PAGE_HEIGHT,
            None,
        ));
        tx.execute(
            "UPDATE pages SET position = position + 1 WHERE notebook_id = ?1 AND position >= ?2",
            params![notebook_id, index],
        )?;
        let now = now_ms();
        let id = new_id();
        tx.execute(
            "INSERT INTO pages (id, notebook_id, position, template, paper_color, width, height, created_at, updated_at, section)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?8, ?9)",
            params![id, notebook_id, index, template.unwrap_or(&n_template), paper, width, height, now, section],
        )?;
        tx.execute(
            "UPDATE notebooks SET updated_at = ?2 WHERE id = ?1",
            params![notebook_id, now],
        )?;
        tx.commit()?;
        let lock = notebook_lock(&conn, notebook_id)?;
        Ok(self.reveal_page(get_page(&conn, &id)?, &lock))
    }

    pub fn duplicate_page(&self, page_id: &str) -> AppResult<Page> {
        let mut conn = self.conn();
        let page = get_page(&conn, page_id)?;
        let tx = conn.transaction()?;
        tx.execute(
            "UPDATE pages SET position = position + 1 WHERE notebook_id = ?1 AND position > ?2",
            params![page.notebook_id, page.position],
        )?;
        let id = new_id();
        let now = now_ms();
        tx.execute(
            "INSERT INTO pages (id, notebook_id, position, template, paper_color, width, height, favourite, thumbnail, created_at, updated_at, background_asset, background_page, section)
             SELECT ?1, notebook_id, position + 1, template, paper_color, width, height, 0, thumbnail, ?2, ?2, background_asset, background_page, section FROM pages WHERE id = ?3",
            params![id, now, page_id],
        )?;
        copy_elements(&tx, page_id, &id)?;
        tx.commit()?;
        let lock = notebook_lock(&conn, &page.notebook_id)?;
        Ok(self.reveal_page(get_page(&conn, &id)?, &lock))
    }

    /// Permanently removes a page. A notebook always keeps at least one page.
    pub fn delete_page(&self, page_id: &str) -> AppResult<()> {
        let mut conn = self.conn();
        let page = get_page(&conn, page_id)?;
        let count: i64 = conn.query_row(
            "SELECT COUNT(*) FROM pages WHERE notebook_id = ?1",
            [&page.notebook_id],
            |r| r.get(0),
        )?;
        if count <= 1 {
            return Err(AppError::Invalid(
                "a notebook must keep at least one page".into(),
            ));
        }
        let tx = conn.transaction()?;
        tx.execute("DELETE FROM pages WHERE id = ?1", [page_id])?;
        tx.execute(
            "UPDATE pages SET position = position - 1 WHERE notebook_id = ?1 AND position > ?2",
            params![page.notebook_id, page.position],
        )?;
        tx.execute(
            "UPDATE notebooks SET updated_at = ?2 WHERE id = ?1",
            params![page.notebook_id, now_ms()],
        )?;
        tx.commit()?;
        Ok(())
    }

    /// Moves a page to `new_index` within its notebook, shifting the others.
    pub fn move_page(&self, page_id: &str, new_index: i64) -> AppResult<()> {
        let mut conn = self.conn();
        let page = get_page(&conn, page_id)?;
        let count: i64 = conn.query_row(
            "SELECT COUNT(*) FROM pages WHERE notebook_id = ?1",
            [&page.notebook_id],
            |r| r.get(0),
        )?;
        let target = new_index.clamp(0, count - 1);
        if target == page.position {
            return Ok(());
        }
        let tx = conn.transaction()?;
        if target > page.position {
            tx.execute(
                "UPDATE pages SET position = position - 1 WHERE notebook_id = ?1 AND position > ?2 AND position <= ?3",
                params![page.notebook_id, page.position, target],
            )?;
        } else {
            tx.execute(
                "UPDATE pages SET position = position + 1 WHERE notebook_id = ?1 AND position >= ?2 AND position < ?3",
                params![page.notebook_id, target, page.position],
            )?;
        }
        tx.execute(
            "UPDATE pages SET position = ?2 WHERE id = ?1",
            params![page_id, target],
        )?;
        tx.commit()?;
        Ok(())
    }

    pub fn set_page_template(
        &self,
        page_id: &str,
        template: &str,
        paper_color: Option<&str>,
    ) -> AppResult<()> {
        validate_template(template)?;
        if let Some(color) = paper_color {
            validate_color(color)?;
        }
        let conn = self.conn();
        let n = conn.execute(
            // Another paper: its picture is redrawn (the editor fills in missing ones).
            "UPDATE pages SET thumbnail = CASE WHEN template = ?2 AND paper_color = COALESCE(?3, paper_color) THEN thumbnail ELSE NULL END,
                              template = ?2, paper_color = COALESCE(?3, paper_color), updated_at = ?4 WHERE id = ?1",
            params![page_id, template, paper_color, now_ms()],
        )?;
        expect_one(n, page_id)
    }

    pub fn set_page_favourite(&self, page_id: &str, favourite: bool) -> AppResult<()> {
        let conn = self.conn();
        let n = conn.execute(
            "UPDATE pages SET favourite = ?2 WHERE id = ?1",
            params![page_id, favourite],
        )?;
        expect_one(n, page_id)
    }

    pub fn set_page_thumbnail(&self, page_id: &str, thumbnail: &str) -> AppResult<()> {
        if !thumbnail.starts_with("data:image/") {
            return Err(AppError::Invalid(
                "thumbnail must be an image data URL".into(),
            ));
        }
        let conn = self.conn();
        let (_, lock) = page_notebook(&conn, page_id)?;
        let stored = match self.need_key(&lock)? {
            Some(key) => lock::encrypt_text(&key, thumbnail),
            None => thumbnail.to_string(),
        };
        let n = conn.execute(
            "UPDATE pages SET thumbnail = ?2 WHERE id = ?1",
            params![page_id, stored],
        )?;
        expect_one(n, page_id)
    }

    // --------------------------------------------------------------- elements

    pub fn elements(&self, page_id: &str) -> AppResult<Vec<Element>> {
        let conn = self.conn();
        let (_, lock) = page_notebook(&conn, page_id)?;
        let key = self.need_key(&lock)?;
        let mut stmt = conn
            .prepare("SELECT id, kind, z, data FROM elements WHERE page_id = ?1 ORDER BY z, id")?;
        let rows = stmt.query_map([page_id], |r| {
            let raw: String = r.get(3)?;
            Ok((r.get(0)?, r.get(1)?, r.get(2)?, raw))
        })?;
        let mut out = Vec::new();
        for row in rows {
            let (id, kind, z, raw): (String, String, i64, String) = row?;
            let raw = match &key {
                Some(k) => lock::decrypt_text(k, &raw)?,
                None => raw,
            };
            let data = serde_json::from_str(&raw).unwrap_or(serde_json::Value::Null);
            out.push(Element { id, kind, z, data });
        }
        Ok(out)
    }

    /// Replaces the full element set of a page atomically.
    pub fn save_elements(&self, page_id: &str, elements: &[Element]) -> AppResult<()> {
        let mut conn = self.conn();
        let (notebook_id, lock) = page_notebook(&conn, page_id)?;
        let key = self.need_key(&lock)?;
        let tx = conn.transaction()?;
        tx.execute("DELETE FROM elements WHERE page_id = ?1", [page_id])?;
        {
            let mut insert = tx.prepare(
                "INSERT INTO elements (id, page_id, kind, z, data, text_content) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            )?;
            for el in elements {
                // Encrypted when locked (and kept out of search).
                let (data, text) = match &key {
                    Some(k) => (lock::encrypt_text(k, &el.data.to_string()), None),
                    None => (el.data.to_string(), el.data.get("text").and_then(|t| t.as_str()).map(str::to_string)),
                };
                insert.execute(params![el.id, page_id, el.kind, el.z, data, text])?;
            }
        }
        let now = now_ms();
        tx.execute(
            "UPDATE pages SET updated_at = ?2 WHERE id = ?1",
            params![page_id, now],
        )?;
        tx.execute(
            "UPDATE notebooks SET updated_at = ?2 WHERE id = ?1",
            params![notebook_id, now],
        )?;
        tx.commit()?;
        Ok(())
    }

    // ----------------------------------------------------------------- search

    /// Case-insensitive search across notebook titles, folder names and typed
    /// text (text boxes and sticky notes).
    pub fn search(&self, query: &str) -> AppResult<Vec<SearchHit>> {
        let q = query.trim();
        if q.is_empty() {
            return Ok(Vec::new());
        }
        let pattern = format!(
            "%{}%",
            q.replace('\\', "\\\\")
                .replace('%', "\\%")
                .replace('_', "\\_")
        );
        let conn = self.conn();
        let mut hits = Vec::new();
        {
            let mut stmt = conn.prepare(
                &format!(
                    "SELECT id, title FROM notebooks
                     WHERE trashed_at IS NULL AND (folder_id IS NULL OR folder_id NOT IN {TRASHED_FOLDERS}) AND title LIKE ?1 ESCAPE '\\'
                     ORDER BY updated_at DESC LIMIT 50"
                ),
            )?;
            let rows = stmt.query_map([&pattern], |r| {
                Ok(SearchHit {
                    notebook_id: r.get(0)?,
                    notebook_title: r.get(1)?,
                    page_id: None,
                    page_number: None,
                    snippet: r.get(1)?,
                })
            })?;
            for row in rows {
                hits.push(row?);
            }
        }
        {
            let mut stmt = conn.prepare(
                &format!("SELECT n.id, n.title, p.id, p.position, e.text_content
                 FROM elements e JOIN pages p ON p.id = e.page_id JOIN notebooks n ON n.id = p.notebook_id
                 WHERE n.trashed_at IS NULL AND (n.folder_id IS NULL OR n.folder_id NOT IN {TRASHED_FOLDERS}) AND e.text_content LIKE ?1 ESCAPE '\\'
                 ORDER BY n.updated_at DESC, p.position LIMIT 200"),
            )?;
            let rows = stmt.query_map([&pattern], |r| {
                let text: String = r.get(4)?;
                Ok(SearchHit {
                    notebook_id: r.get(0)?,
                    notebook_title: r.get(1)?,
                    page_id: Some(r.get(2)?),
                    page_number: Some(r.get::<_, i64>(3)? + 1),
                    snippet: snippet(&text, q),
                })
            })?;
            for row in rows {
                hits.push(row?);
            }
        }
        {
            // Handwriting the AI has read.
            let mut stmt = conn.prepare(
                &format!("SELECT n.id, n.title, p.id, p.position, p.ink_text
                 FROM pages p JOIN notebooks n ON n.id = p.notebook_id
                 WHERE n.trashed_at IS NULL AND n.lock IS NULL AND (n.folder_id IS NULL OR n.folder_id NOT IN {TRASHED_FOLDERS}) AND p.ink_text LIKE ?1 ESCAPE '\\'
                 ORDER BY n.updated_at DESC, p.position LIMIT 200"),
            )?;
            let rows = stmt.query_map([&pattern], |r| {
                let text: String = r.get(4)?;
                Ok(SearchHit {
                    notebook_id: r.get(0)?,
                    notebook_title: r.get(1)?,
                    page_id: Some(r.get(2)?),
                    page_number: Some(r.get::<_, i64>(3)? + 1),
                    snippet: format!("✍ {}", snippet(&text, q)),
                })
            })?;
            for row in rows {
                let hit = row?;
                // Once per page (typed text on it may have matched already).
                if !hits.iter().any(|h| h.page_id == hit.page_id) {
                    hits.push(hit);
                }
            }
        }
        Ok(hits)
    }

    // --------------------------------------------------------- reusable pieces

    /// The saved pieces, most recently used (or saved) first.
    pub fn snippets(&self) -> AppResult<Vec<Snippet>> {
        let conn = self.conn();
        let mut stmt = conn.prepare(
            "SELECT id, name, data, preview, width, height, created_at, used_at FROM snippets ORDER BY COALESCE(used_at, created_at) DESC, used_at IS NULL, rowid DESC",
        )?;
        let rows = stmt.query_map([], |r| {
            Ok(Snippet {
                id: r.get(0)?,
                name: r.get(1)?,
                data: r.get(2)?,
                preview: r.get(3)?,
                width: r.get(4)?,
                height: r.get(5)?,
                created_at: r.get(6)?,
                used_at: r.get(7)?,
            })
        })?;
        Ok(rows.collect::<Result<_, _>>()?)
    }

    /// Saves a selection as a piece. Not from a locked notebook: the piece
    /// isn't encrypted, so its content would no longer be protected.
    pub fn save_snippet(&self, name: &str, data: &str, preview: Option<&str>, width: f64, height: f64, from_notebook: Option<&str>) -> AppResult<Snippet> {
        let conn = self.conn();
        if let Some(nb) = from_notebook {
            if notebook_lock(&conn, nb)?.is_some() {
                return Err(AppError::Invalid(
                    "This notebook is password protected: what's in it can't go into the library (it would be kept unencrypted there).".into(),
                ));
            }
        }
        let name = validate_name(name)?;
        serde_json::from_str::<serde_json::Value>(data).map_err(|e| AppError::Invalid(format!("damaged piece: {e}")))?;
        let s = Snippet {
            id: new_id(),
            name,
            data: data.to_string(),
            preview: preview.map(str::to_string),
            width,
            height,
            created_at: now_ms(),
            used_at: None,
        };
        conn.execute(
            "INSERT INTO snippets (id, name, data, preview, width, height, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            params![s.id, s.name, s.data, s.preview, s.width, s.height, s.created_at],
        )?;
        Ok(s)
    }

    pub fn rename_snippet(&self, id: &str, name: &str) -> AppResult<()> {
        let name = validate_name(name)?;
        let n = self.conn().execute("UPDATE snippets SET name = ?2 WHERE id = ?1", params![id, name])?;
        expect_one(n, id)
    }

    pub fn delete_snippet(&self, id: &str) -> AppResult<()> {
        self.conn().execute("DELETE FROM snippets WHERE id = ?1", [id])?;
        Ok(())
    }

    /// A piece put on a page: it moves to the front of the list.
    pub fn use_snippet(&self, id: &str) -> AppResult<()> {
        self.conn().execute("UPDATE snippets SET used_at = ?2 WHERE id = ?1", params![id, now_ms()])?;
        Ok(())
    }

    // ------------------------------------------------------------- AI reading

    /// Handwritten pages whose handwriting hasn't been read since they changed
    /// (not in locked or trashed notebooks), most recently edited first.
    pub fn pages_to_read(&self, limit: i64, settle_ms: i64) -> AppResult<Vec<PageToRead>> {
        let conn = self.conn();
        let mut stmt = conn.prepare(&format!(
            "SELECT p.id, p.notebook_id, p.width, p.height FROM pages p JOIN notebooks n ON n.id = p.notebook_id
             WHERE n.trashed_at IS NULL AND n.lock IS NULL AND (n.folder_id IS NULL OR n.folder_id NOT IN {TRASHED_FOLDERS})
               AND (p.ink_text_at IS NULL OR p.ink_text_at < p.updated_at)
               AND p.updated_at < ?2
               AND EXISTS (SELECT 1 FROM elements e WHERE e.page_id = p.id AND e.kind = 'stroke')
             ORDER BY p.updated_at DESC LIMIT ?1"
        ))?;
        // Not while it's being written on (a minute's quiet first).
        let rows = stmt.query_map(params![limit, now_ms() - settle_ms], |r| Ok(PageToRead { page_id: r.get(0)?, notebook_id: r.get(1)?, width: r.get(2)?, height: r.get(3)? }))?;
        Ok(rows.collect::<Result<_, _>>()?)
    }

    /// How much handwriting has been read: pages read, and handwritten pages in all.
    pub fn reading_progress(&self) -> AppResult<(i64, i64)> {
        let conn = self.conn();
        let base = format!(
            "FROM pages p JOIN notebooks n ON n.id = p.notebook_id
             WHERE n.trashed_at IS NULL AND n.lock IS NULL AND (n.folder_id IS NULL OR n.folder_id NOT IN {TRASHED_FOLDERS})
               AND EXISTS (SELECT 1 FROM elements e WHERE e.page_id = p.id AND e.kind = 'stroke')"
        );
        let total: i64 = conn.query_row(&format!("SELECT COUNT(*) {base}"), [], |r| r.get(0))?;
        let done: i64 = conn.query_row(&format!("SELECT COUNT(*) {base} AND p.ink_text_at >= p.updated_at"), [], |r| r.get(0))?;
        Ok((done, total))
    }

    /// What the AI read in a page's handwriting (kept for search; not for locked notebooks).
    pub fn set_page_ink_text(&self, page_id: &str, text: &str) -> AppResult<()> {
        let conn = self.conn();
        let (_, lock) = page_notebook(&conn, page_id)?;
        if lock.is_some() {
            return Ok(());
        }
        // Read as of now: the page's own time doesn't change (it wasn't edited).
        conn.execute("UPDATE pages SET ink_text = ?2, ink_text_at = MAX(?3, updated_at) WHERE id = ?1", params![page_id, text.trim(), now_ms()])?;
        Ok(())
    }

    /// One page's text: typed text, then the handwriting the AI read.
    fn page_text(&self, conn: &Connection, page_id: &str, key: &Option<ContentKey>) -> AppResult<String> {
        let mut parts = Vec::new();
        let mut stmt = conn.prepare("SELECT data FROM elements WHERE page_id = ?1 ORDER BY z, id")?;
        let rows: Vec<String> = stmt.query_map([page_id], |r| r.get(0))?.collect::<Result<_, _>>()?;
        for raw in rows {
            let raw = match key {
                Some(k) => lock::decrypt_text(k, &raw)?,
                None => raw,
            };
            if let Some(t) = serde_json::from_str::<serde_json::Value>(&raw).ok().and_then(|v| v.get("text").and_then(|t| t.as_str()).map(str::to_string)) {
                if !t.trim().is_empty() {
                    parts.push(t.trim().to_string());
                }
            }
        }
        let ink: Option<String> = conn.query_row("SELECT ink_text FROM pages WHERE id = ?1", [page_id], |r| r.get(0))?;
        if let Some(i) = ink.filter(|i| !i.trim().is_empty()) {
            parts.push(format!("[handwriting] {}", i.trim()));
        }
        Ok(parts.join("\n"))
    }

    /// Notes to answer a question with: one notebook's pages (unlocked, if
    /// locked), or the library's pages most related to `question` (locked
    /// notebooks left out), up to about `budget` characters.
    pub fn notes_for_ai(&self, notebook_id: Option<&str>, question: &str, budget: usize) -> AppResult<String> {
        let conn = self.conn();
        // (notebook title, page number, text) for every page considered.
        let mut pages: Vec<(String, i64, String)> = Vec::new();
        let notebooks: Vec<(String, String, Option<String>)> = match notebook_id {
            Some(id) => conn.query_row("SELECT id, title, lock FROM notebooks WHERE id = ?1", [id], |r| Ok(vec![(r.get(0)?, r.get(1)?, r.get(2)?)]))?,
            None => {
                let mut stmt = conn.prepare(&format!(
                    "SELECT id, title, lock FROM notebooks WHERE trashed_at IS NULL AND lock IS NULL AND (folder_id IS NULL OR folder_id NOT IN {TRASHED_FOLDERS}) ORDER BY updated_at DESC"
                ))?;
                let rows = stmt.query_map([], |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?)))?;
                rows.collect::<Result<_, _>>()?
            }
        };
        for (id, title, lock) in notebooks {
            let key = self.need_key(&lock)?;
            let mut stmt = conn.prepare("SELECT id, position FROM pages WHERE notebook_id = ?1 ORDER BY position")?;
            let ids: Vec<(String, i64)> = stmt.query_map([&id], |r| Ok((r.get(0)?, r.get(1)?)))?.collect::<Result<_, _>>()?;
            for (pid, pos) in ids {
                let text = self.page_text(&conn, &pid, &key)?;
                if !text.is_empty() {
                    pages.push((title.clone(), pos + 1, text));
                }
            }
        }
        let total: usize = pages.iter().map(|p| p.2.len()).sum();
        if notebook_id.is_none() && total > budget {
            // Most related first: how many of the question's words each page has.
            let words: Vec<String> = question.to_lowercase().split(|c: char| !c.is_alphanumeric()).filter(|w| w.chars().count() > 2).map(str::to_string).collect();
            let score = |p: &(String, i64, String)| {
                let t = format!("{} {}", p.0, p.2).to_lowercase();
                words.iter().filter(|w| t.contains(w.as_str())).count()
            };
            pages.sort_by_key(|p| std::cmp::Reverse(score(p)));
        }
        let mut out = String::new();
        let mut last = String::new();
        for (title, n, text) in pages {
            let block = format!("{}--- page {n} ---\n{text}\n", if title != last { format!("\n# Notebook: {title}\n") } else { String::new() });
            if out.len() + block.len() > budget {
                if out.is_empty() {
                    out.push_str(&block.chars().take(budget).collect::<String>());
                }
                break;
            }
            last = title;
            out.push_str(&block);
        }
        Ok(out)
    }
}

// ------------------------------------------------------------------ helpers

fn snippet(text: &str, query: &str) -> String {
    let lower = text.to_lowercase();
    let q = query.to_lowercase();
    let chars: Vec<char> = text.chars().collect();
    let start_char = lower
        .find(&q)
        .map(|byte| lower[..byte].chars().count())
        .unwrap_or(0);
    let from = start_char.saturating_sub(30);
    let to = (start_char + q.chars().count() + 50).min(chars.len());
    let mut s: String = chars[from..to].iter().collect();
    if from > 0 {
        s = format!("…{s}");
    }
    if to < chars.len() {
        s.push('…');
    }
    s.replace('\n', " ")
}

fn copy_elements(conn: &Connection, from_page: &str, to_page: &str) -> AppResult<()> {
    let rows: Vec<(String, i64, String, Option<String>)> = {
        let mut stmt =
            conn.prepare("SELECT kind, z, data, text_content FROM elements WHERE page_id = ?1")?;
        let mapped = stmt.query_map([from_page], |r| {
            Ok((r.get(0)?, r.get(1)?, r.get(2)?, r.get(3)?))
        })?;
        mapped.collect::<Result<_, _>>()?
    };
    for (kind, z, data, text) in rows {
        // Give the copy fresh element ids, both as row id and inside the payload.
        let id = new_id();
        let data = match serde_json::from_str::<serde_json::Value>(&data) {
            Ok(serde_json::Value::Object(mut map)) => {
                if map.contains_key("id") {
                    map.insert("id".into(), serde_json::Value::String(id.clone()));
                }
                serde_json::Value::Object(map).to_string()
            }
            _ => data,
        };
        conn.execute(
            "INSERT INTO elements (id, page_id, kind, z, data, text_content) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            params![id, to_page, kind, z, data, text],
        )?;
    }
    Ok(())
}

fn ensure_exists(conn: &Connection, table: &str, id: &str) -> AppResult<()> {
    // `table` is always a compile-time constant from this module.
    let found: Option<i64> = conn
        .query_row(&format!("SELECT 1 FROM {table} WHERE id = ?1"), [id], |r| {
            r.get(0)
        })
        .optional()?;
    found
        .map(|_| ())
        .ok_or_else(|| AppError::NotFound(id.to_string()))
}

fn expect_one(changed: usize, id: &str) -> AppResult<()> {
    if changed == 0 {
        Err(AppError::NotFound(id.to_string()))
    } else {
        Ok(())
    }
}

fn touch_row(conn: &Connection, sql: &str, id: &str, value: &str) -> AppResult<()> {
    expect_one(conn.execute(sql, params![id, value, now_ms()])?, id)
}

fn notebook_lock(conn: &Connection, id: &str) -> AppResult<Option<String>> {
    conn.query_row("SELECT lock FROM notebooks WHERE id = ?1", [id], |r| r.get(0))
        .optional()?
        .ok_or_else(|| AppError::NotFound(id.to_string()))
}

fn folder_lock(conn: &Connection, id: &str) -> AppResult<Option<String>> {
    conn.query_row("SELECT lock FROM folders WHERE id = ?1", [id], |r| r.get(0))
        .optional()?
        .ok_or_else(|| AppError::NotFound(id.to_string()))
}

/// A page's notebook and that notebook's lock.
fn page_notebook(conn: &Connection, page_id: &str) -> AppResult<(String, Option<String>)> {
    conn.query_row(
        "SELECT p.notebook_id, n.lock FROM pages p JOIN notebooks n ON n.id = p.notebook_id WHERE p.id = ?1",
        [page_id],
        |r| Ok((r.get(0)?, r.get(1)?)),
    )
    .optional()?
    .ok_or_else(|| AppError::NotFound(page_id.to_string()))
}

/// A folder and every folder inside it.
fn folder_tree(conn: &Connection, id: &str) -> AppResult<Vec<String>> {
    let mut stmt = conn.prepare(
        "WITH RECURSIVE tree(id) AS (SELECT ?1 UNION ALL SELECT f.id FROM folders f JOIN tree t ON f.parent_id = t.id) SELECT id FROM tree",
    )?;
    let rows = stmt.query_map([id], |r| r.get(0))?;
    Ok(rows.collect::<Result<_, _>>()?)
}

fn notebooks_in(conn: &Connection, folders: &[String]) -> AppResult<Vec<String>> {
    let mut out = Vec::new();
    let mut stmt = conn.prepare("SELECT id FROM notebooks WHERE folder_id = ?1")?;
    for f in folders {
        let rows = stmt.query_map([f], |r| r.get::<_, String>(0))?;
        for r in rows {
            out.push(r?);
        }
    }
    Ok(out)
}

/// Locks a folder, the folders inside it and the notebooks in them (those
/// without a password of their own) with one lock.
fn apply_lock_to_tree(conn: &Connection, folder: &str, lock: &str, key: &ContentKey) -> AppResult<()> {
    let now = now_ms();
    let folders = folder_tree(conn, folder)?;
    for f in &folders {
        conn.execute("UPDATE folders SET lock = ?2, updated_at = ?3 WHERE id = ?1 AND lock IS NULL", params![f, lock, now])?;
    }
    for n in notebooks_in(conn, &folders)? {
        if notebook_lock(conn, &n)?.is_none() {
            encrypt_notebook(conn, &n, key)?;
            conn.execute("UPDATE notebooks SET lock = ?2, updated_at = ?3 WHERE id = ?1", params![n, lock, now])?;
        }
    }
    Ok(())
}

/// Encrypts a notebook's content (what isn't already): ink and text, page
/// pictures, settings and imported PDFs. Typed text leaves the search index.
fn encrypt_notebook(conn: &Connection, id: &str, key: &ContentKey) -> AppResult<()> {
    let rows: Vec<(String, String)> = {
        let mut stmt = conn.prepare("SELECT e.id, e.data FROM elements e JOIN pages p ON p.id = e.page_id WHERE p.notebook_id = ?1")?;
        let rows = stmt.query_map([id], |r| Ok((r.get(0)?, r.get(1)?)))?;
        rows.collect::<Result<_, _>>()?
    };
    for (eid, data) in rows {
        if !lock::is_encrypted_text(&data) {
            conn.execute("UPDATE elements SET data = ?2, text_content = NULL WHERE id = ?1", params![eid, lock::encrypt_text(key, &data)])?;
        }
    }
    // Handwriting read by the AI is plain text: gone (it isn't read while locked).
    conn.execute("UPDATE pages SET ink_text = NULL, ink_text_at = NULL WHERE notebook_id = ?1", [id])?;
    let thumbs: Vec<(String, String)> = {
        let mut stmt = conn.prepare("SELECT id, thumbnail FROM pages WHERE notebook_id = ?1 AND thumbnail IS NOT NULL")?;
        let rows = stmt.query_map([id], |r| Ok((r.get(0)?, r.get(1)?)))?;
        rows.collect::<Result<_, _>>()?
    };
    for (pid, t) in thumbs {
        if !lock::is_encrypted_text(&t) {
            conn.execute("UPDATE pages SET thumbnail = ?2 WHERE id = ?1", params![pid, lock::encrypt_text(key, &t)])?;
        }
    }
    let meta: Option<String> = conn.query_row("SELECT meta FROM notebooks WHERE id = ?1", [id], |r| r.get(0))?;
    if let Some(m) = meta.filter(|m| !lock::is_encrypted_text(m)) {
        conn.execute("UPDATE notebooks SET meta = ?2 WHERE id = ?1", params![id, lock::encrypt_text(key, &m)])?;
    }
    let assets: Vec<String> = {
        let mut stmt = conn.prepare("SELECT id FROM assets WHERE notebook_id = ?1")?;
        let rows = stmt.query_map([id], |r| r.get(0))?;
        rows.collect::<Result<_, _>>()?
    };
    for aid in assets {
        let data: Vec<u8> = conn.query_row("SELECT data FROM assets WHERE id = ?1", [&aid], |r| r.get(0))?;
        if !lock::is_encrypted_blob(&data) {
            conn.execute("UPDATE assets SET data = ?2 WHERE id = ?1", params![aid, lock::encrypt_blob(key, &data)])?;
        }
    }
    Ok(())
}

/// Decrypts a notebook's content again (and puts its text back in search).
fn decrypt_notebook(conn: &Connection, id: &str, key: &ContentKey) -> AppResult<()> {
    let rows: Vec<(String, String)> = {
        let mut stmt = conn.prepare("SELECT e.id, e.data FROM elements e JOIN pages p ON p.id = e.page_id WHERE p.notebook_id = ?1")?;
        let rows = stmt.query_map([id], |r| Ok((r.get(0)?, r.get(1)?)))?;
        rows.collect::<Result<_, _>>()?
    };
    for (eid, data) in rows {
        let plain = lock::decrypt_text(key, &data)?;
        let text = serde_json::from_str::<serde_json::Value>(&plain).ok().and_then(|v| v.get("text").and_then(|t| t.as_str()).map(str::to_string));
        conn.execute("UPDATE elements SET data = ?2, text_content = ?3 WHERE id = ?1", params![eid, plain, text])?;
    }
    let thumbs: Vec<(String, String)> = {
        let mut stmt = conn.prepare("SELECT id, thumbnail FROM pages WHERE notebook_id = ?1 AND thumbnail IS NOT NULL")?;
        let rows = stmt.query_map([id], |r| Ok((r.get(0)?, r.get(1)?)))?;
        rows.collect::<Result<_, _>>()?
    };
    for (pid, t) in thumbs {
        conn.execute("UPDATE pages SET thumbnail = ?2 WHERE id = ?1", params![pid, lock::decrypt_text(key, &t)?])?;
    }
    let meta: Option<String> = conn.query_row("SELECT meta FROM notebooks WHERE id = ?1", [id], |r| r.get(0))?;
    if let Some(m) = meta {
        conn.execute("UPDATE notebooks SET meta = ?2 WHERE id = ?1", params![id, lock::decrypt_text(key, &m)?])?;
    }
    let assets: Vec<String> = {
        let mut stmt = conn.prepare("SELECT id FROM assets WHERE notebook_id = ?1")?;
        let rows = stmt.query_map([id], |r| r.get(0))?;
        rows.collect::<Result<_, _>>()?
    };
    for aid in assets {
        let data: Vec<u8> = conn.query_row("SELECT data FROM assets WHERE id = ?1", [&aid], |r| r.get(0))?;
        conn.execute("UPDATE assets SET data = ?2 WHERE id = ?1", params![aid, lock::decrypt_blob(key, &data)?])?;
    }
    Ok(())
}

fn query_folders(conn: &Connection, clause: &str) -> AppResult<Vec<Folder>> {
    let mut stmt = conn.prepare(&format!(
        "SELECT id, name, parent_id, color, favourite, created_at, updated_at, trashed_at, lock FROM folders {clause}"
    ))?;
    let rows = stmt.query_map([], |r| {
        Ok(Folder {
            id: r.get(0)?,
            name: r.get(1)?,
            parent_id: r.get(2)?,
            color: r.get(3)?,
            favourite: r.get(4)?,
            created_at: r.get(5)?,
            updated_at: r.get(6)?,
            trashed_at: r.get(7)?,
            locked: false,
            unlocked: false,
            lock_id: None,
            lock: r.get(8)?,
        })
    })?;
    Ok(rows.collect::<Result<_, _>>()?)
}

const NOTEBOOK_SELECT: &str =
    "SELECT n.id, n.title, n.folder_id, n.cover_color, n.default_template, n.favourite,
        (SELECT COUNT(*) FROM pages p WHERE p.notebook_id = n.id),
        (SELECT p.thumbnail FROM pages p WHERE p.notebook_id = n.id ORDER BY p.position LIMIT 1),
        n.created_at, n.updated_at, n.trashed_at,
        COALESCE((SELECT p.template LIKE 'cover%' FROM pages p WHERE p.notebook_id = n.id ORDER BY p.position LIMIT 1), 0),
        n.meta, n.lock
     FROM notebooks n LEFT JOIN folders f ON f.id = n.folder_id";

fn notebook_from_row(r: &rusqlite::Row<'_>) -> rusqlite::Result<Notebook> {
    Ok(Notebook {
        id: r.get(0)?,
        title: r.get(1)?,
        folder_id: r.get(2)?,
        cover_color: r.get(3)?,
        default_template: r.get(4)?,
        favourite: r.get(5)?,
        page_count: r.get(6)?,
        thumbnail: r.get(7)?,
        created_at: r.get(8)?,
        updated_at: r.get(9)?,
        trashed_at: r.get(10)?,
        has_cover: r.get(11)?,
        meta: r.get(12)?,
        locked: false,
        unlocked: false,
        lock_id: None,
        lock: r.get(13)?,
    })
}

fn query_notebooks(conn: &Connection, clause: &str) -> AppResult<Vec<Notebook>> {
    let mut stmt = conn.prepare(&format!("{NOTEBOOK_SELECT} {clause}"))?;
    let rows = stmt.query_map([], notebook_from_row)?;
    Ok(rows.collect::<Result<_, _>>()?)
}

fn get_notebook(conn: &Connection, id: &str) -> AppResult<Notebook> {
    conn.query_row(
        &format!("{NOTEBOOK_SELECT} WHERE n.id = ?1"),
        [id],
        notebook_from_row,
    )
    .optional()?
    .ok_or_else(|| AppError::NotFound(id.to_string()))
}

fn page_from_row(r: &rusqlite::Row<'_>) -> rusqlite::Result<Page> {
    Ok(Page {
        id: r.get(0)?,
        notebook_id: r.get(1)?,
        position: r.get(2)?,
        template: r.get(3)?,
        paper_color: r.get(4)?,
        width: r.get(5)?,
        height: r.get(6)?,
        favourite: r.get(7)?,
        thumbnail: r.get(8)?,
        created_at: r.get(9)?,
        updated_at: r.get(10)?,
        background_asset: r.get(11)?,
        background_page: r.get(12)?,
        section: r.get(13)?,
    })
}

fn get_page(conn: &Connection, id: &str) -> AppResult<Page> {
    conn.query_row(
        "SELECT id, notebook_id, position, template, paper_color, width, height, favourite, thumbnail, created_at, updated_at, background_asset, background_page, section
         FROM pages WHERE id = ?1",
        [id],
        page_from_row,
    )
    .optional()?
    .ok_or_else(|| AppError::NotFound(id.to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn db() -> Db {
        Db::open_in_memory().unwrap()
    }

    fn notebook(db: &Db, title: &str) -> Notebook {
        db.create_notebook(&NewNotebook {
            title: title.into(),
            ..Default::default()
        })
        .unwrap()
    }

    fn write_text(db: &Db, nb: &str, text: &str) -> String {
        let page = db.pages(nb).unwrap()[0].id.clone();
        db.save_elements(&page, &[Element { id: new_id(), kind: "text".into(), z: 0, data: json!({ "text": text }) }]).unwrap();
        page
    }

    /// Every stored value of a notebook's rows, as the database file holds them.
    fn raw_rows(db: &Db, nb: &str) -> String {
        db.with_conn(|c| {
            let mut out = String::new();
            let mut stmt = c.prepare("SELECT e.data || COALESCE(e.text_content, '') FROM elements e JOIN pages p ON p.id = e.page_id WHERE p.notebook_id = ?1")?;
            for r in stmt.query_map([nb], |r| r.get::<_, String>(0))? {
                out += &r?;
            }
            let mut stmt = c.prepare("SELECT COALESCE(thumbnail, '') FROM pages WHERE notebook_id = ?1")?;
            for r in stmt.query_map([nb], |r| r.get::<_, String>(0))? {
                out += &r?;
            }
            out += &c.query_row("SELECT COALESCE(meta, '') FROM notebooks WHERE id = ?1", [nb], |r| r.get::<_, String>(0))?;
            Ok(out)
        })
        .unwrap()
    }

    #[test]
    fn pieces_are_kept_listed_and_never_taken_from_a_locked_notebook() {
        let db = db();
        let nb = notebook(&db, "Maths");
        let a = db.save_snippet("Stamp", "[{\"kind\":\"text\",\"text\":\"Très bien\"}]", Some("data:image/png;base64,AA"), 120.0, 40.0, Some(&nb.id)).unwrap();
        let b = db.save_snippet("Triangle", "[]", None, 80.0, 70.0, None).unwrap();
        assert_eq!(db.snippets().unwrap().iter().map(|s| s.name.as_str()).collect::<Vec<_>>(), ["Triangle", "Stamp"]);
        db.use_snippet(&a.id).unwrap();
        assert_eq!(db.snippets().unwrap()[0].id, a.id);
        db.rename_snippet(&b.id, "Right triangle").unwrap();
        assert!(db.snippets().unwrap().iter().any(|s| s.name == "Right triangle"));
        db.delete_snippet(&a.id).unwrap();
        assert_eq!(db.snippets().unwrap().len(), 1);
        assert!(db.save_snippet("Bad", "not json", None, 1.0, 1.0, None).is_err());
        db.lock_notebook(&nb.id, "secret").unwrap();
        assert!(db.save_snippet("Secret", "[]", None, 1.0, 1.0, Some(&nb.id)).unwrap_err().to_string().contains("password"));
    }

    #[test]
    fn handwriting_read_by_the_ai_is_searchable_and_gathered_for_questions() {
        let db = db();
        let nb = notebook(&db, "Physics");
        let page = db.pages(&nb.id).unwrap()[0].id.clone();
        db.save_elements(&page, &[
            Element { id: new_id(), kind: "stroke".into(), z: 0, data: json!({ "points": [[0, 0], [5, 5]] }) },
            Element { id: new_id(), kind: "text".into(), z: 1, data: json!({ "text": "Chapter 2" }) },
        ]).unwrap();
        // Waits until the page has been left alone a while.
        assert!(db.pages_to_read(10, 60_000).unwrap().is_empty());
        let queue = db.pages_to_read(10, -1_000).unwrap();
        assert_eq!(queue.len(), 1);
        assert_eq!(db.reading_progress().unwrap(), (0, 1));
        db.set_page_ink_text(&page, "Newton's second law: F = ma").unwrap();
        assert!(db.pages_to_read(10, -1_000).unwrap().is_empty());
        assert_eq!(db.reading_progress().unwrap(), (1, 1));
        let hits = db.search("second law").unwrap();
        assert_eq!(hits.len(), 1);
        assert!(hits[0].snippet.contains("second law"));
        let notes = db.notes_for_ai(Some(&nb.id), "", 10_000).unwrap();
        assert!(notes.contains("Chapter 2") && notes.contains("F = ma") && notes.contains("# Notebook: Physics"), "{notes}");
        // Across the library: the related pages first, within the budget.
        let other = notebook(&db, "Recipes");
        write_text(&db, &other.id, &"flour sugar eggs ".repeat(40));
        let notes = db.notes_for_ai(None, "What is Newton's law?", 200).unwrap();
        assert!(notes.contains("Newton") && !notes.contains("flour"), "{notes}");
        // Edited again: read again.
        db.save_elements(&page, &[Element { id: new_id(), kind: "stroke".into(), z: 0, data: json!({ "points": [[1, 1]] }) }]).unwrap();
        assert_eq!(db.pages_to_read(10, -1_000).unwrap().len(), 1);
        // Locked: what was read is gone, and it isn't read or gathered.
        db.set_page_ink_text(&page, "Newton again").unwrap();
        db.lock_notebook(&nb.id, "secret").unwrap();
        assert!(db.search("Newton").unwrap().is_empty());
        assert!(db.pages_to_read(10, -1_000).unwrap().is_empty());
        assert!(!db.notes_for_ai(None, "Newton", 10_000).unwrap().contains("Newton"));
        // Asked about directly while unlocked, its typed text is there.
        assert!(db.notes_for_ai(Some(&nb.id), "", 10_000).unwrap().contains("[handwriting]") == false);
        db.lock_all();
        assert!(db.notes_for_ai(Some(&nb.id), "", 10_000).is_err());
    }

    #[test]
    fn a_locked_notebook_is_encrypted_and_opens_only_with_its_password() {
        let db = db();
        let nb = notebook(&db, "Diary");
        let page = write_text(&db, &nb.id, "my secret plan");
        db.set_page_thumbnail(&page, "data:image/png;base64,SECRETPIC").unwrap();
        db.set_notebook_meta(&nb.id, Some("{\"note\":\"hidden settings\"}")).unwrap();
        assert_eq!(db.search("secret").unwrap().len(), 1);

        let locked = db.lock_notebook(&nb.id, "hunter22").unwrap();
        assert!(locked.locked && locked.unlocked);
        let raw = raw_rows(&db, &nb.id);
        assert!(!raw.contains("secret plan") && !raw.contains("SECRETPIC") && !raw.contains("hidden settings"), "{raw}");
        // Out of search (its title is still found).
        assert!(db.search("secret plan").unwrap().is_empty());
        // Unlocked this session: it reads and writes as before.
        assert_eq!(db.elements(&page).unwrap()[0].data["text"], "my secret plan");
        write_text(&db, &nb.id, "another secret");
        assert!(!raw_rows(&db, &nb.id).contains("another secret"));
        assert_eq!(db.notebook(&nb.id).unwrap().meta.as_deref(), Some("{\"note\":\"hidden settings\"}"));

        db.lock_all();
        assert!(matches!(db.elements(&page), Err(AppError::Locked)));
        assert!(matches!(db.save_elements(&page, &[]), Err(AppError::Locked)));
        let shown = db.library().unwrap().notebooks.into_iter().find(|n| n.id == nb.id).unwrap();
        assert!(shown.locked && !shown.unlocked && shown.thumbnail.is_none() && shown.meta.is_none());
        assert!(db.pages(&nb.id).unwrap()[0].thumbnail.is_none());
        assert!(db.unlock("notebook", &nb.id, "wrong pass").unwrap_err().to_string().contains("Wrong password"));
        db.unlock("notebook", &nb.id, "hunter22").unwrap();
        assert_eq!(db.elements(&page).unwrap()[0].data["text"], "another secret");
        assert_eq!(db.pages(&nb.id).unwrap()[0].thumbnail.as_deref(), Some("data:image/png;base64,SECRETPIC"));

        // A new password, then none at all: back in search, readable on disk.
        db.change_password("notebook", &nb.id, "newpass1").unwrap();
        db.lock_all();
        assert!(db.unlock("notebook", &nb.id, "hunter22").is_err());
        db.unlock("notebook", &nb.id, "newpass1").unwrap();
        db.remove_lock("notebook", &nb.id).unwrap();
        db.lock_all();
        assert_eq!(db.elements(&page).unwrap()[0].data["text"], "another secret");
        assert_eq!(db.search("another").unwrap().len(), 1);
        assert!(!db.notebook(&nb.id).unwrap().locked);
    }

    #[test]
    fn a_locked_folder_covers_everything_inside_it() {
        let db = db();
        let vault = db.create_folder("Private", None, None).unwrap();
        let inner = db.create_folder("Inner", Some(&vault.id), None).unwrap();
        let a = db.create_notebook(&NewNotebook { title: "A".into(), folder_id: Some(inner.id.clone()), ..Default::default() }).unwrap();
        write_text(&db, &a.id, "alpha secret");
        let own = db.create_notebook(&NewNotebook { title: "Own".into(), folder_id: Some(vault.id.clone()), ..Default::default() }).unwrap();
        db.lock_notebook(&own.id, "ownpass1").unwrap();
        db.lock_folder(&vault.id, "vaultpass").unwrap();
        assert!(!raw_rows(&db, &a.id).contains("alpha"));
        let lib = db.library().unwrap();
        assert!(lib.folders.iter().all(|f| f.locked));

        // Added later inside: locked with it.
        let b = db.create_notebook(&NewNotebook { title: "B".into(), folder_id: Some(inner.id.clone()), ..Default::default() }).unwrap();
        assert!(b.locked);
        write_text(&db, &b.id, "bravo secret");
        assert!(!raw_rows(&db, &b.id).contains("bravo"));
        let plain = notebook(&db, "Plain");
        write_text(&db, &plain.id, "charlie secret");
        db.move_notebook(&plain.id, Some(&vault.id)).unwrap();
        assert!(!raw_rows(&db, &plain.id).contains("charlie"));
        let sub = db.create_folder("Later", Some(&vault.id), None).unwrap();
        assert!(sub.locked);

        db.lock_all();
        // Locked: nothing can be added inside, and nothing read.
        assert!(matches!(db.create_notebook(&NewNotebook { title: "C".into(), folder_id: Some(vault.id.clone()), ..Default::default() }), Err(AppError::Locked)));
        let page_a = db.pages(&a.id).unwrap()[0].id.clone();
        assert!(matches!(db.elements(&page_a), Err(AppError::Locked)));
        // The folder's password opens all of it, but not a notebook's own lock.
        db.unlock("folder", &vault.id, "vaultpass").unwrap();
        assert_eq!(db.elements(&page_a).unwrap()[0].data["text"], "alpha secret");
        let page_own = db.pages(&own.id).unwrap()[0].id.clone();
        assert!(matches!(db.elements(&page_own), Err(AppError::Locked)));
        // Removing the folder's password decrypts what shares it.
        db.remove_lock("folder", &vault.id).unwrap();
        db.lock_all();
        assert_eq!(db.elements(&page_a).unwrap()[0].data["text"], "alpha secret");
        assert!(db.notebook(&own.id).unwrap().locked);
    }

    #[test]
    fn an_imported_pdf_in_a_locked_notebook_is_encrypted() {
        let db = db();
        let asset = db.store_pdf_asset(b"%PDF-1.7 secret worksheet").unwrap();
        let nb = db.create_pdf_notebook(&NewPdfNotebook { title: "Worksheet".into(), folder_id: None, cover_color: None, asset_id: asset.clone(), pages: vec![PdfPageSize { width: 600.0, height: 800.0 }] }).unwrap();
        db.lock_notebook(&nb.id, "pdfpass1").unwrap();
        let raw: Vec<u8> = db.with_conn(|c| Ok(c.query_row("SELECT data FROM assets WHERE id = ?1", [&asset], |r| r.get(0))?)).unwrap();
        assert!(!raw.windows(6).any(|w| w == b"secret"));
        assert_eq!(db.asset_bytes(&asset).unwrap(), b"%PDF-1.7 secret worksheet");
        db.lock_all();
        assert!(matches!(db.asset_bytes(&asset), Err(AppError::Locked)));
        // A copy is locked with the same password.
        db.unlock("notebook", &nb.id, "pdfpass1").unwrap();
        let copy = db.duplicate_notebook(&nb.id).unwrap();
        assert!(copy.locked && copy.unlocked);
    }

    #[test]
    fn new_notebook_has_one_page_with_default_template() {
        let db = db();
        let nb = notebook(&db, "Physics");
        assert_eq!(nb.page_count, 1);
        let pages = db.pages(&nb.id).unwrap();
        assert_eq!(pages[0].template, "ruled");
        assert_eq!(pages[0].position, 0);
    }

    #[test]
    fn rejects_empty_names_and_unknown_templates() {
        let db = db();
        assert!(db
            .create_notebook(&NewNotebook {
                title: "  ".into(),
                ..Default::default()
            })
            .is_err());
        let bad = NewNotebook {
            title: "x".into(),
            template: Some("hexagons".into()),
            ..Default::default()
        };
        assert!(db.create_notebook(&bad).is_err());
    }

    #[test]
    fn page_insert_move_and_delete_keep_positions_contiguous() {
        let db = db();
        let nb = notebook(&db, "Book");
        let p1 = db.add_page(&nb.id, None, Some("grid")).unwrap();
        let p2 = db.add_page(&nb.id, Some(0), None).unwrap();
        let ids: Vec<_> = db
            .pages(&nb.id)
            .unwrap()
            .into_iter()
            .map(|p| p.id)
            .collect();
        assert_eq!(ids[0], p2.id);
        assert_eq!(ids[2], p1.id);

        db.move_page(&p2.id, 2).unwrap();
        let pages = db.pages(&nb.id).unwrap();
        assert_eq!(
            pages.iter().map(|p| p.position).collect::<Vec<_>>(),
            vec![0, 1, 2]
        );
        assert_eq!(pages[2].id, p2.id);

        db.delete_page(&pages[0].id).unwrap();
        let pages = db.pages(&nb.id).unwrap();
        assert_eq!(
            pages.iter().map(|p| p.position).collect::<Vec<_>>(),
            vec![0, 1]
        );
    }

    #[test]
    fn notebooks_take_custom_paper_size_and_colour() {
        let db = db();
        let nb = db
            .create_notebook(&NewNotebook {
                title: "Landscape".into(),
                width: Some(1123.0),
                height: Some(794.0),
                paper_color: Some("#22262d".into()),
                ..Default::default()
            })
            .unwrap();
        let page = &db.pages(&nb.id).unwrap()[0];
        assert_eq!((page.width, page.height), (1123.0, 794.0));
        assert_eq!(page.paper_color, "#22262d");
        let added = db.add_page(&nb.id, None, None).unwrap();
        assert_eq!(
            (added.width, added.paper_color.as_str()),
            (1123.0, "#22262d")
        );
        db.set_page_template(&page.id, "grid", Some("#ffffff"))
            .unwrap();
        assert!(db
            .set_page_template(&page.id, "grid", Some("red; drop"))
            .is_err());
        let bad = NewNotebook {
            title: "x".into(),
            width: Some(5.0),
            ..Default::default()
        };
        assert!(db.create_notebook(&bad).is_err());
    }

    #[test]
    fn pdf_notebooks_have_one_page_per_pdf_page_and_copy_with_their_pdf() {
        let db = db();
        let asset = db.store_pdf_asset(b"%PDF-1.7 fake").unwrap();
        assert!(db.store_pdf_asset(b"not a pdf").is_err());
        let nb = db
            .create_pdf_notebook(&NewPdfNotebook {
                title: "Lecture 3".into(),
                folder_id: None,
                cover_color: None,
                asset_id: asset.clone(),
                pages: vec![
                    PdfPageSize {
                        width: 794.0,
                        height: 1123.0,
                    },
                    PdfPageSize {
                        width: 1123.0,
                        height: 794.0,
                    },
                ],
            })
            .unwrap();
        let pages = db.pages(&nb.id).unwrap();
        assert_eq!(pages.len(), 2);
        assert_eq!(pages[1].background_asset.as_deref(), Some(asset.as_str()));
        assert_eq!(pages[1].background_page, Some(1));
        assert_eq!((pages[1].width, pages[1].height), (1123.0, 794.0));
        assert_eq!(db.asset_bytes(&asset).unwrap(), b"%PDF-1.7 fake");
        // The same stored PDF can't be claimed twice.
        let again = NewPdfNotebook {
            title: "x".into(),
            folder_id: None,
            cover_color: None,
            asset_id: asset.clone(),
            pages: vec![PdfPageSize {
                width: 100.0,
                height: 100.0,
            }],
        };
        assert!(db.create_pdf_notebook(&again).is_err());
        // Duplicates get their own PDF copy; deleting the original keeps it.
        let copy = db.duplicate_notebook(&nb.id).unwrap();
        let copy_asset = db.pages(&copy.id).unwrap()[0]
            .background_asset
            .clone()
            .unwrap();
        assert_ne!(copy_asset, asset);
        db.delete_notebook(&nb.id).unwrap();
        assert!(db.asset_bytes(&asset).is_err());
        assert_eq!(db.asset_bytes(&copy_asset).unwrap(), b"%PDF-1.7 fake");
        // Added pages are plain paper.
        let added = db.add_page(&copy.id, None, None).unwrap();
        assert_eq!(added.background_asset, None);
    }

    #[test]
    fn folder_colour_can_change() {
        let db = db();
        let f = db.create_folder("Maths", None, Some("#2563eb")).unwrap();
        db.set_folder_color(&f.id, "#16a34a").unwrap();
        assert_eq!(db.library().unwrap().folders[0].color, "#16a34a");
        assert!(db.set_folder_color(&f.id, "green").is_err());
        assert!(db.set_folder_color("missing", "#16a34a").is_err());
    }

    #[test]
    fn notebook_meta_and_page_sections() {
        let db = db();
        let nb = notebook(&db, "Daftar");
        assert_eq!(nb.meta, None);
        db.set_notebook_meta(&nb.id, Some(r#"{"kind":"daftar","classes":["1AC-2"]}"#)).unwrap();
        assert!(db.set_notebook_meta(&nb.id, Some("not json")).is_err());
        assert_eq!(db.notebook(&nb.id).unwrap().meta.as_deref(), Some(r#"{"kind":"daftar","classes":["1AC-2"]}"#));
        let first = db.pages(&nb.id).unwrap()[0].clone();
        assert_eq!(first.section, None);
        db.set_page_section(&first.id, Some("1AC-2")).unwrap();
        // New pages and copies keep the section of the page next to them.
        let added = db.add_page(&nb.id, Some(1), None).unwrap();
        assert_eq!(added.section.as_deref(), Some("1AC-2"));
        let copy = db.duplicate_page(&first.id).unwrap();
        assert_eq!(copy.section.as_deref(), Some("1AC-2"));
        let dup = db.duplicate_notebook(&nb.id).unwrap();
        assert_eq!(dup.meta, db.notebook(&nb.id).unwrap().meta);
        assert!(db.pages(&dup.id).unwrap().iter().all(|p| p.section.as_deref() == Some("1AC-2")));
        db.set_page_section(&first.id, None).unwrap();
        assert!(db.set_page_section("missing", Some("x")).is_err());
    }

    #[test]
    fn upgrades_a_version_2_database() {
        let dir = std::env::temp_dir().join(format!("inkleaf-mig-{}", new_id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("v2.db");
        {
            let db = Db::open(&path).unwrap();
            notebook(&db, "Old notes");
            // Turn it back into a version 2 database.
            let conn = db.conn();
            conn.execute_batch("ALTER TABLE notebooks DROP COLUMN meta; ALTER TABLE pages DROP COLUMN section;").unwrap();
            conn.pragma_update(None, "user_version", 2).unwrap();
        }
        let db = Db::open(&path).unwrap();
        let nbs = db.library().unwrap().notebooks;
        assert_eq!(nbs.len(), 1);
        assert_eq!(nbs[0].title, "Old notes");
        assert_eq!(db.pages(&nbs[0].id).unwrap()[0].section, None);
        drop(db);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn cannot_delete_last_page() {
        let db = db();
        let nb = notebook(&db, "Solo");
        let page = &db.pages(&nb.id).unwrap()[0];
        assert!(db.delete_page(&page.id).is_err());
    }

    #[test]
    fn elements_round_trip_and_are_searchable() {
        let db = db();
        let nb = notebook(&db, "Chemistry");
        let page = db.pages(&nb.id).unwrap().remove(0);
        let els = vec![
            Element {
                id: "a".into(),
                kind: "stroke".into(),
                z: 0,
                data: json!({"points": [[1,2,0.5]]}),
            },
            Element {
                id: "b".into(),
                kind: "text".into(),
                z: 1,
                data: json!({"text": "Avogadro constant is 6.022e23"}),
            },
        ];
        db.save_elements(&page.id, &els).unwrap();
        assert_eq!(db.elements(&page.id).unwrap(), els);

        let hits = db.search("avogadro").unwrap();
        assert_eq!(hits.len(), 1);
        assert_eq!(hits[0].page_number, Some(1));
        assert!(hits[0].snippet.contains("Avogadro"));
        assert_eq!(db.search("chem").unwrap()[0].page_id, None);
        // LIKE wildcards in the query are treated literally.
        assert!(db.search("%").unwrap().is_empty());
    }

    #[test]
    fn trash_and_recover_notebook() {
        let db = db();
        let nb = notebook(&db, "Draft");
        db.set_notebook_trashed(&nb.id, true).unwrap();
        assert!(db.library().unwrap().notebooks.is_empty());
        assert_eq!(db.trash().unwrap().notebooks.len(), 1);
        db.set_notebook_trashed(&nb.id, false).unwrap();
        assert_eq!(db.library().unwrap().notebooks.len(), 1);
        db.set_notebook_trashed(&nb.id, true).unwrap();
        db.empty_trash().unwrap();
        assert!(db.notebook(&nb.id).is_err());
    }

    #[test]
    fn notebooks_in_trashed_folder_are_hidden_and_recovered_to_root() {
        let db = db();
        let folder = db.create_folder("School", None, None).unwrap();
        let nb = db
            .create_notebook(&NewNotebook {
                title: "Maths".into(),
                folder_id: Some(folder.id.clone()),
                ..Default::default()
            })
            .unwrap();
        db.set_folder_trashed(&folder.id, true).unwrap();
        assert!(db.library().unwrap().notebooks.is_empty());
        db.set_notebook_trashed(&nb.id, true).unwrap();
        db.set_notebook_trashed(&nb.id, false).unwrap();
        assert_eq!(db.notebook(&nb.id).unwrap().folder_id, None);
    }

    #[test]
    fn folder_moves_reject_cycles() {
        let db = db();
        let a = db.create_folder("A", None, None).unwrap();
        let b = db.create_folder("B", Some(&a.id), None).unwrap();
        assert!(db.move_folder(&a.id, Some(&b.id)).is_err());
        assert!(db.move_folder(&a.id, Some(&a.id)).is_err());
        db.move_folder(&b.id, None).unwrap();
    }

    #[test]
    fn duplicate_notebook_copies_pages_and_elements_with_new_ids() {
        let db = db();
        let nb = notebook(&db, "Original");
        db.add_page(&nb.id, None, None).unwrap();
        let page = db.pages(&nb.id).unwrap().remove(0);
        db.save_elements(
            &page.id,
            &[Element {
                id: "e1".into(),
                kind: "text".into(),
                z: 0,
                data: json!({"id": "e1", "text": "hi"}),
            }],
        )
        .unwrap();
        let copy = db.duplicate_notebook(&nb.id).unwrap();
        assert_eq!(copy.title, "Original copy");
        assert_eq!(copy.page_count, 2);
        let copied = db.elements(&db.pages(&copy.id).unwrap()[0].id).unwrap();
        assert_eq!(copied.len(), 1);
        assert_ne!(copied[0].id, "e1");
        assert_eq!(copied[0].data["id"], json!(copied[0].id));
    }

    #[test]
    fn new_notebooks_can_start_with_a_cover_in_the_cover_colour() {
        let db = Db::open_in_memory().unwrap();
        let nb = db
            .create_notebook(&NewNotebook {
                title: "Biology".into(),
                folder_id: None,
                cover_color: Some("#0d9488".into()),
                template: Some("grid".into()),
                paper_color: None,
                width: None,
                height: None,
                cover_page: true,
            })
            .unwrap();
        assert!(nb.has_cover);
        assert_eq!(nb.page_count, 2);
        assert_eq!(nb.default_template, "grid");
        let pages = db.pages(&nb.id).unwrap();
        assert_eq!((pages[0].template.as_str(), pages[0].paper_color.as_str()), ("cover", "#0d9488"));
        assert_eq!(pages[1].template, "grid");
        // Changing the cover colour recolours the cover page.
        db.set_notebook_cover(&nb.id, "#dc2626").unwrap();
        assert_eq!(db.pages(&nb.id).unwrap()[0].paper_color, "#dc2626");
        assert!(db.set_notebook_cover(&nb.id, "red").is_err());
    }

    #[test]
    fn snippet_handles_unicode() {
        let s = snippet(
            "Ünïcödé text with the word needle somewhere in a long sentence that keeps going on",
            "NEEDLE",
        );
        assert!(s.contains("needle"));
    }
}


#[cfg(test)]
mod trashed_folder_tests {
    use super::*;

    fn notebook(db: &Db, title: &str, folder: Option<&str>) -> Notebook {
        db.create_notebook(&NewNotebook { title: title.into(), folder_id: folder.map(str::to_string), cover_color: None, template: None, paper_color: None, width: None, height: None, cover_page: false }).unwrap()
    }
    fn count(db: &Db, table: &str) -> i64 {
        db.conn().query_row(&format!("SELECT COUNT(*) FROM {table}"), [], |r| r.get(0)).unwrap()
    }

    #[test]
    fn a_trashed_folder_takes_everything_inside_with_it() {
        let db = Db::open_in_memory().unwrap();
        let class = db.create_folder("Class", None, None).unwrap();
        let unit = db.create_folder("Unit 1", Some(&class.id), None).unwrap();
        let other = db.create_folder("Other", None, None).unwrap();
        notebook(&db, "Lesson A", Some(&class.id));
        let deep = notebook(&db, "Lesson B", Some(&unit.id));
        let page = db.pages(&deep.id).unwrap()[0].id.clone();
        db.save_elements(&page, &[Element { id: new_id(), kind: "text".into(), z: 0, data: serde_json::json!({ "text": "fractions" }) }]).unwrap();
        notebook(&db, "Kept", Some(&other.id));
        notebook(&db, "Loose", None);

        // In the trash: nothing inside shows in the library or in search.
        db.set_folder_trashed(&class.id, true).unwrap();
        let lib = db.library().unwrap();
        assert_eq!(lib.folders.iter().map(|f| f.name.as_str()).collect::<Vec<_>>(), vec!["Other"]);
        let mut titles: Vec<_> = lib.notebooks.iter().map(|n| n.title.as_str()).collect();
        titles.sort();
        assert_eq!(titles, vec!["Kept", "Loose"]);
        assert!(db.search("fractions").unwrap().is_empty());
        assert!(db.search("Lesson").unwrap().is_empty());

        // Recovering brings it all back.
        db.set_folder_trashed(&class.id, false).unwrap();
        assert_eq!(db.library().unwrap().notebooks.len(), 4);
        assert_eq!(db.search("fractions").unwrap().len(), 1);

        // Emptying the trash deletes the folder, its subfolder and both notebooks, with their pages.
        db.set_folder_trashed(&class.id, true).unwrap();
        db.empty_trash().unwrap();
        assert_eq!(count(&db, "folders"), 1);
        assert_eq!(count(&db, "notebooks"), 2);
        assert_eq!(count(&db, "elements"), 0);
        let lib = db.library().unwrap();
        assert_eq!(lib.notebooks.len(), 2);
    }

    #[test]
    fn deleting_a_folder_for_good_deletes_its_contents() {
        let db = Db::open_in_memory().unwrap();
        let f = db.create_folder("Old year", None, None).unwrap();
        let sub = db.create_folder("Term 2", Some(&f.id), None).unwrap();
        notebook(&db, "A", Some(&f.id));
        notebook(&db, "B", Some(&sub.id));
        notebook(&db, "Elsewhere", None);
        db.set_folder_trashed(&f.id, true).unwrap();
        db.delete_folder(&f.id).unwrap();
        assert_eq!(count(&db, "folders"), 0);
        assert_eq!(db.library().unwrap().notebooks.iter().map(|n| n.title.clone()).collect::<Vec<_>>(), vec!["Elsewhere"]);
        assert_eq!(count(&db, "notebooks"), 1);
        assert!(db.delete_folder("missing").is_err());
    }
}
