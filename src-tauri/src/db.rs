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

const SCHEMA_VERSION: i64 = 4;

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
            conn.pragma_update(None, "user_version", SCHEMA_VERSION)?;
        }
        // PDFs stored for an import that never finished.
        conn.execute(
            "DELETE FROM assets WHERE notebook_id IS NULL AND created_at < ?1",
            [now_ms() - 3_600_000],
        )?;
        Ok(())
    }

    // ---------------------------------------------------------------- library

    pub fn library(&self) -> AppResult<Library> {
        let conn = self.conn();
        Ok(Library {
            folders: query_folders(
                &conn,
                "WHERE trashed_at IS NULL ORDER BY name COLLATE NOCASE",
            )?,
            notebooks: query_notebooks(
                &conn,
                "WHERE n.trashed_at IS NULL AND (n.folder_id IS NULL OR f.trashed_at IS NULL)
                 ORDER BY n.updated_at DESC",
            )?,
        })
    }

    pub fn trash(&self) -> AppResult<TrashContents> {
        let conn = self.conn();
        Ok(TrashContents {
            folders: query_folders(
                &conn,
                "WHERE trashed_at IS NOT NULL ORDER BY trashed_at DESC",
            )?,
            notebooks: query_notebooks(
                &conn,
                "WHERE n.trashed_at IS NOT NULL ORDER BY n.trashed_at DESC",
            )?,
        })
    }

    pub fn empty_trash(&self) -> AppResult<()> {
        let conn = self.conn();
        conn.execute("DELETE FROM notebooks WHERE trashed_at IS NOT NULL", [])?;
        conn.execute("DELETE FROM folders WHERE trashed_at IS NOT NULL", [])?;
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
        };
        conn.execute(
            "INSERT INTO folders (id, name, parent_id, color, favourite, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, 0, ?5, ?5)",
            params![folder.id, folder.name, folder.parent_id, folder.color, now],
        )?;
        Ok(folder)
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
        let n = conn.execute(
            "UPDATE folders SET parent_id = ?2, updated_at = ?3 WHERE id = ?1",
            params![id, parent_id, now_ms()],
        )?;
        expect_one(n, id)
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

    pub fn delete_folder(&self, id: &str) -> AppResult<()> {
        let conn = self.conn();
        expect_one(conn.execute("DELETE FROM folders WHERE id = ?1", [id])?, id)
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
        let cover_color = input.cover_color.clone().unwrap_or_else(|| "#3b82f6".into());
        validate_color(&cover_color)?;
        let now = now_ms();
        let id = new_id();
        let tx = conn.transaction()?;
        tx.execute(
            "INSERT INTO notebooks (id, title, folder_id, cover_color, default_template, favourite, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, 0, ?6, ?6)",
            params![
                id,
                title,
                input.folder_id,
                cover_color,
                template,
                now
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
        get_notebook(&conn, &id)
    }

    pub fn notebook(&self, id: &str) -> AppResult<Notebook> {
        get_notebook(&self.conn(), id)
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
        let conn = self.conn();
        if let Some(folder) = folder_id {
            ensure_exists(&conn, "folders", folder)?;
        }
        let n = conn.execute(
            "UPDATE notebooks SET folder_id = ?2, updated_at = ?3 WHERE id = ?1",
            params![id, folder_id, now_ms()],
        )?;
        expect_one(n, id)
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
        let n = conn.execute(
            "UPDATE notebooks SET meta = ?2, updated_at = ?3 WHERE id = ?1",
            params![id, meta, now_ms()],
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
            "INSERT INTO notebooks (id, title, folder_id, cover_color, default_template, favourite, created_at, updated_at, meta)
             VALUES (?1, ?2, ?3, ?4, ?5, 0, ?6, ?6, ?7)",
            params![
                new_nb,
                format!("{} copy", source.title),
                source.folder_id,
                source.cover_color,
                source.default_template,
                now,
                source.meta
            ],
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
        get_notebook(&conn, &new_nb)
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
        let now = now_ms();
        let id = new_id();
        let tx = conn.transaction()?;
        tx.execute(
            "INSERT INTO notebooks (id, title, folder_id, cover_color, default_template, favourite, created_at, updated_at)
             VALUES (?1, ?2, ?3, ?4, 'blank', 0, ?5, ?5)",
            params![id, title, input.folder_id, input.cover_color.clone().unwrap_or_else(|| "#dc2626".into()), now],
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
        tx.commit()?;
        get_notebook(&conn, &id)
    }

    pub fn asset_bytes(&self, id: &str) -> AppResult<Vec<u8>> {
        self.conn()
            .query_row("SELECT data FROM assets WHERE id = ?1", [id], |r| r.get(0))
            .optional()?
            .ok_or_else(|| AppError::NotFound(id.to_string()))
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
        Ok(rows.collect::<Result<_, _>>()?)
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
        get_page(&conn, &id)
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
        get_page(&conn, &id)
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
            "UPDATE pages SET template = ?2, paper_color = COALESCE(?3, paper_color), updated_at = ?4 WHERE id = ?1",
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
        let n = conn.execute(
            "UPDATE pages SET thumbnail = ?2 WHERE id = ?1",
            params![page_id, thumbnail],
        )?;
        expect_one(n, page_id)
    }

    // --------------------------------------------------------------- elements

    pub fn elements(&self, page_id: &str) -> AppResult<Vec<Element>> {
        let conn = self.conn();
        ensure_exists(&conn, "pages", page_id)?;
        let mut stmt = conn
            .prepare("SELECT id, kind, z, data FROM elements WHERE page_id = ?1 ORDER BY z, id")?;
        let rows = stmt.query_map([page_id], |r| {
            let raw: String = r.get(3)?;
            Ok((r.get(0)?, r.get(1)?, r.get(2)?, raw))
        })?;
        let mut out = Vec::new();
        for row in rows {
            let (id, kind, z, raw): (String, String, i64, String) = row?;
            let data = serde_json::from_str(&raw).unwrap_or(serde_json::Value::Null);
            out.push(Element { id, kind, z, data });
        }
        Ok(out)
    }

    /// Replaces the full element set of a page atomically.
    pub fn save_elements(&self, page_id: &str, elements: &[Element]) -> AppResult<()> {
        let mut conn = self.conn();
        let notebook_id: String = conn
            .query_row(
                "SELECT notebook_id FROM pages WHERE id = ?1",
                [page_id],
                |r| r.get(0),
            )
            .optional()?
            .ok_or_else(|| AppError::NotFound(page_id.to_string()))?;
        let tx = conn.transaction()?;
        tx.execute("DELETE FROM elements WHERE page_id = ?1", [page_id])?;
        {
            let mut insert = tx.prepare(
                "INSERT INTO elements (id, page_id, kind, z, data, text_content) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            )?;
            for el in elements {
                let text = el
                    .data
                    .get("text")
                    .and_then(|t| t.as_str())
                    .map(str::to_string);
                insert.execute(params![
                    el.id,
                    page_id,
                    el.kind,
                    el.z,
                    el.data.to_string(),
                    text
                ])?;
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
                "SELECT id, title FROM notebooks WHERE trashed_at IS NULL AND title LIKE ?1 ESCAPE '\\'
                 ORDER BY updated_at DESC LIMIT 50",
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
                "SELECT n.id, n.title, p.id, p.position, e.text_content
                 FROM elements e JOIN pages p ON p.id = e.page_id JOIN notebooks n ON n.id = p.notebook_id
                 WHERE n.trashed_at IS NULL AND e.text_content LIKE ?1 ESCAPE '\\'
                 ORDER BY n.updated_at DESC, p.position LIMIT 200",
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
        Ok(hits)
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

fn query_folders(conn: &Connection, clause: &str) -> AppResult<Vec<Folder>> {
    let mut stmt = conn.prepare(&format!(
        "SELECT id, name, parent_id, color, favourite, created_at, updated_at, trashed_at FROM folders {clause}"
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
        n.meta
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
